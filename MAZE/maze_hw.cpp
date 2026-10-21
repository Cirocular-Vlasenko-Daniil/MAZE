// MAZE — всё, что про плату ESPidi: экран, MIDI, кнопки, энкодер, клок, файлы.
// Логика — в maze_core и maze_ui (они же гоняются тестами на хосте), здесь только железо.
// Экран, очередь MIDI, задача движка и энкодер — как в RED SEA, там они проверены на этой плате.
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESPidiHub.h>
#include <LittleFS.h>
#include <Wire.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <hal/uart_ll.h>

#include "maze_core.h"
#include "maze_ui.h"

using namespace maze;

namespace pin {
constexpr uint8_t PLAY = espidi::pin::PLAY, TAP = espidi::pin::TAP, PAGE = espidi::pin::PAGE, ENC_SW = espidi::pin::ENC_SW;
constexpr uint8_t ENC_A = 5, ENC_B = 6;  // как в RED SEA: направление проверено на плате
}  // namespace pin

// ───────────────────────── экран ─────────────────────────
// Рисуем в буфер Adafruit_SSD1306 (формат страниц — тот же, что у maze::FB); flush()
// копирует кадр и сразу возвращается, задача отправляет его по I²C (~12,7 мс).
// Кадр, совпадающий с уже отправленным, не передаётся.
class MazeDisplay : public Adafruit_SSD1306 {
 public:
  using Adafruit_SSD1306::Adafruit_SSD1306;
  void startSender() { xTaskCreate(senderTask, "oled", 3072, this, 2, &task); }
  void flush() {
    if (!task) {
      display();
      return;
    }
    portENTER_CRITICAL(&mux);
    memcpy(pending, getBuffer(), FRAME);
    hasPending = true;
    portEXIT_CRITICAL(&mux);
    xTaskNotifyGive(task);
  }

 private:
  static constexpr uint16_t FRAME = 128 * 32 / 8;
  uint8_t pending[FRAME], sending[FRAME], lastSent[FRAME];
  volatile bool hasPending = false;
  bool hasLast = false;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t task = nullptr;

  static void senderTask(void *arg) {
    MazeDisplay *d = static_cast<MazeDisplay *>(arg);
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      for (;;) {
        portENTER_CRITICAL(&d->mux);
        if (!d->hasPending) {
          portEXIT_CRITICAL(&d->mux);
          break;
        }
        memcpy(d->sending, d->pending, FRAME);
        d->hasPending = false;
        portEXIT_CRITICAL(&d->mux);
        if (!d->hasLast || memcmp(d->sending, d->lastSent, FRAME)) {
          d->sendFrame(d->sending);
          memcpy(d->lastSent, d->sending, FRAME);
          d->hasLast = true;
        }
      }
    }
  }
  void sendFrame(const uint8_t *buf) {
    static const uint8_t addressing[] = {SSD1306_PAGEADDR, 0, 0xFF, SSD1306_COLUMNADDR};
    const uint16_t chunk = min(256, I2C_BUFFER_LENGTH);
    wire->setClock(wireClk);
    ssd1306_commandList(addressing, sizeof(addressing));
    ssd1306_command1(0);
    ssd1306_command1(127);
    wire->beginTransmission(i2caddr);
    wire->write((uint8_t)0x40);
    uint16_t out = 1;
    for (uint16_t i = 0; i < FRAME; i++) {
      if (out >= chunk) {
        wire->endTransmission();
        wire->beginTransmission(i2caddr);
        wire->write((uint8_t)0x40);
        out = 1;
      }
      wire->write(buf[i]);
      out++;
    }
    wire->endTransmission();
    wire->setClock(restoreClk);
  }
};

MazeDisplay display(128, 32, &Wire, -1);
HardwareSerial midi(1);

