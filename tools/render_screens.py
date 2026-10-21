#!/usr/bin/env python3
"""Рендерит экраны прототипа без браузера: jsc исполняет proto/maze.js
со сценами из tools/screens/scenes.js, буфер 128×32 → PNG в screens/.

    tools/render_screens.py            # все сцены + screens/all.png
"""
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
JSC = "/System/Library/Frameworks/JavaScriptCore.framework/Versions/Current/Helpers/jsc"
SCRIPTS = [
    "tools/screens/stub.js",
    "assets/font/kaleidoscope_font.js",
    "assets/maze_assets.js",
    "proto/maze.js",
    "tools/screens/scenes.js",
]
OUT = ROOT / "screens"
ON, OFF = (230, 238, 255), (4, 6, 8)


def oled(rows, scale=6, gap=1):
    img = Image.new("RGB", (128 * scale, 32 * scale), OFF)
    d = ImageDraw.Draw(img)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "#":
                d.rectangle([x * scale, y * scale, x * scale + scale - 1 - gap, y * scale + scale - 1 - gap], fill=ON)
    return img


def main():
    out = subprocess.run([JSC, *SCRIPTS], cwd=ROOT, capture_output=True, text=True)
    if out.returncode or out.stderr:
        raise SystemExit(out.stderr or out.stdout)
    scenes, name = {}, None
    for line in out.stdout.splitlines():
        if line.startswith("SCENE "):
            name = line[6:]
            scenes[name] = []
        elif name:
            scenes[name].append(line)
    OUT.mkdir(exist_ok=True)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 22)
    except OSError:
        font = ImageFont.load_default()
    tiles = []
    for name, rows in scenes.items():
        img = oled(rows)
        img.save(OUT / f"{name}.png")
        tiles.append((name, img))
    pad, cap = 24, 34
    w = 128 * 6 + 2 * pad
    sheet = Image.new("RGB", (w, len(tiles) * (32 * 6 + cap + pad) + pad), (17, 18, 20))
    d = ImageDraw.Draw(sheet)
    y = pad
    for name, img in tiles:
        d.text((pad, y), name, fill=(138, 144, 154), font=font)
        sheet.paste(img, (pad, y + cap))
        y += 32 * 6 + cap + pad
    sheet.save(OUT / "all.png")
    print("ok:", len(tiles), "сцен →", OUT)


if __name__ == "__main__":
    main()
