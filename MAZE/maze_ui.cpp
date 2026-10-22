// MAZE — экран и органы управления. Перенос proto/maze.js: те же пиксели, те же жесты.
#include "maze_ui.h"
#include "maze_demos.h"

#include <stdio.h>

#include "maze_assets.h"

namespace maze {

using maze_assets::Glyph;

// ───────────────────────── шрифт ─────────────────────────
// '-' — простая черта 3 px по середине строки: минус и «--» (пусто)
static const Glyph MINUS = {5, 3, {8, 8, 8, 0, 0, 0, 0}};
static const Glyph *glyphOf(char ch, bool light = false) {
  const Glyph *t = light ? maze_assets::KLAATU_LIGHT : maze_assets::KLAATU;
  if (ch == '-') return &MINUS;
  if (ch >= 'A' && ch <= 'Z') return &t[ch - 'A'];
  if (ch >= 'a' && ch <= 'z') return &t[26 + ch - 'a'];
  if (ch >= '0' && ch <= '9') return &t[52 + ch - '0'];
  static const char SYM[] = " :+#/.";
  const char *p = ch ? strchr(SYM, ch) : nullptr;
  return &maze_assets::SYMBOLS[p ? p - SYM : 0];
}
// y — верх 8-пиксельной строки; буква занимает ряды y+1…y+5
static int drawGlyph(FB &fb, const Glyph *g, int x, int y, int v = 1) {
  for (int i = 0; i < g->n; i++)
    for (int r = 0; r < 8; r++)
      if (g->cols[i] >> r & 1) fb.set(x + i, y + r, v);
  return g->w;
}
static int text(FB &fb, int x, int y, const char *s, int v = 1, bool light = false) {
  for (; *s; s++) x += drawGlyph(fb, glyphOf(*s, light), x, y, v);
  return x;
}
static int textW(const char *s, bool light = false) {
  int w = 0;
  for (; *s; s++) w += glyphOf(*s, light)->w;
  return w;
}
// надпись в инверсной плашке, как имя страницы у Калейдоскопа
static int label(FB &fb, int x, int y, const char *s) {
  int w = textW(s) - 1;
  fb.fill(x, y, w + 1, 7, 1);
  text(fb, x + 1, y, s, 0);
  return x + w + 2;
}

// ───────────────────────── тайл ─────────────────────────
static const uint8_t STRIPE[4][5] = {{0, 0, 0, 0, 0}, {1, 1, 1, 1, 1}, {1, 1, 0, 1, 1}, {1, 0, 1, 0, 1}};
static const int BODY = 7;

// лад, в котором рисуем и правим ноты: render() и onEnc() ставят его из проекта
static Key gKey = {SCALE_CHRM, 0};

static int octDelta(int o) {
  int d = o - OCT_BASE;
  return d < -3 ? -3 : d > 3 ? 3 : d;
}
// Буква ноты в поле 5×5 (ряды 1–5 от y). Цельные — заглавные Klaatu с точками,
// диезы — строчные Klaatu без точек. Строчные уже 5 px — по центру; у g хвост
// поднимаем, чтобы низ буквы стоял на 5-м ряду и не лез в зазор.
static void drawNoteGlyph(FB &fb, int x0, int y, int n) {
  char letter = NOTE_LETTER[n];
  if (!NOTE_SHARP[n]) {
    drawGlyph(fb, glyphOf(letter), x0 + 1, y);
    return;
  }
  const Glyph *g = glyphOf(letter - 'A' + 'a', true);
  int bottom = 0;
  for (int i = 0; i < g->n; i++)
    for (int r = 7; r >= 0; r--)
      if (g->cols[i] >> r & 1) {
        if (r > bottom) bottom = r;
        break;
      }
  int x = x0 + 1 + (5 - g->n) / 2;
  drawGlyph(fb, g, x, y - (bottom > 5 ? bottom - 5 : 0));
}
// Только буквы: эффекты на тайле не показываются, пустая клетка — точка
static void drawTile(FB &fb, int ox, int oy, const Tile &tile) {
  Shown t = shownNote(gKey, tile);
  if (t.n < 0) {
    fb.set(ox + 3, oy + 3);
    return;
  }
  int d = octDelta(t.o);
  drawNoteGlyph(fb, ox, oy + (d > 0 ? 1 : d < 0 ? -1 : 0), t.n);
  if (d) {
    int row = d > 0 ? oy : oy + 6;
    int a = d < 0 ? -d : d;
    for (int i = 0; i < 5; i++)
      if (STRIPE[a][i]) fb.set(ox + 1 + i, row);
  }
}

// ───────────────────────── маршрут на карте ─────────────────────────
struct MapOrg {
  void operator()(Cell p, int &x, int &y) const {
    x = 1 + 8 * p.c;
    y = 8 * p.r;
  }
};
struct LineOrg {  // маршрут в линию: клетка k — в столбце k
  void operator()(Cell p, int &x, int &y) const {
    x = 8 * p.c;
    y = 0;
  }
};

// Связка — один пиксель в зазоре между соседними клетками маршрута;
// в пустой клетке к точке по центру — по пикселю туда, откуда и куда идёт путь.
template <class Org, class TileOf>
static void drawPath(FB &fb, Org org, TileOf tileOf, const Cell *path, int n, bool closed) {
  auto pair = [&](Cell a, Cell b) {
    if (!adjacent(a, b)) return;
    int lx, ly;
    org(a.c < b.c || a.r < b.r ? a : b, lx, ly);
    if (a.r == b.r) fb.set(lx + 7, ly + 3);
    else fb.set(lx + 3, ly + 7);
    int ax, ay, bx, by;
    org(a, ax, ay);
    org(b, bx, by);
    if (tileOf(a).n < 0) fb.set(ax + 3 + (b.c - a.c), ay + 3 + (b.r - a.r));
    if (tileOf(b).n < 0) fb.set(bx + 3 + (a.c - b.c), by + 3 + (a.r - b.r));
  };
  for (int k = 1; k < n; k++) pair(path[k - 1], path[k]);
  if (closed && n > 2) pair(path[n - 1], path[0]);
}

// Указатель — инверсия клетки, без отметки направления; на ударе ноты вокруг
// замыкается рамка. invert = false — клетку уже инвертировал другой указатель.
// У заглушённого маршрута — шахматка вместо инверсии.
static void drawPointer(FB &fb, int ox, int oy, bool hit, bool invert = true, bool muted = false) {
  if (muted) {
    for (int j = 0; j < BODY; j++)
      for (int i = 0; i < BODY; i++)
        if ((i + j) % 2 == 0) fb.set(ox + i, oy + j, 2);
    return;
  }
  if (invert) fb.fill(ox, oy, BODY, BODY, 2);
  if (hit) fb.frame(ox - 1, oy - 1, 9, 9, 1);
}
// Курсор — неоконченная рамка: уголки по 3 px в зазорах
static const int8_t CORNERS[12][2] = {{-1, -1}, {0, -1}, {-1, 0}, {7, -1}, {6, -1}, {7, 0},
                                      {-1, 7},  {0, 7},  {-1, 6}, {7, 7},  {6, 7},  {7, 6}};
static void drawCursor(FB &fb, int ox, int oy) {
  for (auto &c : CORNERS) fb.set(ox + c[0], oy + c[1]);
}

static const char *const PLAY_SPR[5] = {"#....", "###..", "#####", "###..", "#...."};
static const char *const PAUSE_SPR[5] = {"##.##", "##.##", "##.##", "##.##", "##.##"};
static const char *const CARET_SPR[3] = {"..#..", ".###.", "#####"};
static void sprite(FB &fb, const char *const *rows, int h, int x, int y) {
  for (int j = 0; j < h; j++)
    for (int i = 0; rows[j][i]; i++)
      if (rows[j][i] == '#') fb.set(x + i, y + j);
}

// Шкалы значений Калейдоскопа — 24 px, биты из прошивки (старший — начало шкалы).
static int roundDiv(int a, int b) { return (2 * a + b) / (2 * b); }
static void scaleBar(FB &fb, int x, int y0, int k) {  // k — 0…23, вертикально снизу вверх
  k = k < 0 ? 0 : k > 23 ? 23 : k;
  uint32_t mask = maze_assets::RISING[k];
  for (int b = 0; b < 24; b++)
    if (mask >> (31 - b) & 1) fb.set(x, y0 + 23 - b);
}
// растущая шкала по горизонтали, 24 px в рамке
static void holdBar(FB &fb, int x, int y, uint32_t elapsed) {
  int k = roundDiv(elapsed * 23, HOLD_MS);
  k = k > 23 ? 23 : k;
  uint32_t mask = maze_assets::RISING[k];
  for (int b = 0; b < 24; b++)
    if (mask >> (31 - b) & 1)
      for (int j = 2; j <= 4; j++) fb.set(x + b, y + j);
  fb.frame(x - 1, y + 1, 26, 5, 1);
}

// Показ A:B языком карты: B клеток, A-я — квадрат (сработает), остальные — точки,
// последний проход — инверсия. Только показ — значение меняется энкодером в TRG.
static void drawTrig(FB &fb, int x, int y, const Fx &fx) {
  uint8_t a, b;
  if (!trigAB(fx.trig, a, b)) return;
  int last = fx.count ? (fx.count - 1) % b : -1;
  for (int i = 0; i < b; i++) {
    int cx = x + 7 * i;
    if (i == a - 1) fb.fill(cx + 1, y + 2, 4, 4, 1);
    else fb.fill(cx + 2, y + 3, 2, 2, 1);
    if (i == last) fb.fill(cx, y + 1, 6, 6, 2);
  }
}

// ───────────────────────── параметры тайла ─────────────────────────
// Страницы NOTE, FX, COND. VEL и LEN «RT» — как у маршрута.
static const char *const PAGES[3] = {"NOTE", "FX", "COND"};
static const uint8_t VELS[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127};
static const int VELS_N = sizeof(VELS);
static int listStep(const uint8_t *list, int n, uint8_t cur, int d) {
  int i = 0;
  while (i < n && list[i] != cur) i++;
  i = i + d < 0 ? 0 : i + d >= n ? n - 1 : i + d;
  return list[i];
}
static int gateStep(uint8_t cur, int d) {  // 0 (RT), 5…100
  int v = cur + 5 * d;
  return v < 0 ? 0 : v > 100 ? 100 : v;
}
static int wrap(int v, int n) { return ((v % n) + n) % n; }

struct PInfo {
  const char *name;
  char text[8];
  char help[16];
};
static int paramCount(const Tile &t, int page) {
  if (page == 0) return 4;
  if (page == 1) return 1 + (t.fx.code ? fxDef(t.fx.code)->n : 0);
  return t.fx.code ? 2 : 1;
}
static void paramInfo(const Tile &t, int page, int k, PInfo &p) {
  p.text[0] = p.help[0] = 0;
  if (page == 0) {
    static const char *const names[4] = {"KEY", "OCT", "VEL", "LEN"};
    p.name = names[k];
    Shown sh = shownNote(gKey, t);
    if (k == 0) {
      strcpy(p.text, sh.n < 0 ? "--" : NOTE_NAME[sh.n]);
      strcpy(p.help, "NOTE NAME");
    } else if (k == 1) {
      if (sh.n < 0) strcpy(p.text, "--");
      else snprintf(p.text, sizeof(p.text), "%d", sh.o);
      strcpy(p.help, "OCTAVE");
    } else if (k == 2) {
      if (t.vel) snprintf(p.text, sizeof(p.text), "%d", t.vel);
      else strcpy(p.text, "RT");
      strcpy(p.help, t.vel ? "VELOCITY" : "VEL FROM ROUTE");
    } else {
      if (t.gate) snprintf(p.text, sizeof(p.text), "%d", t.gate);
      else strcpy(p.text, "RT");
      strcpy(p.help, t.gate ? "NOTE LENGTH" : "LEN FROM ROUTE");
    }
    return;
  }
  if (page == 1) {
    const FxDef *d = t.fx.code ? fxDef(t.fx.code) : nullptr;
    if (k == 0) {
      p.name = "FX";
      if (d) snprintf(p.text, sizeof(p.text), "%c", d->code);
      else strcpy(p.text, "--");
      strcpy(p.help, d ? d->name : "NO EFFECT");
      return;
    }
    const Param &pr = d->p[k - 1];
    p.name = pr.name;
    paramText(pr, t.fx.p[k - 1], p.text);
    strcpy(p.help, pr.help);
    return;
  }
  if (!t.fx.code) {
    p.name = "IF";
    strcpy(p.text, "--");
    strcpy(p.help, "SET FX FIRST");
    return;
  }
  if (k == 0) {
    p.name = "IF";
    strcpy(p.text, COND_NAME[t.fx.cond]);
    strcpy(p.help, COND_HELP[t.fx.cond]);
  } else {
    p.name = "TRG";
    trigText(t.fx.trig, p.text);
    trigHelp(t.fx.trig, p.help);
  }
}
static void paramSet(Tile &t, int page, int k, int d) {
  if (page == 0) {
    // ноты — только из лада; правка записывает в тайл то, что видно
    Shown sh = shownNote(gKey, t);
    if (k == 0) {  // по кругу: пусто → ноты лада от C → пусто
      int8_t list[13];
      int n = 0, at = 0;
      list[n++] = -1;
      for (int pc = 0; pc < 12; pc++)
        if (gKey.scale == SCALE_CHRM || inKey(gKey, pc)) {
          if (pc == sh.n) at = n;
          list[n++] = pc;
        }
      t.n = list[wrap(at + d, n)];
      if (t.n >= 0 && sh.n >= 0) t.o = sh.o;  // из пустой — в записанную октаву
    } else if (k == 1) {
      int o = sh.o + d;
      t.n = sh.n;
      t.o = o < OCT_MIN ? OCT_MIN : o > OCT_MAX ? OCT_MAX : o;
    }
    else if (k == 2) t.vel = listStep(VELS, VELS_N, t.vel, d);
    else t.gate = gateStep(t.gate, d);
    return;
  }
  if (page == 1) {
    if (k == 0) {
      int i = wrap(fxIndex(t.fx.code) + 1 + d, FX_N + 1);  // '--', A R X …
      setFx(t.fx, i ? FXDEFS[i - 1].code : 0);
      return;
    }
    const Param &pr = fxDef(t.fx.code)->p[k - 1];
    int v = t.fx.p[k - 1] + d;
    t.fx.p[k - 1] = v < 0 ? 0 : v >= pr.count ? pr.count - 1 : v;
    return;
  }
  if (!t.fx.code) return;
  if (k == 0) t.fx.cond = wrap(t.fx.cond + d, COND_N);
  else t.fx.trig = wrap(t.fx.trig + d, TRIG_N);
}

// Строка параметра: имя (выбранное — инверсией) и значение
static void paramRow(FB &fb, int x, int y, const char *name, const char *val, bool selected, int valueX = 0) {
  int nx = selected ? label(fb, x, y, name) + 1 : text(fb, x + 1, y, name) + 2;
  text(fb, nx > valueX ? nx : valueX, y, val);
}

static const int PANEL = 66;  // панель справа от карты 4×8 (карта — x 0…64)

// Карта + панель (правка с карты кнопкой энкодера). Шапка панели — первый параметр
// страницы под её именем: NOTE и нота, FX и команда; на COND шапка — имя страницы.
static void drawTileMapPanel(FB &fb, const Ui &u, const Tile &t) {
  int n = paramCount(t, u.page);
  const int vx = PANEL + 32;  // значения — в одну колонку
  PInfo p;
  if (u.page == 2) {
    label(fb, PANEL, 0, "COND");
    for (int k = 0; k < n; k++) {
      paramInfo(t, 2, k, p);
      paramRow(fb, PANEL, 8 + 8 * k, p.name, p.text, k == u.sel, vx);
    }
    if (t.fx.code) drawTrig(fb, PANEL + 1, 24, t.fx);
    return;
  }
  paramInfo(t, u.page, 0, p);
  paramRow(fb, PANEL, 0, PAGES[u.page], p.text, u.sel == 0, vx);
  for (int k = 1; k < n; k++) {
    paramInfo(t, u.page, k, p);
    paramRow(fb, PANEL, 8 * k, p.name, p.text, k == u.sel, vx);
  }
}

// Расширенный вид (правка из маршрута в линию): соседи 3×3, страницы, параметры парами
// и внизу подсказка в духе LSDj.
static void drawTileHood(FB &fb, const Ui &u, const Tile &t) {
  const State &S = *u.S;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%d", u.cur.c + 1, u.cur.r + 1);
  text(fb, 1, 0, buf);
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      Cell p = {(int8_t)(u.cur.c + dc), (int8_t)(u.cur.r + dr)};
      if (p.c < 0 || p.r < 0 || p.c >= COLS || p.r >= ROWS) continue;
      drawTile(fb, 1 + 8 * (dc + 1), 8 + 8 * (dr + 1), S.at(p));
    }
  drawCursor(fb, 9, 16);
  int x = 28;
  for (int i = 0; i < 3; i++) x = i == u.page ? label(fb, x, 0, PAGES[i]) + 4 : text(fb, x + 1, 0, PAGES[i]) + 5;
  int n = paramCount(t, u.page);
  PInfo p;
  if (u.page == 2 && t.fx.code) {
    paramInfo(t, 2, 0, p);
    paramRow(fb, 28, 8, p.name, p.text, u.sel == 0);
    paramInfo(t, 2, 1, p);
    paramRow(fb, 78, 8, p.name, p.text, u.sel == 1);
    drawTrig(fb, 29, 16, t.fx);
  } else
    for (int i = 0; i < n; i++) {
      paramInfo(t, u.page, i, p);
      paramRow(fb, 28 + 50 * (i % 2), 8 + 8 * (i / 2), p.name, p.text, i == u.sel);
    }
  if (u.sel < n) {
    paramInfo(t, u.page, u.sel, p);
    text(fb, 28, 24, p.help, 1, true);
  }
}

