// Сцены для render_screens.py: состояние → буфер 128×32 → ASCII.
const NOW = 0; // фаза мигания: курсор и предпросмотр видны

function out(name) {
  print('SCENE ' + name);
  for (let y = 0; y < 32; y++) {
    let s = '';
    for (let x = 0; x < 128; x++) s += fb.p[y * 128 + x] ? '#' : '.';
    print(s);
  }
}
function dump(name, st) {
  render(fb, st, NOW);
  out(name);
}

// позиции указателей и последние эффекты, как посреди игры
function base() {
  const st = makeState();
  [2, 3, 5, 4].forEach((i, k) => (st.routes[k].i = i));
  st.cur = { c: 2, r: 2 };
  return st;
}
function playing(st) {
  ['R', 'D', 'X', 'O'].forEach((code, k) => (st.routes[k].fx = code));
  st.routes[2].fxFlash = NOW + 100; // свежий эффект маршрута 3 — инверсией
  st.routes[1].flash = NOW + 100; // удар маршрута 2 — рамка вокруг указателя
  st.playing = true;
  return st;
}

dump('map', base());
dump('map_playing', playing(base()));

let m = playing(base());
m.focus = 3;
m.cur = { c: 0, r: 3 };
dump('map_route4_rev', m);

// правка тайла: с карты — карта + панель, из маршрута в линию — соседи 3×3 с подсказкой.
// (6,1) B4 — поворот, (1,1) D5 — арпеджио (3 параметра)
function tileScene(name, layout, cur, page, sel, cond) {
  const st = base();
  st.cur = cur;
  st.tile = { page, sel, back: layout === 'hood' ? 'line' : 'map', layout };
  st.mode = 'tile';
  if (cond) st.grid[cur.r][cur.c].fx.cond = cond;
  const fx = st.grid[cur.r][cur.c].fx;
  if (fx) fx.count = 3; // три прохода — инверсия на третьей клетке
  dump(name, st);
}
tileScene('tile_map_note', 'map', { c: 6, r: 1 }, 0, 0);
tileScene('tile_map_note_vel', 'map', { c: 6, r: 1 }, 0, 2);
tileScene('tile_map_fx', 'map', { c: 1, r: 1 }, 1, 0);
tileScene('tile_map_fx_param', 'map', { c: 1, r: 1 }, 1, 1);
tileScene('tile_map_cond', 'map', { c: 6, r: 1 }, 2, 0);
tileScene('tile_map_cond_trig', 'map', { c: 6, r: 1 }, 2, 1);
tileScene('tile_hood_note', 'hood', { c: 6, r: 1 }, 0, 2);
tileScene('tile_hood_fx', 'hood', { c: 1, r: 1 }, 1, 0);
tileScene('tile_hood_fx_param', 'hood', { c: 1, r: 1 }, 1, 1);
tileScene('tile_hood_cond', 'hood', { c: 6, r: 1 }, 2, 0);
tileScene('tile_hood_cond_trig', 'hood', { c: 6, r: 1 }, 2, 1);
tileScene('tile_hood_cond_pre', 'hood', { c: 3, r: 0 }, 2, 1);

// карта до и после поворота соседей вокруг (6,1)
m = base();
m.cur = { c: 6, r: 1 };
dump('map_before_rotate', m);
m = base();
m.pending.push({ code: 'O', p: { c: 6, r: 1 }, a: 'CW', b: '90' });
applyPending(m);
m.cur = { c: 6, r: 1 };
dump('map_after_rotate', m);

m = base();
m.focus = 3;
m.mode = 'draw';
m.draw = { path: [{ c: 0, r: 3 }, { c: 1, r: 3 }, { c: 2, r: 3 }, { c: 2, r: 2 }], closed: false };
m.cur = { c: 5, r: 1 };
dump('draw', m);

m = base();
m.mode = 'draw';
m.draw = { path: [{ c: 1, r: 1 }, { c: 2, r: 1 }, { c: 2, r: 2 }, { c: 1, r: 2 }], closed: false };
m.cur = { c: 1, r: 1 };
dump('draw_closing', m);

m = base();
m.mode = 'line';
m.lineSel = 4;
dump('line', m);

m = base();
m.focus = 3;
m.mode = 'settings';
m.setSel = 3;
dump('settings', m);

// хроматика на всей карте, октавы 4–6
m = base();
const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
for (let r = 0; r < 4; r++)
  for (let c = 0; c < 8; c++) {
    const k = r * 8 + c;
    m.grid[r][c] = parseNote(names[k % 12] + (4 + Math.floor(k / 12)));
  }
m.cur = { c: 6, r: 3 };
dump('chroma_map', m);

// крупно: хроматика C…B в октаве 5, ниже — с октавами 7 и 3
fb.clear();
names.forEach((n, i) => {
  drawTile(fb, 1 + 8 * i, 0, parseNote(n + '5'));
  drawTile(fb, 1 + 8 * i, 8, parseNote(n + (i % 2 ? '3' : '7')));
});
out('chroma_rows');

// экран проектов
function projScene(name, sel, prep) {
  const st = base();
  prep && prep(st);
  st.mode = 'map';
  openProj(st);
  st.proj.sel = sel;
  prep && prep.after && prep.after(st);
  dump(name, st);
}
projScene('proj', 0);
projScene('proj_dirty', 0, st => {
  st.grid[1][0].n = 7;
  st.lastJSON = serialize(st);
});
const empty = st => {};
empty.after = st => (st.proj.hold = { act: 'SAVE', t0: NOW - 540 });
projScene('proj_empty_save', 8, empty);
const draft = st => {
  st.slots[2] = { data: serialize(st), auto: true };
};
projScene('proj_draft', 2, draft);
projScene('proj_demo', 3);
projScene('proj_key', 0, st => Object.assign(st, { scale: 10, root: 9 }));
projScene('proj_key_sel', 0, Object.assign(st => Object.assign(st, { scale: 1, root: 9 }), { after: st => (st.proj.field = 1) }));
projScene('proj_key_other', 1);

// лад: та же карта демо 01 в A PMIN — ноты приведены к ладу, записанные не тронуты
const sc = base();
Object.assign(sc, { scale: 10, root: 9 });
dump('map_scale', sc);
const scT = base();
Object.assign(scT, { scale: 10, root: 9, mode: 'tile', cur: { c: 4, r: 1 } });
openTile(scT, 'map');
dump('tile_map_note_scale', scT);

// демо-проекты 02…05 (assets/demos.json) — карта сразу после LOAD
for (let k = 1; k < DEMOS.list.length; k++) {
  const st = makeState();
  applyProject(st, st.slots[k].data);
  st.slot = k;
  dump('demo_' + String(k + 1).padStart(2, '0'), st);
}

// мьют маршрута 3, стирание удержанием, ведомый режим
m = playing(base());
m.routes[2].muted = true;
dump('map_mute', m);
m = base();
m.cur = { c: 3, r: 1 };
m.hold = { act: 'TILE', t0: NOW - 540 };
dump('clear_tile_hold', m);
m = base();
m.mode = 'line';
m.lineSel = 2;
m.hold = { act: 'CHAIN', t0: NOW - 540 };
dump('clear_chain_hold', m);
m = playing(base());
m.sync = 'EXT';
dump('map_ext', m);
