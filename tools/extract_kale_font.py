#!/usr/bin/env python3
"""Достаёт 5×5-шрифт и шкалы из прошивки KALEIDOSCOPE v81 beta.

Прошивки лежат в github.com/vadjuse/kaleidoscope, папка "Release FWs".
Три сборки v81 (alien / alien_light / classic) одинакового размера и
отличаются только таблицей глифов — по этому её и нашли.

    tools/extract_kale_font.py "<путь к Release FWs>"  →  assets/font/kaleidoscope_font.json (+ .js)
                                                          assets/font/specimen.png

Формат глифа (как в прошивке): байт ширины w, затем w-2 столбца.
Столбец — байт SSD1306-страницы: бит 0 — верхний ряд (всегда пуст),
биты 1–5 — заглавная высота 5 px, биты 6–7 — хвосты (g, j, p, q, y).
Шаг символа = w (2 px между заглавными), как на листе FONT.jpg.
"""
import json
import string
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "font"

FILES = {
    "klaatu": "blink81beta_alien_font_11Oct2026.bin",
    "klaatu_light": "blink81beta_alien_light_font_11Oct2026.bin",
    "classic": "blink81beta_classic_font_11Oct2026.bin",
}
GLYPHS = 0x1B544  # 62 записи по 12 байт: A a B b … Z z, затем 1 2 … 9 0
SYMBOLS = 0x1B988  # общие для всех вариантов
SCALES = 0x1B86C  # шкалы значений: 32-битные маски, 24 px

LABELS = [c for pair in zip(string.ascii_uppercase, string.ascii_lowercase) for c in pair]
LABELS += list("1234567890")
# Символы общие для всех трёх сборок. Привязка к кодам — по виду глифа:
# таблицу кодов в прошивке не разбирал.
SYMBOL_LABELS = ["dither", " ", ":", "wide_m", "+", "dash_dotted", ".", "space_2", "#", "/"]


def record(b, off):
    w = b[off]
    return {"w": w, "cols": list(b[off + 1 : off + 1 + max(0, w - 2)])}


def scales(b):
    words = [int.from_bytes(b[SCALES + 4 * i : SCALES + 4 * i + 4], "big") for i in range(71)]
    return {
        "_note": "маска 32 бит, старший бит — начало шкалы, длина 24 px",
        "rising": words[0:24],
        "central_left": words[24:36],
        "central_right": words[36:48],
        "one_hot_8": words[48:56],
        "binary_4": [0] + words[56:71],
    }


def specimen(font, path):
    from PIL import Image

    lines = [
        "ABCDEFGHIJKLM",
        "NOPQRSTUVWXYZ",
        "abcdefghijklm",
        "nopqrstuvwxyz",
        "1234567890 :+#/.",
    ]
    variants = list(font["variants"])
    scale, pad, line_h = 4, 4, 8
    width = 128
    height = len(variants) * (len(lines) + 1) * line_h
    img = Image.new("1", (width, height), 0)
    px = img.load()
    y = 0
    for v in variants:
        glyphs = dict(font["variants"][v])
        glyphs.update(font["symbols"])
        for text in [v.upper().replace("_", " ")] + lines:
            x = 0
            for ch in text:
                g = glyphs.get(ch) or glyphs.get(ch.upper())
                if not g:
                    x += 4
                    continue
                for cx, col in enumerate(g["cols"]):
                    for row in range(8):
                        if col >> row & 1 and x + cx < width:
                            px[x + cx, y + row] = 1
                x += g["w"]
            y += line_h
        y += 0
    big = img.resize((width * scale, height * scale), Image.NEAREST).convert("RGB")
    canvas = Image.new("RGB", (big.width + 2 * pad * scale, big.height + 2 * pad * scale), (0, 0, 0))
    canvas.paste(big, (pad * scale, pad * scale))
    canvas.save(path)


def main():
    fw = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    font = {
        "source": "KALEIDOSCOPE v81 beta (Vadjuse), github.com/vadjuse/kaleidoscope/Release FWs",
        "credit": "Шрифт Klaatu — Merih Yanbasti (титры мануала KALEIDOSCOPE)",
        "format": "столбцы SSD1306: бит0 = верхний ряд; ряды 1–5 = 5 px, 6–7 = хвосты; шаг = w",
        "variants": {},
    }
    for name, fname in FILES.items():
        b = (fw / fname).read_bytes()
        font["variants"][name] = {
            LABELS[i]: record(b, GLYPHS + 12 * i) for i in range(len(LABELS))
        }
        if name == "klaatu":
            font["symbols"] = {
                SYMBOL_LABELS[i]: record(b, SYMBOLS + 12 * i) for i in range(len(SYMBOL_LABELS))
            }
            font["scales"] = scales(b)
    OUT.mkdir(parents=True, exist_ok=True)
    data = json.dumps(font, ensure_ascii=False, indent=1)
    (OUT / "kaleidoscope_font.json").write_text(data)
    # Тот же шрифт для прототипа: подключается <script>, работает и с file://
    (OUT / "kaleidoscope_font.js").write_text("window.KFONT = " + data + ";\n")
    specimen(font, OUT / "specimen.png")
    print("ok:", OUT / "kaleidoscope_font.json", OUT / "kaleidoscope_font.js", OUT / "specimen.png")


if __name__ == "__main__":
    main()