// ───────────────────────── поиск пути ─────────────────────────
// кратчайший путь по соседям без диагоналей в обход уже пройденных клеток;
// в первую клетку маршрута войти можно — это замыкание
int shortestPath(const Cell *path, int n, Cell to, Cell *out, bool &closing) {
  Cell from = path[n - 1], start = path[0];
  auto key = [](Cell p) { return p.r * COLS + p.c; };
  bool blocked[CELLS] = {}, seen[CELLS] = {};
  int8_t prev[CELLS];
  for (int i = 0; i < n; i++) blocked[key(path[i])] = true;
  closing = n >= 3 && same(to, start);
  if (!closing && blocked[key(to)]) return -1;
  Cell q[CELLS];
  int qh = 0, qt = 0;
  seen[key(from)] = true;
  prev[key(from)] = -1;
  q[qt++] = from;
  static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (qh < qt) {
    Cell p = q[qh++];
    if (same(p, to)) break;
    for (auto &d : D) {
      Cell nn = {(int8_t)(p.c + d[0]), (int8_t)(p.r + d[1])};
      if (nn.c < 0 || nn.r < 0 || nn.c >= COLS || nn.r >= ROWS) continue;
      int k = key(nn);
      if (seen[k]) continue;
      // в первую клетку — только как в конец пути
      if (blocked[k] && !(closing && same(nn, start) && same(nn, to))) continue;
      seen[k] = true;
      prev[k] = key(p);
      q[qt++] = nn;
    }
  }
  if (!seen[key(to)]) return -1;
  Cell tmp[CELLS];
  int m = 0;
  for (int k = key(to); k != key(from); k = prev[k]) tmp[m++] = {(int8_t)(k % COLS), (int8_t)(k / COLS)};
  for (int i = 0; i < m; i++) out[i] = tmp[m - 1 - i];
  return m;
}