// ───────────────────────── MIDI наружу ─────────────────────────
// Очередь вместо прямой записи в UART: midiPump() докладывает байты в аппаратный FIFO
// понемногу. Реалтайм (Clock, Start, Stop) — отдельной очередью, уходит следующим байтом.
namespace MidiOut {
constexpr uint16_t RT_SIZE = 64, TX_SIZE = 1024;  // степени двойки
constexpr uint32_t FIFO_KEEP = 3;
uint8_t rt[RT_SIZE], tx[TX_SIZE];
volatile uint16_t rtHead = 0, rtTail = 0, txHead = 0, txTail = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace MidiOut

static void midiPump() {
  using namespace MidiOut;
  portENTER_CRITICAL(&mux);
  uart_dev_t *hw = UART_LL_GET_HW(1);
  uint32_t inFifo = UART_LL_FIFO_DEF_LEN - uart_ll_get_txfifo_len(hw);
  while (inFifo < FIFO_KEEP) {
    uint8_t b;
    if (rtTail != rtHead) {
      b = rt[rtTail];
      rtTail = (rtTail + 1) & (RT_SIZE - 1);
    } else if (txTail != txHead) {
      b = tx[txTail];
      txTail = (txTail + 1) & (TX_SIZE - 1);
    } else break;
    uart_ll_write_txfifo(hw, &b, 1);
    inFifo++;
  }
  portEXIT_CRITICAL(&mux);
}
static void midiRealtime(uint8_t b) {
  using namespace MidiOut;
  portENTER_CRITICAL(&mux);
  uint16_t next = (rtHead + 1) & (RT_SIZE - 1);
  if (next != rtTail) {
    rt[rtHead] = b;
    rtHead = next;
  }
  portEXIT_CRITICAL(&mux);
  midiPump();
}
static void midiMessage(uint8_t s, uint8_t d1, uint8_t d2, uint8_t len = 3) {
  using namespace MidiOut;
  for (;;) {
    portENTER_CRITICAL(&mux);
    uint16_t freeBytes = (txTail - txHead - 1) & (TX_SIZE - 1);
    if (freeBytes >= len) {
      tx[txHead] = s;
      txHead = (txHead + 1) & (TX_SIZE - 1);
      tx[txHead] = d1;
      txHead = (txHead + 1) & (TX_SIZE - 1);
      if (len == 3) {
        tx[txHead] = d2;
        txHead = (txHead + 1) & (TX_SIZE - 1);
      }
    }
    portEXIT_CRITICAL(&mux);
    if (freeBytes >= len) break;
    midiPump();
    delayMicroseconds(100);
  }
  midiPump();
}

// ───────────────────────── ноты по времени ─────────────────────────
// Ретчет и арпеджио — несколько нот внутри шага, note off — по длине ноты, афтертач (P) —
// давление при ноте или плавно вверх/вниз. Всё ставится в расписание в мкс от момента тика.
struct Ev {
  uint32_t at;
  uint8_t s, d1, d2;
  bool used;
};
static Ev evs[256];
static uint32_t tickNowUs = 0;  // момент текущего тика — от него отсчитываются ноты шага

static void schedule(uint32_t at, uint8_t s, uint8_t d1, uint8_t d2) {
  for (Ev &e : evs)
    if (!e.used) {
      e = {at, s, d1, d2, true};
      return;
    }
  // расписание полно — отправляем сразу, чтобы note off не потерялся
  midiMessage(s, d1, d2, (s & 0xF0) == 0xD0 ? 2 : 3);
}
// note off той же ноты, назначенный позже нового note on, сработал бы посреди новой ноты
// (легато, перекрытие) — переносим его вплотную перед новым note on
static void pullOffsBefore(uint8_t ch, uint8_t note, uint32_t at) {
  for (Ev &e : evs)
    if (e.used && e.s == (0x80 | ch) && e.d1 == note && (int32_t)(e.at - at) >= 0) e.at = at - 1;
}
static void runDue(uint32_t now) {
  for (int pass = 0; pass < 2; pass++)  // сначала note off, потом остальное — в один момент
    for (Ev &e : evs)
      if (e.used && (int32_t)(now - e.at) >= 0 && ((e.s & 0xF0) == 0x80) == (pass == 0)) {
        midiMessage(e.s, e.d1, e.d2, (e.s & 0xF0) == 0xD0 ? 2 : 3);
        e.used = false;
      }
}
// пауза: все взятые ноты отпустить, ещё не сыгранные — забыть
static void allNotesOff() {
  for (Ev &e : evs)
    if (e.used) {
      if ((e.s & 0xF0) == 0x80) midiMessage(e.s, e.d1, e.d2);
      e.used = false;
    }
}

struct HwOut : Output {
  void note(const Note &n) override {
    uint8_t ch = (n.ch - 1) & 15;
    uint32_t on = tickNowUs + n.at, off = on + (n.dur > 1000 ? n.dur : 1000);
    pullOffsBefore(ch, n.note, on);
    if (n.at == 0) midiMessage(0x90 | ch, n.note, n.vel);
    else schedule(on, 0x90 | ch, n.note, n.vel);
    schedule(off, 0x80 | ch, n.note, 0);
    if (!n.pressure && !n.shape) return;
    if (n.shape == 0) {  // HOLD: давление на всю ноту
      schedule(on, 0xD0 | ch, n.pressure, 0);
      schedule(off, 0xD0 | ch, 0, 0);
      return;
    }
    for (int k = 0; k <= 6; k++) {  // RISE 0 → значение, FALL значение → 0
      uint8_t v = n.shape == 1 ? n.pressure * k / 6 : n.pressure * (6 - k) / 6;
      schedule(on + (off - on) * k / 7, 0xD0 | ch, v, 0);
    }
  }
};
static HwOut out;

// ───────────────────────── проекты в LittleFS ─────────────────────────
// /maze/slotNN.bin — байт флагов (бит 0 — автосохранение) и проект; /maze/current — номер слота.
// Запись откладывается: loop() делает её вне блокировки движка, чтобы флеш не сбивал такты.
struct FileStore : Store {
  bool ok = false;
  struct {
    bool pending;
    uint8_t blob[BLOB];
    bool autoFlag;
  } w[SLOTS];
  int cur = -1, curPending = -1;

