#pragma once
#include "canvas/Arduino_Canvas.h"
#include <stdarg.h>
#include "utf8RusGFX.h"

// Block 8-E18D: declarations kept for compile/link only; implementations are no-op stubs.
// Block 8-E18D: объявления для линковки; реализации — no-op.

void gfxDrawTextUtf8(Arduino_Canvas* gfx, int x, int y, const char* text, uint16_t color,
                     uint16_t bgcolor, uint8_t size, const GFXfont* font = nullptr,
                     bool uppercase = false);
void gfxDrawText1b(Arduino_Canvas* gfx, int x, int y, const char* text, uint16_t color,
                   uint16_t bgcolor, uint8_t size, const GFXfont* font = nullptr,
                   bool uppercase = false);
void gfxDrawText(Arduino_Canvas* gfx, int x, int y, const char* text, uint16_t color,
                 uint16_t bgcolor, uint8_t size, const GFXfont* font = nullptr,
                 bool uppercase = false);
void gfxDrawNumber(Arduino_Canvas* gfx, int x, int y, int num, uint16_t color, uint16_t bgcolor,
                   uint8_t size, const GFXfont* font = nullptr);
void gfxDrawFormatted(Arduino_Canvas* gfx, int x, int y, const char* fmt, uint16_t color,
                      uint16_t bgcolor, uint8_t size, const GFXfont* font, ...);

void gfxDrawPixel(Arduino_Canvas* gfx, int x, int y, uint16_t color);
void gfxDrawLine(Arduino_Canvas* gfx, int x0, int y0, int x1, int y1, uint16_t color);
void gfxDrawRect(Arduino_Canvas* gfx, int x, int y, int w, int h, uint16_t color);
void gfxFillRect(Arduino_Canvas* gfx, int x, int y, int w, int h, uint16_t color);
void gfxDrawBitmap(Arduino_Canvas* gfx, int x, int y, const uint16_t* bitmap, int w, int h);

void gfxClearArea(Arduino_Canvas* gfx, int x, int y, int w, int h, uint16_t bgcolor);
void gfxClearScreen(Arduino_Canvas* gfx, uint16_t bgcolor);
void gfxFlushScreen(Arduino_Canvas* gfx);
