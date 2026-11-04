// Tested YoRadio recommendations for the ESP32-S3 4848S040.
// Проверенные рекомендации YoRadio для ESP32-S3 4848S040.
// Ownership: platform/board.
//
// THIS FILE IS NOT PHYSICAL TRUTH AND NOT A RESTRICTION.
// Every value here is a recommendation the user may override in myoptions.h.
// Nothing in it may be read as a hardware constraint, and no compile-time check
// may reject a build merely because a user chose different values.
// Display consumers now read kActiveBoardDefaults.display: the resolved contract
// supplies backend/DisplayPort policy, and LVGL reads partial_draw_lines directly.
// Live I2S_DOUT / I2S_BCLK / I2S_LRC and WAKE_PIN stay in myoptions.h, because
// Audio.h and the wake-range check use them inside #if. A later slice may either
// start applying these recommendations through #ifndef, or delete the file once
// that role is clearly unwanted.
// Display-потребители теперь читают kActiveBoardDefaults.display: resolved-контракт
// передаёт политику backend/DisplayPort, LVGL берёт partial_draw_lines напрямую.
// Живые I2S и WAKE_PIN остаются в myoptions.h. Позже рекомендации можно начать
// применять через #ifndef либо удалить файл, если эта роль не нужна.
// ЭТО НЕ ФИЗИЧЕСКАЯ ПРАВДА И НЕ ЗАПРЕТ.
// Любое значение здесь — рекомендация, переопределяемая в myoptions.h. Ни одна
// compile-time проверка не вправе отвергнуть сборку только из-за другого выбора.
//
// S04-2's tested host recommendation is now live through the resolved display
// contract and LVGL. It remains a recommendation, not proof of other combinations.
// Рекомендация S04-2 теперь действует через resolved display contract и LVGL,
// но не является доказательством всех иных сочетаний.
//
// Touch servicing defaults are recorded here by S05. Orientation is resolved
// into official esp_lcd_touch flags; no calibration is active.
// UI profile selection (Square480) remains S06.
// S05 записывает здесь defaults обслуживания тача. Orientation передаётся
// штатными флагами esp_lcd_touch; calibration не активна. UI-профиль — S06.
#ifndef YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_4848S040_H
#define YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_4848S040_H

#include "../../board_id.h"
#include "../../board_contract.h"

// ---------------------------------------------------------------------------
// Section 1 — raw literals (single textual source of these recommendations)
// Секция 1 — числовые литералы (единственный текстовый источник рекомендаций)
// ---------------------------------------------------------------------------

// Recommended external I2S DAC pins: the rear-connector wiring this board is
// normally used with. These are WIRES, not board traces, so they are a default
// and never immutable BoardProfile wiring.
// Рекомендуемые пины внешнего I2S ЦАП: разводка заднего разъёма. Это ПРОВОДА,
// а не дорожки платы, поэтому это умолчание, а не immutable-разводка профиля.
#define YORADIO_BD_4848_I2S_DOUT     40
#define YORADIO_BD_4848_I2S_BCLK      1
#define YORADIO_BD_4848_I2S_LRCK      2

// Recommended deep-sleep wake source: the stock BOOT button on GPIO0, active low.
// Works with no extra wiring because the button provides the pull itself.
// Рекомендуемый источник пробуждения: штатная кнопка BOOT на GPIO0, активный ноль.
#define YORADIO_BD_4848_WAKE_GPIO     0

// Tested display host policy for this board (S01 path). Recommendation only.
// Native 480×480 stays in PanelMode / BoardProfile and is not repeated here.
// Restart-in-VSYNC is a build-family sdkconfig switch, not one of these macros.
// Проверенная политика хоста дисплея (путь S01). Только рекомендация.
// Родные 480×480 остаются в PanelMode / BoardProfile и здесь не повторяются.
// Restart-in-VSYNC — переключатель sdkconfig семейства сборки, не эти макросы.
#define YORADIO_BD_4848_DRAW_BUFFERS            1
#define YORADIO_BD_4848_PARTIAL_DRAW_LINES      160
#define YORADIO_BD_4848_RGB_FRAMEBUFFERS        1
#define YORADIO_BD_4848_BOUNCE_PX               0
#define YORADIO_BD_4848_PCLK_STARTUP_HZ         6000000u
#define YORADIO_BD_4848_PCLK_RUNTIME_HZ         10000000u
#define YORADIO_BD_4848_PCLK_FLASH_GUARD_HZ     6000000u

// Tested GT911 servicing policy. Controller/address/pins are BoardProfile facts;
// Flip Touch is persisted user correction and is deliberately absent here.
// Проверенная политика обслуживания GT911. Controller/address/pins принадлежат
// BoardProfile, Flip Touch — persisted user correction и здесь отсутствует.
#define YORADIO_BD_4848_TOUCH_I2C_HZ             100000u

// ---------------------------------------------------------------------------
// Section 2 — constexpr typed view
// Секция 2 — типизированный constexpr-вид
// ---------------------------------------------------------------------------

#ifdef __cplusplus