  static String path(int k) {
    char b[32];
    snprintf(b, sizeof(b), "/maze/slot%02d.bin", k + 1);
    return b;
  }
  bool read(int k, uint8_t *blob, bool &autoFlag) override {
    if (w[k].pending) {  // ещё не записано — отдаём то, что будет записано
      memcpy(blob, w[k].blob, BLOB);
      autoFlag = w[k].autoFlag;
      return true;
    }
    if (!ok) return false;
    File f = LittleFS.open(path(k), "r");
    if (!f) return false;
    uint8_t flags = 0;
    bool good = f.size() == BLOB + 1 && f.read(&flags, 1) == 1 && f.read(blob, BLOB) == BLOB;
    f.close();
    autoFlag = flags & 1;
    return good;
  }
  bool write(int k, const uint8_t *blob, bool autoFlag) override {
    memcpy(w[k].blob, blob, BLOB);
    w[k].autoFlag = autoFlag;
    w[k].pending = true;
    return true;
  }
  int current() override {
    if (cur >= 0 || !ok) return cur;
    File f = LittleFS.open("/maze/current", "r");
    if (f) {
      int v = f.read();
      cur = v >= 0 && v < SLOTS ? v : -1;
      f.close();
    }
    return cur;
  }
  void setCurrent(int k) override { cur = curPending = k; }
  bool ready() override { return ok; }
  void flush() {
    if (!ok) return;
    for (int k = 0; k < SLOTS; k++) {
      if (!w[k].pending) continue;
      String tmp = path(k) + ".tmp";
      File f = LittleFS.open(tmp, "w");
      if (!f) {
        esp_rom_printf("MAZE: can't write %s\n", tmp.c_str());
        continue;
      }
      uint8_t flags = w[k].autoFlag ? 1 : 0;
      bool good = f.write(&flags, 1) == 1 && f.write(w[k].blob, BLOB) == BLOB;
      f.close();
      if (good) {
        LittleFS.remove(path(k));
        LittleFS.rename(tmp, path(k));  // целиком или никак
        w[k].pending = false;
      }
    }
    if (curPending >= 0) {
      File f = LittleFS.open("/maze/current", "w");
      if (f) {
        f.write((uint8_t)curPending);
        f.close();
        curPending = -1;
      }
    }
  }
};

// ───────────────────────── состояние ─────────────────────────
static State S;
static Ui ui;
static FileStore store;

SemaphoreHandle_t engineMutex = nullptr;
static inline void lock() { xSemaphoreTakeRecursive(engineMutex, portMAX_DELAY); }
static inline void unlock() { xSemaphoreGiveRecursive(engineMutex); }

// ───────────────────────── энкодер ─────────────────────────
static volatile int encoderTicks = 0;
static volatile uint8_t lastEncState = 0;
static void IRAM_ATTR encoderISR() {
  uint8_t st = (digitalRead(pin::ENC_A) << 1) | digitalRead(pin::ENC_B);
  if (st == lastEncState) return;
  int8_t dir = 0;
  if ((lastEncState == 0 && st == 1) || (lastEncState == 1 && st == 3) || (lastEncState == 3 && st == 2) || (lastEncState == 2 && st == 0)) dir = 1;
  else if ((lastEncState == 0 && st == 2) || (lastEncState == 2 && st == 3) || (lastEncState == 3 && st == 1) || (lastEncState == 1 && st == 0)) dir = -1;
  encoderTicks += dir;
  lastEncState = st;
}

// ───────────────────────── движок ─────────────────────────
// Задача движка с приоритетом выше loop() просыпается каждые 0,5 мс и точно к моменту
// очередного своего тика. Клок на MIDI IN — ведомый: тики приходят снаружи, свой молчит.
static TaskHandle_t engineTask = nullptr;
static esp_timer_handle_t engineTimer = nullptr, engineTickTimer = nullptr;
constexpr uint32_t ENGINE_PERIOD_US = 500;
static uint32_t nextTickUs = 0, lastExtUs = 0;
static bool extSeen = false, wasRunning = false, wasExt = false;

static void onEngineTimer(void *) {
  if (engineTask) xTaskNotifyGive(engineTask);
}

static void readMidiIn(uint32_t now) {
  uint8_t count = 0;
  while (midi.available() && count++ < 64) {
    uint8_t b = midi.read();
    if (b < 0xF8) continue;  // ноты с MIDI IN не принимаются — только клок и транспорт
    if (b == 0xF8) {
      if (extSeen) {
        uint32_t dt = now - lastExtUs;
        if (dt > 2000 && dt < 125000) S.tickUs = S.ext ? (S.tickUs * 7 + dt) / 8 : dt;  // сглаживаем
      }
      lastExtUs = now;
      extSeen = true;
      S.ext = true;
      tickNowUs = now;
      ui.beforeTick(millis());
      extTick(S, millis(), &out);
      if (S.tickUs) S.bpm = (60000000u / PPQN + S.tickUs / 2) / S.tickUs;
    } else if (b == 0xFA) extStart(S);
    else if (b == 0xFB) extContinue(S);
    else if (b == 0xFC) extStop(S);
  }
}

static void engineLoop(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2));
    lock();
    uint32_t now = micros();
    readMidiIn(now);
    if (extSeen && now - lastExtUs > 500000) {  // клок пропал — снова ведущий, со своим темпом
      extSeen = false;
      S.ext = false;
      S.resumeArmed = false;
    }
    // идёт ли игра (ведомый на взводе ещё не идёт); смена — Start/Stop наружу и снятие нот
    bool running = S.playing && !S.resumeArmed;
    if (running != wasRunning) {
      if (!S.ext) midiRealtime(running ? 0xFA : 0xFC);  // ведущий: Start / Stop наружу
      if (!running) allNotesOff();
      else if (!S.ext) nextTickUs = now;  // старт — сразу, без ожидания тика
    }
    wasRunning = running;
    if (!S.ext) {
      S.tickUs = internalTickUs(S.bpm);
      if (wasExt) nextTickUs = now;
      for (uint8_t n = 0; n < 8 && (int32_t)(now - nextTickUs) >= 0; n++) {
        tickNowUs = nextTickUs;
        midiRealtime(0xF8);  // ведущий: клок наружу всегда, чтобы ведомые знали темп
        ui.beforeTick(millis());
        onTick(S, millis(), &out);
        nextTickUs += S.tickUs;
      }
      if ((int32_t)(now - nextTickUs) > (int32_t)(8 * S.tickUs)) nextTickUs = now;  // стояли (флеш) — без «догоняния»
    }
    wasExt = S.ext;
    runDue(now);
    midiPump();
    uint32_t next = nextTickUs;
    bool internal = !S.ext;
    unlock();
    if (internal) {  // свой тик раньше очередного пробуждения — разбудить точно к нему
      int32_t dt = (int32_t)(next - micros());
      if (dt > 0 && dt <= (int32_t)ENGINE_PERIOD_US) {
        esp_timer_stop(engineTickTimer);
        esp_timer_start_once(engineTickTimer, dt);
      }
    }
  }
}

