// YoRadio recommendations for the VIEWE UEDX48480040E-WB-A.
// Рекомендации YoRadio для VIEWE UEDX48480040E-WB-A.
// Ownership: platform/board. Same structure and macro names as the 4848S040 defaults;
// options.h applies the YORADIO_BD_* macros when myoptions.h does not set the live ones.
// Владелец: platform/board. Структура и имена макросов как у 4848S040; options.h
// применяет YORADIO_BD_*, если myoptions.h не задал действующие макросы.
//
// Display host policy is the D6 policy of the RGB3Wire host (6 MHz first scanout, one
// promotion to 10 MHz, flash guard at 6 MHz), not yet device-verified on this panel.
// Политика хоста — D6 (6 МГц первый scanout, одно повышение до 10 МГц, flash guard 6 МГц);
// на этой панели ещё не проверена на устройстве.
#ifndef YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_UEDX48480040_H
#define YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_UEDX48480040_H

#include "../../board_id.h"
#include "../../board_contract.h"

// ---------------------------------------------------------------------------
// Section 1 — raw literals
// ---------------------------------------------------------------------------

// External I2S DAC wiring (UEDX-0 decision): BCLK 19 and DOUT 20 on the native USB
// pins (J1-9/10), WS 48 shared with the panel command SCK after init. The SD card
// keeps 47/45/42/46. These are WIRES, so they are defaults, not profile wiring.
// Проводка внешнего ЦАП (решение UEDX-0): BCLK 19 и DOUT 20 на пинах нативного USB
// (J1-9/10), WS 48 — общий с SCK команд панели после init. SD остаётся на 47/45/42/46.
#define YORADIO_BD_I2S_DOUT          20
#define YORADIO_BD_I2S_BCLK          19
#define YORADIO_BD_I2S_LRCK          48

// Builtin LED must stay off: Arduino's esp32s3 builtin LED is virtual pin 97 that RMT
// drives on GPIO48, which here is the panel command SCK and the I2S WS (S07 GPIO48 lesson).
// The board's real WS2812 sits on GPIO42 (SD CMD) and is not driven.
// Встроенный LED выключен: виртуальный пин 97 через RMT уводит GPIO48, а здесь это SCK
// команд панели и WS I2S. Настоящий WS2812 платы висит на GPIO42 (CMD SD) и не используется.
#define YORADIO_BD_USE_BUILTIN_LED   false

// Recommended wake source: stock BOOT button on GPIO0, active low (S13 applies it).
#define YORADIO_BD_UEDX_WAKE_GPIO     0

#define YORADIO_BD_UEDX_DRAW_BUFFERS            1
#define YORADIO_BD_UEDX_PARTIAL_DRAW_LINES      160
#define YORADIO_BD_UEDX_RGB_FRAMEBUFFERS        1
#define YORADIO_BD_UEDX_BOUNCE_PX               0
#define YORADIO_BD_RGB_BOUNCE10                 0
#define YORADIO_BD_RGB_FLASH_GUARD              1
#define YORADIO_BD_UEDX_PCLK_STARTUP_HZ         6000000u
#define YORADIO_BD_UEDX_PCLK_RUNTIME_HZ         10000000u
#define YORADIO_BD_UEDX_PCLK_FLASH_GUARD_HZ     6000000u

// FT6336U polling clock: conservative 100 kHz, as on GT911 (the part supports 400 kHz).
#define YORADIO_BD_UEDX_TOUCH_I2C_HZ             100000u

// ---------------------------------------------------------------------------
// Section 2 — constexpr typed view
// ---------------------------------------------------------------------------
#ifdef __cplusplus

namespace yoradio {

inline constexpr BoardDefaults kBoardDefaults_Esp32S3_Uedx48480040 = {
    .audio = {
        .i2s_dout_gpio = YORADIO_BD_I2S_DOUT,
        .i2s_bclk_gpio = YORADIO_BD_I2S_BCLK,
        .i2s_lrck_gpio = YORADIO_BD_I2S_LRCK,
        .i2s_mclk_gpio = kPinNotConnected,
        // No onboard output exists, so the external I2S DAC is the only path.
        .prefer_external_output = true,
    },
    .power = {
        .wake_gpio       = YORADIO_BD_UEDX_WAKE_GPIO,
        .wake_active_low = true,
    },
    .display = {
        .render_mode            = DisplayRenderMode::Partial,
        .draw_buffer_count      = YORADIO_BD_UEDX_DRAW_BUFFERS,
        .partial_draw_lines     = YORADIO_BD_UEDX_PARTIAL_DRAW_LINES,
        .draw_buffer_memory     = DisplayBufferMemory::Psram,
        .rgb_framebuffer_count  = YORADIO_BD_UEDX_RGB_FRAMEBUFFERS,
        .rgb_framebuffer_memory = DisplayBufferMemory::Psram,
        .rgb_framebuffer_owner  = RgbFramebufferOwner::PanelDriver,
        .double_framebuffer     = false,
        .bounce_buffer_px       = YORADIO_BD_UEDX_BOUNCE_PX,
        .flush                  = DisplayFlush::PartialBlitThenFullWriteback,
        .startup_pclk_hz        = YORADIO_BD_UEDX_PCLK_STARTUP_HZ,
        .runtime_pclk_hz        = YORADIO_BD_UEDX_PCLK_RUNTIME_HZ,
        .flash_guard_enabled    = (YORADIO_BD_RGB_FLASH_GUARD) != 0,
        .flash_guard_pclk_hz    = YORADIO_BD_UEDX_PCLK_FLASH_GUARD_HZ,
        .promote_pclk_once_on_boot_to_main = true,
    },
    .touch = {
        .service_mode = TouchServiceMode::Polling,
        .i2c_clock_hz = YORADIO_BD_UEDX_TOUCH_I2C_HZ,
    },
    .ui = {
        .profile = UiProfile::Square480,
    },
};

static_assert(kBoardDefaults_Esp32S3_Uedx48480040.display.flash_guard_enabled ==
                  ((YORADIO_BD_RGB_FLASH_GUARD) != 0),
              "flash guard field and YORADIO_BD_RGB_FLASH_GUARD must agree");
static_assert(((YORADIO_BD_RGB_BOUNCE10) != 0) ==
                  (kBoardDefaults_Esp32S3_Uedx48480040.display.bounce_buffer_px != 0),
              "bounce field and YORADIO_BD_RGB_BOUNCE10 must agree");

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_UEDX48480040_H
