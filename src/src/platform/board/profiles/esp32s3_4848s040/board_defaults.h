// Tested YoRadio recommendations for the ESP32-S3 4848S040.
// Проверенные рекомендации YoRadio для ESP32-S3 4848S040.
// Ownership: platform/board.
//
// BoardProfile holds physical facts. This file supplies tested host/touch policy,
// audio/wake recommendations and explicit UI profile selection. Only supported
// build choices have myoptions.h overrides; backend compatibility checks remain.
// Display consumers read kActiveBoardDefaults.display: the resolved contract
// supplies backend/DisplayPort policy, and LVGL reads partial_draw_lines directly.
// Touch service mode and I2C clock are read through ResolvedTouchConfig.
// The raw YORADIO_BD_* macros below are the defaults options.h applies when
// myoptions.h does not define the live macro: I2S_DOUT / I2S_BCLK / I2S_LRC,
// USE_BUILTIN_LED, YORADIO_RGB_BOUNCE10 and YORADIO_RGB_FLASH_GUARD. The live
// macros stay macros because Audio.h and the checkers use them inside #if.
// PowerDefaults and WAKE_PIN are NOT applied here (S13).
// Код дисплея читает kActiveBoardDefaults.display: ResolvedDisplayConfig
// передаёт политику драйверу/DisplayPort, LVGL берёт partial_draw_lines напрямую.
// Режим опроса тача и частота I2C читаются через ResolvedTouchConfig.
// Макросы YORADIO_BD_* ниже — умолчания, которые options.h применяет, если myoptions.h
// не задал действующий макрос: I2S_DOUT / I2S_BCLK / I2S_LRC, USE_BUILTIN_LED,
// YORADIO_RGB_BOUNCE10 и YORADIO_RGB_FLASH_GUARD. Действующие макросы остаются макросами,
// потому что Audio.h и проверки используют их внутри #if.
// PowerDefaults и WAKE_PIN здесь НЕ применяются (S13).
// Физические факты хранятся в BoardProfile. Здесь — проверенная политика дисплея
// и тача, рекомендации I2S/пробуждения и явный выбор UI-профиля. Через myoptions.h
// меняются только поддерживаемые настройки; проверки совместимости сохраняются.
//
// The display numbers below are the tested 4848S040 host policy. They are
// not proof of other combinations. Touch orientation uses official
// esp_lcd_touch flags; no calibration is stored here. ui.profile selects Square480
// factory metrics; physical display geometry remains in the display contract.
// Числа дисплея ниже — проверенная политика хоста 4848S040. Они не доказывают
// другие сочетания. Ориентация тача передаётся штатными флагами esp_lcd_touch;
// калибровки здесь нет. ui.profile выбирает заводские метрики Square480,
// а физический размер экрана остаётся в контракте дисплея.
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
// а не дорожки платы, поэтому это рекомендация, а не заводская разводка профиля.
// The names are common to all profiles; options.h reads YORADIO_BD_I2S_*.
// Имена общие для всех профилей; options.h читает YORADIO_BD_I2S_*.
#define YORADIO_BD_I2S_DOUT          40
#define YORADIO_BD_I2S_BCLK           1
#define YORADIO_BD_I2S_LRCK           2

// Builtin LED default: off. On this board GPIO48 is the DisplayCommand SCK and the
// SD SCK, while Arduino's default builtin LED is virtual pin 97 that digitalWrite
// maps onto GPIO48 through RMT. There is no WS2812 on this board.
// Builtin LED по умолчанию: выключен. На этой плате GPIO48 — SCK DisplayCommand и SCK SD,
// а штатный builtin LED Arduino — виртуальный пин 97, который digitalWrite через RMT
// уводит на GPIO48. WS2812 на плате нет.
#define YORADIO_BD_USE_BUILTIN_LED   false

// Recommended deep-sleep wake source: the stock BOOT button on GPIO0, active low.
// Works with no extra wiring because the button provides the pull itself.
// Рекомендуемый источник пробуждения: штатная кнопка BOOT на GPIO0, активный ноль.
#define YORADIO_BD_4848_WAKE_GPIO     0

