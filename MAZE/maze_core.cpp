// MAZE — реализация ядра. Перенос proto/maze.js: те же правила, тот же порядок действий.
#include "maze_core.h"

#include <stdio.h>

namespace maze {

const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char NOTE_LETTER[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
const uint8_t NOTE_SHARP[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

const char *const SPEED_NAME[5] = {"2/1", "1/1", "1/2", "1/4", "1/8"};
const uint8_t SPEED_TICKS[5] = {3, 6, 12, 24, 48};

const char *endName(bool closed, uint8_t end) {
  static const char *const open[] = {"LOOP", "PONG", "REV"};
  static const char *const ring[] = {"FWD", "REV"};
  return closed ? ring[end % 2] : open[end % 3];
}

const char *const COND_NAME[COND_N] = {"PASS", "MEET", "NEAR", "CHNG"};
const char *const COND_HELP[COND_N] = {"PTR ARRIVES", "TWO PTRS HERE", "PTR NEXT DOOR", "NEIGHB MOVED"};

// ── условие по счёту
bool trigAB(uint8_t trig, uint8_t &a, uint8_t &b) {
  if (trig == 0 || trig > 35) return false;
  uint8_t k = 1;
  for (b = 2; b <= 8; b++)
    for (a = 1; a <= b; a++, k++)
      if (k == trig) return true;
  return false;
}
void trigText(uint8_t trig, char *out) {
  uint8_t a, b;
  if (trigAB(trig, a, b)) sprintf(out, "%u:%u", a, b);
  else strcpy(out, trig == TRIG_PRE ? "PRE" : trig == TRIG_NPRE ? "/PRE" : "--");
}
void trigHelp(uint8_t trig, char *out) {
  uint8_t a, b;
  if (trigAB(trig, a, b)) sprintf(out, "PASS %u OF %u", a, b);
  else strcpy(out, trig == TRIG_PRE ? "LAST FX FIRED" : trig == TRIG_NPRE ? "LAST FX SILENT" : "EVERY PASS");
}

// ── эффекты
static const char *const L_CHD[] = {"OCT", "5TH", "MAJ", "MIN", "SUS", "7TH"};
static const char *const L_NUM[] = {"2", "3", "4"};
static const char *const L_ADIR[] = {"UP", "DN", "UD", "RND"};
static const char *const L_RMP[] = {"FLAT", "UP", "DN"};
static const char *const L_SHP[] = {"HOLD", "RISE", "FALL"};
static const char *const L_ROT[] = {"CW", "CCW"};
static const char *const L_ANG[] = {"45", "90", "180"};
static const char *const L_LR[] = {"L", "R"};
static const char *const L_UD[] = {"UP", "DN"};
static const char *const L_SPD[] = {"FAST", "SLOW"};
static const char *const L_MOD[] = {"FWD", "BACK", "HOME"};

#define LIST(n, l, h) {n, sizeof(l) / sizeof(l[0]), l, 0, 0, false, h}
#define NUM(n, cnt, base, step, sign, h) {n, cnt, nullptr, base, step, sign, h}
const FxDef FXDEFS[FX_N] = {
    {'A', 3, {LIST("CHD", L_CHD, "CHORD"), LIST("NUM", L_NUM, "NOTES IN STEP"), LIST("DIR", L_ADIR, "ARP DIRECTION")}, {2, 1, 0}, "ARPEGGIO"},
    {'R', 2, {NUM("CNT", 7, 2, 1, false, "HITS IN STEP"), LIST("RMP", L_RMP, "VOLUME RAMP")}, {1, 0}, "RETRIGGER"},
    {'X', 1, {NUM("AMT", 8, 8, 8, true, "VELOCITY BOOST")}, {3}, "ACCENT"},
    {'P', 2, {NUM("VAL", 17, 0, 8, false, "PRESSURE"), LIST("SHP", L_SHP, "PRESSURE SHAPE")}, {8, 0}, "AFTERTOUCH"},
    {'I', 1, {NUM("INT", 49, -24, 1, true, "SEMITONES")}, {31}, "INTERVAL"},
    {'L', 0, {}, {}, "LEGATO"},
    {'O', 2, {LIST("DIR", L_ROT, "CW OR CCW"), LIST("ANG", L_ANG, "ANGLE")}, {0, 1}, "ROTATE AROUND"},
    {'Z', 2, {LIST("DIR", L_LR, "LEFT OR RIGHT"), NUM("STP", 3, 1, 1, false, "CELLS TO SHIFT")}, {1, 0}, "SHIFT ROW"},
    {'V', 2, {LIST("DIR", L_UD, "UP OR DOWN"), NUM("STP", 3, 1, 1, false, "CELLS TO SHIFT")}, {1, 0}, "SHIFT COLUMN"},
    {'D', 0, {}, {}, "TURN AROUND"},
    {'W', 1, {NUM("STP", 8, 1, 1, false, "STEPS TO WAIT")}, {1}, "WAIT SILENT"},
    {'T', 1, {NUM("STP", 8, 1, 1, false, "STEPS TO HOLD")}, {1}, "WAIT HOLDING"},
    {'S', 1, {LIST("DIR", L_SPD, "FASTER/SLOWER")}, {0}, "SPEED STEP"},
    {'H', 2, {LIST("MOD", L_MOD, "WHERE TO JUMP"), NUM("N", 8, 1, 1, false, "STEPS")}, {0, 1}, "JUMP"},
};
#undef LIST
#undef NUM

int fxIndex(char code) {
  for (int i = 0; i < FX_N; i++)
    if (FXDEFS[i].code == code) return i;
  return -1;
}
const FxDef *fxDef(char code) {
  int i = fxIndex(code);
  return i < 0 ? nullptr : &FXDEFS[i];
}
int paramValue(const Param &p, uint8_t i) {
  int v = p.base + i * p.step;
  return v > 127 ? 127 : v;  // VAL афтертача: 0…120 и 127
}
void paramText(const Param &p, uint8_t i, char *out) {
  if (p.list) {
    strcpy(out, p.list[i]);
    return;
  }
  int v = paramValue(p, i);
  sprintf(out, p.sign && v > 0 ? "+%d" : "%d", v);
}
void setFx(Fx &fx, char code) {
  uint8_t cond = fx.cond;
  fx = Fx();
  if (!code) return;
  const FxDef *d = fxDef(code);
  fx.code = code;
  fx.cond = cond;
  for (int k = 0; k < d->n; k++) fx.p[k] = d->def[k];
}

// ── маршрут
uint8_t startIndex(const Route &r) {
  return !r.closed && r.end == E_REV && r.len ? r.len - 1 : 0;
}
uint32_t stepTicks(const Route &r) { return SPEED_TICKS[r.speed]; }
uint32_t internalTickUs(uint16_t bpm) { return 60000000u / ((uint32_t)bpm * PPQN); }

Next nextIndex(const Route &r) {
  int len = r.len;
  if (len <= 1) return {0, r.dir};
  bool fwd = r.closed ? r.end == E_FWD : r.end == E_LOOP;
  bool rev = r.closed ? r.end == E_CREV : r.end == E_REV;
  if (fwd) return {(uint8_t)((r.i + 1) % len), 1};
  if (rev) return {(uint8_t)((r.i - 1 + len) % len), -1};
  int8_t dir = r.dir;
  int i = r.i + dir;
  if (i < 0 || i >= len) {
    dir = -dir;
    i = r.i + dir;
  }
  return {(uint8_t)i, dir};
}
void flipDir(Route &r) {
  if (r.closed) r.end = 1 - r.end;
  else if (r.end == E_PONG) r.dir = -r.dir;
  else r.end = r.end == E_LOOP ? E_REV : E_LOOP;
}

void State::resetMap() {
  for (int k = 0; k < CELLS; k++) pool[k].clear();
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) live[r][c] = src[r][c] = r * COLS + c;
  opN = 0;
}

// ── условия (проверяются, когда указатель пришёл на тайл; соседи — все 8 клеток)
bool condMet(const State &S, int ri, Cell p, Tile &t) {
  switch (t.fx.cond) {
    case PASS:
      return true;
    case MEET:
    case NEAR:
      for (int k = 0; k < ROUTES; k++) {
        const Route &o = S.routes[k];
        if (k == ri || !o.len) continue;
        Cell q = o.cells[o.i];
        if (t.fx.cond == MEET ? same(q, p) : around(q, p)) return true;
      }
      return false;
    case CHNG:  // взведён, когда соседа сдвинул или повернул эффект (вопрос в бэклоге)
      if (!t.fx.armed) return false;
      t.fx.armed = false;
      return true;
  }
  return false;
}
bool trigMet(const Fx &fx, const Route &r) {
  if (fx.trig == TRIG_PRE) return r.prevFx == 1;
  if (fx.trig == TRIG_NPRE) return r.prevFx != 1;
  uint8_t a, b;
  if (!trigAB(fx.trig, a, b)) return true;
  return (fx.count - 1) % b == (uint32_t)(a - 1);
}

static uint32_t rnd(State &S) {  // xorshift — для RND арпеджио
  S.rng ^= S.rng << 13;
  S.rng ^= S.rng >> 17;
  S.rng ^= S.rng << 5;
  return S.rng;
}

static const int8_t CHORDS[6][4] = {{0, 12, -1, -1}, {0, 7, 12, -1}, {0, 4, 7, 12}, {0, 3, 7, 12}, {0, 5, 7, 12}, {0, 4, 7, 10}};

void applyFx(State &S, Route &r, Cell p, const Fx &fx, Note *notes, int &n, uint32_t stepUs) {
  const FxDef *d = fxDef(fx.code);
  auto val = [&](int k) { return paramValue(d->p[k], fx.p[k]); };
  switch (fx.code) {
    case 'A': {
      if (!n) break;
      Note base = notes[0];
      int8_t tones[4];
      int tn = 0;
      for (int k = 0; k < 4 && CHORDS[fx.p[0]][k] >= 0; k++) tones[tn++] = CHORDS[fx.p[0]][k];
      int8_t seq[8];
      int sn = 0;
      uint8_t dir = fx.p[2];  // UP DN UD RND
      if (dir == 1)
        for (int k = tn - 1; k >= 0; k--) seq[sn++] = tones[k];
      else {
        for (int k = 0; k < tn; k++) seq[sn++] = tones[k];
        if (dir == 2)
          for (int k = tn - 2; k >= 1; k--) seq[sn++] = tones[k];
      }
      int cnt = 2 + fx.p[1];
      n = 0;
      for (int k = 0; k < cnt; k++) {
        int iv = dir == 3 ? tones[rnd(S) % tn] : seq[k % sn];
        Note nt = base;
        int m = base.note + iv;
        nt.note = m > 127 ? 127 : m;
        nt.at = (uint32_t)((uint64_t)k * stepUs / cnt);
        nt.dur = (uint32_t)((uint64_t)base.dur * 9 / (10 * cnt));
        notes[n++] = nt;
      }
      break;
    }
    case 'R': {
      if (!n) break;
      Note base = notes[0];
      int cnt = val(0);
      n = 0;
      for (int k = 0; k < cnt; k++) {
        Note nt = base;
        int num = fx.p[1] == 0 ? cnt : fx.p[1] == 1 ? k + 1 : cnt - k;  // FLAT UP DN
        int v = (base.vel * num * 2 + cnt) / (2 * cnt);                   // round(vel * num / cnt)
        nt.vel = v < 1 ? 1 : v;
        nt.at = (uint32_t)((uint64_t)k * stepUs / cnt);
        nt.dur = stepUs / cnt / 2;
        notes[n++] = nt;
      }
      break;
    }
    case 'X':
      for (int k = 0; k < n; k++) notes[k].vel = notes[k].vel + val(0) > 127 ? 127 : notes[k].vel + val(0);
      break;
    case 'P':
      for (int k = 0; k < n; k++) {
        notes[k].pressure = val(0);
        notes[k].shape = fx.p[1];
      }
      break;
    case 'I':
      for (int k = 0; k < n; k++) {
        int m = notes[k].note + val(0);
        notes[k].note = m < 0 ? 0 : m > 127 ? 127 : m;
      }
      break;
    case 'L':
      for (int k = 0; k < n; k++) notes[k].dur = stepUs + stepUs / 50;
      break;
    case 'O':
    case 'Z':
    case 'V':
      if (S.opN < 16) S.ops[S.opN++] = {fx.code, p, fx.p[0], fx.p[1]};
      break;
    case 'D':
      flipDir(r);
      break;
    case 'W':
      r.wait = val(0);
      break;
    case 'T':
      r.wait = val(0);
      for (int k = 0; k < n; k++) notes[k].dur = (uint32_t)((uint64_t)stepUs * (r.wait + 1) * 98 / 100);
      break;
    case 'S': {
      int sp = r.speed + (fx.p[0] == 0 ? -1 : 1);
      r.speed = sp < 0 ? 0 : sp > 4 ? 4 : sp;
      break;
    }
    case 'H':
      r.jump = true;
      r.jumpMode = fx.p[0];
      r.jumpN = val(1);
      break;
  }
}

// Поворот 8 соседей (тор: за краем — противоположный край) и сдвиги рядов/столбцов.
// Тайл переезжает целиком. Тайлы с CHNG рядом с изменёнными клетками взводятся.
static const int8_t RING[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};
void applyPending(State &S) {
  uint8_t before[ROWS][COLS];
  memcpy(before, S.live, sizeof(before));
  for (int k = 0; k < S.opN; k++) {
    const TileOp &op = S.ops[k];
    uint8_t snap[ROWS][COLS];
    memcpy(snap, S.live, sizeof(snap));
    if (op.code == 'O') {
      static const int8_t steps[] = {1, 2, 4};
      int sh = steps[op.b] * (op.a == 0 ? 1 : -1);
      for (int j = 0; j < 8; j++) {
        int fc = (op.at.c + RING[j][0] + COLS) % COLS, fr = (op.at.r + RING[j][1] + ROWS) % ROWS;
        const int8_t *t = RING[(j + sh + 8) % 8];
        int tc = (op.at.c + t[0] + COLS) % COLS, tr = (op.at.r + t[1] + ROWS) % ROWS;
        S.live[tr][tc] = snap[fr][fc];
      }
    } else if (op.code == 'Z') {
      int s = (op.b + 1) * (op.a == 1 ? 1 : -1);
      for (int c = 0; c < COLS; c++) S.live[op.at.r][(c + s + COLS) % COLS] = snap[op.at.r][c];
    } else {
      int s = (op.b + 1) * (op.a == 1 ? 1 : -1);
      for (int r = 0; r < ROWS; r++) S.live[(r + s + ROWS) % ROWS][op.at.c] = snap[r][op.at.c];
    }
  }
  S.opN = 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      Tile &t = S.pool[S.live[r][c]];
      if (!t.fx.code || t.fx.cond != CHNG) continue;
      for (int rr = 0; rr < ROWS && !t.fx.armed; rr++)
        for (int cc = 0; cc < COLS; cc++)
          if (S.live[rr][cc] != before[rr][cc] && around({(int8_t)cc, (int8_t)rr}, {(int8_t)c, (int8_t)r})) {
            t.fx.armed = true;
            break;
          }
    }
}

void trigger(State &S, int ri, uint32_t nowMs, Output *out) {
  Route &r = S.routes[ri];
  Cell p = r.cells[r.i];
  Tile &t = S.at(p);
  uint32_t stepUs = stepTicks(r) * S.tickUs;
  uint8_t vel = t.vel ? t.vel : r.vel;
  uint8_t gate = t.gate ? t.gate : r.gate;
  Note notes[MAX_NOTES];
  int n = 0;
  if (t.n >= 0) notes[n++] = {(uint8_t)ri, r.ch, (uint8_t)(12 * (t.o + 1) + t.n), vel, 0, (uint32_t)((uint64_t)stepUs * gate / 100), 0, 0};
  if (t.fx.code) {
    // каждый проход считается; если счёт не совпал, условие не проверяется (взведённый CHNG ждёт)
    Fx &fx = t.fx;
    fx.count++;
    bool fired = trigMet(fx, r) && condMet(S, ri, p, t);
    r.prevFx = fired;  // для PRE следующего тайла с эффектом
    if (fired) {
      applyFx(S, r, p, fx, notes, n, stepUs);
      r.lastFx = fx.code;  // в колонке маршрута — последний сработавший
      r.fxFlashUntil = nowMs + 160;
    }
  }
  if (r.muted) return;  // заглушённый молчит, но указатель идёт и эффекты срабатывают
  if (n) r.flashUntil = nowMs + (stepUs / 2000 < 120 ? stepUs / 2000 : 120);
  if (out)
    for (int k = 0; k < n; k++) out->note(notes[k]);
}

void advance(Route &r) {
  if (!r.jump) {
    Next nx = nextIndex(r);
    r.i = nx.i;
    r.dir = nx.dir;
    return;
  }
  r.jump = false;
  if (r.jumpMode == 2) {  // HOME
    r.i = startIndex(r);
    r.dir = 1;
    return;
  }
  if (r.jumpMode == 1) flipDir(r);  // BACK
  for (int k = 0; k < r.jumpN; k++) {
    Next nx = nextIndex(r);
    r.i = nx.i;
    r.dir = nx.dir;
  }
  if (r.jumpMode == 1) flipDir(r);
}

// пауза: указатели стоят; продолжение — сетка с начала, с тех же позиций; счёт тоже с начала
void restartGrid(State &S) {
  for (Route &r : S.routes) {
    r.fresh = true;
    r.acc = 0;
    r.prevFx = -1;
  }
  for (Tile &t : S.pool) t.fx.count = 0;
  S.ticks = 0;
}

void onTick(State &S, uint32_t nowMs, Output *out) {
  if (!S.playing || S.resumeArmed) return;
  bool due[ROUTES], any = false;
  for (int ri = 0; ri < ROUTES; ri++) {
    Route &r = S.routes[ri];
    due[ri] = false;
    if (!r.len) continue;
    if (r.fresh) due[ri] = true;
    else due[ri] = ++r.acc >= stepTicks(r);
    any |= due[ri];
  }
  if (any && S.opN) applyPending(S);  // изменения тайлов — к следующему шагу
  for (int ri = 0; ri < ROUTES; ri++) {
    if (!due[ri]) continue;
    Route &r = S.routes[ri];
    r.acc = 0;
    if (r.fresh) r.fresh = false;
    else if (r.wait) {
      r.wait--;  // стоит на месте: W молчит, у T нота ещё тянется
      continue;
    } else advance(r);
    trigger(S, ri, nowMs, out);
  }
  S.ticks++;
}

void togglePlay(State &S) {
  if (S.playing) {
    S.playing = false;
    S.resumeArmed = false;
    return;
  }
  S.playing = true;
  if (S.ext) S.resumeArmed = true;  // ведомый: с ближайшего шага внешнего клока
  else restartGrid(S);
}
void extTick(State &S, uint32_t nowMs, Output *out) {
  if (S.playing && S.resumeArmed && S.extPhase % STEP_TICKS == 0) {
    S.resumeArmed = false;
    restartGrid(S);
  }
  onTick(S, nowMs, out);
  S.extPhase++;
}
void extStart(State &S) {
  S.extPhase = 0;
  if (!S.playing) togglePlay(S);
}
void extContinue(State &S) {
  if (!S.playing) togglePlay(S);
}
void extStop(State &S) {
  if (S.playing) togglePlay(S);
}

void clearTile(State &S, Cell p) { S.at(p).clear(); }
void clearRoute(Route &r) {
  r.len = 0;
  r.closed = false;
  r.i = 0;
  r.dir = 1;
  r.wait = 0;
  r.jump = false;
  r.prevFx = -1;
  r.lastFx = 0;
  r.acc = 0;
  r.fresh = true;
}

// ── проекты
static uint32_t crc32(const uint8_t *p, int n) {
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}
static const int TILES_AT = 7, ROUTES_AT = TILES_AT + CELLS * 10;

int serialize(const State &S, uint8_t *o) {
  memset(o, 0, BLOB);
  memcpy(o, "MAZE", 4);
  o[4] = 1;  // версия формата
  o[6] = S.bpm > 255 ? 255 : S.bpm;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      const Tile &t = S.pool[S.src[r][c]];
      uint8_t *b = o + TILES_AT + (r * COLS + c) * 10;
      b[0] = (uint8_t)t.n;
      b[1] = t.o;
      b[2] = t.vel;
      b[3] = t.gate;
      b[4] = (uint8_t)t.fx.code;
      b[5] = t.fx.p[0];
      b[6] = t.fx.p[1];
      b[7] = t.fx.p[2];
      b[8] = t.fx.cond;
      b[9] = t.fx.trig;
    }
  for (int k = 0; k < ROUTES; k++) {
    const Route &r = S.routes[k];
    uint8_t *b = o + ROUTES_AT + k * (7 + MAX_LEN);
    b[0] = r.len;
    b[1] = r.closed;
    b[2] = r.speed;
    b[3] = r.end;
    b[4] = r.ch;
    b[5] = r.vel;
    b[6] = r.gate;
    for (int i = 0; i < r.len; i++) b[7 + i] = r.cells[i].r * COLS + r.cells[i].c;
  }
  uint32_t crc = crc32(o, BLOB - 4);
  memcpy(o + BLOB - 4, &crc, 4);
  return BLOB;
}

