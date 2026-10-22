// Проверки движка эффектов: jsc tools/screens/stub.js assets/font/kaleidoscope_font.js
//   assets/maze_assets.js assets/maze_demos.js proto/maze.js tests/fx.js
let fails = 0;
function ok(cond, msg) {
  print((cond ? 'PASS ' : 'FAIL ') + msg);
  if (!cond) fails++;
}
const note = st => (st.n < 0 ? '-' : A.NOTE_NAME[st.n] + st.o);
const grid = st => st.grid.map(row => row.map(note).join(' ')).join(' / ');
function blank() {
  const st = makeState();
  st.routes.forEach(r => (r.cells = []));
  st.grid.forEach(row => row.forEach(t => (t.fx = null)));
  return st;
}
// прогнать маршрут ri на k шагов (без звука): шаги идут по времени
function steps(st, k) {
  st.playing = true;
  let now = 0;
  st.routes.forEach(r => {
    r.next = 0;
    r.fresh = true;
  });
  const dur = stepDur(st, st.routes.find(r => r.cells.length)) * 1000;
  for (let i = 0; i < k; i++) {
    tick(st, now);
    now += dur;
  }
  return now;
}

// поворот: O на (6,1), CW 90 — соседи сдвигаются на 2 позиции по кольцу
let st = blank();
const t0 = st.grid[0][5]; // левый верхний сосед (5,0) — A4
st.pending.push({ code: 'O', p: { c: 6, r: 1 }, a: 'CW', b: '90' });
applyPending(st);
ok(st.grid[0][7] === t0, 'O CW 90: левый верхний сосед уехал в правый верхний');
ok(st.grid[1][6].n === A.NOTE_NAME.indexOf('B') && st.grid[1][6].o === 4, 'O: центр на месте');

// поворот у края — тор: соседи (0,0) берутся с противоположных краёв
st = blank();
const corner = st.grid[3][7]; // (7,3) — сосед (0,0) слева сверху через тор
st.pending.push({ code: 'O', p: { c: 0, r: 0 }, a: 'CW', b: '45' });
applyPending(st);
ok(st.grid[3][0] === corner, 'O на краю: сосед через тор сдвинулся на одну позицию');

// сдвиг ряда вправо на 1 с заворотом
st = blank();
const row0 = [...st.grid[0]];
st.pending.push({ code: 'Z', p: { c: 2, r: 0 }, a: 'R', b: '1' });
applyPending(st);
ok(st.grid[0][0] === row0[7] && st.grid[0][3] === row0[2], 'Z R 1: ряд сдвинут вправо с заворотом');

// сдвиг столбца вниз на 2
st = blank();
const col3 = st.grid.map(row => row[3]);
st.pending.push({ code: 'V', p: { c: 3, r: 1 }, a: 'DN', b: '2' });
applyPending(st);
ok(st.grid[2][3] === col3[0] && st.grid[0][3] === col3[2], 'V DN 2: столбец сдвинут вниз с заворотом');

// CHNG: сосед изменён эффектом — тайл взведён, срабатывает при приходе указателя один раз
st = blank();
st.grid[1][4].fx = newFx('X', 'CHNG'); // (4,1) рядом с кольцом (6,1)? нет — рядом с (5,*)
st.pending.push({ code: 'O', p: { c: 6, r: 1 }, a: 'CW', b: '90' });
applyPending(st);
ok(st.grid[1][4].fx.armed, 'CHNG: соседа повернули — тайл взведён');
st.routes[0].cells = cells([[4, 1], [4, 2]]);
st.routes[0].i = 0;
ok(condMet(st, 0, { c: 4, r: 1 }, st.grid[1][4]) && !st.grid[1][4].fx.armed, 'CHNG: сработал при приходе и разряжен');
ok(!condMet(st, 0, { c: 4, r: 1 }, st.grid[1][4]), 'CHNG: второй раз без изменений не срабатывает');

// ручная правка не взводит (взвод только в applyPending)
st = blank();
st.grid[1][4].fx = newFx('X', 'CHNG');
st.grid[1][5].n = 3;
ok(!st.grid[1][4].fx.armed, 'CHNG: ручная правка соседа не взводит');

// NEAR: 8 соседей, включая диагональ
st = blank();
st.grid[1][1].fx = newFx('X', 'NEAR');
st.routes[0].cells = cells([[1, 1]]);
st.routes[1].cells = cells([[2, 2]]);
ok(condMet(st, 0, { c: 1, r: 1 }, st.grid[1][1]), 'NEAR: указатель по диагонали считается соседом');
st.routes[1].cells = cells([[3, 3]]);
ok(!condMet(st, 0, { c: 1, r: 1 }, st.grid[1][1]), 'NEAR: дальше одной клетки — нет');