// Tested display host policy for this board (S01 path), checked by static_assert.
// Native 480×480 stays in PanelMode / BoardProfile and is not repeated here.
// Restart-in-VSYNC is a build-family sdkconfig switch, not one of these macros.
// Проверенная политика дисплея (путь S01), совместимость проверяется static_assert.
// Родные 480×480 остаются в PanelMode / BoardProfile и здесь не повторяются.
// Restart-in-VSYNC — переключатель sdkconfig семейства сборки, не эти макросы.
#define YORADIO_BD_4848_DRAW_BUFFERS            1
#define YORADIO_BD_4848_PARTIAL_DRAW_LINES      160
#define YORADIO_BD_4848_RGB_FRAMEBUFFERS        1
#define YORADIO_BD_4848_BOUNCE_PX               0
// Flag-form defaults that options.h applies to YORADIO_RGB_BOUNCE10 / YORADIO_RGB_FLASH_GUARD
// (the flags stay macros for #if). Bounce 0 = off, flash guard 1 = on.
// Умолчания в виде флагов для YORADIO_RGB_BOUNCE10 / YORADIO_RGB_FLASH_GUARD, которые
// применяет options.h (флаги остаются макросами для #if). Bounce 0 — выкл., flash guard 1 — вкл.
#define YORADIO_BD_RGB_BOUNCE10                 0
#define YORADIO_BD_RGB_FLASH_GUARD              1
#define YORADIO_BD_4848_PCLK_STARTUP_HZ         6000000u
#define YORADIO_BD_4848_PCLK_RUNTIME_HZ         10000000u
#define YORADIO_BD_4848_PCLK_FLASH_GUARD_HZ     6000000u

// Tested GT911 servicing policy. Controller/address/pins are BoardProfile facts;
// Flip Touch is persisted user correction and is deliberately absent here.
// Проверенная политика обслуживания GT911. Контроллер, адрес и пины принадлежат
// BoardProfile, Flip Touch — сохраняемая пользовательская коррекция, её здесь нет.
#define YORADIO_BD_4848_TOUCH_I2C_HZ             100000u

// ---------------------------------------------------------------------------
// Section 2 — constexpr typed view
// Секция 2 — типизированный constexpr-вид
// ---------------------------------------------------------------------------

#ifdef __cplusplus

namespace yoradio {

inline constexpr BoardDefaults kBoardDefaults_Esp32S3_4848S040 = {
    .audio = {
        .i2s_dout_gpio = YORADIO_BD_I2S_DOUT,
        .i2s_bclk_gpio = YORADIO_BD_I2S_BCLK,
        .i2s_lrck_gpio = YORADIO_BD_I2S_LRCK,
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
        .flash_guard_enabled  = (YORADIO_BD_RGB_FLASH_GUARD) != 0,
        .flash_guard_pclk_hz  = YORADIO_BD_4848_PCLK_FLASH_GUARD_HZ,
        .promote_pclk_once_on_boot_to_main = true,
    },
    .touch = {
        .service_mode = TouchServiceMode::Polling,
        .i2c_clock_hz = YORADIO_BD_4848_TOUCH_I2C_HZ,
    },
    .ui = {
        .profile = UiProfile::Square480,
    },
};

// These checks describe this defaults object. They do not read myoptions.h
// and do not reject YORADIO_RGB_BOUNCE10 or any other user override.
// Проверки описывают этот объект рекомендаций. Они не читают myoptions.h
// и не отвергают YORADIO_RGB_BOUNCE10 или другую поддерживаемую настройку.
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
static_assert(kBoardDefaults_Esp32S3_4848S040.display.flash_guard_enabled,
              "display default flash guard is on");
// The typed fields and the flag macros that options.h applies describe the same default.
// Типизированные поля и флаговые макросы, которые применяет options.h, — одно умолчание.
static_assert(kBoardDefaults_Esp32S3_4848S040.display.flash_guard_enabled ==
                  ((YORADIO_BD_RGB_FLASH_GUARD) != 0),
              "flash guard field and YORADIO_BD_RGB_FLASH_GUARD must agree");
static_assert(((YORADIO_BD_RGB_BOUNCE10) != 0) ==
                  (kBoardDefaults_Esp32S3_4848S040.display.bounce_buffer_px != 0),
              "bounce field and YORADIO_BD_RGB_BOUNCE10 must agree");
static_assert(kBoardDefaults_Esp32S3_4848S040.display.promote_pclk_once_on_boot_to_main,
              "display default promotes PCLK once at Boot to Main");
static_assert(kBoardDefaults_Esp32S3_4848S040.touch.service_mode ==
                  TouchServiceMode::Polling,
              "touch default is polling");
static_assert(kBoardDefaults_Esp32S3_4848S040.touch.i2c_clock_hz == 100000u,
              "touch default I2C clock is 100 kHz");
static_assert(kBoardDefaults_Esp32S3_4848S040.ui.profile == UiProfile::Square480,
              "4848S040 must explicitly select the Square480 UI profile");

static_assert(YORADIO_BD_I2S_DOUT == 40, "audio dout recommendation unchanged");
static_assert(YORADIO_BD_I2S_BCLK == 1, "audio bclk recommendation unchanged");
static_assert(YORADIO_BD_I2S_LRCK == 2, "audio lrck recommendation unchanged");
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
