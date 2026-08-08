// Direct esp_lcd ST7701 RGB backend (BASE-DISP-ESPLCD-PARITY Slice 2).
// Прямой esp_lcd ST7701 RGB backend (BASE-DISP-ESPLCD-PARITY Slice 2).
// Ownership: display TU; NOT selected by DisplayPort::flush until Slice 3.
// Владелец: display TU; DisplayPort::flush не переключается до Slice 3.
// Hardware: ESP32-S3 4848S040; Arduino_GFX remains the accepted runtime backend.
// Железо: ESP32-S3 4848S040; Arduino_GFX остаётся принятым runtime backend.
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
// Does not touch DisplayPort / LVGL flush ownership.
// Не затрагивает DisplayPort / владение LVGL flush.
bool begin();

bool isReady();

uint16_t* framebuffer();
size_t framebufferBytes();

// Full-FB C2M cache sync (parity with Arduino_RGB_Display::flush when auto_flush=false).
// Полный C2M sync FB (паритет с Arduino_RGB_Display::flush при auto_flush=false).
bool cacheWritebackFull();

// Fill entire FB with RGB565 color + full writeback.
// Залить весь FB цветом RGB565 + полный writeback.
bool fillRgb565(uint16_t color);

// Write a simple orientation/color diagnostic pattern + full writeback.
// Простой диагностический паттерн ориентации/цвета + полный writeback.
bool drawBringupPattern();

// Backlight via existing product path: analogWrite(ST7701_BL), 0..100 → 0..255.
// Подсветка через текущий продуктовый путь: analogWrite(ST7701_BL).
void setBrightnessPercent(uint8_t percent);

// Tear down panel handle if begin() partially failed or for harness exit.
// Снос panel handle при частичном сбое begin() или выходе harness.
void end();

}  // namespace yoradio_esp_lcd_st7701

#endif
