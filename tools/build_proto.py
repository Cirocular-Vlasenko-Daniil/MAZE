#!/usr/bin/env python3
"""Собирает proto/index.html в один файл proto/dist/maze_proto.html:
подключённые <script src> вставляются внутрь, файл открывается откуда угодно.

    tools/build_proto.py [папка-копия]   # копия — для локального превью
"""
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "proto" / "index.html"
OUT = ROOT / "proto" / "dist" / "maze_proto.html"


def inline(m):
    path = (SRC.parent / m.group(1)).resolve()
    return "<script>\n" + path.read_text() + "\n</script>"


def main():
    html = re.sub(r'<script src="([^"]+)"></script>', inline, SRC.read_text())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(html)
    print("ok:", OUT, f"{len(html) // 1024} КБ")
    if len(sys.argv) > 1:
        dst = Path(sys.argv[1])
        dst.mkdir(parents=True, exist_ok=True)
        shutil.copy(OUT, dst / "index.html")
        print("copy:", dst / "index.html")


if __name__ == "__main__":
    main()
