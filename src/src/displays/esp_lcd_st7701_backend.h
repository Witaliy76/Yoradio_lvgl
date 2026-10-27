// Direct esp_lcd ST7701 RGB backend behind DisplayPort.
// Прямой esp_lcd ST7701 RGB backend за интерфейсом DisplayPort.
// Ownership: display TU; active 4848S040 DisplayPort backend.
// Владелец: display TU; активный backend DisplayPort для 4848S040.
// Hardware: ESP32-S3 4848S040; historical Arduino_GFX behavior is parity provenance only.
// Железо: ESP32-S3 4848S040; поведение Arduino_GFX сохранено только как provenance паритета.
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_ESP_LCD_ST7701_BACKEND_H
#define YORADIO_ESP_LCD_ST7701_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

namespace yoradio_esp_lcd_st7701 {

constexpr uint16_t kWidth = 480;
constexpr uint16_t kHeight = 480;
constexpr size_t kFramebufferBytes = static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);  // 460800

// RGB pixel clock. Nominal is the panel clock at creation. Flash-guard is the clock held
// while flash is written (S01-4X-P, S01-5B). These are the only two PCLK values in the product.
// Пиксельная частота RGB. Nominal — частота при создании панели. Flash-guard — частота,
// которая держится во время записи во flash (S01-4X-P, S01-5B). Других значений PCLK нет.
constexpr uint32_t kPclkNominalHz = 10000000u;
constexpr uint32_t kPclkFlashGuardHz = 6000000u;

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

// MANUAL / SERVICE ONLY (S01-5D). Asks esp_lcd to restart RGB DMA at the next VSYNC; requests
// issued before that VSYNC coalesce into one restart. No-op when the panel is not ready, does
// not expose the raw handle, and rejected (returns false, no esp_lcd call) under bounce buffers,
// where the driver owns scanout handling.
// Reachable only from the diagnostic CLI through DisplayPort::restartRgbScanout(). No automatic
// product caller exists: the automatic resync layer was removed in S01-5D because a one-shot
// restart during PLAY is nondeterministic. Each call increments restartExec.
// ТОЛЬКО РУЧНОЙ / СЕРВИСНЫЙ ВЫЗОВ (S01-5D). Просит esp_lcd перезапустить RGB DMA на следующем
// VSYNC; запросы до этого VSYNC схлопываются в один. No-op если панель не готова, raw handle не
// раскрывает, при bounce-буферах отклоняется без вызова esp_lcd.
// Доступен только из диагностического CLI через DisplayPort::restartRgbScanout(). Автоматических
// продуктовых вызывающих нет: автоматический слой resync удалён в S01-5D, поскольку одиночный
// restart во время PLAY недетерминирован. Каждый вызов инкрементирует restartExec.
bool restartRgbScanout();

// Number of esp_lcd_rgb_panel_restart() calls actually issued / rejected for bounce > 0.
// Сколько раз esp_lcd_rgb_panel_restart() действительно вызван / отклонён из-за bounce > 0.
uint32_t restartExecCount();
uint32_t restartRejectedBounceCount();

// Effective bounce size of the created panel, in pixels. 0 = direct T0 (GDMA reads the PSRAM FB).
// Эффективный размер bounce созданной панели в пикселях. 0 = direct T0.
uint16_t bounceBufferPx();

// Monotonic VSYNC count from the panel's on_vsync callback (ISR, increment only).
// The driver runs that callback BEFORE it applies a pending PCLK in the same VSYNC ISR.
// Монотонный счётчик VSYNC из on_vsync (ISR, только инкремент). Драйвер вызывает
// этот callback ДО применения отложенного PCLK в том же VSYNC ISR.
uint32_t vsyncSeq();
bool vsyncCounterReady();

// Effective nominal PCLK: the startup value (kPclkFlashGuardHz) during the Boot phase, then
// kPclkNominalHz after promotePclkToRuntimeNominal(). The flash guard reads this as its UP
// target, so a guarded boot write cannot promote the clock before the startup phase ends.
// Действующий номинальный PCLK: стартовое значение (kPclkFlashGuardHz) в фазе Boot, затем
// kPclkNominalHz после promotePclkToRuntimeNominal(). Guard читает это как цель UP.
uint32_t nominalPclkHz();

// One-shot promotion of the startup PCLK to the runtime nominal at the Boot->Main handoff. An
// ordinary set-PCLK transition: no scanout restart, no panel reinit, no restart-in-VSYNC change.
// False when already promoted or the panel is not ready (the latter is not latched, so a later
// call may still succeed).
// Одноразовое повышение стартового PCLK до runtime-номинала на переходе Boot->Main. Обычный
// переход set-PCLK.
bool promotePclkToRuntimeNominal();

// Record a new RGB PCLK. esp_lcd stores it and programs the divider in a later VSYNC ISR,
// after on_vsync. True means "recorded", not "applied". Never restarts scanout.
// Записать новый PCLK. esp_lcd запоминает его и программирует делитель в одном из
// следующих VSYNC ISR, после on_vsync. True = «записано», а не «применено». Без restart.
bool requestRgbPclkHz(uint32_t hz);

}  // namespace yoradio_esp_lcd_st7701

#endif
