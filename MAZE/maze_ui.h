// MAZE — экран и органы управления без Arduino. Перенос proto/maze.js: та же отрисовка
// в буфер 128×32 (формат страниц SSD1306 — тот же, что у Adafruit_SSD1306::getBuffer()),
// те же жесты. Время — в миллисекундах, его передаёт вызывающий.
#pragma once
#include "maze_core.h"

namespace maze {

// буфер страниц SSD1306: байт buf[x + (y/8)*128], бит y&7
struct FB {
  uint8_t *buf;
  static constexpr int W = 128, H = 32;
  void clear() { memset(buf, 0, W * H / 8); }
  // v: 1 — зажечь, 0 — погасить, 2 — инвертировать
  void set(int x, int y, int v = 1) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t &b = buf[x + (y >> 3) * W];
    uint8_t m = 1 << (y & 7);
    if (v == 2) b ^= m;
    else if (v) b |= m;
    else b &= ~m;
  }
  bool get(int x, int y) const { return buf[x + (y >> 3) * W] >> (y & 7) & 1; }
  void fill(int x, int y, int w, int h, int v = 1) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) set(x + i, y + j, v);
  }
  void frame(int x, int y, int w, int h, int v = 1) {
    for (int i = 0; i < w; i++) {
      set(x + i, y, v);
      set(x + i, y + h - 1, v);
    }
    for (int j = 1; j < h - 1; j++) {
      set(x, y + j, v);
      set(x + w - 1, y + j, v);
    }
  }
};

// Слоты проектов на флеше. В прошивке — LittleFS, на хосте — память.
constexpr uint8_t SLOT_AUTO = 1, SLOT_DEMO = 2;

struct Store {
  // flags: SLOT_AUTO — черновик (пишется автосохранением), SLOT_DEMO — демо, положенное прошивкой
  virtual bool read(int slot, uint8_t *blob, uint8_t &flags) = 0;  // false — слот пуст
  virtual bool write(int slot, const uint8_t *blob, uint8_t flags) = 0;
  virtual int current() = 0;  // загруженный слот (-1 — ещё не было)
  virtual void setCurrent(int slot) = 0;
  virtual bool ready() { return true; }  // false — флеш не смонтирован, сохранения пропадут
  // версия демо-проектов, уже разложенных по слотам 01…; 0 — ещё не раскладывали
  virtual int demoVersion() { return 0; }
  virtual void setDemoVersion(int) {}
  // вход нот с MIDI IN — общий для устройства, не для проекта (IN_OFF, IN_ALL, канал)
  virtual int inChannel() { return 0; }
  virtual void setInChannel(int) {}
  virtual ~Store() {}
};

enum Mode : uint8_t { M_MAP, M_TILE, M_DRAW, M_LINE, M_SETTINGS, M_PROJ };

constexpr uint32_t HOLD_MS = 900;  // стирание, LOAD, SAVE — удержанием, пока не заполнится шкала
constexpr uint32_t MUTE_MS = 500;  // удержание PLAY — мьют маршрута в фокусе
constexpr uint32_t AUTOSAVE_MS = 2000;

struct Ui {
  State *S = nullptr;
  Store *store = nullptr;

  Mode mode = M_MAP, view = M_MAP;  // view — куда вернуться из настроек
  Cell cur = {0, 0};
  uint8_t focus = 0, lineSel = 0, setSel = 0;

  // рисование маршрута
  Cell path[MAX_LEN];
  uint8_t pathN = 0;
  bool pathClosed = false;

  // правка тайла: с карты — карта + панель, из линии — расширенный вид с соседями
  uint8_t page = 0, sel = 0;
  Mode tileBack = M_MAP;
  bool hood = false;

  // удержания: 0 нет, 1 тайл, 2 цепь
  uint8_t hold = 0;
  uint32_t holdT0 = 0;
  bool playHold = false;
  uint32_t playT0 = 0;

  // кнопки
  bool tap = false, pageBtn = false, pageUsed = false, tapClean = true;
  uint32_t tapDownAt = 0, lastTapUp = 0, lastSw = 0;
  bool hadTapUp = false, hadSw = false;

  // проекты
  bool used[SLOTS] = {}, autoF[SLOTS] = {}, demoF[SLOTS] = {};
  int slot = 0;
  bool autosave = false;
  uint8_t saved[BLOB], last[BLOB];
  uint32_t lastChange = 0, lastCheck = 0;
  uint8_t projSel = 0;
  Mode projBack = M_MAP;
  uint8_t projHold = 0;  // 1 LOAD, 2 SAVE
  uint8_t projField = 3;  // что правит энкодер: 0 тоника, 1 лад, 2 вход нот IN, 3 темп
  uint8_t inCh = 0;  // вход нот с MIDI IN: IN_OFF, IN_ALL, 2…17 — канал 1…16
  uint32_t projT0 = 0, projDone = 0;
  uint8_t projDoneRow = 8;
  int queued = -1;  // слот ждёт начала такта
  uint8_t queuedBlob[BLOB];
  bool queuedHas = false, queuedAuto = false;
  int selCached = -1;  // миниатюра выбранного слота
  uint8_t selBlob[BLOB];
  bool selHas = false;

  void boot(uint32_t now);  // загрузить последний слот (на чистой плате — демо в слот 01)

  // жесты; now — мс
  void onEnc(int d, uint32_t now);
  void onSwDown(uint32_t now);
  void onSwUp(uint32_t now);
  void onPlayDown(uint32_t now);
  void onPlayUp(uint32_t now);
  void onPageDown(uint32_t now);
  void onPageUp(uint32_t now);
  void onTapDown(uint32_t now);
  void onTapUp(uint32_t now);
  void update(uint32_t now);  // удержания, автосохранение, отложенная загрузка на паузе
  // из такта движка, перед onTick: загрузка проекта с начала такта
  void beforeTick(uint32_t now);

  void render(FB &fb, uint32_t now);

  bool dirty() const { return memcmp(last, saved, BLOB) != 0; }

  void saveSlot(int k);  // ручное сохранение (SAVE)

 private:
  void openTile(Mode back);
  void openProj();
  void finishDraw();
  void projAction(uint32_t now);
  void loadNow(int k, const uint8_t *blob, bool has, bool autoFlag);
};

// для тестов и рендера сцен
int shortestPath(const Cell *path, int n, Cell to, Cell *out, bool &closing);

}  // namespace maze