bool deserialize(State &S, const uint8_t *in, int len) {
  if (len != BLOB || memcmp(in, "MAZE", 4) || in[4] != 1) return false;
  uint32_t crc;
  memcpy(&crc, in + BLOB - 4, 4);
  if (crc != crc32(in, BLOB - 4)) return false;
  S.resetMap();
  S.bpm = in[6] ? in[6] : 120;
  for (int k = 0; k < CELLS; k++) {
    const uint8_t *b = in + TILES_AT + k * 10;
    Tile &t = S.pool[k];
    t.n = (int8_t)b[0];
    t.o = b[1];
    t.vel = b[2];
    t.gate = b[3];
    t.fx.code = (char)b[4];
    t.fx.p[0] = b[5];
    t.fx.p[1] = b[6];
    t.fx.p[2] = b[7];
    t.fx.cond = b[8];
    t.fx.trig = b[9];
  }
  for (int k = 0; k < ROUTES; k++) {
    Route &r = S.routes[k];
    r = Route();
    const uint8_t *b = in + ROUTES_AT + k * (7 + MAX_LEN);
    r.len = b[0] > MAX_LEN ? MAX_LEN : b[0];
    r.closed = b[1];
    r.speed = b[2] > 4 ? 1 : b[2];
    r.end = b[3];
    r.ch = b[4];
    r.vel = b[5];
    r.gate = b[6];
    for (int i = 0; i < r.len; i++) r.cells[i] = {(int8_t)(b[7 + i] % COLS), (int8_t)(b[7 + i] / COLS)};
    r.i = startIndex(r);
  }
  restartGrid(S);
  return true;
}

