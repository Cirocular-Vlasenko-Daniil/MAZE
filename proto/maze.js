// MAZE — прототип: главный экран, экран тайла и эффекты. Всё, что на экране,
// рисуется в буфер 128×32 по 1 биту, как будет на SSD1306; канвас только увеличивает пиксели.
'use strict';

const KF = window.KFONT;
const A = window.MAZE_ASSETS;
const W = 128;
const H = 32;
const COLS = 8;
const ROWS = 4;
const PANEL = 66; // панель справа от карты 4×8 (карта — x 0…64)

// ───────────────────────── буфер ─────────────────────────
class FB {
  constructor(w = W, h = H) {
    this.w = w;
    this.h = h;
    this.p = new Uint8Array(w * h);
  }
  clear() {
    this.p.fill(0);
  }
  // v: 1 — зажечь, 0 — погасить, 2 — инвертировать
  set(x, y, v = 1) {
    if (x < 0 || y < 0 || x >= this.w || y >= this.h) return;
    const i = y * this.w + x;
    this.p[i] = v === 2 ? this.p[i] ^ 1 : v;
  }
  fill(x, y, w, h, v = 1) {
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) this.set(x + i, y + j, v);
  }
  frame(x, y, w, h, v = 1) {
    for (let i = 0; i < w; i++) {
      this.set(x + i, y, v);
      this.set(x + i, y + h - 1, v);
    }
    for (let j = 1; j < h - 1; j++) {
      this.set(x, y + j, v);
      this.set(x + w - 1, y + j, v);
    }
  }
  sprite(rows, x, y, v = 1) {
    rows.forEach((row, j) => row.forEach((b, i) => b && this.set(x + i, y + j, v)));
  }
}

// ───────────────────────── шрифт ─────────────────────────
// '-' — простая черта 3 px по середине строки: минус и «--» (пусто)
const MINUS = { w: 5, cols: [8, 8, 8] };
function glyphOf(ch, font = 'klaatu') {
  if (ch === '-') return MINUS;
  return KF.variants[font][ch] || KF.symbols[ch] || KF.symbols[' '];
}
// y — верх 8-пиксельной строки; буква занимает ряды y+1…y+5
function drawGlyph(fb, g, x, y, v = 1) {
  g.cols.forEach((col, i) => {
    for (let r = 0; r < 8; r++) if ((col >> r) & 1) fb.set(x + i, y + r, v);
  });
  return g.w;
}
function text(fb, x, y, s, v = 1, font = 'klaatu') {
  for (const ch of s) x += drawGlyph(fb, glyphOf(ch, font), x, y, v);
  return x;
}
function textW(s, font = 'klaatu') {
  let w = 0;
  for (const ch of s) w += glyphOf(ch, font).w;
  return w;
}
// надпись в инверсной плашке, как имя страницы у Калейдоскопа
function label(fb, x, y, s) {
  const w = textW(s) - 1;
  fb.fill(x, y, w + 1, 7, 1);
  text(fb, x + 1, y, s, 0);
  return x + w + 2;
}

// ───────────────────────── тайл ─────────────────────────
const octDelta = t => Math.max(-3, Math.min(3, t.o - A.OCT_BASE));

// Буква ноты в поле 5×5 (ряды 1–5 от y). Цельные — заглавные Klaatu с точками,
// диезы — строчные Klaatu без точек. Строчные уже 5 px — по центру; у g хвост
// поднимаем, чтобы низ буквы стоял на 5-м ряду и не лез в зазор.
function drawNoteGlyph(fb, x0, y, n) {
  const letter = A.NOTE_LETTER[n];
  if (!A.NOTE_SHARP[n]) return drawGlyph(fb, glyphOf(letter, 'klaatu'), x0 + 1, y, 1);
  const g = glyphOf(letter.toLowerCase(), 'klaatu_light');
  const bottom = Math.max(...g.cols.map(c => 31 - Math.clz32(c)));
  const x = x0 + 1 + Math.floor((5 - g.cols.length) / 2);
  return drawGlyph(fb, g, x, y - Math.max(0, bottom - 5), 1);
}

// Только буквы: эффекты на тайле не показываются, пустая клетка — точка
function drawTile(fb, ox, oy, t) {
  if (t.n < 0) {
    fb.set(ox + 3, oy + 3, 1);
    return;
  }
  const d = octDelta(t);
  const shift = d > 0 ? 1 : d < 0 ? -1 : 0;
  drawNoteGlyph(fb, ox, oy + shift, t.n);
  if (d) {
    const row = d > 0 ? oy : oy + 6;
    A.STRIPE[Math.abs(d)].forEach((b, i) => b && fb.set(ox + 1 + i, row, 1));
  }
}

// ───────────────────────── маршрут на карте ─────────────────────────
const adjacent = (a, b) => Math.abs(a.c - b.c) + Math.abs(a.r - b.r) === 1;
const around = (a, b) => Math.max(Math.abs(a.c - b.c), Math.abs(a.r - b.r)) === 1; // 8 соседей
const dirTo = (a, b) => (adjacent(a, b) ? [b.c - a.c, b.r - a.r] : null);

// Пустая клетка на маршруте: к точке по центру — по пикселю туда, откуда
// и куда идёт путь. Не больше 3 px, за пределы клетки не выходит.
function emptyMark(fb, ox, oy, [dx, dy]) {
  fb.set(ox + 3 + dx, oy + 3 + dy, 1);
}

// Связка — один пиксель в зазоре между соседними клетками маршрута
function link(fb, org, a, b) {
  const [lx, ly] = org(a.c < b.c || a.r < b.r ? a : b);
  if (a.r === b.r) fb.set(lx + 7, ly + 3);
  else fb.set(lx + 3, ly + 7);
}

function drawPath(fb, org, tileOf, path, closed) {
  const n = path.length;
  const pairs = [];
  for (let k = 1; k < n; k++) pairs.push([path[k - 1], path[k]]);
  if (closed && n > 2) pairs.push([path[n - 1], path[0]]);
  pairs.forEach(([a, b]) => {
    if (!adjacent(a, b)) return;
    link(fb, org, a, b);
    if (tileOf(a).n < 0) emptyMark(fb, ...org(a), dirTo(a, b));
    if (tileOf(b).n < 0) emptyMark(fb, ...org(b), dirTo(b, a));
  });
}

// Указатель — инверсия клетки, без отметки направления; на ударе ноты вокруг
// замыкается рамка. invert = false — клетку уже инвертировал другой указатель.
// У заглушённого маршрута — шахматка (дизеринг из шрифта Калейдоскопа) вместо инверсии.
function drawPointer(fb, ox, oy, hit, invert = true, muted = false) {
  if (muted) {
    for (let j = 0; j < A.BODY; j++) for (let i = 0; i < A.BODY; i++) if ((i + j) % 2 === 0) fb.set(ox + i, oy + j, 2);
    return;
  }
  if (invert) fb.fill(ox, oy, A.BODY, A.BODY, 2);
  if (hit) fb.frame(ox - 1, oy - 1, 9, 9, 1);
}
// Курсор — неоконченная рамка: уголки по 3 px в зазорах
function drawCursor(fb, ox, oy) {
  A.CORNERS.forEach(([x, y]) => fb.set(ox + x, oy + y, 1));
}

// Шкалы значений Калейдоскопа — 24 px, биты из прошивки (старший — начало шкалы).
// Вертикальная растущая — прогресс маршрута в панели.
function scaleBar(fb, x, y0, w, frac) {
  const k = Math.max(0, Math.min(23, Math.round(frac * 23)));
  const mask = KF.scales.rising[k] >>> 0;
  for (let b = 0; b < 24; b++) {
    if ((mask >>> (31 - b)) & 1) for (let i = 0; i < w; i++) fb.set(x + i, y0 + 23 - b, 1);
  }
}
// ───────────────────────── модель ─────────────────────────
const SPEEDS = [
  { t: '2/1', m: 2 },
  { t: '1/1', m: 1 },
  { t: '1/2', m: 0.5 },
  { t: '1/4', m: 0.25 },
  { t: '1/8', m: 0.125 },
];
// открытый: вперёд по кругу, туда-обратно, назад по кругу (старт с конца цепи);
// замкнутый — по кругу в одну из сторон
const ENDS_OPEN = ['LOOP', 'PONG', 'REV'];
const ENDS_CLOSED = ['FWD', 'REV'];
const MAX_LEN = 16;
const VELS = [null, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127]; // null — как у маршрута
const GATES = [null, ...Array.from({ length: 20 }, (_, i) => 5 * (i + 1))]; // % шага

