#!/usr/bin/env python3
"""Сверяет экраны прошивки (tests/host/scenes.cpp) с прототипом (tools/screens/scenes.js)
попиксельно. Выход 1 — если хоть одна сцена расходится."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JSC = "/System/Library/Frameworks/JavaScriptCore.framework/Versions/Current/Helpers/jsc"


def parse(text):
    scenes, name = {}, None
    for line in text.splitlines():
        if line.startswith("SCENE "):
            name = line[6:]
            scenes[name] = []
        elif name:
            scenes[name].append(line)
    return scenes


def main():
    js = subprocess.run(
        [JSC, "tools/screens/stub.js", "assets/font/kaleidoscope_font.js", "assets/maze_assets.js", "proto/maze.js", "tools/screens/scenes.js"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    ).stdout
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "scenes"
        subprocess.run(
            ["g++", "-std=gnu++17", "-O1", "-Wall", "-Wno-unused-function", "tests/host/scenes.cpp", "MAZE/maze_core.cpp", "MAZE/maze_ui.cpp", "-o", str(exe)],
            cwd=ROOT, check=True,
        )
        cpp = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    a, b = parse(js), parse(cpp)
    bad = 0
    for name in b:
        if name not in a:
            print(f"{name:24} нет в прототипе")
            continue
        diff = sum(1 for ra, rb in zip(a[name], b[name]) for ca, cb in zip(ra, rb) if ca != cb)
        if diff:
            bad += 1
            print(f"{name:24} РАЗНИЦА {diff} px")
            for y, (ra, rb) in enumerate(zip(a[name], b[name])):
                if ra != rb:
                    print(f"  y{y:02} js  {ra}\n      cpp {rb}")
        else:
            print(f"{name:24} совпадает")
    missing = [n for n in a if n not in b and n != "chroma_rows"]
    if missing:
        print("не перенесены:", ", ".join(missing))
    print("— все сцены совпали с прототипом" if not bad else f"— расходятся сцены: {bad}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
