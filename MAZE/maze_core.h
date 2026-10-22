// MAZE — ядро без Arduino: карта, маршруты, эффекты, условия, проекты.
// Тот же код собирается в прошивку и на хосте (tests/host). Поведение — как у
// прототипа proto/maze.js; тайминг — на тиках клока, 24 на четверть, чтобы
// ведущий и ведомый режим шли одним путём.
#pragma once
#include <stdint.h>
#include <string.h>

namespace maze {

constexpr int COLS = 8, ROWS = 4, CELLS = COLS * ROWS, ROUTES = 4, MAX_LEN = 16, SLOTS = 32;
constexpr int OCT_BASE = 5, OCT_MIN = 2, OCT_MAX = 8;
constexpr int PPQN = 24, BAR_TICKS = 4 * PPQN, STEP_TICKS = PPQN / 4;  // 1/1 = шестнадцатые
constexpr int MAX_NOTES = 8;  // нот за один шаг маршрута (ретчет, арпеджио)

// ── ноты
extern const char *const NOTE_NAME[12];
extern const char NOTE_LETTER[12];
extern const uint8_t NOTE_SHARP[12];

// ── скорость и концы маршрута
extern const char *const SPEED_NAME[5];
extern const uint8_t SPEED_TICKS[5];  // 2/1 1/1 1/2 1/4 1/8 → 3 6 12 24 48 тиков на шаг
enum EndOpen : uint8_t { E_LOOP, E_PONG, E_REV };
enum EndClosed : uint8_t { E_FWD, E_CREV };
const char *endName(bool closed, uint8_t end);
inline uint8_t endCount(bool closed) { return closed ? 2 : 3; }

// ── условия
enum Cond : uint8_t { PASS, MEET, NEAR, CHNG, COND_N };
extern const char *const COND_NAME[COND_N];
extern const char *const COND_HELP[COND_N];
// условие по счёту: 0 '--', 1..35 — A:B (1:2 … 8:8), 36 PRE, 37 /PRE
constexpr uint8_t TRIG_N = 38, TRIG_PRE = 36, TRIG_NPRE = 37;
void trigText(uint8_t trig, char *out);  // до 4 знаков
bool trigAB(uint8_t trig, uint8_t &a, uint8_t &b);
void trigHelp(uint8_t trig, char *out);  // до 14 знаков

// ── эффекты: код-буква и до трёх параметров (индексы значений)
struct Param {
  const char *name;
  uint8_t count;            // сколько значений
  const char *const *list;  // текстом, или числом base + i*step
  int8_t base, step;
  bool sign;                // '+' у положительных
  const char *help;         // подсказка внизу расширенного экрана
};
struct FxDef {
  char code;
  uint8_t n;
  Param p[3];
  uint8_t def[3];    // значения по умолчанию
  const char *name;  // полное имя — подсказка в духе LSDj
};
constexpr int FX_N = 14;
extern const FxDef FXDEFS[FX_N];  // A R X P I L O Z V D W T S H
const FxDef *fxDef(char code);
int fxIndex(char code);  // -1 — нет эффекта
int paramValue(const Param &p, uint8_t i);
void paramText(const Param &p, uint8_t i, char *out);

struct Fx {
  char code = 0;  // 0 — нет эффекта
  uint8_t p[3] = {0, 0, 0};
  uint8_t cond = PASS;
  uint8_t trig = 0;
  bool armed = false;   // CHNG: соседа сдвинул эффект
  uint16_t count = 0;   // проходов указателей с начала игры
};
void setFx(Fx &fx, char code);  // новый эффект с параметрами по умолчанию, условие сохраняется

struct Tile {
  int8_t n = -1;   // 0..11, -1 — пусто
  uint8_t o = OCT_BASE;
  uint8_t vel = 0;   // 0 — как у маршрута
  uint8_t gate = 0;  // % шага, 0 — как у маршрута
  Fx fx;
  void clear() { *this = Tile(); }
};

struct Cell {
  int8_t c, r;
};
inline bool same(Cell a, Cell b) { return a.c == b.c && a.r == b.r; }
inline bool adjacent(Cell a, Cell b) {
  int dc = a.c - b.c, dr = a.r - b.r;
  return (dc < 0 ? -dc : dc) + (dr < 0 ? -dr : dr) == 1;
}
inline bool around(Cell a, Cell b) {  // одна из 8 соседних клеток
  int dc = a.c - b.c, dr = a.r - b.r;
  dc = dc < 0 ? -dc : dc;
  dr = dr < 0 ? -dr : dr;
  return (dc > dr ? dc : dr) == 1;
}

struct Route {
  uint8_t len = 0;
  Cell cells[MAX_LEN];
  bool closed = false;
  uint8_t speed = 1, end = 0, ch = 1, vel = 100, gate = 50;
  // ход
  uint8_t i = 0;
  int8_t dir = 1;
  bool fresh = true;
  uint8_t wait = 0;   // W/T: стоит ещё столько шагов
  bool jump = false;  // H: следующий ход — прыжок
  uint8_t jumpMode = 0, jumpN = 1;
  int8_t prevFx = -1;  // для PRE: -1 — ещё не было, 0/1 — промолчал/сработал
  bool muted = false;
  uint16_t acc = 0;  // тиков с прошлого шага
  // для экрана, мс
  uint32_t flashUntil = 0;
  char lastFx = 0;
  uint32_t fxFlashUntil = 0;
};
uint8_t startIndex(const Route &r);

// Нота наружу: смещение и длина в мкс от момента шага. pressure/shape — афтертач (P).
struct Note {
  uint8_t route, ch, note, vel;
  uint32_t at, dur;
  uint8_t pressure;  // 0 — без афтертача
  uint8_t shape;     // 0 HOLD, 1 RISE, 2 FALL
};
struct Output {
  virtual void note(const Note &n) = 0;
  virtual ~Output() {}
};

// изменение тайлов эффектами O, Z, V — со следующего шага
struct TileOp {
  char code;
  Cell at;
  uint8_t a, b;  // индексы параметров
};

struct State {
  // Тайлы — в пуле; live — где они сейчас (их двигают эффекты), src — исходник,
  // где их расставили руками (он и сохраняется). Правка тайла меняет его в пуле — в обоих.
  Tile pool[CELLS];
  uint8_t live[ROWS][COLS];
  uint8_t src[ROWS][COLS];
  Route routes[ROUTES];
  bool playing = false;
  uint16_t bpm = 120;
  bool ext = false;  // клок на MIDI IN — ведомый
  uint32_t tickUs = 0;  // длина тика: своя или измеренная по внешнему клоку
  uint32_t ticks = 0;   // тиков с начала сетки
  bool resumeArmed = false;  // ведомый: снять паузу на ближайшем шаге внешнего клока
  uint32_t extPhase = 0;     // тиков внешнего клока со Start
  TileOp ops[16];
  uint8_t opN = 0;
  uint32_t rng = 0x9E3779B9u;