void newProject(State &S) {
  S.resetMap();
  for (int k = 0; k < ROUTES; k++) {
    S.routes[k] = Route();
    S.routes[k].ch = k + 1;
  }
  restartGrid(S);
}

bool blobHasNote(const uint8_t *blob, int c, int r) { return (int8_t)blob[TILES_AT + (r * COLS + c) * 10] >= 0; }
uint16_t blobBpm(const uint8_t *blob) { return blob[6]; }

// ── демо — как у прототипа
void loadDemo(State &S) {
  static const char *const grid[ROWS][COLS] = {
      {"C5", "-", "E5", "G5", "-", "A4", "-", "C6"},
      {"-", "D5", "-", "B4", "F#5", "-", "B4", "-"},
      {"G4", "-", "C5", "-", "-", "D#5", "-", "E5"},
      {"-", "A5", "-", "F5", "G#4", "-", "-", "C4"},
  };
  newProject(S);
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      const char *s = grid[r][c];
      if (s[0] == '-') continue;
      Tile &t = S.srcAt(c, r);
      bool sharp = s[1] == '#';
      for (int n = 0; n < 12; n++)
        if (NOTE_LETTER[n] == s[0] && NOTE_SHARP[n] == sharp) t.n = n;
      t.o = s[sharp ? 2 : 1] - '0';
    }
  struct D {
    int c, r;
    char code;
    uint8_t cond;
    int8_t p0, p1;
    uint8_t trig;
  };
  // [столбец, ряд, эффект, условие, параметры, условие по счёту]
  static const D fx[] = {
      {2, 0, 'R', PASS, 1, 0, 1},     // ретчет ×3 на каждом втором проходе (1:2)
      {3, 0, 'X', PASS, -1, -1, TRIG_PRE},  // акцент — только если перед этим сработал ретчет
      {1, 1, 'A', PASS, -1, -1, 0},   // арпеджио MAJ, 3 ноты вверх
      {5, 2, 'D', MEET, -1, -1, 0},   // маршруты 2 и 4 встретились — разворот
      {7, 3, 'X', PASS, -1, -1, 0},   // акцент +32
      {3, 3, 'I', PASS, -1, -1, 0},   // интервал +7
      {6, 1, 'O', PASS, -1, -1, 9},   // каждый 4-й проход (4:4) — поворот соседей на 90°
      {4, 1, 'R', CHNG, 2, 2, 0},     // соседей повернули — ретчет ×4 на спад
      {4, 3, 'W', PASS, 1, -1, 0},    // стоп 2 шага молча
  };
  for (const D &d : fx) {
    Fx &f = S.srcAt(d.c, d.r).fx;
    setFx(f, d.code);
    f.cond = d.cond;
    if (d.p0 >= 0) f.p[0] = d.p0;
    if (d.p1 >= 0) f.p[1] = d.p1;
    f.trig = d.trig;
  }
  struct RD {
    int8_t cells[MAX_LEN][2];
    uint8_t len;
    bool closed;
    uint8_t speed, end;
  };
  static const RD routes[] = {
      {{{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {2, 1}, {1, 1}, {0, 1}}, 8, true, 1, 0},
      {{{4, 1}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {7, 3}}, 6, false, 2, 1},
      {{{0, 2}, {0, 3}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}}, 8, false, 1, 0},
      {{{5, 0}, {6, 0}, {7, 0}, {7, 1}, {6, 1}, {5, 1}, {5, 2}}, 7, false, 3, 2},
  };
  for (int k = 0; k < ROUTES; k++) {
    Route &r = S.routes[k];
    const RD &d = routes[k];
    r.len = d.len;
    r.closed = d.closed;
    r.speed = d.speed;
    r.end = d.end;
    for (int i = 0; i < d.len; i++) r.cells[i] = {d.cells[i][0], d.cells[i][1]};
    r.i = startIndex(r);
  }
}

}  // namespace maze