// Эффекты: параметры — [имя, значения, подсказка до 14 знаков]; в тайле хранятся индексы значений
const nums = (a, b, f = String) => Array.from({ length: b - a + 1 }, (_, i) => f(a + i));
const sign = v => (v > 0 ? '+' + v : String(v));
const FXDEF = {
  A: [['CHD', ['OCT', '5TH', 'MAJ', 'MIN', 'SUS', '7TH'], 'CHORD'], ['NUM', ['2', '3', '4'], 'NOTES IN STEP'], ['DIR', ['UP', 'DN', 'UD', 'RND'], 'ARP DIRECTION']],
  R: [['CNT', nums(2, 8), 'HITS IN STEP'], ['RMP', ['FLAT', 'UP', 'DN'], 'VOLUME RAMP']],
  X: [['AMT', nums(1, 8, v => '+' + 8 * v), 'VELOCITY BOOST']],
  P: [['VAL', [...nums(0, 15, v => String(8 * v)), '127'], 'PRESSURE'], ['SHP', ['HOLD', 'RISE', 'FALL'], 'PRESSURE SHAPE']],
  I: [['INT', nums(-24, 24, sign), 'SEMITONES']],
  L: [],
  O: [['DIR', ['CW', 'CCW'], 'CW OR CCW'], ['ANG', ['45', '90', '180'], 'ANGLE']],
  Z: [['DIR', ['L', 'R'], 'LEFT OR RIGHT'], ['STP', nums(1, 3), 'CELLS TO SHIFT']],
  V: [['DIR', ['UP', 'DN'], 'UP OR DOWN'], ['STP', nums(1, 3), 'CELLS TO SHIFT']],
  D: [],
  W: [['STP', nums(1, 8), 'STEPS TO WAIT']],
  T: [['STP', nums(1, 8), 'STEPS TO HOLD']],
  S: [['DIR', ['FAST', 'SLOW'], 'FASTER/SLOWER']],
  H: [['MOD', ['FWD', 'BACK', 'HOME'], 'WHERE TO JUMP'], ['N', nums(1, 8), 'STEPS']],
};
const FX_DEFAULT = { A: [2, 1, 0], R: [1, 0], X: [3], P: [8, 0], I: [31], O: [0, 1], Z: [1, 0], V: [1, 0], W: [1], T: [1], S: [0], H: [0, 1] };
const FX_ORDER = Object.keys(FXDEF);
const CONDS = Object.keys(A.COND);
const fxVal = (fx, k) => FXDEF[fx.code][k][1][fx.p[k]];
// trig — условие по счёту (см. TRIGS), count — сколько раз указатели прошли тайл с начала игры
const newFx = (code, cond = 'PASS') => ({ code, p: [...(FX_DEFAULT[code] || [])], cond, armed: false, trig: '--', count: 0 });

// Условие по счёту, как условия триггеров у Elektron: A:B — на A-м из каждых B проходов
// указателя через тайл; PRE — если сработал последний эффект, мимо которого прошёл этот
// указатель, /PRE — если не сработал; '--' — каждый раз.
const TRIGS = ['--'];
for (let b = 2; b <= 8; b++) for (let a = 1; a <= b; a++) TRIGS.push(`${a}:${b}`);
TRIGS.push('PRE', '/PRE');
const trigAB = trig => {
  const m = /^(\d):(\d)$/.exec(trig);
  return m ? [+m[1], +m[2]] : null;
};
function trigMet(fx, r) {
  if (fx.trig === 'PRE') return r.prevFx === true;
  if (fx.trig === '/PRE') return r.prevFx !== true;
  const ab = trigAB(fx.trig);
  return !ab || (fx.count - 1) % ab[1] === ab[0] - 1;
}
function trigHelp(trig) {
  const ab = trigAB(trig);
  if (ab) return `PASS ${ab[0]} OF ${ab[1]}`;
  return { '--': 'EVERY PASS', PRE: 'LAST FX FIRED', '/PRE': 'LAST FX SILENT' }[trig];
}

