// Те же сцены, что tools/screens/scenes.js, но из кода прошивки. compare.py сверяет их
// с прототипом попиксельно.
#include <stdio.h>

#include "../../MAZE/maze_ui.h"
#include "../../MAZE/maze_demos.h"

using namespace maze;

struct MemStore : Store {
  uint8_t blob[SLOTS][BLOB];
  bool has[SLOTS] = {}, autoF[SLOTS] = {};
  int cur = -1;
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
    return true;
  }
  int current() override { return cur; }
  void setCurrent(int k) override { cur = k; }
  int demoV = 0;
  int demoVersion() override { return demoV; }
  void setDemoVersion(int v) override { demoV = v; }
};

static uint8_t buf[512];
static FB fb{buf};

static void out(const char *name) {
  printf("SCENE %s\n", name);
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 128; x++) putchar(fb.get(x, y) ? '#' : '.');
    putchar('\n');
  }
}

struct Scene {
  State S;
  MemStore store;
  Ui u;
  Scene() {
    u.S = &S;
    u.store = &store;
    S.tickUs = internalTickUs(120);
    u.boot(0);  // чистая «плата» — демо в слот 01, как makeState() прототипа
    const uint8_t pos[4] = {2, 3, 5, 4};
    for (int k = 0; k < ROUTES; k++) S.routes[k].i = pos[k];
    u.cur = {2, 2};
  }
  void playing() {
    const char fx[4] = {'R', 'D', 'X', 'O'};
    for (int k = 0; k < ROUTES; k++) S.routes[k].lastFx = fx[k];
    S.routes[2].fxFlashUntil = 100;
    S.routes[1].flashUntil = 100;
    S.playing = true;
  }
  void dump(const char *name) {
    u.render(fb, 0);
    out(name);
  }
};

static void tileScene(const char *name, bool hood, Cell cur, int page, int sel, int cond = -1) {
  Scene s;
  s.u.cur = cur;
  s.u.page = page;
  s.u.sel = sel;
  s.u.hood = hood;
  s.u.tileBack = hood ? M_LINE : M_MAP;
  s.u.mode = M_TILE;
  Tile &t = s.S.at(cur);
  if (cond >= 0) t.fx.cond = cond;
  if (t.fx.code) t.fx.count = 3;
  s.dump(name);
}

int main() {
  { Scene s; s.dump("map"); }
  { Scene s; s.playing(); s.dump("map_playing"); }
  { Scene s; s.playing(); s.u.focus = 3; s.u.cur = {0, 3}; s.dump("map_route4_rev"); }
  tileScene("tile_map_note", false, {6, 1}, 0, 0);
  tileScene("tile_map_note_vel", false, {6, 1}, 0, 2);
  tileScene("tile_map_fx", false, {1, 1}, 1, 0);
  tileScene("tile_map_fx_param", false, {1, 1}, 1, 1);
  tileScene("tile_map_cond", false, {6, 1}, 2, 0);
  tileScene("tile_map_cond_trig", false, {6, 1}, 2, 1);
  tileScene("tile_hood_note", true, {6, 1}, 0, 2);
  tileScene("tile_hood_fx", true, {1, 1}, 1, 0);
  tileScene("tile_hood_fx_param", true, {1, 1}, 1, 1);
  tileScene("tile_hood_cond", true, {6, 1}, 2, 0);
  tileScene("tile_hood_cond_trig", true, {6, 1}, 2, 1);
  tileScene("tile_hood_cond_pre", true, {3, 0}, 2, 1);
  { Scene s; s.u.cur = {6, 1}; s.dump("map_before_rotate"); }
  {
    Scene s;
    s.S.ops[s.S.opN++] = {'O', {6, 1}, 0, 1};
    applyPending(s.S);
    s.u.cur = {6, 1};
    s.dump("map_after_rotate");
  }
  {
    Scene s;
    s.u.focus = 3;
    s.u.mode = M_DRAW;
    const Cell p[] = {{0, 3}, {1, 3}, {2, 3}, {2, 2}};
    memcpy(s.u.path, p, sizeof(p));
    s.u.pathN = 4;
    s.u.cur = {5, 1};
    s.dump("draw");
  }
  {
    Scene s;
    s.u.mode = M_DRAW;
    const Cell p[] = {{1, 1}, {2, 1}, {2, 2}, {1, 2}};
    memcpy(s.u.path, p, sizeof(p));
    s.u.pathN = 4;
    s.u.cur = {1, 1};
    s.dump("draw_closing");
  }
  { Scene s; s.u.mode = M_LINE; s.u.lineSel = 4; s.dump("line"); }
  { Scene s; s.u.focus = 3; s.u.mode = M_SETTINGS; s.u.setSel = 3; s.dump("settings"); }
  {
    Scene s;
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++) {
        int k = r * COLS + c;
        Tile &t = s.S.at({(int8_t)c, (int8_t)r});
        t.clear();
        t.n = k % 12;
        t.o = 4 + k / 12;
      }
    s.u.cur = {6, 3};
    s.dump("chroma_map");
  }
  auto proj = [](const char *name, int sel, void (*prep)(Scene &), void (*after)(Scene &)) {
    Scene s;
    if (prep) prep(s);
    s.u.mode = M_MAP;
    s.u.onTapDown(1000);  // TAP+PAGE
    s.u.onPageDown(1001);
    s.u.onPageUp(1002);
    s.u.onTapUp(1003);
    s.u.projSel = sel;
    if (after) after(s);
    s.dump(name);
  };
  proj("proj", 0, nullptr, nullptr);
  proj("proj_dirty", 0, [](Scene &s) {
    s.S.at({0, 1}).n = 7;
    serialize(s.S, s.u.last);
  }, nullptr);
  proj("proj_empty_save", 8, nullptr, [](Scene &s) {
    s.u.projHold = 2;
    s.u.projT0 = (uint32_t)-540;
  });
  proj("proj_draft", 2, [](Scene &s) {
    uint8_t b[BLOB];
    serialize(s.S, b);
    s.store.write(2, b, true);
    s.u.used[2] = s.u.autoF[2] = true;
  }, nullptr);
  proj("proj_demo", 3, nullptr, nullptr);
  for (int k = 1; k < DEMO_N; k++) {  // демо-проекты 02…05 — карта сразу после LOAD
    Scene s;
    deserialize(s.S, s.store.blob[k], BLOB);
    s.u.slot = k;
    s.u.cur = {0, 0};
    char name[16];
    snprintf(name, sizeof(name), "demo_%02d", k + 1);
    s.dump(name);
  }
  { Scene s; s.playing(); s.S.routes[2].muted = true; s.dump("map_mute"); }
  { Scene s; s.u.cur = {3, 1}; s.u.hold = 1; s.u.holdT0 = (uint32_t)-540; s.dump("clear_tile_hold"); }
  { Scene s; s.u.mode = M_LINE; s.u.lineSel = 2; s.u.hold = 2; s.u.holdT0 = (uint32_t)-540; s.dump("clear_chain_hold"); }
  { Scene s; s.playing(); s.S.ext = true; s.dump("map_ext"); }
  return 0;
}
