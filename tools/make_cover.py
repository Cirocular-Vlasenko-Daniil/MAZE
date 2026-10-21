#!/usr/bin/env python3
"""Обложка MAZE для ESPidi HUB: 64×32, 1 бит. Слово MAZE в тайлах — MA сверху, ZE снизу,
тем же шрифтом Klaatu и языком карты, увеличенными вдвое: клетка 16 px, тело тайла 14,
буква 10×10. Варианты — в assets/cover/, выбранный — cover.png в корне (его берёт espidi.json).

    tools/make_cover.py [вариант]     # по умолчанию route
"""
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FONT = json.loads((ROOT / "assets" / "font" / "kaleidoscope_font.json").read_text())["variants"]["klaatu"]
W, H, CELL = 64, 32, 16


def canvas():
    return Image.new("1", (W, H), 0)


def put(img, x, y, v=1):
    if 0 <= x < W and 0 <= y < H:
        img.putpixel((x, y), v if v != 2 else 1 - img.getpixel((x, y)))


def block(img, x, y, w, h, v=1):
    for j in range(h):
        for i in range(w):
            put(img, x + i, y + j, v)


def letter(img, ch, c, r):
    """буква Klaatu ×2 в теле клетки (c, r): поля по 2 px, как у тайла на карте"""
    x0, y0 = CELL * c + 2, CELL * r + 2
    for i, col in enumerate(FONT[ch]["cols"]):
        for row in range(1, 6):
            if col >> row & 1:
                block(img, x0 + 2 * i, y0 + 2 * (row - 1), 2, 2)


def empty(img, c, r):  # пустая клетка — точка по центру
    block(img, CELL * c + 6, CELL * r + 6, 2, 2)


def link(img, a, b):  # связка маршрута — точка в зазоре между клетками
    (ac, ar), (bc, br) = sorted([a, b])
    if ar == br:
        block(img, CELL * ac + 14, CELL * ar + 6, 2, 2)
    else:
        block(img, CELL * ac + 6, CELL * ar + 14, 2, 2)


def pointer(img, c, r):  # указатель — инверсия тела клетки
    block(img, CELL * c, CELL * r, 14, 14, 2)


WORD = [("M", 1, 0), ("A", 2, 0), ("Z", 1, 1), ("E", 2, 1)]


def tiles():
    """буквы в тайлах, по краям пустые клетки"""
    img = canvas()
    for ch, c, r in WORD:
        letter(img, ch, c, r)
    for c in (0, 3):
        for r in (0, 1):
            empty(img, c, r)
    return img


def route():
    """буквы на замкнутом маршруте M → A → E → Z, указатель на M"""
    img = tiles()
    for a, b in [((1, 0), (2, 0)), ((2, 0), (2, 1)), ((2, 1), (1, 1)), ((1, 1), (1, 0))]:
        link(img, a, b)
    pointer(img, 1, 0)
    return img


def wide():
    """только слово: тайлы шире, буквы по центру половин"""
    img = canvas()
    for ch, c, r in WORD:
        x0, y0 = 32 * (c - 1) + 11, 16 * r + 2
        for i, col in enumerate(FONT[ch]["cols"]):
            for row in range(1, 6):
                if col >> row & 1:
                    block(img, x0 + 2 * i, y0 + 2 * (row - 1), 2, 2)
    return img


VARIANTS = {"tiles": tiles, "route": route, "wide": wide}


def main():
    pick = sys.argv[1] if len(sys.argv) > 1 else "route"
    out = ROOT / "assets" / "cover"
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in VARIANTS.items():
        img = fn()
        img.save(out / f"cover_{name}.png")
        img.resize((W * 8, H * 8), Image.NEAREST).convert("RGB").save(out / f"cover_{name}_x8.png")
    VARIANTS[pick]().save(ROOT / "cover.png")
    print("ok: cover.png =", pick)


if __name__ == "__main__":
    main()
