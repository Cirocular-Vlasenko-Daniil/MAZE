# MAZE

**Esoteric tile sequencer for [ESPidi](https://github.com/EugeneCarlo/ESPidi)** (ESP32-C3, OLED 128×32, encoder, MIDI in/out).
Installs as a firmware slot of [ESPidi HUB](https://espidi-hub.github.io/).

[Русская версия](README.ru.md)

![MAZE map](screens/map_playing.png)

Draw up to four routes across a 4×8 map of tiles. A pointer walks each route and plays the note in every tile it steps on;
tiles also carry effects that ratchet, transpose, rotate the map or shift its rows, so the pattern keeps rewriting itself.
Inspired by KALEIDOSCOPE (Vadjuse) and ORCA; the Klaatu pixel font comes from the KALEIDOSCOPE firmware.

## Features

- **Map 4×8, 4 routes, 4 pointers.** A route is up to 16 cells, drawn with the encoder; the path between clicks is the shortest one.
  Open routes play LOOP, PONG or REV, closed ones FWD or REV.
- **Per route:** speed 2/1 · 1/1 · 1/2 · 1/4 · 1/8 (1/1 = sixteenths), MIDI channel, default velocity and note length, mute.
- **Tile:** note C…B with sharps (or empty), octave 2–8, velocity, length, one effect and its condition.
- **14 effects.**
  - MIDI: **A** arpeggio, **R** ratchet, **X** accent, **P** aftertouch, **I** interval, **L** legato.
  - Map (the tiles move): **O** rotate the 8 neighbours, **Z** shift the row, **V** shift the column.
  - Pointer: **D** turn around, **W** wait silent, **T** wait holding, **S** speed step, **H** jump.
- **Conditions:** IF — PASS (pointer arrives), MEET (two pointers on the tile), NEAR (a pointer next door), CHNG (a neighbour was moved);
  TRG — Elektron-style A:B (1:2…8:8), PRE, /PRE.
- **32 project slots** on flash. The source map is saved, not the result of the effects, so LOAD brings the map back.
  A draft autosaves; LOAD while playing waits for the next bar.
- **5 demo projects** in slots 01–05, shipped with the firmware: MIX, MIDI (note effects), MAP (the map rewrites itself),
  COND (conditions and counters), POINTER (pointer effects). They go only into empty slots, your projects stay.
- **Sync:** internal clock with MIDI clock out, or follows MIDI clock on MIDI IN automatically (EXT); start is quantised to the next step.

![Tile editor](screens/tile_map_fx.png)
![Tile editor with neighbours](screens/tile_hood_fx.png)

## Controls

TAP is a modifier. "Hold" means hold until the bar on screen fills.

| Control | Map | Elsewhere |
|---|---|---|
| Encoder | cursor X | value / step / slot |
| TAP + encoder | cursor Y | choose parameter |
| Encoder button | edit tile | — |
| Hold encoder button | clear tile | in line view: clear the route's chain |
| TAP + encoder button | draw the route from the cursor: click adds a point, double click finishes, PLAY cancels | — |
| TAP + TAP | play / pause | |
| PLAY | next route (1→4) | |
| Hold PLAY | mute the route | |
| TAP + PLAY | route settings: speed, end, channel, velocity, length | |
| PAGE | map ↔ route as a line (tile edit from here shows the 3×3 neighbours and a hint line) | next page NOTE → FX → COND |
| PAGE + encoder | tempo | |
| TAP + PAGE | projects | |

**Projects screen:** encoder — tempo of the loaded project, TAP + encoder — slot,
hold encoder button — LOAD, hold TAP + encoder button — SAVE, PAGE or PLAY — back.

## Install

Open [espidi-hub.github.io](https://espidi-hub.github.io/), choose a slot, GitHub tab, paste `Cirocular-Vlasenko-Daniil/MAZE`.
Or with the installer from the [ESPidi-HUB](https://github.com/Cirocular-Vlasenko-Daniil/ESPidi-HUB) repository:

```bash
python3 tools/espidi_hub.py install -p /dev/cu.usbmodem1101 --slot 2 Cirocular-Vlasenko-Daniil/MAZE
```

## Build

Build with **`FlashMode=dio`**: the HUB bootloader runs the flash in DIO, and a QIO build can't read its own flash (projects would not save).

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app,FlashMode=dio \
  --library path/to/ESPidi-HUB/sdk/ESPidiHub --output-dir build MAZE
```

The workflow in `.github/workflows/firmware.yml` builds every push and publishes `espidi/MAZE.ino.bin`
when the version in `ESPIDI_FIRMWARE()` changes.

## Repository

| Path | |
|---|---|
| `MAZE/` | firmware: `maze_core` engine (no Arduino), `maze_ui` screen and gestures, `maze_hw.cpp` display, MIDI, buttons, flash |
| `proto/` | browser prototype, the reference for behaviour; `proto/dist/maze_proto.html` is a single file that runs anywhere |
| `tests/` | `fx.js` (prototype, JavaScriptCore), `host/` (firmware on the computer: screens pixel-by-pixel against the prototype, engine and gestures) |
| `assets/`, `tools/` | Klaatu font extracted from KALEIDOSCOPE, sprites, cover, generators |
| `docs/DESIGN.md` | all design decisions and the backlog (in Russian) |

## License

Code — MIT, see [LICENSE](LICENSE). The Klaatu font (`assets/font/`, glyph tables in `MAZE/maze_assets.h`) is by Merih Yanbasti,
used with permission and not covered by the MIT license.