// ───────────────────────── экраны ─────────────────────────
struct DrawOpts {
  bool blocked = false, closing = false;
  Cell tail[CELLS];  // путь от последней точки до курсора (с замыканием)
  int tailN = 0, previewN = 0;
};

static void drawMap(FB &fb, const Ui &u, uint32_t now, const DrawOpts &o) {
  const State &S = *u.S;
  MapOrg org;
  auto tileOf = [&](Cell p) -> const Tile & { return S.at(p); };
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) drawTile(fb, 1 + 8 * c, 8 * r, S.at({(int8_t)c, (int8_t)r}));
  if (u.mode == M_DRAW) {
    drawPath(fb, org, tileOf, u.path, u.pathN, u.pathClosed);
    // предпросмотр пути до курсора — мигает
    if (o.tailN && now % 400 < 260) {
      Cell pv[MAX_LEN + CELLS];
      int n = 0;
      pv[n++] = u.path[u.pathN - 1];
      for (int i = 0; i < o.tailN; i++) pv[n++] = o.tail[i];
      drawPath(fb, org, tileOf, pv, n, false);
    }
  } else {
    const Route &fr = S.routes[u.focus];
    drawPath(fb, org, tileOf, fr.cells, fr.len, fr.closed);
    uint32_t inverted = 0;  // клетка, уже инвертированная другим указателем
    for (const Route &r : S.routes) {
      if (!r.len) continue;
      Cell p = r.cells[r.i];
      uint32_t bit = 1u << (p.r * COLS + p.c);
      drawPointer(fb, 1 + 8 * p.c, 8 * p.r, (int32_t)(r.flashUntil - now) > 0, !(inverted & bit), r.muted);
      if (!r.muted) inverted |= bit;
    }
  }
  // курсор (на экране тайла мигает)
  if (u.mode != M_TILE || now % 300 < 180) drawCursor(fb, 1 + 8 * u.cur.c, 8 * u.cur.r);
}