// MEET
st.routes[1].cells = cells([[1, 1]]);
st.grid[1][1].fx = newFx('X', 'MEET');
ok(condMet(st, 0, { c: 1, r: 1 }, st.grid[1][1]), 'MEET: второй указатель на том же тайле');

// D: разворот на месте; LOOP ↔ REV, PONG — направление
st = blank();
let r = st.routes[0];
Object.assign(r, { cells: cells([[0, 0], [1, 0], [2, 0], [3, 0]]), closed: false, end: 0, i: 2 });
flipDir(r);
ok(ENDS_OPEN[r.end] === 'REV' && r.i === 2 && nextIndex(r).i === 1, 'D: LOOP → REV, указатель остался на месте и идёт назад');
r.end = 1;
r.dir = 1;
flipDir(r);
ok(r.dir === -1, 'D: у PONG разворот сразу');
r.closed = true;
r.end = 0;
flipDir(r);
ok(ENDS_CLOSED[r.end] === 'REV', 'D: замкнутый FWD → REV');

// W: стоит N шагов молча; T: стоит и нота тянется
st = blank();
r = st.routes[0];
Object.assign(r, { cells: cells([[0, 0], [1, 0], [2, 0], [3, 0]]), closed: false, end: 0, i: 0 });
st.grid[0][1].fx = newFx('W', 'PASS'); // стоп 2 шага
steps(st, 5); // такты: 0, 1 (W), стоит, стоит, 2
ok(r.i === 2, 'W 2: за 5 тактов указатель прошёл 0 → 1, постоял два шага и встал на 2');

// H — прыжок: HOME
st = blank();
r = st.routes[0];
Object.assign(r, { cells: cells([[0, 0], [1, 0], [2, 0], [3, 0]]), closed: false, end: 0, i: 0 });
st.grid[0][2].fx = newFx('H', 'PASS');
st.grid[0][2].fx.p = [2, 0]; // HOME
steps(st, 4); // 0, 1, 2 (J) → 0
ok(r.i === 0, 'H HOME: после клетки с прыжком указатель в начале маршрута');

// H — прыжок: FWD 2
st.grid[0][2].fx.p = [0, 1];
r.i = 0;
r.jump = null;
steps(st, 4); // 0,1,2(J) → +2 → 0 (по кругу: 2→3→0)
ok(r.i === 0, 'H FWD 2: перепрыгнул на два шага вперёд по кругу');

// S: скорость на ступень
st = blank();
r = st.routes[0];
Object.assign(r, { cells: cells([[0, 0], [1, 0]]), speed: 1 });
st.grid[0][0].fx = newFx('S', 'PASS');
steps(st, 1);
ok(r.speed === 0, 'S FAST: 1/1 → 2/1');

// ретчет и арпеджио: число нот и смещения
st = blank();
r = st.routes[0];
r.cells = cells([[0, 0]]);
const notes = [{ midi: 72, vel: 100, at: 0, dur: 0.06 }];
const fxR = newFx('R');
fxR.p = [2, 1]; // 4 удара, нарастание
applyFx(st, r, { c: 0, r: 0 }, fxR, notes, 0.12);
ok(notes.length === 4 && notes[3].at === 0.09 && notes[0].vel === 25 && notes[3].vel === 100, 'R 4 UP: четыре удара, громкость нарастает');
const arp = [{ midi: 72, vel: 100, at: 0, dur: 0.06 }];
applyFx(st, r, { c: 0, r: 0 }, newFx('A'), arp, 0.12);
ok(arp.map(n => n.midi).join() === '72,76,79', 'A MAJ 3 UP: C E G');
const iv = [{ midi: 72, vel: 100, at: 0, dur: 0.06 }];
applyFx(st, r, { c: 0, r: 0 }, newFx('I'), iv, 0.12);
ok(iv[0].midi === 79, 'I +7: квинта вверх');

print(fails ? `\n${fails} FAIL` : '\nвсе проверки прошли');

// ── условие по счёту и проекты
fails = 0;
st = blank();
r = st.routes[0];
Object.assign(r, { cells: cells([[0, 0]]), closed: false, end: 0, i: 0 });
st.grid[0][0].fx = Object.assign(newFx('X', 'PASS'), { trig: '2:3' });
const fired = [];
for (let k = 0; k < 6; k++) {
  r.fx = null;
  trigger(st, 0, 0);
  fired.push(r.fx ? 1 : 0);
}
ok(fired.join('') === '010010', 'TRG 2:3: срабатывает на втором из каждых трёх проходов');

