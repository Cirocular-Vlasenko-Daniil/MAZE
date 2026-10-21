// MAZE — точка входа для Arduino IDE.
//
// Здесь только имя для ESPidi HUB и обложка; весь код — в maze_*.cpp рядом
// (setup() и loop() — в maze_hw.cpp). Дескриптор должен лежать именно в .ino:
// Arduino выбирает библиотеки, сканируя его, и без этого ESPidiHub.h не находится.

#include <ESPidiHub.h>

ESPIDI_FIRMWARE("maze", "MAZE", "0.2.1", "Daniil Vlasenko");
#include "cover.h"  // из cover.png: tools/make_cover.py, затем espidi_hub.py cover cover.png