// Вкладки маршрутов (выбранная — инверсией); под каждой — шкала прогресса и внизу
// последний сработавший эффект (в момент срабатывания — инверсией).
// Заглушённый маршрут: цифра зачёркнута, шкала пунктиром.
static void drawTabs(FB &fb, const State &S, int focus, uint32_t now, int x0) {
  for (int i = 0; i < ROUTES; i++) {
    const Route &r = S.routes[i];
    int x = x0 + 8 * i;
    bool empty = !r.len;
    drawGlyph(fb, glyphOf('1' + i, empty), x, 0);
    if (r.muted) fb.fill(x - 1, 3, 7, 1, 1);
    if (!empty) {
      scaleBar(fb, x + 6, 8, r.len > 1 ? roundDiv(r.i * 23, r.len - 1) : 23);
      if (r.muted)
        for (int y = 9; y < 32; y += 2) fb.set(x + 6, y, 0);
      if (r.lastFx) {
        drawGlyph(fb, glyphOf(r.lastFx), x, 24);
        if ((int32_t)(r.fxFlashUntil - now) > 0) fb.fill(x - 1, 24, 7, 7, 2);
      }
    }
    if (i == focus) fb.fill(x - 1, 0, 7, 7, 2);
  }
}

static void tempoText(const State &S, char *buf, size_t n) {
  if (S.ext) snprintf(buf, n, "EXT");
  else snprintf(buf, n, "%3d", S.bpm);
}

static void drawPanel(FB &fb, const Ui &u, uint32_t now) {
  const State &S = *u.S;
  drawTabs(fb, S, u.focus, now, PANEL);
  // транспорт и темп справа сверху
  sprite(fb, S.playing ? PLAY_SPR : PAUSE_SPR, 5, 100, 1);
  char buf[16];
  tempoText(S, buf, sizeof(buf));
  text(fb, 108, 0, buf);
  // маршрут в фокусе: скорость, конец, канал
  const Route &r = S.routes[u.focus];
  if (!r.len) {
    text(fb, 100, 8, "NO");
    text(fb, 100, 16, "RT");
    return;
  }
  // после смены тональности (K, M) вместо скорости — звучащая тоника
  if ((int32_t)(S.keyFlashUntil - now) > 0) {
    label(fb, 100, 8, NOTE_NAME[S.soundingRoot()]);
  } else text(fb, 100, 8, SPEED_NAME[r.speed]);
  text(fb, 100, 16, endName(r.closed, r.end));
  snprintf(buf, sizeof(buf), "CH%02d", r.ch);
  text(fb, 100, 24, buf);
}