// PRE: акцент только после сработавшего ретчета 1:2
st = blank();
r = st.routes[0];
Object.assign(r, { cells: cells([[0, 0], [1, 0]]), closed: false, end: 0, i: 0 });
st.grid[0][0].fx = Object.assign(newFx('R', 'PASS'), { trig: '1:2' });
st.grid[0][1].fx = Object.assign(newFx('X', 'PASS'), { trig: 'PRE' });
const pre = [];
for (let k = 0; k < 4; k++) {
  r.i = 0;
  trigger(st, 0, 0);
  r.i = 1;
  r.fx = null;
  trigger(st, 0, 0);
  pre.push(r.fx ? 1 : 0);
}
ok(pre.join('') === '1010', 'PRE: акцент срабатывает только за сработавшим ретчетом');
st.grid[0][1].fx.trig = '/PRE';
const npre = [];
for (let k = 0; k < 4; k++) {
  r.i = 0;
  trigger(st, 0, 0);
  r.i = 1;
  r.fx = null;
  trigger(st, 0, 0);
  npre.push(r.fx ? 1 : 0);
}
ok(npre.join('') === '0101', '/PRE: наоборот — когда ретчет промолчал');

// CHNG со счётом: на непопадании тайл остаётся взведённым
st = blank();
r = st.routes[0];
Object.assign(r, { cells: cells([[0, 0]]), closed: false, end: 0, i: 0 });
st.grid[0][0].fx = Object.assign(newFx('X', 'CHNG'), { trig: '2:2', armed: true });
r.fx = null;
trigger(st, 0, 0);
ok(!r.fx && st.grid[0][0].fx.armed, 'TRG + CHNG: на непопадании тайл остаётся взведённым');
trigger(st, 0, 0);
ok(r.fx === 'X' && !st.grid[0][0].fx.armed, 'TRG + CHNG: на втором проходе сработал');

// счёт с начала при старте
st = makeState();
st.grid[0][2].fx.count = 5;
restartGrid(st, 0);
ok(st.grid[0][2].fx.count === 0 && st.routes.every(x => x.prevFx === null), 'старт: счёт и PRE с начала');
ok(TRIGS.length === 1 + 35 + 2 && TRIGS[1] === '1:2' && TRIGS[35] === '8:8' && TRIGS[36] === 'PRE', 'список TRG: --, 35 значений A:B, PRE, /PRE');

// исходник: эффекты двигают живую карту, сохраняется исходник
st = makeState();
const before = serialize(st);
st.pending.push({ code: 'O', p: { c: 6, r: 1 }, a: 'CW', b: '90' });
applyPending(st);
ok(serialize(st) === before, 'поворот не меняет исходник — сохранять нечего');
st.grid[0][7].n = 2; // правим тайл, который поворот перенёс: он же и в исходнике
ok(serialize(st) !== before, 'правка тайла попадает в исходник');

// автосохранение: только черновик, через 2 с после правки
st = makeState();
ok(!st.autosave, 'демо-слот сохранён вручную — автосохранение выключено');
st.grid[0][0].n = 3;
ok(!autosaveStep(st, 0) && !autosaveStep(st, 3000), 'ручной слот: правка не пишется сама');
loadNow(st, { k: 5, data: null, auto: true }, 0);
ok(st.autosave && st.slot === 5 && st.slots[5] === null, 'пустой слот: новый проект, автосохранение включено');
st.grid[0][0].n = 4;
ok(!autosaveStep(st, 1000), 'черновик: через секунду ещё не пишет');
ok(autosaveStep(st, 3500) && st.slots[5].auto, 'черновик: через 2 с записан как автосохранение');
saveSlot(st, 5);
st.grid[0][1].n = 4;
autosaveStep(st, 10000);
ok(!autosaveStep(st, 20000) && !st.slots[5].auto && isDirty(st), 'после SAVE: автосохранение выключено, правка видна как «+»');
loadNow(st, { k: 5, data: st.slots[5].data, auto: st.slots[5].auto }, 0);
ok(!isDirty(st) && st.grid[0][1].n === -1, 'LOAD возвращает исходник из слота');

print(fails ? `${fails} FAIL` : 'счёт и проекты: все проверки прошли');