namespace yoradio {

inline constexpr BoardDefaults kBoardDefaults_Esp32S3_4848S040 = {
    .audio = {
        .i2s_dout_gpio = YORADIO_BD_4848_I2S_DOUT,
        .i2s_bclk_gpio = YORADIO_BD_4848_I2S_BCLK,
        .i2s_lrck_gpio = YORADIO_BD_4848_I2S_LRCK,
        // MCLK is not used by the current output path.
        // MCLK в текущем тракте не используется.
        .i2s_mclk_gpio = kPinNotConnected,
        // The onboard mono path needs hardware straps, so the external I2S
        // output is the recommended one. The user may still choose otherwise.
        // Встроенный моно-тракт требует перемычек, поэтому рекомендуется внешний
        // выход I2S. Пользователь по-прежнему может выбрать иначе.
        .prefer_external_output = true,
    },
    .power = {
        .wake_gpio       = YORADIO_BD_4848_WAKE_GPIO,
        .wake_active_low = true,
    },
    .display = {
        .render_mode          = DisplayRenderMode::Partial,
        .draw_buffer_count    = YORADIO_BD_4848_DRAW_BUFFERS,
        .partial_draw_lines   = YORADIO_BD_4848_PARTIAL_DRAW_LINES,
        .draw_buffer_memory   = DisplayBufferMemory::Psram,
        .rgb_framebuffer_count  = YORADIO_BD_4848_RGB_FRAMEBUFFERS,
        .rgb_framebuffer_memory = DisplayBufferMemory::Psram,
        .rgb_framebuffer_owner  = RgbFramebufferOwner::PanelDriver,
        .double_framebuffer   = false,
        .bounce_buffer_px     = YORADIO_BD_4848_BOUNCE_PX,
        .flush                = DisplayFlush::PartialBlitThenFullWriteback,
        .startup_pclk_hz      = YORADIO_BD_4848_PCLK_STARTUP_HZ,
        .runtime_pclk_hz      = YORADIO_BD_4848_PCLK_RUNTIME_HZ,
        .flash_guard_pclk_hz  = YORADIO_BD_4848_PCLK_FLASH_GUARD_HZ,
        .promote_pclk_once_on_boot_to_main = true,
    },
    .touch = {
        .service_mode = TouchServiceMode::Polling,
        .i2c_clock_hz = YORADIO_BD_4848_TOUCH_I2C_HZ,
    },
};

// These checks describe this defaults object. They do not read myoptions.h
// and do not reject YORADIO_RGB_BOUNCE10 or any other user override.
// Проверки описывают этот объект рекомендаций. Они не читают myoptions.h
// и не отвергают YORADIO_RGB_BOUNCE10 или другой пользовательский override.
static_assert(kBoardDefaults_Esp32S3_4848S040.display.render_mode == DisplayRenderMode::Partial,
              "display default is partial rendering");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.draw_buffer_count == 1,
              "display default is one draw buffer");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.partial_draw_lines == 160,
              "display default partial height is 160");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.draw_buffer_memory == DisplayBufferMemory::Psram,
              "display default draw buffer is PSRAM");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.rgb_framebuffer_count == 1,
              "display default is one RGB framebuffer");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.rgb_framebuffer_memory == DisplayBufferMemory::Psram,
              "display default RGB framebuffer is PSRAM");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.rgb_framebuffer_owner == RgbFramebufferOwner::PanelDriver,
              "display default framebuffer owner is the panel driver");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.double_framebuffer == false,
              "display default is not double-buffered");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.bounce_buffer_px == 0,
              "display default bounce is 0");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.flush == DisplayFlush::PartialBlitThenFullWriteback,
              "display default flush is partial blit plus full writeback");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.startup_pclk_hz == 6000000u,
              "display default startup PCLK is 6 MHz");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.runtime_pclk_hz == 10000000u,
              "display default runtime PCLK is 10 MHz");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.flash_guard_pclk_hz == 6000000u,
              "display default flash-guard PCLK is 6 MHz");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.promote_pclk_once_on_boot_to_main,
              "display default promotes PCLK once at Boot to Main");
static_assert(kBoardDefaults_Esp32S3_4848S040.touch.service_mode ==
                  TouchServiceMode::Polling,
              "touch default is polling");
static_assert(kBoardDefaults_Esp32S3_4848S040.touch.i2c_clock_hz == 100000u,
              "touch default I2C clock is 100 kHz");

static_assert(YORADIO_BD_4848_I2S_DOUT == 40, "audio dout recommendation unchanged");
static_assert(YORADIO_BD_4848_I2S_BCLK == 1, "audio bclk recommendation unchanged");
static_assert(YORADIO_BD_4848_I2S_LRCK == 2, "audio lrck recommendation unchanged");
static_assert(kBoardDefaults_Esp32S3_4848S040.audio.i2s_dout_gpio == 40, "audio dout field unchanged");
static_assert(kBoardDefaults_Esp32S3_4848S040.audio.i2s_bclk_gpio == 1, "audio bclk field unchanged");
static_assert(kBoardDefaults_Esp32S3_4848S040.audio.i2s_lrck_gpio == 2, "audio lrck field unchanged");
static_assert(kBoardDefaults_Esp32S3_4848S040.audio.i2s_mclk_gpio == kPinNotConnected,
              "audio mclk recommendation unchanged");
static_assert(kBoardDefaults_Esp32S3_4848S040.audio.prefer_external_output,
              "audio prefers external output");
static_assert(YORADIO_BD_4848_WAKE_GPIO == 0, "wake gpio recommendation unchanged");
static_assert(kBoardDefaults_Esp32S3_4848S040.power.wake_gpio == 0, "wake gpio field unchanged");
static_assert(kBoardDefaults_Esp32S3_4848S040.power.wake_active_low, "wake remains active low");

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_4848S040_H