static void drawLine(FB &fb, const Ui &u, uint32_t now) {
  const State &S = *u.S;
  const Route &r = S.routes[u.focus];
  char buf[24];
  if (!r.len) {
    snprintf(buf, sizeof(buf), "RT%d", u.focus + 1);
    label(fb, 0, 0, buf);
    text(fb, 0, 12, "NO ROUTE");
    return;
  }
  // маршрут развёрнут в строку: до 16 клеток × 8 px = 128 px; соседи всегда слева и справа
  Cell line[MAX_LEN];
  for (int k = 0; k < r.len; k++) {
    line[k] = {(int8_t)k, 0};
    drawTile(fb, 8 * k, 0, S.at(r.cells[k]));
  }
  auto ltile = [&](Cell q) -> const Tile & { return S.at(r.cells[q.c]); };
  drawPath(fb, LineOrg(), ltile, line, r.len, false);
  drawPointer(fb, 8 * r.i, 0, (int32_t)(r.flashUntil - now) > 0, true, r.muted);
  drawCursor(fb, 8 * u.lineSel, 0);  // выбранный шаг — курсор
  sprite(fb, CARET_SPR, 3, 8 * r.i + 1, 10);
  snprintf(buf, sizeof(buf), "RT%d", u.focus + 1);
  int x = label(fb, 0, 16, buf);
  snprintf(buf, sizeof(buf), "%s %s", SPEED_NAME[r.speed], endName(r.closed, r.end));
  text(fb, x + 2, 16, buf);
  if (r.muted) strcpy(buf, "MUTE");
  else snprintf(buf, sizeof(buf), "CH%02d", r.ch);
  text(fb, 100, 16, buf);
  const Tile &tile = S.at(r.cells[u.lineSel]);
  Shown t = shownNote(S.key(), tile);
  char nm[16];
  if (t.n < 0) strcpy(nm, "---");
  else snprintf(nm, sizeof(nm), "%s%d", NOTE_NAME[t.n], t.o);
  if (tile.fx.code) snprintf(buf, sizeof(buf), "ST%02d %s %c", u.lineSel + 1, nm, tile.fx.code);
  else snprintf(buf, sizeof(buf), "ST%02d %s", u.lineSel + 1, nm);
  text(fb, 0, 24, buf);
  sprite(fb, S.playing ? PLAY_SPR : PAUSE_SPR, 5, 100, 25);
  tempoText(S, buf, sizeof(buf));
  text(fb, 108, 24, buf);
}

// Настройки маршрута: скорость, конец, канал, velocity и длина ноты по умолчанию
// KEY — транспонировать ли маршрут вместе с фразой (K, M)
static const char *const SET_NAME[6] = {"SPD", "END", "CH", "VEL", "LEN", "KEY"};
static const uint8_t SET_X[6] = {0, 64, 0, 64, 0, 64}, SET_Y[6] = {8, 8, 16, 16, 24, 24};
static void drawSettings(FB &fb, const Ui &u) {
  const Route &r = u.S->routes[u.focus];
  char buf[16];
  snprintf(buf, sizeof(buf), "RT%d", u.focus + 1);
  int x = label(fb, 0, 0, buf);
  text(fb, x + 2, 0, r.closed ? "SET LOOP" : "SET");
  char vals[6][8];
  strcpy(vals[0], SPEED_NAME[r.speed]);
  strcpy(vals[1], endName(r.closed, r.end));
  snprintf(vals[2], sizeof(vals[2]), "%02d", r.ch);
  snprintf(vals[3], sizeof(vals[3]), "%d", r.vel);
  snprintf(vals[4], sizeof(vals[4]), "%d", r.gate);
  strcpy(vals[5], r.keyOn ? "ON" : "OFF");
  for (int i = 0; i < 6; i++) {
    // две колонки: имя (выбранное — в плашке) и значение рядом
    if (i == u.setSel) label(fb, SET_X[i], SET_Y[i], SET_NAME[i]);
    else text(fb, SET_X[i] + 1, SET_Y[i], SET_NAME[i]);
    text(fb, SET_X[i] + 25, SET_Y[i], vals[i]);
  }
}

static void drawDrawMode(FB &fb, const Ui &u, const DrawOpts &o) {
  // лишний интерфейс убран: только карта и счётчик шагов
  int n = u.pathN + o.previewN;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d", n > 99 ? 99 : n);
  text(fb, PANEL, 0, buf);
  text(fb, PANEL + 14, 0, "/16", 1, true);
  if (o.blocked) text(fb, PANEL, 24, "NO WAY");
  else if (u.pathClosed || o.closing) text(fb, PANEL, 24, "LOOP");
}

// Пока держишь кнопку энкодера: на карте — панель CLR с нотой тайла и шкалой,
// в линии — две нижние строки CLR RTn CHAIN и шкала.
static void drawClearHold(FB &fb, const Ui &u, uint32_t now) {
  uint32_t el = now - u.holdT0;
  char buf[16];
  if (u.hold == 1) {
    fb.fill(PANEL - 1, 0, 128 - PANEL + 1, 32, 0);
    Shown t = shownNote(u.S->key(), u.S->at(u.cur));
    label(fb, PANEL, 0, "CLR");
    if (t.n < 0) strcpy(buf, "--");
    else snprintf(buf, sizeof(buf), "%s%d", NOTE_NAME[t.n], t.o);
    text(fb, PANEL + 26, 0, buf);
    holdBar(fb, PANEL + 1, 8, el);
    text(fb, PANEL, 24, "TILE", 1, true);
  } else {
    fb.fill(0, 16, 128, 16, 0);
    int x = label(fb, 0, 16, "CLR");
    snprintf(buf, sizeof(buf), "RT%d CHAIN", u.focus + 1);
    text(fb, x + 2, 16, buf);
    holdBar(fb, 1, 24, el);
    text(fb, 30, 24, "TILES STAY", 1, true);
  }
}

