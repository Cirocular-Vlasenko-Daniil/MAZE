// Проверки ядра и жестов прошивки — те же, что tests/fx.js у прототипа, но на тиках клока.
#include <stdio.h>
#include <vector>

#include "../../MAZE/maze_ui.h"
#include "../../MAZE/maze_demos.h"

using namespace maze;

static int fails = 0, passes = 0;
static void ok(bool c, const char *msg) {
  if (c) passes++;
  else {
    fails++;
    printf("FAIL %s\n", msg);
  }
}

struct Cap : Output {
  std::vector<Note> notes;
  void note(const Note &n) override { notes.push_back(n); }
};
struct MemStore : Store {
  uint8_t blob[SLOTS][BLOB];
  bool has[SLOTS] = {}, autoF[SLOTS] = {};
  int cur = -1, writes = 0;
  bool read(int k, uint8_t *b, bool &a) override {
    if (!has[k]) return false;
    memcpy(b, blob[k], BLOB);
    a = autoF[k];
    return true;
  }
  bool write(int k, const uint8_t *b, bool a) override {
    memcpy(blob[k], b, BLOB);
    has[k] = true;
    autoF[k] = a;
    writes++;
    return true;
  }
  int current() override { return cur; }
  void setCurrent(int k) override { cur = k; }
  int demoV = 0;
  int demoVersion() override { return demoV; }
  void setDemoVersion(int v) override { demoV = v; }
};

static void blank(State &S) {
  newProject(S);
  S.tickUs = internalTickUs(120);  // 20 833 мкс, шестнадцатая — 125 мс
}
static void route(State &S, int k, std::initializer_list<Cell> cells, bool closed = false, uint8_t end = 0) {
  Route &r = S.routes[k];
  r.len = 0;
  for (Cell c : cells) r.cells[r.len++] = c;
  r.closed = closed;
  r.end = end;
  r.i = startIndex(r);
}
static void ticks(State &S, int n, Output *o = nullptr) {
  for (int k = 0; k < n; k++) onTick(S, 0, o);
}