// ── стирание, мьют, ведомый режим
fails = 0;
st = makeState();
st.cur = { c: 2, r: 0 }; // E5 с ретчетом
st.mode = 'map';
onSw(st);
ok(st.hold && st.hold.act === 'TILE' && st.mode === 'map', 'кнопка энкодера на карте: ждём — коротко или долго');
onSwUp(st);
ok(st.mode === 'tile', 'коротко: открылась правка тайла');
st.mode = 'map';
onSw(st);
st.hold.t0 -= HOLD_MS;
tick(st, performance.now());
const e5 = st.grid[0][2];
ok(e5.n === -1 && !e5.fx && e5.vel === null, 'удержание: тайл стёрт — без ноты и эффекта');
onSwUp(st);
ok(st.mode === 'map', 'после стирания отпускание ничего не открывает');

st.mode = 'line';
st.focus = 2;
onSw(st);
st.hold.t0 -= HOLD_MS;
tick(st, performance.now());
ok(st.routes[2].cells.length === 0 && st.grid[2][0].n >= 0, 'удержание в линии: цепь маршрута 3 стёрта, тайлы на месте');

// мьют: удержание PLAY
st = makeState();
st.mode = 'map';
onPlay(st);
st.playHold.t0 -= MUTE_MS;
tick(st, performance.now());
onPlayUp(st);
ok(st.routes[0].muted && st.focus === 0, 'удержание PLAY: маршрут 1 заглушён, фокус не сменился');
onPlay(st);
onPlayUp(st);
ok(st.focus === 1, 'короткое PLAY: следующий маршрут');
r = st.routes[0];
r.i = 2; // E5 — ретчет (1:2) на первом проходе
r.flash = 0;
trigger(st, 0, 1000);
ok(r.flash === 0 && r.fx === 'R', 'заглушённый маршрут молчит, а эффект срабатывает');

// ведомый: пауза снимается с ближайшего шага внешнего клока
st = makeState();
st.ext = { on: true, bpm: 120, t0: 0 }; // шестнадцатая = 125 мс
togglePlay(st, 1010);
ok(st.playing && st.routes[0].next === 1125, 'ведомый: старт квантован к следующему шагу (1010 → 1125 мс)');
extTransport(st, 'STOP', 1500);
ok(!st.playing, 'Stop с MIDI IN — пауза');
extTransport(st, 'CONT', 1600);
ok(st.playing && st.routes[0].next === 1625, 'Continue — с ближайшего шага');
st.ext.on = false;
togglePlay(st, 2000);
togglePlay(st, 2010);
ok(st.routes[0].next === 2010, 'ведущий: старт сразу');

print(fails ? `${fails} FAIL` : 'стирание, мьют и синхронизация: все проверки прошли');

// ── лад
fails = 0;
const apm = { scale: 10, root: 9 }; // A PMIN
ok(quant(apm, 77) === 76 && quant(apm, 71) === 72 && quant(apm, 69) === 69, 'лад: F→E, B→C, A на месте');
ok(quant({ scale: 0, root: 0 }, 66) === 65, 'лад: F# в C MAJ — вниз, к F');
const tF = parseNote('F#5');
ok(shown({ scale: 0, root: 0 }, tF).n === 5 && shown({ scale: CHRM, root: 0 }, tF).n === 6 && tF.n === 6, 'лад: показ F#→F, в CHRM снова F#, записанная нота не тронута');
st = makeState();
Object.assign(st, { scale: 0, root: 0 });
let ns = [{ midi: 64, vel: 100, at: 0, dur: 0.1 }];
applyFx(st, st.routes[0], { c: 0, r: 0 }, Object.assign(newFx('I'), { p: [26] }), ns, 0.1);
ok(ns[0].midi === 67, 'лад: I +2 — две ступени (E → G)');
ns = [{ midi: 71, vel: 100, at: 0, dur: 0.1 }];
applyFx(st, st.routes[0], { c: 0, r: 0 }, newFx('A'), ns, 0.1);
ok(ns.map(n => n.midi).join() === '71,74,77', 'лад: A MAJ на B в C MAJ — B D F');
Object.assign(st, { scale: 10, root: 9 });
ns = [{ midi: 69, vel: 100, at: 0, dur: 0.1 }];
applyFx(st, st.routes[0], { c: 0, r: 0 }, Object.assign(newFx('A'), { p: [3, 1, 0] }), ns, 0.1);
ok(ns.map(n => n.midi).join() === '69,72,76', 'лад: A MIN в A PMIN — A C E');
// ноты в тайле — только из лада
const tk = parseNote('C5');
setKey(st, tk, 1);
ok(tk.n === 2, 'лад: KEY в A PMIN после C — D (минуя C#)');
setKey(st, tk, -2);
ok(tk.n === -1, 'лад: KEY назад до пусто');
applyProject(st, serialize(st));
ok(st.scale === 10 && st.root === 9, 'лад: сохраняется в проекте');
applyProject(st, JSON.stringify(Object.assign(JSON.parse(serialize(st)), { scale: undefined, root: undefined })));
ok(st.scale === CHRM && st.root === 0, 'лад: проект без лада — CHRM');
print(fails ? `${fails} FAIL` : 'лад: все проверки прошли');

