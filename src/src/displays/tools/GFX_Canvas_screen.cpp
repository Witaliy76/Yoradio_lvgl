// Legacy Canvas GFX wrappers removed in Block 8-E18D (no-op link stubs only).
// Legacy Canvas GFX wrappers удалены в Block 8-E18D (только no-op для линковки).

#include "GFX_Canvas_screen.h"

void gfxDrawTextUtf8(Arduino_Canvas* gfx, int x, int y, const char* text, uint16_t color,
                     uint16_t bgcolor, uint8_t size, const GFXfont* font, bool uppercase) {
    (void)gfx; (void)x; (void)y; (void)text; (void)color; (void)bgcolor; (void)size; (void)font;
    (void)uppercase;
}

void gfxDrawText1b(Arduino_Canvas* gfx, int x, int y, const char* text, uint16_t color,
                   uint16_t bgcolor, uint8_t size, const GFXfont* font, bool uppercase) {
    (void)gfx; (void)x; (void)y; (void)text; (void)color; (void)bgcolor; (void)size; (void)font;
    (void)uppercase;
}

void gfxDrawText(Arduino_Canvas* gfx, int x, int y, const char* text, uint16_t color,
                 uint16_t bgcolor, uint8_t size, const GFXfont* font, bool uppercase) {
    gfxDrawTextUtf8(gfx, x, y, text, color, bgcolor, size, font, uppercase);
}

void gfxDrawNumber(Arduino_Canvas* gfx, int x, int y, int num, uint16_t color, uint16_t bgcolor,
                   uint8_t size, const GFXfont* font) {
    (void)gfx; (void)x; (void)y; (void)num; (void)color; (void)bgcolor; (void)size; (void)font;
}

void gfxDrawFormatted(Arduino_Canvas* gfx, int x, int y, const char* fmt, uint16_t color,
                      uint16_t bgcolor, uint8_t size, const GFXfont* font, ...) {
    (void)gfx; (void)x; (void)y; (void)fmt; (void)color; (void)bgcolor; (void)size; (void)font;
}

void gfxDrawPixel(Arduino_Canvas* gfx, int x, int y, uint16_t color) {
    (void)gfx; (void)x; (void)y; (void)color;
}

void gfxDrawLine(Arduino_Canvas* gfx, int x0, int y0, int x1, int y1, uint16_t color) {
    (void)gfx; (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
}

void gfxDrawRect(Arduino_Canvas* gfx, int x, int y, int w, int h, uint16_t color) {
    (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)color;
}

void gfxFillRect(Arduino_Canvas* gfx, int x, int y, int w, int h, uint16_t color) {
    (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)color;
}

void gfxDrawBitmap(Arduino_Canvas* gfx, int x, int y, const uint16_t* bitmap, int w, int h) {
    (void)gfx; (void)x; (void)y; (void)bitmap; (void)w; (void)h;
}

void gfxClearArea(Arduino_Canvas* gfx, int x, int y, int w, int h, uint16_t bgcolor) {
    (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)bgcolor;
}

void gfxClearScreen(Arduino_Canvas* gfx, uint16_t bgcolor) {
    (void)gfx; (void)bgcolor;
}

void gfxFlushScreen(Arduino_Canvas* gfx) {
    (void)gfx;
}
