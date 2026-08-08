// Direct esp_lcd ST7701 RGB backend (BASE-DISP-ESPLCD-PARITY Slice 2/3).
// Прямой esp_lcd ST7701 RGB backend (BASE-DISP-ESPLCD-PARITY Slice 2/3).
// Ownership: display TU; Slice 3 makes this the active DisplayPort backend.
// Владелец: display TU; в Slice 3 это активный backend DisplayPort.
// Hardware: ESP32-S3 4848S040; Arduino_GFX retained as parity reference only.
// Железо: ESP32-S3 4848S040; Arduino_GFX остаётся только эталоном паритета.
#ifndef YORADIO_ESP_LCD_ST7701_BACKEND_H
#define YORADIO_ESP_LCD_ST7701_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

namespace yoradio_esp_lcd_st7701 {

constexpr uint16_t kWidth = 480;
constexpr uint16_t kHeight = 480;
constexpr size_t kFramebufferBytes = static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);  // 460800

// True when this TU is linked into the firmware image (Slice 2 compile proof).
// true, если этот TU слинкован в прошивку (доказательство сборки Slice 2).
bool backendPresent();

// Direct panel bring-up: Type9 + esp_lcd RGB + single PSRAM FB.
// Прямой bring-up: Type9 + esp_lcd RGB + один PSRAM FB.
// Sole owner of the RGB peripheral + physical framebuffer once selected.
// Единственный владелец RGB-периферии и физического FB после выбора.
bool begin();

bool isReady();

// Semantic runtime inversion through the native esp_lcd RGB output operation.
// Runtime-инверсия через штатную RGB-операцию esp_lcd.
bool setInverted(bool inverted);

uint16_t* framebuffer();
size_t framebufferBytes();

// Full-FB C2M cache sync (parity with Arduino_RGB_Display::flush when auto_flush=false).
// Полный C2M sync FB (паритет с Arduino_RGB_Display::flush при auto_flush=false).
bool cacheWritebackFull();

// Fill entire FB with RGB565 color + full writeback.
// Залить весь FB цветом RGB565 + полный writeback.
bool fillRgb565(uint16_t color);

// CPU blit of an RGB565 rectangle into the physical FB. NO cache writeback here —
// caller performs the accepted full-FB C2M afterwards (parity with auto_flush=false).
// CPU-блит RGB565-прямоугольника в физический FB. БЕЗ writeback — вызывающий
// делает принятый полный C2M после (паритет с auto_flush=false).
//
// Semantics are byte-identical to Arduino_GFX gfx_draw_bitmap_to_framebuffer()
// at rotation 0 / offsets 0: source stride is the passed-in `w` (not the clipped
// width), off-screen rows/columns are skipped, fully off-screen returns false.
// Семантика байт-в-байт как gfx_draw_bitmap_to_framebuffer() при rotation 0:
// stride источника = переданный `w`, вылет за экран отсекается.
bool blitRgb565(int16_t x, int16_t y, const uint16_t* src, int16_t w, int16_t h);

// Backlight via existing product path: analogWrite(ST7701_BL), 0..100 → 0..255.
// Подсветка через текущий продуктовый путь: analogWrite(ST7701_BL).
void setBrightnessPercent(uint8_t percent);

// Tear down panel handle if begin() partially failed or for harness exit.
// Снос panel handle при частичном сбое begin() или выходе harness.
void end();

}  // namespace yoradio_esp_lcd_st7701

#endif