function parseNote(s) {
  const t = { n: -1, o: A.OCT_BASE, vel: null, gate: null, fx: null };
  const m = s && s.match(/^([A-G]#?)(\d)$/);
  if (m) Object.assign(t, { n: A.NOTE_NAME.indexOf(m[1]), o: +m[2] });
  return t;
}
const cells = list => list.map(([c, r]) => ({ c, r }));
const endsOf = r => (r.closed ? ENDS_CLOSED : ENDS_OPEN);
const startIndex = r => (!r.closed && ENDS_OPEN[r.end] === 'REV' ? r.cells.length - 1 : 0);

function makeRoute(i) {
  return { cells: [], closed: false, speed: 1, end: 0, ch: i + 1, vel: 100, gate: 50, i: 0, dir: 1, next: 0, fresh: true, flash: 0, fx: null, fxFlash: 0, wait: 0, jump: null, prevFx: null, muted: false };
}

function makeState() {
  const S = {
    cols: COLS,
    rows: ROWS,
    grid: null, // живая карта — её двигают эффекты
    src: null, // исходник — те же тайлы на исходных местах; он и сохраняется
    routes: [0, 1, 2, 3].map(makeRoute),
    focus: 0,
    cur: { c: 0, r: 0 },
    mode: 'map', // map | tile | draw | line | settings
    view: 'map', // куда вернуться из настроек
    playing: false,
    bpm: 120,
    sync: 'INT',
    draw: null,
    tile: null, // экран тайла: { page, sel, back }
    lineSel: 0,
    setSel: 0,
    pending: [], // изменения тайлов — со следующего шага
    t0: 0, // начало сетки — от него считаются такты
    bar: 0,
    queued: null, // загрузка проекта ждёт начала такта
    slots: Array(SLOTS).fill(null), // { data, auto } — auto: черновик, пишется автосохранением
    slot: 0, // загруженный слот
    autosave: false,
    saved: '', // проект в слоте — с чем сравнивать
    lastJSON: '',
    lastChange: 0,
    proj: null, // экран проектов: { sel, hold, back, done }
    ext: { on: false, bpm: 120, t0: 0 }, // клок на MIDI IN (в прототипе — имитация)
    hold: null, // удержание кнопки энкодера на карте/в линии: { act, t0 }
    playHold: null, // удержание PLAY: { t0 }
  };
  // демо-проекты идут с прошивкой и лежат в слотах 01…05, открыт первый
  DEMOS.list.forEach((_, k) => (S.slots[k] = { data: demoProject(k), auto: false }));
  S.demos = DEMOS.version;
  applyProject(S, S.slots[0].data);
  return S;
}

// демо k из assets/maze_demos.js (генерирует tools/gen_demos.py) — в формате проекта
function demoProject(k) {
  const d = DEMOS.list[k];
  const src = d.grid.map(row => row.map(parseNote));
  d.fx.forEach(([c, r, code, cond, p, trig]) => {
    const fx = (src[r][c].fx = newFx(code, cond));
    if (p) p.forEach((v, j) => (fx.p[j] = v));
    if (trig) fx.trig = trig;
  });
  const routes = d.routes.map((x, i) => Object.assign(makeRoute(i), { cells: cells(x.cells), closed: x.closed, speed: x.speed, end: x.end }));
  return serialize({ src, routes, bpm: d.bpm, sync: 'INT' });
}

// ───────────────────────── проекты ─────────────────────────
// Проект — исходник: карта, как её расставили руками, маршруты, темп, синхронизация.
// Эффекты O, Z, V двигают только живую карту; тайлы в обоих слоях — одни и те же объекты,
// поэтому правка тайла попадает и в исходник. Вернуть карту к исходнику — LOAD слота.
const SLOTS = 32;
const HOLD_MS = 900; // LOAD и SAVE — удержанием, пока не заполнится шкала
function serialize(S) {
  const fx = f => f && { code: f.code, p: f.p, cond: f.cond, trig: f.trig };
  return JSON.stringify({
    v: 1,
    grid: S.src.map(row => row.map(t => ({ n: t.n, o: t.o, vel: t.vel, gate: t.gate, fx: fx(t.fx) }))),
    routes: S.routes.map(r => ({ cells: r.cells.map(p => [p.c, p.r]), closed: r.closed, speed: r.speed, end: r.end, ch: r.ch, vel: r.vel, gate: r.gate })),
    bpm: S.bpm,
    sync: S.sync,
  });
}
// data = null — новый пустой проект
function applyProject(S, data, now = 0) {
  const d = data ? JSON.parse(data) : null;
  const tile = t => ({ ...t, fx: t.fx && Object.assign(newFx(t.fx.code, t.fx.cond), t.fx, { p: [...t.fx.p] }) });
  S.src = d ? d.grid.map(row => row.map(tile)) : Array.from({ length: ROWS }, () => Array.from({ length: COLS }, () => parseNote('-')));
  S.grid = S.src.map(row => [...row]);
  S.routes = [0, 1, 2, 3].map(makeRoute);
  if (d) {
    d.routes.forEach((x, i) => {
      const r = Object.assign(S.routes[i], { cells: cells(x.cells), closed: x.closed, speed: x.speed, end: x.end, ch: x.ch, vel: x.vel, gate: x.gate });
      r.i = startIndex(r);
    });
    S.bpm = d.bpm;
    S.sync = d.sync;
  }
  S.pending = [];
  S.saved = S.lastJSON = serialize(S);
  if (S.playing) restartGrid(S, now);
}
const isDirty = S => S.lastJSON !== S.saved;

// Автосохранение — только для черновика: слот был пустым при загрузке или уже писался
// автосохранением. После ручного SAVE слот защищён, автосохранение выключено.
// Пишем через 2 с после последней правки исходника.
function autosaveStep(S, now) {
  const json = serialize(S);
  if (json !== S.lastJSON) {
    S.lastJSON = json;
    S.lastChange = now;
  }
  if (!S.autosave || json === S.saved || now - S.lastChange < 2000) return false;
  S.slots[S.slot] = { data: json, auto: true };
  S.saved = json;
  persist(S);
  return true;
}
// LOAD: во время игры — с начала следующего такта
function loadSlot(S, k) {
  const slot = S.slots[k];
  S.queued = { k, data: slot ? slot.data : null, auto: !slot || slot.auto };
}
function loadNow(S, q, now) {
  applyProject(S, q.data, now);
  S.slot = q.k;
  S.autosave = q.auto;
  persist(S);
}
function saveSlot(S, k) {
  S.lastJSON = serialize(S);
  S.slots[k] = { data: S.lastJSON, auto: false };
  S.saved = S.lastJSON;
  S.slot = k;
  S.autosave = false;
  persist(S);
}
// в браузере слоты живут в localStorage; в jsc его нет
function persist(S) {
  try {
    localStorage.setItem('maze.v1', JSON.stringify({ slots: S.slots, slot: S.slot, demos: S.demos }));
  } catch (e) {}
}
function restore(S) {
  try {
    const d = JSON.parse(localStorage.getItem('maze.v1'));
    if (!d || !d.slots) return;
    S.slots = d.slots;
    S.slot = d.slot;
    // новая версия демо — в пустые слоты 01…05, записанное не трогаем
    if (d.demos !== DEMOS.version) DEMOS.list.forEach((_, k) => S.slots[k] || (S.slots[k] = { data: demoProject(k), auto: false }));
    S.demos = DEMOS.version;
    const s = S.slots[S.slot];
    applyProject(S, s ? s.data : null);
    S.autosave = !s || s.auto;
  } catch (e) {}
}


const tileAt = (S, p) => S.grid[p.r][p.c];
const stepDur = (S, r) => 60 / S.bpm / 4 / SPEEDS[r.speed].m; // 1/1 = шестнадцатые
const same = (a, b) => a.c === b.c && a.r === b.r;

function nextIndex(r) {
  const len = r.cells.length;
  if (len <= 1) return { i: 0, dir: r.dir };
  const mode = endsOf(r)[r.end];
  if (mode === 'FWD' || mode === 'LOOP') return { i: (r.i + 1) % len, dir: 1 };
  if (mode === 'REV') return { i: (r.i - 1 + len) % len, dir: -1 };
  let dir = r.dir;
  let i = r.i + dir;
  if (i < 0 || i >= len) {
    dir = -dir;
    i = r.i + dir;
  }
  return { i, dir };
}
// разворот на месте: LOOP ↔ REV, у PONG — сразу в другую сторону, замкнутый FWD ↔ REV
function flipDir(r) {
  if (r.closed) r.end = 1 - r.end;
  else if (ENDS_OPEN[r.end] === 'PONG') r.dir = -r.dir;
  else r.end = ENDS_OPEN[r.end] === 'LOOP' ? 2 : 0;
}

// кратчайший путь по соседям без диагоналей в обход уже пройденных клеток;
// в первую клетку маршрута войти можно — это замыкание
function shortestPath(S, path, to) {
  const from = path[path.length - 1];
  const key = p => p.r * S.cols + p.c;
  const start = path[0];
  const blocked = new Set(path.map(key));
  const closing = path.length >= 3 && same(to, start);
  if (!closing && blocked.has(key(to))) return null;
  const prev = new Map([[key(from), null]]);
  const q = [from];
  while (q.length) {
    const p = q.shift();
    if (same(p, to)) break;
    for (const [dc, dr] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const n = { c: p.c + dc, r: p.r + dr };
      if (n.c < 0 || n.r < 0 || n.c >= S.cols || n.r >= S.rows) continue;
      const k = key(n);
      if (prev.has(k)) continue;
      // в первую клетку — только как в конец пути
      if (blocked.has(k) && !(closing && same(n, start) && same(n, to))) continue;
      prev.set(k, p);
      q.push(n);
    }
  }
  if (!prev.has(key(to))) return null;
  const out = [];
  for (let p = to; p && key(p) !== key(from); p = prev.get(key(p))) out.unshift({ c: p.c, r: p.r });
  return { cells: out, closing };
}

// ───────────────────────── звук ─────────────────────────
let actx = null;
const WAVES = ['triangle', 'square', 'sawtooth', 'sine'];
// shape — афтертач: HOLD держит, RISE нарастает, FALL спадает
function beep(route, midi, vel, at, dur, shape) {
  if (!UI.sound) return;
  actx = actx || new AudioContext();
  const t = actx.currentTime + at;
  const o = actx.createOscillator();
  const g = actx.createGain();
  o.type = WAVES[route];
  o.frequency.value = 440 * Math.pow(2, (midi - 69) / 12);
  const peak = (route === 1 || route === 2 ? 0.05 : 0.11) * Math.max(0.05, vel / 127);
  const end = t + Math.max(0.05, dur);
  g.gain.setValueAtTime(0.0001, t);
  if (shape === 'RISE') {
    g.gain.exponentialRampToValueAtTime(peak * 0.3, t + 0.005);
    g.gain.exponentialRampToValueAtTime(peak * 1.5, end);
  } else if (shape === 'HOLD') {
    g.gain.exponentialRampToValueAtTime(peak, t + 0.005);
    g.gain.setValueAtTime(peak, end - 0.02);
  } else {
    g.gain.exponentialRampToValueAtTime(peak, t + 0.005);
    g.gain.exponentialRampToValueAtTime(peak * (shape === 'FALL' ? 0.02 : 0.3), end);
  }
  g.gain.exponentialRampToValueAtTime(0.0001, end + 0.03);
  o.connect(g).connect(actx.destination);
  o.start(t);
  o.stop(end + 0.05);
}

// ───────────────────────── эффекты ─────────────────────────
// Условие проверяется, когда указатель приходит на тайл (соседи — все 8 клеток)
function condMet(S, ri, p, t) {
  const others = S.routes.filter((r, k) => k !== ri && r.cells.length).map(r => r.cells[r.i]);
  switch (t.fx.cond) {
    case 'PASS':
      return true;
    case 'MEET':
      return others.some(q => same(q, p));
    case 'NEAR':
      return others.some(q => around(q, p));
    case 'CHNG': // взведён, когда соседа сдвинул или повернул эффект (вопрос в бэклоге)
      if (!t.fx.armed) return false;
      t.fx.armed = false;
      return true;
  }
  return false;
}

const CHORDS = { OCT: [0, 12], '5TH': [0, 7, 12], MAJ: [0, 4, 7, 12], MIN: [0, 3, 7, 12], SUS: [0, 5, 7, 12], '7TH': [0, 4, 7, 10] };

// MIDI-эффекты меняют ноты шага; тайловые — откладываются до следующего шага;
// эффекты указателя меняют состояние маршрута и держатся, пока настройку не изменят.
function applyFx(S, r, p, fx, notes, step) {
  const v = k => fxVal(fx, k);
  switch (fx.code) {
    case 'A': {
      if (!notes.length) break;
      const base = notes[0];
      const tones = CHORDS[v(0)];
      const n = +v(1);
      const seq = { UP: tones, DN: [...tones].reverse(), UD: [...tones, ...tones.slice(1, -1).reverse()] }[v(2)];
      notes.length = 0;
      for (let k = 0; k < n; k++) {
        const iv = v(2) === 'RND' ? tones[Math.floor(Math.random() * tones.length)] : seq[k % seq.length];
        notes.push({ ...base, midi: base.midi + iv, at: (k * step) / n, dur: (base.dur / n) * 0.9 });
      }
      break;
    }
    case 'R': {
      if (!notes.length) break;
      const base = notes[0];
      const n = +v(0);
      notes.length = 0;
      for (let k = 0; k < n; k++) {
        const ramp = { FLAT: 1, UP: (k + 1) / n, DN: (n - k) / n }[v(1)];
        notes.push({ ...base, vel: Math.max(1, Math.round(base.vel * ramp)), at: (k * step) / n, dur: (step / n) * 0.5 });
      }
      break;
    }
    case 'X':
      notes.forEach(nt => (nt.vel = Math.min(127, nt.vel + +v(0))));
      break;
    case 'P':
      notes.forEach(nt => {
        nt.at_v = +v(0);
        nt.shape = v(1);
      });
      break;
    case 'I':
      notes.forEach(nt => (nt.midi = Math.max(0, Math.min(127, nt.midi + +v(0)))));
      break;
    case 'L':
      notes.forEach(nt => (nt.dur = step * 1.02));
      break;
    case 'O':
    case 'Z':
    case 'V':
      S.pending.push({ code: fx.code, p: { ...p }, a: v(0), b: v(1) });
      break;
    case 'D':
      flipDir(r);
      break;
    case 'W':
      r.wait = +v(0);
      break;
    case 'T':
      r.wait = +v(0);
      notes.forEach(nt => (nt.dur = step * (r.wait + 1) * 0.98));
      break;
    case 'S':
      r.speed = Math.max(0, Math.min(SPEEDS.length - 1, r.speed + (v(0) === 'FAST' ? -1 : 1)));
      break;
    case 'H':
      r.jump = { mode: v(0), n: +v(1) };
      break;
  }
}

// Поворот 8 соседей (тор: за краем карты — противоположный край) и сдвиги рядов/столбцов.
// Тайл переезжает целиком. Тайлы с условием CHNG рядом с изменёнными клетками взводятся.
const RING = [[-1, -1], [0, -1], [1, -1], [1, 0], [1, 1], [0, 1], [-1, 1], [-1, 0]];
const wrap = (c, r) => ({ c: (c + COLS) % COLS, r: (r + ROWS) % ROWS });
function applyPending(S) {
  const before = S.grid.map(row => [...row]);
  S.pending.forEach(op => {
    const g = S.grid;
    const snap = g.map(row => [...row]);
    if (op.code === 'O') {
      const k = { 45: 1, 90: 2, 180: 4 }[op.b] * (op.a === 'CW' ? 1 : -1);
      RING.forEach(([dc, dr], j) => {
        const from = wrap(op.p.c + dc, op.p.r + dr);
        const [tc, tr] = RING[(j + k + 8) % 8];
        const to = wrap(op.p.c + tc, op.p.r + tr);
        g[to.r][to.c] = snap[from.r][from.c];
      });
    } else if (op.code === 'Z') {
      const s = +op.b * (op.a === 'R' ? 1 : -1);
      for (let c = 0; c < COLS; c++) g[op.p.r][(c + s + COLS) % COLS] = snap[op.p.r][c];
    } else {
      const s = +op.b * (op.a === 'DN' ? 1 : -1);
      for (let r = 0; r < ROWS; r++) g[(r + s + ROWS) % ROWS][op.p.c] = snap[r][op.p.c];
    }
  });
  S.pending = [];
  const changed = [];
  for (let r = 0; r < ROWS; r++) for (let c = 0; c < COLS; c++) if (S.grid[r][c] !== before[r][c]) changed.push({ c, r });
  for (let r = 0; r < ROWS; r++)
    for (let c = 0; c < COLS; c++) {
      const t = S.grid[r][c];
      if (t.fx && t.fx.cond === 'CHNG' && changed.some(q => around(q, { c, r }))) t.fx.armed = true;
    }
}

// ───────────────────────── транспорт ─────────────────────────
function trigger(S, ri, now) {
  const r = S.routes[ri];
  const p = r.cells[r.i];
  const t = tileAt(S, p);
  const step = stepDur(S, r);
  const vel = t.vel ?? r.vel;
  const gate = (t.gate ?? r.gate) / 100;
  const notes = t.n >= 0 ? [{ midi: 12 * (t.o + 1) + t.n, vel, at: 0, dur: step * gate }] : [];
  if (t.fx) {
    // каждый проход считается; если счёт не совпал, условие не проверяется (взведённый CHNG ждёт)
    const fx = t.fx;
    fx.count++;
    const fired = trigMet(fx, r) && condMet(S, ri, p, t);
    r.prevFx = fired; // для PRE следующего тайла с эффектом
    if (fired) {
      applyFx(S, r, p, fx, notes, step);
      r.fx = fx.code; // в колонке маршрута — последний сработавший
      r.fxFlash = now + 160;
    }
  }
  // заглушённый маршрут молчит, но указатель идёт и эффекты срабатывают
  if (r.muted) return;
  if (notes.length) r.flash = now + Math.min(0.5 * step, 0.12) * 1000;
  notes.forEach(nt => beep(ri, nt.midi, nt.vel, nt.at, Math.min(nt.dur, 4), nt.shape));
}

function advance(r) {
  if (!r.jump) return Object.assign(r, nextIndex(r));
  const { mode, n } = r.jump;
  r.jump = null;
  if (mode === 'HOME') {
    r.i = startIndex(r);
    r.dir = 1;
    return;
  }
  if (mode === 'BACK') flipDir(r);
  for (let k = 0; k < n; k++) Object.assign(r, nextIndex(r));
  if (mode === 'BACK') flipDir(r);
}

const barMs = S => (4 * 60000) / S.bpm;
const MUTE_MS = 500; // удержание PLAY — мьют маршрута в фокусе
function tick(S, now) {
  if (S.proj && S.proj.hold && now - S.proj.hold.t0 >= HOLD_MS) projAction(S, now);
  if (S.hold && now - S.hold.t0 >= HOLD_MS) clearAction(S);
  if (S.playHold && now - S.playHold.t0 >= MUTE_MS) {
    const r = S.routes[S.focus];
    r.muted = !r.muted;
    S.playHold = null;
  }
  S.sync = S.ext.on ? 'EXT' : 'INT';
  if (S.ext.on) S.bpm = S.ext.bpm; // ведомый: темп снаружи
  if (S.queued && (!S.playing || Math.floor((now - S.t0) / barMs(S)) > S.bar)) {
    loadNow(S, S.queued, now);
    S.queued = null;
  }
  if (!S.playing) return;
  S.bar = Math.floor((now - S.t0) / barMs(S));
  if (S.pending.length && S.routes.some(r => r.cells.length && now >= r.next)) applyPending(S);
  S.routes.forEach((r, ri) => {
    if (!r.cells.length) return;
    let guard = 0;
    while (now >= r.next && guard++ < 8) {
      const step = stepDur(S, r) * 1000;
      if (r.fresh) r.fresh = false;
      else if (r.wait > 0) {
        r.wait--; // стоит на месте: W молчит, у T нота ещё тянется
        r.next += step;
        continue;
      } else advance(r);
      trigger(S, ri, now);
      r.next += step;
    }
  });
}
// пауза: указатели стоят; продолжение — сетка с начала, с тех же позиций;
// счёт проходов тоже с начала
function restartGrid(S, now) {
  S.routes.forEach(r => {
    r.next = now;
    r.fresh = true;
    r.prevFx = null;
  });
  S.src.forEach(row => row.forEach(t => t.fx && (t.fx.count = 0)));
  S.t0 = now;
  S.bar = 0;
}
// Ведомый (клок на MIDI IN): снятие паузы — с ближайшего нового шага внешнего клока
// (каждые 6 тиков из 24 на четверть = шестнадцатая).
function resumeAt(S, now) {
  if (!S.ext.on) return now;
  const step = 60000 / S.ext.bpm / 4;
  return S.ext.t0 + Math.ceil((now - S.ext.t0) / step) * step;
}
function togglePlay(S, now = performance.now()) {
  S.playing = !S.playing;
  if (S.playing) restartGrid(S, resumeAt(S, now));
}
// сообщения транспорта с MIDI IN: Stop — пауза, Start и Continue — как TAP+TAP
function extTransport(S, msg, now = performance.now()) {
  if (msg === 'STOP' && S.playing) togglePlay(S, now);
  if ((msg === 'START' || msg === 'CONT') && !S.playing) togglePlay(S, now);
}

// Стирание удержанием кнопки энкодера: на карте — тайл под курсором,
// в линии — цепь маршрута в фокусе (тайлы остаются: на них могут стоять другие маршруты).
function clearAction(S) {
  const act = S.hold.act;
  S.hold = null;
  if (act === 'TILE') Object.assign(tileAt(S, S.cur), { n: -1, o: A.OCT_BASE, vel: null, gate: null, fx: null });
  else {
    const r = S.routes[S.focus];
    Object.assign(r, { cells: [], closed: false, i: 0, dir: 1, fx: null, wait: 0, jump: null, prevFx: null });
    S.lineSel = 0;
  }
}

// ───────────────────────── экран тайла ─────────────────────────
// Страницы NOTE, FX, COND. У параметра — имя, значение и подсказка (help) для
// расширенного экрана.
// VEL и LEN «RT» — как у маршрута.
const PAGES = ['NOTE', 'FX', 'COND'];
const KEYS = ['--', ...A.NOTE_NAME];
const FX_LIST = ['--', ...FX_ORDER];
function tileParams(S, t, page) {
  const cyc = (list, cur, d) => list[(list.indexOf(cur) + d + list.length) % list.length];
  const clampIdx = (list, cur, d) => list[Math.max(0, Math.min(list.length - 1, list.indexOf(cur) + d))];
  if (page === 0)
    return [
      { name: 'KEY', text: KEYS[t.n + 1], help: 'NOTE NAME', set: d => (t.n = ((t.n + 1 + d + 13) % 13) - 1) },
      { name: 'OCT', text: t.n < 0 ? '--' : String(t.o), help: 'OCTAVE', set: d => (t.o = Math.max(A.OCT_MIN, Math.min(A.OCT_MAX, t.o + d))) },
      { name: 'VEL', text: t.vel == null ? 'RT' : String(t.vel), help: t.vel == null ? 'VEL FROM ROUTE' : 'VELOCITY', set: d => (t.vel = clampIdx(VELS, t.vel, d)) },
      { name: 'LEN', text: t.gate == null ? 'RT' : String(t.gate), help: t.gate == null ? 'LEN FROM ROUTE' : 'NOTE LENGTH', set: d => (t.gate = clampIdx(GATES, t.gate, d)) },
    ];
  if (page === 1) {
    const code = t.fx ? t.fx.code : '--';
    const ps = [
      {
        name: 'FX',
        text: code,
        help: t.fx ? A.FX[code][2] : 'NO EFFECT',
        set: d => {
          const next = cyc(FX_LIST, code, d);
          t.fx = next === '--' ? null : newFx(next, t.fx ? t.fx.cond : 'PASS');
        },
      },
    ];
    if (t.fx)
      FXDEF[t.fx.code].forEach(([name, vals, help], k) =>
        ps.push({ name, text: vals[t.fx.p[k]], help, set: d => (t.fx.p[k] = Math.max(0, Math.min(vals.length - 1, t.fx.p[k] + d))) }),
      );
    return ps;
  }
  if (!t.fx) return [{ name: 'IF', text: '--', help: 'SET FX FIRST', set: () => {} }];
  const fx = t.fx;
  return [
    { name: 'IF', text: fx.cond, help: A.COND_HELP[fx.cond], set: d => (fx.cond = cyc(CONDS, fx.cond, d)) },
    { name: 'TRG', text: fx.trig, help: trigHelp(fx.trig), set: d => (fx.trig = cyc(TRIGS, fx.trig, d)) },
  ];
}

// Показ A:B языком карты: B клеток, A-я — квадрат (сработает), остальные — точки
// (пропуск, как пустая клетка), последний проход — инверсия (как указатель).
// Шаг 7 px, до 8 клеток = 56 px. Только показ — значение меняется энкодером в TRG.
function drawTrig(fb, x, y, fx) {
  const ab = trigAB(fx.trig);
  if (!ab) return;
  const last = fx.count ? (fx.count - 1) % ab[1] : -1;
  for (let i = 0; i < ab[1]; i++) {
    const cx = x + 7 * i;
    if (i === ab[0] - 1) fb.fill(cx + 1, y + 2, 4, 4, 1);
    else fb.fill(cx + 2, y + 3, 2, 2, 1);
    if (i === last) fb.fill(cx, y + 1, 6, 6, 2);
  }
}

// Строка параметра: имя (выбранное — инверсией) и значение
function paramRow(fb, x, y, pr, selected, valueX) {
  const nx = selected ? label(fb, x, y, pr.name) + 1 : text(fb, x + 1, y, pr.name) + 2;
  text(fb, Math.max(nx, valueX ?? 0), y, pr.text);
}

// Карта + панель (правка с карты кнопкой энкодера). Шапка панели — первый параметр
// страницы под её именем: NOTE и нота, FX и команда; на COND шапка — имя страницы.
// Остальные параметры — строками ниже, их не больше трёх: прокрутки нет.
function drawTileMapPanel(fb, S, t) {
  const st = S.tile;
  const ps = tileParams(S, t, st.page);
  const vx = PANEL + 32; // значения — в одну колонку
  if (st.page === 2) {
    // COND: условие, условие по счёту и под ним показ A:B
    label(fb, PANEL, 0, 'COND');
    ps.forEach((pr, k) => paramRow(fb, PANEL, 8 + 8 * k, pr, k === st.sel, vx));
    if (t.fx) drawTrig(fb, PANEL + 1, 24, t.fx);
    return;
  }
  paramRow(fb, PANEL, 0, { name: PAGES[st.page], text: ps[0].text }, st.sel === 0, vx);
  ps.slice(1).forEach((pr, k) => paramRow(fb, PANEL, 8 + 8 * k, pr, k + 1 === st.sel, vx));
}

// Расширенный вид (правка из маршрута в линию): слева соседи 3×3 в натуральную
// величину, справа страницы и параметры парами, внизу подсказка в духе LSDj —
// полное имя эффекта или описание параметра под курсором.
function drawTileHood(fb, S, t) {
  const st = S.tile;
  text(fb, 1, 0, `${S.cur.c + 1}:${S.cur.r + 1}`);
  for (let dr = -1; dr <= 1; dr++)
    for (let dc = -1; dc <= 1; dc++) {
      const p = { c: S.cur.c + dc, r: S.cur.r + dr };
      if (p.c < 0 || p.r < 0 || p.c >= COLS || p.r >= ROWS) continue;
      drawTile(fb, 1 + 8 * (dc + 1), 8 + 8 * (dr + 1), tileAt(S, p));
    }
  drawCursor(fb, 9, 16);
  let x = 28;
  PAGES.forEach((pg, i) => (x = i === st.page ? label(fb, x, 0, pg) + 4 : text(fb, x + 1, 0, pg) + 5));
  const ps = tileParams(S, t, st.page);
  if (st.page === 2 && t.fx) {
    // COND: условие и условие по счёту в строку, ниже показ A:B
    paramRow(fb, 28, 8, ps[0], st.sel === 0);
    paramRow(fb, 78, 8, ps[1], st.sel === 1);
    drawTrig(fb, 29, 16, t.fx);
  } else ps.forEach((pr, i) => paramRow(fb, 28 + 50 * (i % 2), 8 + 8 * Math.floor(i / 2), pr, i === st.sel));
  const cur = ps[st.sel];
  if (cur) text(fb, 28, 24, cur.help, 1, 'klaatu_light');
}

// ───────────────────────── экран проектов ─────────────────────────
// Вариант Б: слот крупно. Миниатюра карты 32×16 (тайл с нотой — квадрат 3×3, пустой — точка),
// LOAD — удерживать кнопку энкодера, SAVE — удерживать TAP+кнопку, шкала заполняется.
// TAP+энкодер — выбор слота, энкодер — темп загруженного проекта.
function thumb(fb, x, y, grid) {
  grid.forEach((row, r) => row.forEach((t, c) => (t.n >= 0 ? fb.fill(x + 4 * c, y + 4 * r, 3, 3, 1) : fb.set(x + 4 * c + 1, y + 4 * r + 1, 1))));
}
// растущая шкала Калейдоскопа по горизонтали, 24 px в рамке
function holdBar(fb, x, y, frac) {
  const mask = KF.scales.rising[Math.max(0, Math.min(23, Math.round(frac * 23)))] >>> 0;
  for (let b = 0; b < 24; b++) if ((mask >>> (31 - b)) & 1) for (let j = 2; j <= 4; j++) fb.set(x + b, y + j, 1);
  fb.frame(x - 1, y + 1, 26, 5, 1);
}
function drawProj(fb, S, now) {
  const P = S.proj;
  const slot = S.slots[P.sel];
  const cur = P.sel === S.slot;
  const x0 = label(fb, 0, 0, 'PROJ');
  text(fb, x0 + 3, 0, String(P.sel + 1).padStart(2, '0') + '/' + SLOTS);
  if (cur) text(fb, 100, 0, isDirty(S) ? 'CUR+' : 'CUR');
  // у загруженного слота — исходник как сейчас, у остальных — что записано
  const grid = cur ? S.src : slot && JSON.parse(slot.data).grid;
  if (grid) thumb(fb, 1, 11, grid);
  else text(fb, 2, 16, 'EMPTY', 1, 'klaatu_light');
  const act = P.hold ? P.hold.act : null;
  text(fb, 44, 8, 'LOAD', 1, act === 'LOAD' ? 'klaatu' : 'klaatu_light');
  text(fb, 44, 16, 'SAVE', 1, act === 'SAVE' ? 'klaatu' : 'klaatu_light');
  if (P.hold) holdBar(fb, 78, act === 'LOAD' ? 8 : 16, (now - P.hold.t0) / HOLD_MS);
  else if (now < P.done) text(fb, 78, P.doneRow, 'OK');
  else if (S.queued) text(fb, 78, 8, 'BAR'); // загрузится с начала такта
  if (slot || cur) text(fb, 44, 24, (cur ? S.autosave : slot.auto) ? 'AUTO' : 'SAVED', 1, 'klaatu_light');
  // темп: у загруженного — живой и правится энкодером, у остальных — записанный
  if (cur) text(fb, 92, 24, S.sync === 'EXT' ? 'EXT' : String(S.bpm));
  else if (slot) text(fb, 92, 24, String(JSON.parse(slot.data).bpm), 1, 'klaatu_light');
}
function openProj(S) {
  S.proj = { sel: S.slot, hold: null, back: S.mode === 'line' ? 'line' : 'map', done: 0, doneRow: 8 };
  S.mode = 'proj';
}
function projAction(S, now) {
  const { act } = S.proj.hold;
  S.proj.hold = null;
  if (act === 'SAVE') saveSlot(S, S.proj.sel);
  else loadSlot(S, S.proj.sel);
  S.proj.done = now + 700;
  S.proj.doneRow = act === 'SAVE' ? 16 : 8;
}

// ───────────────────────── рисование экрана ─────────────────────────
const org = p => [1 + 8 * p.c, 8 * p.r];

function drawMap(fb, S, now, opts) {
  const tileOf = p => tileAt(S, p);
  for (let r = 0; r < ROWS; r++) for (let c = 0; c < COLS; c++) drawTile(fb, ...org({ c, r }), S.grid[r][c]);

  if (S.mode === 'draw') {
    drawPath(fb, org, tileOf, S.draw.path, S.draw.closed);
    // предпросмотр пути до курсора — мигает
    if (opts.previewTail && now % 400 < 260) {
      const pv = [S.draw.path[S.draw.path.length - 1], ...opts.previewTail];
      drawPath(fb, org, tileOf, pv, false);
    }
  } else {
    const fr = S.routes[S.focus];
    drawPath(fb, org, tileOf, fr.cells, fr.closed);
    // указатели всех маршрутов
    const inverted = new Set();
    S.routes.forEach(r => {
      if (!r.cells.length) return;
      const p = r.cells[r.i];
      const k = p.r * COLS + p.c;
      drawPointer(fb, ...org(p), now < r.flash, !inverted.has(k), r.muted);
      if (!r.muted) inverted.add(k);
    });
  }
  // курсор (на экране тайла мигает)
  if (S.mode !== 'tile' || now % 300 < 180) drawCursor(fb, ...org(S.cur));
}

// Вкладки маршрутов (выбранная — инверсией); под каждой — шкала прогресса
// и внизу последний сработавший эффект (в момент срабатывания — инверсией).
// Заглушённый маршрут: цифра зачёркнута, шкала пунктиром.
function drawTabs(fb, routes, focus, now, x0) {
  routes.forEach((r, i) => {
    const x = x0 + 8 * i;
    const empty = !r.cells.length;
    drawGlyph(fb, glyphOf(String(i + 1), empty ? 'klaatu_light' : 'klaatu'), x, 0, 1);
    if (r.muted) fb.fill(x - 1, 3, 7, 1, 1);
    if (!empty) {
      scaleBar(fb, x + 6, 8, 1, r.cells.length > 1 ? r.i / (r.cells.length - 1) : 1);
      if (r.muted) for (let y = 9; y < 32; y += 2) fb.set(x + 6, y, 0);
      if (r.fx) {
        drawGlyph(fb, glyphOf(r.fx), x, 24, 1);
        if (now < r.fxFlash) fb.fill(x - 1, 24, 7, 7, 2);
      }
    }
    if (i === focus) fb.fill(x - 1, 0, 7, 7, 2);
  });
}

function drawPanel(fb, S, now) {
  drawTabs(fb, S.routes, S.focus, now, PANEL);
  // транспорт и темп справа сверху
  fb.sprite(S.playing ? A.SPR.play : A.SPR.pause, 100, 1, 1);
  text(fb, 108, 0, S.sync === 'EXT' ? 'EXT' : String(S.bpm).padStart(3, ' '));
  // маршрут в фокусе: скорость, конец, канал
  const r = S.routes[S.focus];
  if (!r.cells.length) {
    text(fb, 100, 8, 'NO');
    text(fb, 100, 16, 'RT');
    return;
  }
  text(fb, 100, 8, SPEEDS[r.speed].t);
  text(fb, 100, 16, endsOf(r)[r.end]);
  text(fb, 100, 24, 'CH' + String(r.ch).padStart(2, '0'));
}

function drawLine(fb, S, now) {
  const r = S.routes[S.focus];
  if (!r.cells.length) {
    label(fb, 0, 0, 'RT' + (S.focus + 1));
    text(fb, 0, 12, 'NO ROUTE');
    return;
  }
  // маршрут развёрнут в строку: до 16 клеток × 8 px = 128 px; соседи всегда слева и справа
  const line = r.cells.map((p, k) => ({ c: k, r: 0, src: p }));
  const lorg = q => [8 * q.c, 0];
  const ltile = q => tileAt(S, q.src);
  line.forEach(q => drawTile(fb, ...lorg(q), ltile(q)));
  drawPath(fb, lorg, ltile, line, false);
  drawPointer(fb, 8 * r.i, 0, now < r.flash, true, r.muted);
  drawCursor(fb, 8 * S.lineSel, 0); // выбранный шаг — курсор
  fb.sprite(A.SPR.caret, 8 * r.i + 1, 10, 1);
  const x = label(fb, 0, 16, 'RT' + (S.focus + 1));
  text(fb, x + 2, 16, SPEEDS[r.speed].t + ' ' + endsOf(r)[r.end]);
  text(fb, 100, 16, r.muted ? 'MUTE' : 'CH' + String(r.ch).padStart(2, '0'));
  const t = tileAt(S, r.cells[S.lineSel]);
  const nm = t.n < 0 ? '---' : A.NOTE_NAME[t.n] + t.o;
  text(fb, 0, 24, 'ST' + String(S.lineSel + 1).padStart(2, '0') + ' ' + nm + (t.fx ? ' ' + t.fx.code : ''));
  fb.sprite(S.playing ? A.SPR.play : A.SPR.pause, 100, 25, 1);
  text(fb, 108, 24, S.sync === 'EXT' ? 'EXT' : String(S.bpm).padStart(3, ' '));
}

// Настройки маршрута: скорость, конец, канал, velocity и длина ноты по умолчанию
const SETTINGS = [
  { name: 'SPD', x: 0 },
  { name: 'END', x: 24 },
  { name: 'CH', x: 56 },
  { name: 'VEL', x: 80 },
  { name: 'LEN', x: 104 },
];
function drawSettings(fb, S) {
  const r = S.routes[S.focus];
  const x = label(fb, 0, 0, 'RT' + (S.focus + 1));
  text(fb, x + 2, 0, r.closed ? 'SET LOOP' : 'SET');
  const vals = [SPEEDS[r.speed].t, endsOf(r)[r.end], String(r.ch).padStart(2, '0'), String(r.vel), String(r.gate)];
  SETTINGS.forEach((st, i) => {
    if (i === S.setSel) label(fb, st.x, 12, st.name);
    else text(fb, st.x + 1, 12, st.name);
    text(fb, st.x + 1, 22, vals[i]);
  });
}

function drawDrawMode(fb, S, opts) {
  // лишний интерфейс убран: только карта и счётчик шагов
  const n = S.draw.path.length + (opts.previewCells || []).length;
  text(fb, PANEL, 0, String(Math.min(n, 99)).padStart(2, '0'));
  text(fb, PANEL + 14, 0, '/16', 1, 'klaatu_light');
  if (opts.blocked) text(fb, PANEL, 24, 'NO WAY');
  else if (S.draw.closed || opts.closing) text(fb, PANEL, 24, 'LOOP');
}

// Пока держишь кнопку энкодера: на карте — панель CLR с нотой тайла и шкалой,
// в линии — две нижние строки CLR RTn CHAIN и шкала.
function drawClearHold(fb, S, now) {
  const frac = (now - S.hold.t0) / HOLD_MS;
  if (S.hold.act === 'TILE') {
    fb.fill(PANEL - 1, 0, 128 - PANEL + 1, 32, 0);
    const t = tileAt(S, S.cur);
    label(fb, PANEL, 0, 'CLR');
    text(fb, PANEL + 26, 0, t.n < 0 ? '--' : A.NOTE_NAME[t.n] + t.o);
    holdBar(fb, PANEL + 1, 8, frac);
    text(fb, PANEL, 24, 'TILE', 1, 'klaatu_light');
  } else {
    fb.fill(0, 16, 128, 16, 0);
    const x = label(fb, 0, 16, 'CLR');
    text(fb, x + 2, 16, `RT${S.focus + 1} CHAIN`);
    holdBar(fb, 1, 24, frac);
    text(fb, 30, 24, 'TILES STAY', 1, 'klaatu_light');
  }
}

function render(fb, S, now) {
  fb.clear();
  if (S.hold && now - S.hold.t0 > 200 && (S.mode === 'map' || S.mode === 'line')) {
    if (S.mode === 'line') drawLine(fb, S, now);
    else {
      drawMap(fb, S, now, {});
      drawPanel(fb, S, now);
    }
    return drawClearHold(fb, S, now);
  }
  if (S.mode === 'line') return drawLine(fb, S, now);
  if (S.mode === 'settings') return drawSettings(fb, S);
  if (S.mode === 'tile' && S.tile.layout === 'hood') return drawTileHood(fb, S, tileAt(S, S.cur));
  if (S.mode === 'proj') return drawProj(fb, S, now);
  const opts = {};
  if (S.mode === 'draw' && !S.draw.closed) {
    const last = S.draw.path[S.draw.path.length - 1];
    if (!same(last, S.cur)) {
      const sp = shortestPath(S, S.draw.path, S.cur);
      const add = sp ? (sp.closing ? sp.cells.slice(0, -1) : sp.cells) : [];
      if (!sp || S.draw.path.length + add.length > MAX_LEN) opts.blocked = true;
      else {
        opts.previewCells = add;
        opts.previewTail = sp.cells;
        opts.closing = sp.closing;
      }
    }
  }
  drawMap(fb, S, now, opts);
  if (S.mode === 'draw') drawDrawMode(fb, S, opts);
  else if (S.mode === 'tile') drawTileMapPanel(fb, S, tileAt(S, S.cur));
  else drawPanel(fb, S, now);
}

// ───────────────────────── органы управления ─────────────────────────
// TAP — модификатор; TAP+TAP — старт/пауза; PLAY — маршрут 1→4; TAP+PLAY — настройки;
// энкодер — X, TAP+энкодер — Y; кнопка энкодера — экран тайла; TAP+кнопка — рисовать;
// двойное нажатие кнопки энкодера — закончить маршрут; PAGE — карта ↔ линия,
// на экране тайла — следующая страница; PAGE удерживать + энкодер — темп.
// Экран тайла: энкодер — значение, TAP+энкодер — выбор параметра, кнопка — выход.
const UI = { sound: false, tapLatch: false };
const btn = { tap: false, page: false, pageUsed: false, tapClean: true, tapDownAt: 0, lastTapUp: -1e9, lastSw: -1e9, latchAt: 0 };

const tapHeld = () => btn.tap || UI.tapLatch;
function consumeTap() {
  btn.tapClean = false;
  if (UI.tapLatch) setLatch(false);
}

function moveCursor(S, d, axisY) {
  if (axisY) S.cur.r = (S.cur.r + d + ROWS) % ROWS;
  else S.cur.c = (S.cur.c + d + COLS) % COLS; // заворот в том же ряду
}

// с карты — карта + панель, из маршрута в линию — расширенный вид с соседями
function openTile(S, back) {
  S.tile = { page: 0, sel: 0, back, layout: back === 'line' ? 'hood' : 'map' };
  S.mode = 'tile';
}

function onEnc(S, d) {
  if (btn.page) {
    btn.pageUsed = true;
    if (!S.ext.on) S.bpm = Math.max(40, Math.min(255, S.bpm + d)); // ведомому темп задают снаружи
    return;
  }
  const mod = tapHeld();
  if (mod) consumeTap();
  switch (S.mode) {
    case 'map':
    case 'draw':
      moveCursor(S, d, mod);
      break;
    case 'tile': {
      const ps = tileParams(S, tileAt(S, S.cur), S.tile.page);
      if (mod) S.tile.sel = (S.tile.sel + d + ps.length) % ps.length;
      else if (ps[S.tile.sel]) ps[S.tile.sel].set(d);
      // смена эффекта меняет число параметров
      S.tile.sel = Math.min(S.tile.sel, tileParams(S, tileAt(S, S.cur), S.tile.page).length - 1);
      break;
    }
    case 'line': {
      const r = S.routes[S.focus];
      if (r.cells.length) S.lineSel = (S.lineSel + d + r.cells.length) % r.cells.length;
      break;
    }
    case 'proj':
      // TAP+энкодер — слот, энкодер — темп загруженного проекта (на чужом слоте — ничего)
      if (mod) {
        S.proj.sel = (S.proj.sel + d + SLOTS) % SLOTS;
        S.proj.hold = null;
      } else if (S.proj.sel === S.slot && !S.ext.on) S.bpm = Math.max(40, Math.min(255, S.bpm + d));
      break;
    case 'settings': {
      const r = S.routes[S.focus];
      if (S.setSel === 0) r.speed = Math.max(0, Math.min(SPEEDS.length - 1, r.speed + d));
      if (S.setSel === 1) {
        const n = endsOf(r).length;
        r.end = (r.end + d + n) % n;
        r.i = startIndex(r);
        r.dir = 1;
      }
      if (S.setSel === 2) r.ch = Math.max(1, Math.min(16, r.ch + d));
      if (S.setSel === 3) r.vel = Math.max(1, Math.min(127, r.vel + 8 * d));
      if (S.setSel === 4) r.gate = Math.max(5, Math.min(100, r.gate + 5 * d));
      break;
    }
  }
}

function onSw(S) {
  const now = performance.now();
  const dbl = now - btn.lastSw < 320;
  btn.lastSw = now;
  const mod = tapHeld();
  if (mod) consumeTap();
  switch (S.mode) {
    case 'map':
      if (mod) {
        S.mode = 'draw';
        S.draw = { path: [{ ...S.cur }], closed: false };
      } else S.hold = { act: 'TILE', t0: now }; // короткое — правка (по отпусканию), долгое — стереть
      break;
    case 'tile':
      S.mode = S.tile.back;
      break;
    case 'line':
      if (S.routes[S.focus].cells.length) S.hold = { act: 'CHAIN', t0: now };
      break;
    case 'draw': {
      if (dbl) return finishDraw(S);
      if (S.draw.closed) break;
      const sp = shortestPath(S, S.draw.path, S.cur);
      if (!sp || !sp.cells.length) break;
      const add = sp.closing ? sp.cells.slice(0, -1) : sp.cells;
      if (S.draw.path.length + add.length > MAX_LEN) break;
      S.draw.path.push(...add);
      if (sp.closing) S.draw.closed = true;
      break;
    }
    case 'settings':
      S.setSel = (S.setSel + 1) % SETTINGS.length;
      break;
    case 'proj':
      S.proj.hold = { act: mod ? 'SAVE' : 'LOAD', t0: now };
      break;
  }
}
// Кнопку энкодера отпустили раньше, чем заполнилась шкала: в проектах — отмена,
// на карте и в линии — это было короткое нажатие, открываем правку тайла.
function onSwUp(S) {
  if (S.mode === 'proj') S.proj.hold = null;
  if (!S.hold) return;
  const act = S.hold.act;
  S.hold = null;
  if (act === 'TILE') openTile(S, 'map');
  else {
    S.cur = { ...S.routes[S.focus].cells[S.lineSel] };
    openTile(S, 'line');
  }
}

function finishDraw(S) {
  const r = S.routes[S.focus];
  Object.assign(r, { cells: S.draw.path.map(p => ({ ...p })), closed: S.draw.closed, end: 0, dir: 1, fresh: true, fx: null, wait: 0, jump: null, next: performance.now() });
  r.i = startIndex(r);
  S.draw = null;
  S.mode = 'map';
}

function onPlay(S) {
  const mod = tapHeld();
  if (mod) consumeTap();
  if (S.mode === 'draw') {
    S.draw = null; // PLAY — отмена рисования
    S.mode = 'map';
    return;
  }
  if (S.mode === 'settings') {
    S.mode = S.view;
    return;
  }
  if (S.mode === 'tile') return;
  if (S.mode === 'proj') {
    S.mode = S.proj.back;
    return;
  }
  if (mod) {
    S.view = S.mode;
    S.setSel = 0;
    S.mode = 'settings';
    return;
  }
  S.playHold = { t0: performance.now() }; // короткое — следующий маршрут (по отпусканию), долгое — мьют
}
// PLAY отпущен раньше, чем сработал мьют, — следующий маршрут
function onPlayUp(S) {
  if (!S.playHold) return;
  S.playHold = null;
  S.focus = (S.focus + 1) % 4;
  S.lineSel = 0;
}

function onPageUp(S) {
  if (btn.pageUsed) return;
  if (S.mode === 'proj') S.mode = S.proj.back;
  else if (S.mode === 'tile') {
    S.tile.page = (S.tile.page + 1) % PAGES.length;
    S.tile.sel = 0;
  } else if (S.mode === 'map') {
    S.mode = 'line';
    const r = S.routes[S.focus];
    S.lineSel = Math.min(S.lineSel, Math.max(0, r.cells.length - 1));
  } else if (S.mode === 'line') S.mode = 'map';
}

function tapDown(S) {
  const now = performance.now();
  if (btn.tapClean && now - btn.lastTapUp < 350) {
    togglePlay(S); // TAP+TAP
    btn.lastTapUp = -1e9;
    btn.tapClean = false;
    btn.tap = true;
    return;
  }
  btn.tap = true;
  btn.tapClean = true;
  btn.tapDownAt = now;
}
function tapUp() {
  const now = performance.now();
  btn.tap = false;
  btn.lastTapUp = btn.tapClean && now - btn.tapDownAt < 300 ? now : -1e9;
}

// мышью TAP не удержать вместе с другой кнопкой — клик по TAP «защёлкивает» его
// до следующего действия; два быстрых клика — TAP+TAP
let latchEl = null;
function setLatch(v) {
  UI.tapLatch = v;
  if (latchEl) latchEl.classList.toggle('latched', v);
}

// ───────────────────────── страница ─────────────────────────
let S;
const fb = new FB();
let scr;
let scr1;

function paint(canvas, f, scale, gap) {
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#040608';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#e6eeff';
  for (let y = 0; y < f.h; y++)
    for (let x = 0; x < f.w; x++) if (f.p[y * f.w + x]) ctx.fillRect(x * scale, y * scale, scale - gap, scale - gap);
}

function frame() {
  const now = performance.now();
  render(fb, S, now);
  paint(scr, fb, 6, 1);
  paint(scr1, fb, 1, 0);
  const st = document.getElementById('status');
  if (st)
    st.textContent = `режим: ${S.mode} · маршрут ${S.focus + 1} · курсор ${S.cur.c + 1}:${S.cur.r + 1} · ${S.playing ? 'идёт' : 'пауза'} · TAP ${tapHeld() ? 'зажат' : '—'} · проект ${String(S.slot + 1).padStart(2, '0')}${isDirty(S) ? '+' : ''} ${S.autosave ? 'автосохранение' : 'сохранён вручную'}`;
  requestAnimationFrame(frame);
}

function bindControls() {
  const $ = id => document.getElementById(id);
  latchEl = $('b-tap');
  const press = (el, down) =>
    el.addEventListener('pointerdown', e => {
      e.preventDefault();
      down();
    });
  press($('b-tap'), () => {
    const now = performance.now();
    if (UI.tapLatch && now - btn.latchAt < 400) {
      setLatch(false);
      togglePlay(S);
      return;
    }
    btn.latchAt = now;
    setLatch(!UI.tapLatch);
  });
  press($('b-play'), () => onPlay(S));
  $('b-play').addEventListener('pointerup', () => onPlayUp(S));
  $('b-play').addEventListener('pointerleave', () => (S.playHold = null));
  press($('b-page'), () => {
    btn.page = false;
    btn.pageUsed = false;
    if (UI.tapLatch && (S.mode === 'map' || S.mode === 'line')) {
      setLatch(false);
      return openProj(S); // TAP+PAGE — проекты
    }
    onPageUp(S);
  });
  press($('b-left'), () => onEnc(S, -1));
  press($('b-right'), () => onEnc(S, +1));
  press($('b-sw'), () => onSw(S));
  $('b-sw').addEventListener('pointerup', () => onSwUp(S));
  // увели мышь с кнопки во время удержания — отмена, а не короткое нажатие
  $('b-sw').addEventListener('pointerleave', () => {
    S.hold = null;
    if (S.proj) S.proj.hold = null;
  });
  $('screen').addEventListener(
    'wheel',
    e => {
      e.preventDefault();
      onEnc(S, e.deltaY > 0 ? 1 : -1);
    },
    { passive: false },
  );

  window.addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
    const k = e.key.toLowerCase();
    if (k === 't' || k === 'е') {
      if (!e.repeat) tapDown(S);
    } else if (k === 'p' || k === 'з') {
      if (!e.repeat) onPlay(S);
    } else if (k === 'g' || k === 'п') {
      if (!e.repeat) {
        btn.page = true;
        btn.pageUsed = false;
        if (tapHeld() && (S.mode === 'map' || S.mode === 'line')) {
          consumeTap();
          btn.pageUsed = true;
          openProj(S); // TAP+PAGE — проекты
        }
      }
    } else if (e.key === 'ArrowLeft') onEnc(S, -1);
    else if (e.key === 'ArrowRight') onEnc(S, +1);
    else if (e.key === 'Enter' || e.key === ' ') {
      if (!e.repeat) onSw(S);
    } else return;
    e.preventDefault();
  });
  window.addEventListener('keyup', e => {
    const k = e.key.toLowerCase();
    if (k === 't' || k === 'е') tapUp();
    if (e.key === 'Enter' || e.key === ' ') onSwUp(S);
    if (k === 'p' || k === 'з') onPlayUp(S);
    if (k === 'g' || k === 'п') {
      btn.page = false;
      onPageUp(S);
    }
  });

  $('sound').addEventListener('change', e => (UI.sound = e.target.checked));
  // имитация клока на MIDI IN: есть клок — ведомый; Start, Stop, Continue — как с ведущего
  $('sync').addEventListener('change', e => {
    S.ext.on = e.target.checked;
    S.ext.t0 = performance.now();
  });
  $('extbpm').addEventListener('change', e => (S.ext.bpm = Math.max(40, Math.min(255, +e.target.value || 120))));
  ['START', 'STOP', 'CONT'].forEach(m => $('ext-' + m.toLowerCase()).addEventListener('click', () => S.ext.on && extTransport(S, m)));
  $('reset').addEventListener('click', () => {
    try {
      localStorage.removeItem('maze.v1');
    } catch (e) {}
    location.reload();
  });
}

