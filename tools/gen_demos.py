#!/usr/bin/env python3
"""Демо-проекты из assets/demos.json → MAZE/maze_demos.h (прошивка) и assets/maze_demos.js (прототип).
Проверяет маршруты: клетки на карте, соседние, без повторов, замкнутый — конец рядом с началом.

    tools/gen_demos.py
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COLS, ROWS, MAX_LEN, ROUTES = 8, 4, 16, 4
SPEEDS = ["2/1", "1/1", "1/2", "1/4", "1/8"]
ENDS_OPEN, ENDS_CLOSED = ["LOOP", "PONG", "REV"], ["FWD", "REV"]
CONDS = ["PASS", "MEET", "NEAR", "CHNG"]
FX = "ARXPILOZVDWTSH"
TRIGS = ["--"] + [f"{a}:{b}" for b in range(2, 9) for a in range(1, b + 1)] + ["PRE", "/PRE"]
NOTE = re.compile(r"^[A-G]#?[2-8]$")


def check(d):
    name = d["name"]
    assert len(d["grid"]) == ROWS and all(len(r) == COLS for r in d["grid"]), name
    for row in d["grid"]:
        for s in row:
            assert s == "-" or NOTE.match(s) and s[:2] not in ("E#", "B#"), f"{name}: нота {s}"
    for f in d["fx"]:
        c, r, code, cond, p, trig = f
        assert 0 <= c < COLS and 0 <= r < ROWS and code in FX and cond in CONDS, f"{name}: {f}"
        assert trig is None or trig in TRIGS, f"{name}: {f}"
        assert p is None or len(p) <= 3, f"{name}: {f}"
    assert len({(f[0], f[1]) for f in d["fx"]}) == len(d["fx"]), f"{name}: два эффекта в клетке"
    assert len(d["routes"]) == ROUTES, name
    for k, rt in enumerate(d["routes"]):
        cells = rt["cells"]
        assert len(cells) <= MAX_LEN and len({tuple(x) for x in cells}) == len(cells), f"{name} R{k + 1}"
        for a, b in zip(cells, cells[1:]):
            assert abs(a[0] - b[0]) + abs(a[1] - b[1]) == 1, f"{name} R{k + 1}: {a}→{b} не соседи"
        if rt["closed"]:
            a, b = cells[-1], cells[0]
            assert len(cells) >= 3 and abs(a[0] - b[0]) + abs(a[1] - b[1]) == 1, f"{name} R{k + 1}: не замкнут"
        assert rt["speed"] in SPEEDS and rt["end"] in (ENDS_CLOSED if rt["closed"] else ENDS_OPEN), f"{name} R{k + 1}"


def end_index(rt):
    return (ENDS_CLOSED if rt["closed"] else ENDS_OPEN).index(rt["end"])


def cpp(spec):
    out = [
        "// Сгенерировано tools/gen_demos.py из assets/demos.json — не править руками.",
        "#pragma once",
        '#include "maze_core.h"',
        "",
        "namespace maze {",
        "",
        "struct DemoFx {",
        "  int8_t c, r;",
        "  char code;",
        "  uint8_t cond;",
        "  int8_t p[3];  // -1 — по умолчанию",
        "  uint8_t trig;",
        "};",
        "struct DemoRoute {",
        "  int8_t cells[MAX_LEN][2];",
        "  uint8_t len;",
        "  bool closed;",
        "  uint8_t speed, end;",
        "};",
        "struct Demo {",
        "  const char *name;",
        "  uint8_t bpm;",
        "  const char *grid[ROWS][COLS];",
        "  const DemoFx *fx;",
        "  uint8_t fxN;",
        "  DemoRoute routes[ROUTES];",
        "};",
        "",
        f"constexpr uint8_t DEMO_VERSION = {spec['version']};",
        f"constexpr int DEMO_N = {len(spec['demos'])};",
        "",
    ]
    for i, d in enumerate(spec["demos"]):
        out.append(f"inline const DemoFx DEMO_FX_{i}[] = {{")
        for c, r, code, cond, p, trig in d["fx"]:
            pp = list(p or []) + [-1] * (3 - len(p or []))
            out.append(f"    {{{c}, {r}, '{code}', {cond}, {{{pp[0]}, {pp[1]}, {pp[2]}}}, {TRIGS.index(trig or '--')}}},")
        out.append("};")
    out.append("inline const Demo DEMOS[DEMO_N] = {")
    for i, d in enumerate(spec["demos"]):
        grid = ", ".join("{" + ", ".join(f'"{s}"' for s in row) + "}" for row in d["grid"])
        routes = []
        for rt in d["routes"]:
            cells = ", ".join(f"{{{c}, {r}}}" for c, r in rt["cells"]) or "{0, 0}"
            routes.append(f"{{{{{cells}}}, {len(rt['cells'])}, {str(rt['closed']).lower()}, "
                          f"{SPEEDS.index(rt['speed'])}, {end_index(rt)}}}")
        out.append(f'    {{"{d["name"]}", {d["bpm"]}, {{{grid}}}, DEMO_FX_{i}, {len(d["fx"])},')
        out.append("     {" + ",\n      ".join(routes) + "}},")
    out += ["};", "", "}  // namespace maze", ""]
    return "\n".join(out)


def js(spec):
    demos = []
    for d in spec["demos"]:
        demos.append({
            "name": d["name"],
            "bpm": d["bpm"],
            "grid": d["grid"],
            "fx": d["fx"],
            "routes": [{"cells": rt["cells"], "closed": rt["closed"], "speed": SPEEDS.index(rt["speed"]),
                        "end": end_index(rt)} for rt in d["routes"]],
        })
    body = json.dumps({"version": spec["version"], "list": demos}, ensure_ascii=False)
    return ("// Сгенерировано tools/gen_demos.py из assets/demos.json — не править руками.\n"
            f"const DEMOS = {body};\n")


def main():
    spec = json.loads((ROOT / "assets" / "demos.json").read_text())
    for d in spec["demos"]:
        check(d)
    (ROOT / "MAZE" / "maze_demos.h").write_text(cpp(spec))
    (ROOT / "assets" / "maze_demos.js").write_text(js(spec))
    print(f"ok: {len(spec['demos'])} демо, версия {spec['version']}")


if __name__ == "__main__":
    main()