// Экран проектов, вариант Б: слот крупно. Миниатюра карты 32×16 (нота — квадрат 3×3,
// пусто — точка), LOAD — удерживать кнопку энкодера, SAVE — удерживать TAP+кнопку.
// TAP+энкодер — выбор слота, энкодер — темп загруженного проекта.
static void drawProj(FB &fb, Ui &u, uint32_t now) {
  const State &S = *u.S;
  bool cur = u.projSel == u.slot;
  char buf[16];
  int x0 = label(fb, 0, 0, "PROJ");
  snprintf(buf, sizeof(buf), "%02d/%d", u.projSel + 1, SLOTS);
  text(fb, x0 + 3, 0, buf);
  if (u.store && !u.store->ready()) text(fb, 100, 0, "NOFS");  // сохранять некуда
  else if (cur) text(fb, 100, 0, u.dirty() ? "CUR+" : "CUR");
  // у загруженного слота — исходник как сейчас, у остальных — что записано
  if (!cur && u.selCached != u.projSel) {
    bool af;
    u.selHas = u.used[u.projSel] && u.store && u.store->read(u.projSel, u.selBlob, af);
    u.selCached = u.projSel;
  }
  bool has = cur || u.selHas;
  if (has)
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++) {
        bool note = cur ? u.S->pool[S.src[r][c]].n >= 0 : blobHasNote(u.selBlob, c, r);
        if (note) fb.fill(1 + 4 * c, 11 + 4 * r, 3, 3, 1);
        else fb.set(1 + 4 * c + 1, 11 + 4 * r + 1);
      }
  else text(fb, 2, 16, "EMPTY", 1, true);
  text(fb, 44, 8, "LOAD", 1, u.projHold != 1);
  text(fb, 44, 16, "SAVE", 1, u.projHold != 2);
  if (u.projHold) holdBar(fb, 78, u.projHold == 1 ? 8 : 16, now - u.projT0);
  else if ((int32_t)(u.projDone - now) > 0) text(fb, 78, u.projDoneRow, "OK");
  else if (u.queued >= 0) text(fb, 78, 8, "BAR");  // загрузится с начала такта
  if (has) {
    text(fb, 44, 24, (cur ? u.autosave : u.autoF[u.projSel]) ? "AUTO" : "SAVED", 1, true);
    // тоника, лад, темп — справа: у загруженного живые и правятся энкодером (выбранное — в плашке,
    // TAP+PAGE — следующее), у остальных — записанные, тонким
    Key k = cur ? S.key() : blobKey(u.selBlob);
    char vals[4][8];
    snprintf(vals[0], sizeof(vals[0]), "%s", NOTE_NAME[k.root]);
    snprintf(vals[1], sizeof(vals[1]), "%s", SCALES[k.scale].name);
    if (u.inCh == IN_OFF) strcpy(vals[2], "OFF");
    else if (u.inCh == IN_ALL) strcpy(vals[2], "ALL");
    else snprintf(vals[2], sizeof(vals[2]), "%02d", u.inCh - 1);
    if (cur && S.ext) snprintf(vals[3], sizeof(vals[3]), "EXT");
    else snprintf(vals[3], sizeof(vals[3]), "%d", cur ? S.bpm : blobBpm(u.selBlob));
    auto field = [&](int i, int x, int y) {
      if (cur && u.projField == i) label(fb, x - 1, y, vals[i]);
      else text(fb, x, y, vals[i], 1, !cur && i != 2);  // вход нот — общий, не слота
    };
    bool busy8 = (u.projHold == 1) || ((int32_t)(u.projDone - now) > 0 && u.projDoneRow == 8) || u.queued >= 0;
    if (!busy8) {
      field(0, 128 - textW(vals[1]) - textW(vals[0]) - 3, 8);
      field(1, 128 - textW(vals[1]), 8);
    }
    bool busy16 = (u.projHold == 2) || ((int32_t)(u.projDone - now) > 0 && u.projDoneRow == 16);
    if (!busy16) {  // вход нот с MIDI IN: IN и канал
      int x = 128 - textW(vals[2]);
      text(fb, x - textW("IN") - 2, 16, "IN");
      field(2, x, 16);
    }
    field(3, 128 - textW(vals[3]), 24);
  }
}

void Ui::render(FB &fb, uint32_t now) {
  fb.clear();
  gKey = S->key();
  if (hold && now - holdT0 > 200 && (mode == M_MAP || mode == M_LINE)) {
    if (mode == M_LINE) drawLine(fb, *this, now);
    else {
      drawMap(fb, *this, now, DrawOpts());
      drawPanel(fb, *this, now);
    }
    drawClearHold(fb, *this, now);
    return;
  }
  if (mode == M_LINE) return drawLine(fb, *this, now);
  if (mode == M_SETTINGS) return drawSettings(fb, *this);
  if (mode == M_TILE && hood) return drawTileHood(fb, *this, S->at(cur));
  if (mode == M_PROJ) return drawProj(fb, *this, now);
  DrawOpts o;
  if (mode == M_DRAW && !pathClosed && !same(path[pathN - 1], cur)) {
    int n = shortestPath(path, pathN, cur, o.tail, o.closing);
    int add = n < 0 ? 0 : o.closing ? n - 1 : n;
    if (n < 0 || pathN + add > MAX_LEN) o.blocked = true;
    else {
      o.tailN = n;
      o.previewN = add;
    }
  }
  drawMap(fb, *this, now, o);
  if (mode == M_DRAW) drawDrawMode(fb, *this, o);
  else if (mode == M_TILE) drawTileMapPanel(fb, *this, S->at(cur));
  else drawPanel(fb, *this, now);
}

// ───────────────────────── органы управления ─────────────────────────
// TAP — модификатор; TAP+TAP — старт/пауза; PLAY по отпусканию — маршрут 1→4, удержание —
// мьют; TAP+PLAY — настройки; энкодер — X, TAP+энкодер — Y; кнопка энкодера по отпусканию —
// правка тайла, удержание — стереть; TAP+кнопка — рисовать; двойное нажатие — закончить
// маршрут; PAGE — карта ↔ линия, на экране тайла — страница; PAGE + энкодер — темп;
// TAP+PAGE — проекты.