static void engineBegin() {
  xTaskCreate(engineLoop, "engine", 6144, nullptr, configMAX_PRIORITIES - 4, &engineTask);
  esp_timer_create_args_t args = {};
  args.callback = onEngineTimer;
  args.name = "engine";
  esp_timer_create(&args, &engineTimer);
  args.name = "engine_tick";
  esp_timer_create(&args, &engineTickTimer);
  esp_timer_start_periodic(engineTimer, ENGINE_PERIOD_US);
}

// ───────────────────────── кнопки ─────────────────────────
struct Button {
  uint8_t pin;
  bool down = false, raw = false;
  uint32_t since = 0;
  // true — состояние сменилось (дребезг 5 мс отфильтрован)
  bool poll(uint32_t now) {
    bool r = digitalRead(pin) == LOW;
    if (r != raw) {
      raw = r;
      since = now;
    }
    if (raw != down && now - since >= 5) {
      down = raw;
      return true;
    }
    return false;
  }
};
static Button bPlay{pin::PLAY}, bTap{pin::TAP}, bPage{pin::PAGE}, bSw{pin::ENC_SW};

void setup() {
  for (uint8_t p : {pin::PLAY, pin::TAP, pin::PAGE, pin::ENC_SW, pin::ENC_A, pin::ENC_B}) pinMode(p, INPUT_PULLUP);
  Wire.begin(espidi::pin::OLED_SDA, espidi::pin::OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, espidi::OLED_ADDR);
  display.clearDisplay();
  display.display();
  midi.begin(espidi::MIDI_BAUD, SERIAL_8N1, espidi::pin::MIDI_RX, espidi::pin::MIDI_TX);

  engineMutex = xSemaphoreCreateRecursiveMutex();
  store.ok = espidi::mountFiles();
  if (store.ok) espidi::dataDir("maze");
  esp_rom_printf("MAZE: files %s, /maze %s\n", store.ok ? "mounted" : "NOT MOUNTED",
                 store.ok && LittleFS.exists("/maze") ? "ok" : "missing");
  S.tickUs = internalTickUs(S.bpm);
  ui.S = &S;
  ui.store = &store;
  ui.boot(millis());
  store.flush();

  display.startSender();
  lastEncState = (digitalRead(pin::ENC_A) << 1) | digitalRead(pin::ENC_B);
  attachInterrupt(digitalPinToInterrupt(pin::ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pin::ENC_B), encoderISR, CHANGE);
  engineBegin();
}

void loop() {
  uint32_t now = millis();
  lock();
  if (bTap.poll(now)) bTap.down ? ui.onTapDown(now) : ui.onTapUp(now);
  if (bPlay.poll(now)) bPlay.down ? ui.onPlayDown(now) : ui.onPlayUp(now);
  if (bPage.poll(now)) bPage.down ? ui.onPageDown(now) : ui.onPageUp(now);
  if (bSw.poll(now)) bSw.down ? ui.onSwDown(now) : ui.onSwUp(now);
  int det = encoderTicks / 4;  // 4 отсчёта на щелчок
  if (det) {
    noInterrupts();
    encoderTicks -= det * 4;
    interrupts();
    ui.onEnc(det, now);
  }
  ui.update(now);
  unlock();
  store.flush();  // запись во флеш — вне блокировки движка

  static uint32_t lastFrame = 0;
  if (now - lastFrame >= 33) {  // ~30 кадров в секунду; отрисовка только читает состояние
    lastFrame = now;
    FB fb{display.getBuffer()};
    ui.render(fb, now);
    display.flush();
  }
  delay(1);
}