int main() {
  State S;

  // ── поворот, сдвиги, взвод CHNG
  loadDemo(S);
  uint8_t tl = S.live[0][5];
  S.ops[S.opN++] = {'O', {6, 1}, 0, 1};
  applyPending(S);
  ok(S.live[0][7] == tl, "O CW 90: левый верхний сосед уехал в правый верхний");
  ok(S.at({6, 1}).n == 11 && S.at({6, 1}).o == 4, "O: центр на месте");
  loadDemo(S);
  uint8_t corner = S.live[3][7];
  S.ops[S.opN++] = {'O', {0, 0}, 0, 0};
  applyPending(S);
  ok(S.live[3][0] == corner, "O на краю: сосед через тор сдвинулся на одну позицию");
  loadDemo(S);
  uint8_t row0[COLS];
  memcpy(row0, S.live[0], COLS);
  S.ops[S.opN++] = {'Z', {2, 0}, 1, 0};
  applyPending(S);
  ok(S.live[0][0] == row0[7] && S.live[0][3] == row0[2], "Z R 1: ряд сдвинут вправо с заворотом");
  loadDemo(S);
  uint8_t col3[ROWS];
  for (int r = 0; r < ROWS; r++) col3[r] = S.live[r][3];
  S.ops[S.opN++] = {'V', {3, 1}, 1, 1};
  applyPending(S);
  ok(S.live[2][3] == col3[0] && S.live[0][3] == col3[2], "V DN 2: столбец сдвинут вниз с заворотом");

  blank(S);
  setFx(S.at({4, 1}).fx, 'X');
  S.at({4, 1}).fx.cond = CHNG;
  S.at({5, 0}).n = 0;
  S.ops[S.opN++] = {'O', {6, 1}, 0, 1};
  applyPending(S);
  ok(S.at({4, 1}).fx.armed, "CHNG: соседа повернули — тайл взведён");
  route(S, 0, {{4, 1}, {4, 2}});
  ok(condMet(S, 0, {4, 1}, S.at({4, 1})) && !S.at({4, 1}).fx.armed, "CHNG: сработал при приходе и разряжен");
  ok(!condMet(S, 0, {4, 1}, S.at({4, 1})), "CHNG: второй раз без изменений не срабатывает");

  blank(S);
  setFx(S.at({1, 1}).fx, 'X');
  S.at({1, 1}).fx.cond = NEAR;
  route(S, 0, {{1, 1}});
  route(S, 1, {{2, 2}});
  ok(condMet(S, 0, {1, 1}, S.at({1, 1})), "NEAR: указатель по диагонали считается соседом");
  route(S, 1, {{3, 3}});
  ok(!condMet(S, 0, {1, 1}, S.at({1, 1})), "NEAR: дальше одной клетки — нет");
  route(S, 1, {{1, 1}});
  S.at({1, 1}).fx.cond = MEET;
  ok(condMet(S, 0, {1, 1}, S.at({1, 1})), "MEET: второй указатель на том же тайле");

  // ── разворот
  blank(S);
  route(S, 0, {{0, 0}, {1, 0}, {2, 0}, {3, 0}});
  Route &r0 = S.routes[0];
  r0.i = 2;
  flipDir(r0);
  ok(r0.end == E_REV && r0.i == 2 && nextIndex(r0).i == 1, "D: LOOP → REV на месте");
  r0.end = E_PONG;
  r0.dir = 1;
  flipDir(r0);
  ok(r0.dir == -1, "D: у PONG разворот сразу");
  r0.closed = true;
  r0.end = E_FWD;
  flipDir(r0);
  ok(r0.end == E_CREV, "D: замкнутый FWD → REV");

  // ── ход по тикам: W, H, S
  blank(S);
  route(S, 0, {{0, 0}, {1, 0}, {2, 0}, {3, 0}});
  setFx(S.at({1, 0}).fx, 'W');  // стоп 2 шага
  togglePlay(S);
  ticks(S, 1 + 6 * 4);  // такты: 0, 1 (W), стоит, стоит, 2
  ok(S.routes[0].i == 2, "W 2: за 5 шагов указатель прошёл 0 → 1, постоял два шага и встал на 2");
  blank(S);
  route(S, 0, {{0, 0}, {1, 0}, {2, 0}, {3, 0}});
  setFx(S.at({2, 0}).fx, 'H');
  S.at({2, 0}).fx.p[0] = 2;  // HOME
  togglePlay(S);
  ticks(S, 1 + 6 * 3);
  ok(S.routes[0].i == 0, "H HOME: после клетки с прыжком указатель в начале");
  blank(S);
  route(S, 0, {{0, 0}, {1, 0}});
  setFx(S.at({0, 0}).fx, 'S');
  togglePlay(S);
  ticks(S, 1);
  ok(S.routes[0].speed == 0, "S FAST: 1/1 → 2/1");
  ticks(S, 3);
  ok(S.routes[0].i == 1, "после S шаг — 3 тика (2/1)");

  // ── MIDI-эффекты
  blank(S);
  route(S, 0, {{0, 0}});
  Note ns[MAX_NOTES];
  int n = 1;
  ns[0] = {0, 1, 72, 100, 0, 60000, 0, 0};
  Fx fr;
  setFx(fr, 'R');
  fr.p[0] = 2;  // 4 удара
  fr.p[1] = 1;  // нарастание
  applyFx(S, S.routes[0], {0, 0}, fr, ns, n, 120000);
  ok(n == 4 && ns[3].at == 90000 && ns[0].vel == 25 && ns[3].vel == 100, "R 4 UP: четыре удара, громкость нарастает");
  n = 1;
  ns[0] = {0, 1, 72, 100, 0, 60000, 0, 0};
  Fx fa;
  setFx(fa, 'A');
  applyFx(S, S.routes[0], {0, 0}, fa, ns, n, 120000);
  ok(n == 3 && ns[0].note == 72 && ns[1].note == 76 && ns[2].note == 79, "A MAJ 3 UP: C E G");
  n = 1;
  ns[0] = {0, 1, 72, 100, 0, 60000, 0, 0};
  Fx fi;
  setFx(fi, 'I');
  applyFx(S, S.routes[0], {0, 0}, fi, ns, n, 120000);
  ok(ns[0].note == 79, "I +7: квинта вверх");

  // ── условие по счёту
  blank(S);
  route(S, 0, {{0, 0}});
  setFx(S.at({0, 0}).fx, 'X');
  S.at({0, 0}).fx.trig = 4;  // 2:3
  char fired[8] = {};
  for (int k = 0; k < 6; k++) {
    S.routes[0].lastFx = 0;
    trigger(S, 0, 0, nullptr);
    fired[k] = S.routes[0].lastFx ? '1' : '0';
  }
  ok(!strcmp(fired, "010010"), "TRG 2:3: на втором из каждых трёх проходов");
  blank(S);
  route(S, 0, {{0, 0}, {1, 0}});
  setFx(S.at({0, 0}).fx, 'R');
  S.at({0, 0}).fx.trig = 1;  // 1:2
  setFx(S.at({1, 0}).fx, 'X');
  S.at({1, 0}).fx.trig = TRIG_PRE;
  char pre[8] = {};
  for (int k = 0; k < 4; k++) {
    S.routes[0].i = 0;
    trigger(S, 0, 0, nullptr);
    S.routes[0].i = 1;
    S.routes[0].lastFx = 0;
    trigger(S, 0, 0, nullptr);
    pre[k] = S.routes[0].lastFx ? '1' : '0';
  }
  ok(!strcmp(pre, "1010"), "PRE: акцент только за сработавшим ретчетом");
  blank(S);
  route(S, 0, {{0, 0}});
  setFx(S.at({0, 0}).fx, 'X');
  S.at({0, 0}).fx.cond = CHNG;
  S.at({0, 0}).fx.trig = 2;  // 2:2
  S.at({0, 0}).fx.armed = true;
  S.routes[0].lastFx = 0;
  trigger(S, 0, 0, nullptr);
  ok(!S.routes[0].lastFx && S.at({0, 0}).fx.armed, "TRG + CHNG: на непопадании тайл остаётся взведённым");
  trigger(S, 0, 0, nullptr);
  ok(S.routes[0].lastFx == 'X' && !S.at({0, 0}).fx.armed, "TRG + CHNG: на втором проходе сработал");
  char tt[8];
  trigText(35, tt);
  ok(!strcmp(tt, "8:8"), "список TRG: 35 — 8:8");
  trigText(TRIG_NPRE, tt);
  ok(!strcmp(tt, "/PRE"), "список TRG: /PRE");

  // ── ноты наружу, мьют
  {
    Cap cap;
    loadDemo(S);
    S.tickUs = internalTickUs(120);
    S.playing = false;
    togglePlay(S);
    onTick(S, 0, &cap);
    ok(cap.notes.size() >= 3 && cap.notes[0].ch == 1, "старт: каждый маршрут играет свою первую клетку");
    cap.notes.clear();
    S.routes[0].muted = true;
    for (int k = 0; k < 48; k++) onTick(S, 0, &cap);
    bool any0 = false;
    for (auto &nt : cap.notes) any0 |= nt.route == 0;
    ok(!any0 && cap.notes.size() > 0, "заглушённый маршрут молчит, остальные играют");
  }

  // ── исходник и проекты
  loadDemo(S);
  uint8_t a[BLOB], b[BLOB];
  serialize(S, a);
  S.ops[S.opN++] = {'O', {6, 1}, 0, 1};
  applyPending(S);
  serialize(S, b);
  ok(!memcmp(a, b, BLOB), "поворот не меняет исходник");
  S.at({7, 0}).n = 2;  // тайл, который поворот перенёс: он же и в исходнике
  serialize(S, b);
  ok(memcmp(a, b, BLOB), "правка тайла попадает в исходник");
  State T;
  ok(deserialize(T, a, BLOB), "проект читается обратно");
  serialize(T, b);
  ok(!memcmp(a, b, BLOB), "чтение и запись — без потерь");
  a[100] ^= 1;
  ok(!deserialize(T, a, BLOB), "битый файл не читается (CRC)");

  {
    MemStore st;
    State P;
    Ui u;
    u.S = &P;
    u.store = &st;
    u.boot(0);
    ok(st.has[0] && !st.autoF[0] && u.slot == 0 && !u.autosave, "чистая плата: демо в слоте 01, сохранено вручную");
    bool demos = st.demoV == DEMO_VERSION && st.writes == DEMO_N;
    for (int k = 0; k < DEMO_N; k++) demos &= st.has[k] && !st.autoF[k];
    ok(demos && !st.has[DEMO_N], "чистая плата: демо 01…05 разложены один раз");
    P.at({0, 0}).n = 3;
    u.update(300);
    u.update(3000);
    ok(st.writes == DEMO_N, "ручной слот: правка не пишется сама");
    u.mode = M_MAP;
    u.onTapDown(4000);
    u.onPageDown(4001);  // TAP+PAGE
    u.onPageUp(4002);
    u.onTapUp(4003);
    ok(u.mode == M_PROJ, "TAP+PAGE — проекты");
    int bpm0 = P.bpm;
    u.onEnc(3, 4050);
    ok(P.bpm == bpm0 + 3 && u.projSel == 0, "проекты: энкодер — темп загруженного");
    u.onTapDown(4060);
    u.onEnc(5, 4100);  // TAP+энкодер — слот
    u.onTapUp(4110);
    ok(u.projSel == 5, "проекты: TAP+энкодер — слот");
    u.onEnc(4, 4120);
    ok(P.bpm == bpm0 + 3, "проекты: на чужом слоте энкодер темп не трогает");
    u.onSwDown(4200);  // LOAD пустого слота 06
    u.update(4500);
    u.onSwUp(4600);
    ok(u.slot == 0, "короткое удержание — отмена");
    u.onSwDown(5000);
    u.update(5000 + HOLD_MS);
    u.update(5001 + HOLD_MS);
    ok(u.slot == 5 && u.autosave, "пустой слот: новый проект, автосохранение включено");
    P.at({0, 0}).n = 4;
    u.update(7000);
    u.update(8000);
    ok(!st.has[5], "черновик: через секунду ещё не пишет");
    u.update(9100);
    ok(st.has[5] && st.autoF[5], "черновик: через 2 с записан как автосохранение");
    u.tap = true;
    u.onSwDown(10000);  // T + удержание — SAVE
    u.tap = false;
    u.update(10000 + HOLD_MS);
    ok(!st.autoF[5] && !u.autosave, "SAVE: слот защищён, автосохранение выключено");
    P.at({1, 0}).n = 4;
    u.update(12000);
    u.update(15000);
    ok(!st.autoF[5] && u.dirty(), "после SAVE правка не пишется, видна как «+»");
  }

  // ── демо: новая версия — только в пустые слоты, записанное не трогается
  {
    MemStore st;
    State P;
    Ui u;
    u.S = &P;
    u.store = &st;
    State mine;
    newProject(mine);
    mine.at({5, 2}).n = 9;
    serialize(mine, st.blob[2]);
    st.has[2] = true;  // слот 03 — проект пользователя
    st.has[0] = true;  // слот 01 — старое демо
    loadDemo(mine, 0);
    serialize(mine, st.blob[0]);
    st.cur = 2;
    u.boot(0);
    uint8_t d3[BLOB];
    loadDemo(mine, 2);
    serialize(mine, d3);
    ok(st.writes == DEMO_N - 2 && st.has[1] && st.has[3] && st.has[4], "демо: дописаны только пустые слоты");
    ok(memcmp(st.blob[2], d3, BLOB) != 0 && u.slot == 2 && P.at({5, 2}).n == 9, "демо: проект пользователя на месте и открыт");
    Ui u2;
    u2.S = &P;
    u2.store = &st;
    u2.boot(0);
    ok(st.writes == DEMO_N - 2, "демо: та же версия второй раз не раскладывается");
    bool same = true;
    for (int k = 0; k < DEMO_N; k++) {
      uint8_t a[BLOB], b[BLOB];
      loadDemo(mine, k);
      serialize(mine, a);
      State back;
      same &= deserialize(back, a, BLOB);
      serialize(back, b);
      same &= memcmp(a, b, BLOB) == 0 && blobBpm(a) == DEMOS[k].bpm;
    }
    ok(same, "демо: все пять сериализуются туда-обратно без потерь");
  }

  // ── стирание и мьют удержанием, ведомый режим
  {
    MemStore st;
    State P;
    Ui u;
    u.S = &P;
    u.store = &st;
    u.boot(0);
    u.cur = {2, 0};
    u.onSwDown(100);
    ok(u.hold == 1 && u.mode == M_MAP, "кнопка энкодера на карте: ждём — коротко или долго");
    u.onSwUp(200);
    ok(u.mode == M_TILE && !u.hood, "коротко: правка тайла, карта + панель");
    u.onSwDown(300);
    ok(u.mode == M_MAP, "кнопка в правке — выход");
    u.onSwUp(310);
    u.onSwDown(1000);
    u.update(1000 + HOLD_MS);
    u.onSwUp(1000 + HOLD_MS + 10);
    ok(P.at({2, 0}).n == -1 && !P.at({2, 0}).fx.code && u.mode == M_MAP, "удержание: тайл стёрт, правка не открылась");
    u.onPageDown(3000);
    u.onPageUp(3001);
    ok(u.mode == M_LINE, "PAGE — маршрут в линию");
    u.onSwDown(3100);
    u.onSwUp(3200);
    ok(u.mode == M_TILE && u.hood, "из линии — расширенный вид");
    u.onSwDown(3300);
    u.onSwUp(3310);
    u.focus = 2;
    u.onSwDown(4000);
    u.update(4000 + HOLD_MS);
    ok(P.routes[2].len == 0 && P.at({0, 2}).n >= 0, "удержание в линии: цепь стёрта, тайлы на месте");
    u.onPageDown(6000);
    u.onPageUp(6001);
    u.focus = 0;
    u.onPlayDown(7000);
    u.update(7000 + MUTE_MS);
    u.onPlayUp(7000 + MUTE_MS + 10);
    ok(P.routes[0].muted && u.focus == 0, "удержание PLAY: маршрут заглушён, фокус на месте");
    u.onPlayDown(9000);
    u.onPlayUp(9050);
    ok(u.focus == 1, "короткое PLAY — следующий маршрут");
  }
  {
    State P;
    loadDemo(P);
    P.tickUs = internalTickUs(120);
    P.ext = true;
    extStart(P);
    ok(P.playing && P.resumeArmed, "Start с MIDI IN: ждём границу шага");
    extTick(P, 0, nullptr);
    ok(!P.resumeArmed && P.ticks == 1, "Start: первый тик — граница шага, пошли");
    for (int k = 0; k < 4; k++) extTick(P, 0, nullptr);
    extStop(P);
    ok(!P.playing, "Stop — пауза");
    extContinue(P);
    extTick(P, 0, nullptr);  // фаза 5 — не граница
    ok(P.resumeArmed, "Continue: ждём ближайший шаг");
    extTick(P, 0, nullptr);  // фаза 6 — граница
    ok(!P.resumeArmed && P.ticks == 1, "Continue: пошли с границы шага");
  }

  printf(fails ? "%d FAIL, %d PASS\n" : "PASS: все %2$d проверок ядра и жестов\n", fails, passes);
  return fails ? 1 : 0;
}