void Ui::openTile(Mode back) {
  page = sel = 0;
  tileBack = back;
  hood = back == M_LINE;  // с карты — карта + панель, из линии — соседи 3×3
  mode = M_TILE;
}
void Ui::openProj() {
  projSel = slot;
  projHold = 0;
  projField = 3;
  projBack = mode == M_LINE ? M_LINE : M_MAP;
  projDone = 0;
  selCached = -1;
  mode = M_PROJ;
}

void Ui::onEnc(int d, uint32_t) {
  gKey = S->key();
  if (pageBtn) {
    pageUsed = true;
    if (!S->ext) {  // ведомому темп задают снаружи
      int b = S->bpm + d;
      S->bpm = b < 40 ? 40 : b > 255 ? 255 : b;
    }
    return;
  }
  bool mod = tap;
  if (mod) tapClean = false;
  switch (mode) {
    case M_MAP:
    case M_DRAW:
      if (mod) cur.r = wrap(cur.r + d, ROWS);
      else cur.c = wrap(cur.c + d, COLS);  // заворот в том же ряду
      break;
    case M_TILE: {
      Tile &t = S->at(cur);
      int n = paramCount(t, page);
      if (mod) sel = wrap(sel + d, n);
      else paramSet(t, page, sel, d);
      n = paramCount(t, page);  // смена эффекта меняет число параметров
      if (sel >= n) sel = n - 1;
      break;
    }
    case M_LINE: {
      const Route &r = S->routes[focus];
      if (r.len) lineSel = wrap(lineSel + d, r.len);
      break;
    }
    case M_PROJ:
      // TAP+энкодер — слот, энкодер — темп загруженного проекта (на чужом слоте — ничего)
      if (mod) {
        projSel = wrap(projSel + d, SLOTS);
        projHold = 0;
      } else if (projSel == slot) {
        // энкодер — выбранное: тоника, лад или темп загруженного проекта (на чужом слоте — ничего)
        if (projField == 0) S->root = wrap(S->root + d, 12);
        if (projField == 1) S->scale = wrap(S->scale + d, SCALE_N);
        if (projField == 2) {
          int v = inCh + d;
          inCh = v < 0 ? 0 : v > 17 ? 17 : v;
          if (store) store->setInChannel(inCh);
        }
        if (projField == 3 && !S->ext) {
          int b = S->bpm + d;
          S->bpm = b < 40 ? 40 : b > 255 ? 255 : b;
        }
      }
      break;
    case M_SETTINGS: {
      Route &r = S->routes[focus];
      if (setSel == 0) {
        int v = r.speed + d;
        r.speed = v < 0 ? 0 : v > 4 ? 4 : v;
      } else if (setSel == 1) {
        r.end = wrap(r.end + d, endCount(r.closed));
        r.i = startIndex(r);
        r.dir = 1;
      } else if (setSel == 2) {
        int v = r.ch + d;
        r.ch = v < 1 ? 1 : v > 16 ? 16 : v;
      } else if (setSel == 3) {
        int v = r.vel + 8 * d;
        r.vel = v < 1 ? 1 : v > 127 ? 127 : v;
      } else if (setSel == 4) {
        int v = r.gate + 5 * d;
        r.gate = v < 5 ? 5 : v > 100 ? 100 : v;
      } else r.keyOn = !r.keyOn;
      break;
    }
  }
}

void Ui::onSwDown(uint32_t now) {
  bool dbl = hadSw && now - lastSw < 320;
  lastSw = now;
  hadSw = true;
  bool mod = tap;
  if (mod) tapClean = false;
  switch (mode) {
    case M_MAP:
      if (mod) {
        mode = M_DRAW;
        path[0] = cur;
        pathN = 1;
        pathClosed = false;
      } else {
        hold = 1;  // коротко — правка (по отпусканию), долго — стереть тайл
        holdT0 = now;
      }
      break;
    case M_TILE:
      mode = tileBack;
      break;
    case M_LINE:
      if (S->routes[focus].len) {
        hold = 2;  // коротко — правка шага, долго — стереть цепь
        holdT0 = now;
      }
      break;
    case M_DRAW: {
      if (dbl) return finishDraw();
      if (pathClosed) break;
      Cell out[CELLS];
      bool closing;
      int n = shortestPath(path, pathN, cur, out, closing);
      if (n <= 0) break;
      int add = closing ? n - 1 : n;
      if (pathN + add > MAX_LEN) break;
      for (int i = 0; i < add; i++) path[pathN++] = out[i];
      if (closing) pathClosed = true;
      break;
    }
    case M_SETTINGS:
      setSel = (setSel + 1) % 6;
      break;
    case M_PROJ:
      projHold = mod ? 2 : 1;
      projT0 = now;
      break;
  }
}

// Кнопку энкодера отпустили раньше, чем заполнилась шкала: в проектах — отмена,
// на карте и в линии — это было короткое нажатие, открываем правку тайла.
void Ui::onSwUp(uint32_t) {
  if (mode == M_PROJ) projHold = 0;
  if (!hold) return;
  uint8_t act = hold;
  hold = 0;
  if (act == 1) openTile(M_MAP);
  else {
    cur = S->routes[focus].cells[lineSel];
    openTile(M_LINE);
  }
}

void Ui::finishDraw() {
  Route &r = S->routes[focus];
  r.len = pathN;
  memcpy(r.cells, path, sizeof(Cell) * pathN);
  r.closed = pathClosed;
  r.end = 0;
  r.dir = 1;
  r.fresh = true;
  r.acc = 0;
  r.lastFx = 0;
  r.wait = 0;
  r.jump = false;
  r.i = startIndex(r);
  mode = M_MAP;
}

void Ui::onPlayDown(uint32_t now) {
  bool mod = tap;
  if (mod) tapClean = false;
  if (mode == M_DRAW) {
    mode = M_MAP;  // PLAY — отмена рисования
    return;
  }
  if (mode == M_SETTINGS) {
    mode = view;
    return;
  }
  if (mode == M_TILE) return;
  if (mode == M_PROJ) {
    mode = projBack;
    return;
  }
  if (mod) {
    view = mode;
    setSel = 0;
    mode = M_SETTINGS;
    return;
  }
  playHold = true;  // коротко — следующий маршрут (по отпусканию), долго — мьют
  playT0 = now;
}
// PLAY отпущен раньше, чем сработал мьют, — следующий маршрут
void Ui::onPlayUp(uint32_t) {
  if (!playHold) return;
  playHold = false;
  focus = (focus + 1) % ROUTES;
  lineSel = 0;
}