// ───────────────────────── лист ассетов ─────────────────────────
function sheetCanvas(parent, title, w, h, drawFn, scale = 8) {
  const f = new FB(w, h);
  drawFn(f);
  const box = document.createElement('figure');
  const c = document.createElement('canvas');
  c.width = w * scale;
  c.height = h * scale;
  paint(c, f, scale, 1);
  const cap = document.createElement('figcaption');
  cap.textContent = title;
  box.append(c, cap);
  parent.append(box);
}

// мини-карта для листа: клетки строками, маршрут списком
function miniMap(f, rows, path, closed = false) {
  const grid = rows.map(row => row.map(parseNote));
  grid.forEach((row, r) => row.forEach((t, c) => drawTile(f, ...org({ c, r }), t)));
  drawPath(f, org, q => grid[q.r][q.c], cells(path), closed);
}

function drawSheet() {
  const root = document.getElementById('sheet');
  if (!root) return;
  root.innerHTML = '';
  const strip = list => f => list.forEach((s, i) => drawTile(f, 1 + 8 * i, 0, parseNote(s)));
  sheetCanvas(root, 'Ноты, Klaatu с точками — цельные', 57, 8, strip(['C5', 'D5', 'E5', 'F5', 'G5', 'A5', 'B5']));
  sheetCanvas(root, 'Диезы — строчные Klaatu без точек', 41, 8, strip(['C#5', 'D#5', 'F#5', 'G#5', 'A#5']));
  sheetCanvas(root, 'Хроматика C…B', 97, 8, strip(['C5', 'C#5', 'D5', 'D#5', 'E5', 'F5', 'F#5', 'G5', 'G#5', 'A5', 'A#5', 'B5']));
  sheetCanvas(root, 'Октавы 2…8 (C): над буквой — выше, под — ниже', 57, 8, strip(['C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'C8']));
  sheetCanvas(root, 'Маршрут: точка в зазоре между клетками; в пустой клетке — точка и по пикселю туда, откуда и куда идёт путь', 41, 16, f =>
    miniMap(f, [['C5', '-', '-', 'E5', 'G5'], ['-', '-', 'A4', '-', '-']], [[0, 0], [1, 0], [1, 1], [2, 1], [2, 0], [3, 0], [4, 0], [4, 1]]),
  );
  sheetCanvas(root, 'Замкнутый маршрут', 25, 16, f => miniMap(f, [['C5', '-', 'E5'], ['-', 'G4', '-']], [[0, 0], [1, 0], [2, 0], [2, 1], [1, 1], [0, 1]], true));
  sheetCanvas(root, 'Указатель — инверсия клетки; курсор (внизу) — уголки по 3 px', 41, 16, f => {
    miniMap(f, [['C5', 'E5', '-', 'G5', '-'], ['-', '-', '-', 'A4', '-']], [[0, 0], [1, 0], [2, 0], [3, 0], [3, 1]]);
    drawPointer(f, ...org({ c: 1, r: 0 }), false);
    drawPointer(f, ...org({ c: 3, r: 0 }), false);
    drawCursor(f, ...org({ c: 1, r: 1 }));
  });
  sheetCanvas(root, 'Удар ноты: вокруг указателя замыкается рамка', 25, 16, f => {
    miniMap(f, [['C5', 'E5', 'G5']], [[0, 0], [1, 0], [2, 0]]);
    drawPointer(f, ...org({ c: 1, r: 0 }), true);
  });
  // маршруты для образца панели: прогресс, последний эффект, свежий — у третьего
  const demoRoutes = [
    { cells: [1, 2, 3, 4, 5, 6], i: 2, fx: 'R', fxFlash: 0 },
    { cells: [1, 2, 3, 4, 5, 6], i: 5, fx: 'D', fxFlash: 0 },
    { cells: [1, 2, 3, 4, 5, 6], i: 3, fx: 'X', fxFlash: 1 },
    { cells: [], i: 0, fx: null, fxFlash: 0 },
  ];
  sheetCanvas(root, 'Вкладки: выбранная — инверсией; шкала прогресса и последний эффект внизу, свежий — инверсией', 33, 32, f => drawTabs(f, demoRoutes, 0, 0, 1));
  sheetCanvas(root, 'Условие по счёту 3:4: квадрат — сработает, точка — пропуск, инверсия — последний проход', 29, 8, f =>
    drawTrig(f, 1, 0, { trig: '3:4', count: 2 }),
  );
  sheetCanvas(root, 'Транспорт: идёт · пауза', 13, 7, f => {
    f.sprite(A.SPR.play, 0, 1);
    f.sprite(A.SPR.pause, 8, 1);
  });

  // коды эффектов и условия
  const fxRoot = document.getElementById('fxcodes');
  if (!fxRoot) return;
  fxRoot.innerHTML = '';
  const row = (glyphText, name, note) => {
    const f = new FB(Math.max(7, textW(glyphText) + 1), 7);
    text(f, 1, 0, glyphText);
    const c = document.createElement('canvas');
    c.width = f.w * 5;
    c.height = 7 * 5;
    paint(c, f, 5, 1);
    const el = document.createElement('div');
    el.className = 'fx';
    const nm = document.createElement('span');
    nm.textContent = name;
    const gr = document.createElement('em');
    gr.textContent = note;
    el.append(c, nm, gr);
    fxRoot.append(el);
  };
  Object.entries(A.FX).forEach(([code, [group, name]]) => {
    const ps = FXDEF[code].map(([n]) => n).join(' ');
    row(code, `${code} — ${name} · ${A.FX[code][2]}`, ps ? `${group} · ${ps}` : group);
  });
  Object.entries(A.COND).forEach(([cnd, desc]) => row(cnd, `${desc} · ${A.COND_HELP[cnd]}`, 'условие'));
}

window.addEventListener('DOMContentLoaded', () => {
  scr = document.getElementById('screen');
  scr1 = document.getElementById('screen1');
  bindControls();
  S = makeState();
  restore(S);
  window.MAZE = {
    get S() {
      return S;
    },
    render,
    fb,
    UI,
    tick,
    applyPending,
    autosaveStep,
  };
  drawSheet();
  // такт секвенсора — отдельным таймером, а не кадрами отрисовки
  setInterval(() => tick(S, performance.now()), 4);
  setInterval(() => autosaveStep(S, performance.now()), 250);
  requestAnimationFrame(frame);
});
