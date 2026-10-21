#!/usr/bin/env python3
"""Генерирует MAZE/maze_assets.h из assets/font/kaleidoscope_font.json — тот же шрифт
и те же шкалы, что у прототипа, чтобы прошивка рисовала пиксель в пиксель.

    tools/gen_assets.py
"""
import json
import string
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "font" / "kaleidoscope_font.json"
OUT = ROOT / "MAZE" / "maze_assets.h"

# порядок глифов в массивах: A..Z, a..z, 0..9 — см. glyphIndex() в maze_ui.cpp
ORDER = list(string.ascii_uppercase) + list(string.ascii_lowercase) + list("0123456789")
SYMBOLS = [" ", ":", "+", "#", "/", ".", "dither"]
MAXCOLS = 7


def glyph(g):
    cols = g["cols"] + [0] * (MAXCOLS - len(g["cols"]))
    return "{%d, %d, {%s}}" % (g["w"], len(g["cols"]), ", ".join(str(c) for c in cols))


def table(name, glyphs):
    rows = ",\n".join("    " + glyph(g) for g in glyphs)
    return f"inline const Glyph {name}[{len(glyphs)}] = {{\n{rows},\n}};\n"


def main():
    font = json.loads(SRC.read_text())
    v = font["variants"]
    out = [
        "// Сгенерировано tools/gen_assets.py из assets/font/kaleidoscope_font.json — не править руками.",
        "// Шрифт Klaatu (Merih Yanbasti) и шкалы значений из прошивки KALEIDOSCOPE v81.",
        "// Глиф: шаг w, n столбцов; столбец — байт страницы SSD1306, бит 0 — верхний ряд,",
        "// буква занимает ряды 1–5, хвосты — 6–7.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "namespace maze_assets {",
        "",
        "struct Glyph {",
        "  uint8_t w, n;",
        f"  uint8_t cols[{MAXCOLS}];",
        "};",
        "",
        "// A..Z, a..z, 0..9",
        table("KLAATU", [v["klaatu"][c] for c in ORDER]),
        table("KLAATU_LIGHT", [v["klaatu_light"][c] for c in ORDER]),
        "// ' ' ':' '+' '#' '/' '.' и шахматка (дизеринг)",
        table("SYMBOLS", [font["symbols"][s] for s in SYMBOLS]),
        "// растущая шкала: 24 маски по 32 бита, старший бит — начало шкалы",
        "inline const uint32_t RISING[24] = {"
        + ", ".join("0x%08Xu" % (x & 0xFFFFFFFF) for x in font["scales"]["rising"])
        + "};",
        "",
        "}  // namespace maze_assets",
        "",
    ]
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text("\n".join(out))
    print("ok:", OUT)


if __name__ == "__main__":
    main()