// ── транспонирование: K с начала такта, M сразу; демо 05 — прогрессия по 4 такта
fails = 0;
st = makeState();
applyProject(st, demoProject(4));
st.playing = true;
restartGrid(st, 0);
const bar = barMs(st);
let seqK = '';
for (let now = 0, b = 0; b < 20; now += 5) {
  tick(st, now);
  if (now >= b * bar + 5) {
    if (b % 4 === 0) seqK += A.NOTE_NAME[soundingRoot(st)][0];
    b++;
  }
}
ok(seqK === 'EABEE', 'демо 05: прогрессия E → A → B → E по 4 такта (' + seqK + ')');
st = makeState();
Object.assign(st, { scale: 1, root: 4 });
st.routes[0].key = false;
ns = [{ midi: 64, vel: 100, at: 0, dur: 0.1 }];
applyFx(st, st.routes[0], { c: 0, r: 0 }, Object.assign(newFx('M'), { p: [10] }), ns, 0.1);
ok(st.degShift === 3, 'M +3 — сразу');
applyFx(st, st.routes[0], { c: 0, r: 0 }, Object.assign(newFx('K'), { p: [11] }), ns, 0.1);
ok(st.keyPending === -5 && st.keyShift === 0, 'K B — ждёт такта, в ближайшую сторону');
applyProject(st, serialize(st));
ok(st.routes[0].key === false && st.routes[1].key === true && st.keyShift === 0, 'KEY OFF сохраняется, транспонирование при загрузке сброшено');
print(fails ? `${fails} FAIL` : 'транспонирование: все проверки прошли');

// ── MIDI IN: ноты транспонируют фразу
fails = 0;
st = makeState();
st.root = 2; // опора — D4 = 62
extNote(st, 64, true, 0);
ok(st.keyShift === 0, 'MIDI IN: вход OFF — мимо');
Object.assign(st, { inCh: 1, degShift: 3, keyPending: 2 });
extNote(st, 64, true, 1000);
ok(st.keyShift === 2 && st.degShift === 0 && st.keyPending === null && st.keyFlash === 1000 + KEY_FLASH_MS, 'MIDI IN: E4 — +2, сброшены M и ждущий K');
extNote(st, 59, true, 1000);
ok(st.keyShift === -3, 'MIDI IN: нижняя из зажатых');
extNote(st, 59, false, 1000);
extNote(st, 64, false, 1000);
ok(st.keyShift === -3, 'MIDI IN: защёлка');
extNote(st, 74, true, 1000);
ok(st.keyShift === 12, 'MIDI IN: с октавами');
st.inCh = 2;
extNote(st, 50, true, 1000, 3);
ok(st.keyShift === 12, 'MIDI IN: чужой канал — мимо');
print(fails ? `${fails} FAIL` : 'MIDI IN: все проверки прошли');

// ── экран проектов: энкодер правит тонику, лад и вход нот (ловит перекрытие имён в onEnc)
fails = 0;
st = makeState();
openProj(st);
st.proj.field = 0;
onEnc(st, 2);
st.proj.field = 1;
onEnc(st, -1);
st.proj.field = 2;
onEnc(st, 3);
ok(st.root === 2 && st.scale === CHRM - 1 && st.inCh === 3, 'проекты: энкодер — тоника, лад, IN');
print(fails ? `${fails} FAIL` : 'поля проектов: все проверки прошли');

// ── пауза посреди круга: счёт TRG переживает паузу, прогрессия демо 05 не ломается
fails = 0;
st = makeState();
applyProject(st, demoProject(4));
togglePlay(st, 0);
const barP = barMs(st);
let tp = 0;
for (; tp < 2.6 * barP; tp += 10) tick(st, tp);
togglePlay(st, tp);
tp += 1000;
togglePlay(st, tp);
let chP = '';
for (const tEnd = tp + 16 * barP; tp < tEnd; tp += 10) {
  tick(st, tp);
  const c = A.NOTE_NAME[soundingRoot(st)][0];
  if (chP[chP.length - 1] !== c) chP += c;
}
ok(chP === 'EABE', 'пауза посреди круга: E → A → B → E (' + chP + ')');
print(fails ? `${fails} FAIL` : 'пауза: все проверки прошли');