void Ui::onPageDown(uint32_t) {
  pageBtn = true;
  pageUsed = false;
  if (tap && (mode == M_MAP || mode == M_LINE)) {
    tapClean = false;
    pageUsed = true;
    openProj();  // TAP+PAGE — проекты
  } else if (tap && mode == M_PROJ) {
    tapClean = false;
    pageUsed = true;
    projField = (projField + 1) % 4;  // в проектах TAP+PAGE — следующее поле: тоника → лад → IN → темп
  }
}
void Ui::onPageUp(uint32_t) {
  pageBtn = false;
  if (pageUsed) return;
  if (mode == M_PROJ) mode = projBack;
  else if (mode == M_TILE) {
    page = (page + 1) % 3;
    sel = 0;
  } else if (mode == M_MAP) {
    mode = M_LINE;
    const Route &r = S->routes[focus];
    if (lineSel > (r.len ? r.len - 1 : 0)) lineSel = r.len ? r.len - 1 : 0;
  } else if (mode == M_LINE) mode = M_MAP;
}

void Ui::onTapDown(uint32_t now) {
  if (tapClean && hadTapUp && now - lastTapUp < 350) {
    togglePlay(*S);  // TAP+TAP
    hadTapUp = false;
    tapClean = false;
    tap = true;
    return;
  }
  tap = true;
  tapClean = true;
  tapDownAt = now;
}
void Ui::onTapUp(uint32_t now) {
  tap = false;
  hadTapUp = tapClean && now - tapDownAt < 300;
  if (hadTapUp) lastTapUp = now;
}

// ───────────────────────── проекты ─────────────────────────
// Сохраняется исходник: эффекты двигают только живую карту, LOAD возвращает её к исходнику.
// Автосохранение — только черновику: слот был пустым при загрузке или уже писался
// автосохранением. После ручного SAVE слот защищён.
void Ui::loadNow(int k, const uint8_t *blob, bool has, bool autoFlag) {
  if (!has || !deserialize(*S, blob, BLOB)) newProject(*S);
  slot = k;
  autosave = autoFlag;
  serialize(*S, saved);
  memcpy(last, saved, BLOB);
  selCached = -1;
  if (store) store->setCurrent(k);
}
void Ui::saveSlot(int k) {
  serialize(*S, last);
  if (store) store->write(k, last, false);
  memcpy(saved, last, BLOB);
  used[k] = true;
  autoF[k] = false;
  slot = k;
  autosave = false;
  selCached = -1;
  if (store) store->setCurrent(k);
}
void Ui::projAction(uint32_t now) {
  uint8_t act = projHold;
  projHold = 0;
  if (act == 2) saveSlot(projSel);
  else {  // LOAD — во время игры с начала следующего такта
    bool af = false;
    queuedHas = used[projSel] && store && store->read(projSel, queuedBlob, af);
    queuedAuto = !queuedHas || af;
    queued = projSel;
  }
  projDone = now + 700;
  projDoneRow = act == 2 ? 16 : 8;
}
void Ui::beforeTick(uint32_t) {
  if (queued >= 0 && S->ticks % BAR_TICKS == 0) {
    loadNow(queued, queuedBlob, queuedHas, queuedAuto);
    queued = -1;
  }
}

void Ui::boot(uint32_t now) {
  bool any = false;
  uint8_t tmp[BLOB];
  for (int k = 0; k < SLOTS; k++) {
    bool af = false;
    used[k] = store && store->read(k, tmp, af);
    autoF[k] = af;
    any |= used[k];
  }
  // демо-проекты идут с прошивкой: новая версия раскладывается по пустым слотам 01…05,
  // записанное пользователем не трогается
  if (store && store->demoVersion() != DEMO_VERSION) {
    static State tmpS;  // большой — не на стек
    for (int k = 0; k < DEMO_N && k < SLOTS; k++) {
      if (used[k]) continue;
      loadDemo(tmpS, k);
      serialize(tmpS, tmp);
      store->write(k, tmp, false);
      used[k] = true;
      autoF[k] = false;
    }
    store->setDemoVersion(DEMO_VERSION);
    any = true;
  }
  if (!any) {  // без флеша — просто первое демо
    loadDemo(*S);
    serialize(*S, saved);
    memcpy(last, saved, BLOB);
  } else {
    int k = store->current();
    if (k < 0 || k >= SLOTS) k = 0;
    bool af = false;
    bool has = used[k] && store->read(k, tmp, af);
    loadNow(k, tmp, has, has ? af : true);
  }
  inCh = store ? store->inChannel() : IN_OFF;
  lastChange = lastCheck = now;
}

void Ui::update(uint32_t now) {
  if (projHold && now - projT0 >= HOLD_MS) projAction(now);
  if (hold && now - holdT0 >= HOLD_MS) {
    // стирание удержанием: на карте — тайл, в линии — цепь (тайлы остаются)
    if (hold == 1) clearTile(*S, cur);
    else {
      clearRoute(S->routes[focus]);
      lineSel = 0;
    }
    hold = 0;
  }
  if (playHold && now - playT0 >= MUTE_MS) {
    S->routes[focus].muted = !S->routes[focus].muted;
    playHold = false;
  }
  if (queued >= 0 && !S->playing) {
    loadNow(queued, queuedBlob, queuedHas, queuedAuto);
    queued = -1;
  }
  if (now - lastCheck < 250) return;
  lastCheck = now;
  uint8_t tmp[BLOB];
  serialize(*S, tmp);
  if (memcmp(tmp, last, BLOB)) {
    memcpy(last, tmp, BLOB);
    lastChange = now;
  }
  if (autosave && dirty() && now - lastChange >= AUTOSAVE_MS && store) {
    store->write(slot, last, true);
    memcpy(saved, last, BLOB);
    used[slot] = autoF[slot] = true;
  }
}

}  // namespace maze