  Tile &at(Cell p) { return pool[live[p.r][p.c]]; }
  const Tile &at(Cell p) const { return pool[live[p.r][p.c]]; }
  Tile &srcAt(int c, int r) { return pool[src[r][c]]; }
  void resetMap();  // пустая карта, тайлы на исходных местах
};

uint32_t stepTicks(const Route &r);
uint32_t internalTickUs(uint16_t bpm);

// ── ход маршрута
struct Next {
  uint8_t i;
  int8_t dir;
};
Next nextIndex(const Route &r);
void flipDir(Route &r);  // D: разворот на месте

// ── движок
bool condMet(const State &S, int ri, Cell p, Tile &t);  // CHNG разряжается при срабатывании
bool trigMet(const Fx &fx, const Route &r);
// MIDI-эффекты меняют ноты шага; тайловые — откладываются; указателя — меняют маршрут
void applyFx(State &S, Route &r, Cell p, const Fx &fx, Note *notes, int &n, uint32_t stepUs);
void applyPending(State &S);
void trigger(State &S, int ri, uint32_t nowMs, Output *out);
void advance(Route &r);
void restartGrid(State &S);
// один тик клока (24 на четверть); nowMs — для вспышек на экране
void onTick(State &S, uint32_t nowMs, Output *out);
// старт/пауза: ведущий — сразу, ведомый — с ближайшего шага внешнего клока
void togglePlay(State &S);
// тик внешнего клока (ведомый): снимает взведённую паузу на границе шага
void extTick(State &S, uint32_t nowMs, Output *out);
void extStart(State &S);     // 0xFA
void extContinue(State &S);  // 0xFB
void extStop(State &S);      // 0xFC

// ── правка
void clearTile(State &S, Cell p);
void clearRoute(Route &r);

// ── проекты: исходник в двоичном виде (заголовок, данные, CRC)
constexpr int BLOB = 4 + 2 + 1 + CELLS * 10 + ROUTES * (7 + MAX_LEN) + 4;
int serialize(const State &S, uint8_t *out);  // → BLOB байт
bool deserialize(State &S, const uint8_t *in, int len);  // false — не тот формат
void newProject(State &S);  // пустая карта, маршрутов нет
// миниатюра для экрана проектов: есть ли нота в клетке
bool blobHasNote(const uint8_t *blob, int c, int r);
uint16_t blobBpm(const uint8_t *blob);

// демо-проект — как у прототипа
void loadDemo(State &S, int k = 0);  // демо-проект k (0…DEMO_N-1)

}  // namespace maze
