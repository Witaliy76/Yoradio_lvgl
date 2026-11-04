// Board Contract value types: immutable physical truth of one named board.
// Типы Board Contract: неизменяемая физическая правда одной именованной платы.
// Ownership: platform/board. Depends only on <stdint.h> and board_id.h, because
// src/src/core/options.h is sometimes parsed before Arduino.h — no Arduino types here.
// Владелец: platform/board. Зависит только от <stdint.h> и board_id.h: options.h
// иногда парсится раньше Arduino.h, поэтому Arduino-типы здесь недопустимы.
// S02-1 defines Identity/BuildRequirements only. Display/Touch/Audio/Storage/USB/
// Network/Controls/Power/SharedResources domains arrive in later S02 slices.
// S02-1 определяет только Identity/BuildRequirements; остальные домены — позже.
#ifndef YORADIO_PLATFORM_BOARD_CONTRACT_H
#define YORADIO_PLATFORM_BOARD_CONTRACT_H

#include <stdint.h>
#include "board_id.h"

// ---------------------------------------------------------------------------
// "Not connected" sentinel — one textual source, two projections
// Сентинел «не подключено» — один текстовый источник, две проекции
// ---------------------------------------------------------------------------
// A signal the board does not wire at all. Matches the long-standing YoRadio
// convention, and 255 is not a usable GPIO on any supported target. GPIO0 is a
// real pin on this board, so 0 must never be used as "absent".
// Сигнал, который плата не разводит вовсе. Совпадает с давней конвенцией YoRadio.
// GPIO0 — реальный пин этой платы, поэтому 0 нельзя использовать как «нет пина».
//
// Deliberately defined OUTSIDE the __cplusplus guard, exactly as board_id.h keeps
// its YORADIO_BOARD_* macros outside: a board profile must be able to name "this
// line is not routed" in its Section 1 literals, which are preprocessor-visible by
// design, and a legacy compatibility macro derived from them must stay usable
// inside #if. kPinNotConnected below is the typed projection of this one value and
// must never restate the number.
// Намеренно вне блока __cplusplus, как и макросы YORADIO_BOARD_* в board_id.h:
// профиль платы должен уметь назвать «линия не разведена» в литералах секции 1,
// которые по замыслу видимы препроцессору, а производный legacy-макрос обязан
// оставаться пригодным внутри #if. kPinNotConnected ниже — типизированная проекция
// этого же значения и не вправе повторять число.
#define YORADIO_BP_PIN_NOT_CONNECTED 255

#ifdef __cplusplus

namespace yoradio {

constexpr uint8_t kPinNotConnected = YORADIO_BP_PIN_NOT_CONNECTED;

enum class SocTarget : uint8_t { Unknown = 0, Esp32S3, Esp32P4 };

// Any = the board works with either PSRAM mode; the build family decides.
// Any = плата работает с любым режимом PSRAM; выбор за build family.
enum class PsramMode : uint8_t { None = 0, Any, Quad, Octal };

// What the board NEEDS from the build family. This is a requirement to validate
// against PlatformIO/ESP-IDF configuration, never a copy of that configuration:
// partition layout, toolchain and sdkconfig stay build-family responsibility.
// Что плата ТРЕБУЕТ от build family. Это требование для проверки, а не копия
// конфигурации PlatformIO/ESP-IDF.
struct BuildRequirements {
    SocTarget soc;
    uint32_t  min_flash_bytes;
    bool      requires_psram;
    PsramMode psram_mode;
};

struct BoardIdentity {
    BoardId           id;
    const char*       name;
    const char*       revision;
    BuildRequirements build;
};

enum class PanelController  : uint8_t { Unknown = 0, St7701 };
enum class DisplayTransport : uint8_t { Unknown = 0, Rgb565Parallel16 };
enum class TouchController  : uint8_t { Unknown = 0, Gt911 };

// Factory display wiring. Panel recipe (timings, porches, init table) belongs to
// PanelMode and stays in the proven backend until S04; only wiring lives here.
// Заводская разводка дисплея. Рецепт панели (тайминги, порчи, init-таблица) —
// это PanelMode, он остаётся в проверенном backend до S04; здесь только разводка.
struct DisplayWiring {
    PanelController  controller;
    DisplayTransport transport;
    uint16_t         native_width;
    uint16_t         native_height;

    // Parallel data lines in the EXACT esp_lcd slot order the proven backend
    // programs: [0..4]=B0..B4, [5..10]=G0..G5, [11..15]=R0..R4. That order is
    // Arduino_GFX parity and must never be "tidied" (little-endian pin map).
    // Линии данных строго в том порядке слотов esp_lcd, который программирует
    // проверенный backend: [0..4]=B0..B4, [5..10]=G0..G5, [11..15]=R0..R4.
    uint8_t data_gpio[16];

    uint8_t hsync_gpio;
    uint8_t vsync_gpio;
    uint8_t de_gpio;
    uint8_t pclk_gpio;

    // 9-bit software SPI command bus used only during panel bring-up.
    // 9-битная программная SPI-шина команд, используется только при инициализации.
    uint8_t cmd_cs_gpio;
    uint8_t cmd_sck_gpio;
    uint8_t cmd_sda_gpio;

    // Single PWM backlight line; this board has no separate enable pin.
    // Одна PWM-линия подсветки; отдельного enable-пина на этой плате нет.
    uint8_t backlight_gpio;

    // Panel RESX. kPinNotConnected when the panel reset is not wired to the SoC.
    // Panel RESX. kPinNotConnected, если сброс панели не заведён на SoC.
    uint8_t panel_reset_gpio;
};

// Factory touch wiring. Geometry, rotation and calibration are NOT board truth
// in this struct: they stay in touch_geometry.h until S05.
// Заводская разводка тача. Геометрия, поворот и калибровка здесь не хранятся:
// они остаются в touch_geometry.h до S05.
struct TouchWiring {
    TouchController controller;
    uint8_t         i2c_sda_gpio;
    uint8_t         i2c_scl_gpio;
    uint8_t         int_gpio;
    uint8_t         reset_gpio;
    uint8_t         i2c_address;
};

// Presence of onboard audio hardware ONLY.
// The active output, the I2S pin assignment and the choice of an external DAC are
// deliberately absent: they are user policy, not factory truth. An onboard output
// that is physically present must never become a mandatory output.
// ТОЛЬКО наличие встроенного аудиооборудования.
// Активный выход, назначение I2S-пинов и выбор внешнего ЦАП здесь намеренно
// отсутствуют: это пользовательская политика, а не заводской факт.
struct AudioHardware {
    bool onboard_output_present;
    // true when enabling it needs physical rework (straps/resistors), so it can
    // never be selected by software alone.
    // true, когда включение требует аппаратной перекоммутации (перемычки).
    bool onboard_output_requires_hw_rework;
    bool onboard_output_is_mono;
    // Short factual note, e.g. which straps. No chip model is claimed.
    // Краткая фактическая заметка, например какие перемычки. Модель чипа не заявляется.
    const char* onboard_output_note;
};

// Onboard storage presence. Pin assignment is intentionally NOT a field: the
// 4848S040 SD lines are known only from comments/documentation and are validated
// on hardware in S07. Encoding them here would make unverified data authoritative.
// Наличие встроенного накопителя. Назначение пинов намеренно НЕ поле: линии SD
// известны только из комментариев и проверяются на железе в S07.
struct StorageHardware {
    bool onboard_sd_present;
};

// USB facts. SoC capability and stock wiring are separate statements: the pins are
// fixed by the SoC, while whether they are free depends on this board's wiring.
// Факты USB. Возможность SoC и заводская разводка — разные утверждения.
struct UsbHardware {
    bool    soc_has_native_usb;
    uint8_t soc_native_usb_dm_gpio;
    uint8_t soc_native_usb_dp_gpio;
    // false when stock wiring already uses those pins for something else.
    // Custom Wiring may free them; that does not change this factory fact.
    // false, когда заводская разводка уже занимает эти пины.
    bool    native_usb_pins_free_in_stock_wiring;
};

struct NetworkHardware {
    bool    native_wifi;
    bool    native_bluetooth;
    uint8_t antenna_select_gpio;
};

// Factory power/wake hardware. This records the button that physically exists;
// it is NOT a wake-source assignment — that is BoardDefaults/user policy.
// Заводское оборудование питания/пробуждения. Это описание физически имеющейся
// кнопки, а НЕ назначение источника пробуждения — оно в BoardDefaults/настройках.
struct PowerHardware {
    bool    battery_sense_present;
    bool    boot_button_present;
    uint8_t boot_button_gpio;
    bool    boot_button_active_low;
};

struct BoardProfile {
    BoardIdentity   identity;
    DisplayWiring   display;
    TouchWiring     touch;
    AudioHardware   audio;
    StorageHardware storage;
    UsbHardware     usb;
    NetworkHardware network;
    PowerHardware   power;
};

// ---------------------------------------------------------------------------
// BoardDefaults — tested YoRadio recommendations for one board.
// NOT physical truth and NOT a restriction: a value here does not ban a
// different user setting in myoptions.h. UI policy is still absent (S06).
// Touch servicing and display host recommendations are the defaults below.
//
// Three different layers, not one switch:
//   BoardDefaults.display — the tested recommendation.
//   User overrides — myoptions.h (YORADIO_RGB_BOUNCE10, YORADIO_RGB_FLASH_GUARD,
//     ST7701_BOOT_ORIENTATION_180). This struct does not read them.
//   Backend constraints — what the current driver can actually do. Not stored here.
//
// S04-2 records the display recommendation. Firmware still does not read
// kActiveBoardDefaults, so the live PCLK pair remains in the display backend
// until a later slice connects one effective policy.
// CONFIG_LCD_RGB_RESTART_IN_VSYNC stays a build-family sdkconfig switch.
// It is not a second knob in this struct.
// BoardDefaults — проверенные рекомендации YoRadio для одной платы.
// Это НЕ физическая правда и НЕ запрет на другую настройку в myoptions.h.
// Политика UI всё ещё отсутствует (S06). Рекомендации обслуживания тача и
// хоста дисплея находятся ниже.
//
// Три разных слоя: рекомендация, пользовательский override, ограничение backend.
// S04-2 только записывает рекомендацию дисплея. Прошивка kActiveBoardDefaults
// по-прежнему не читает, живая пара PCLK остаётся в backend.
// CONFIG_LCD_RGB_RESTART_IN_VSYNC остаётся настройкой build-family, не полем здесь.
// ---------------------------------------------------------------------------

// Recommended external I2S DAC pin assignment. Recommendation only.
// Рекомендуемое назначение пинов внешнего I2S ЦАП. Только рекомендация.
struct AudioDefaults {
    uint8_t i2s_dout_gpio;
    uint8_t i2s_bclk_gpio;
    uint8_t i2s_lrck_gpio;
    uint8_t i2s_mclk_gpio;
    // Recommend the external I2S output rather than any onboard path.
    // Рекомендовать внешний I2S-выход, а не встроенный тракт.
    bool    prefer_external_output;
};

struct PowerDefaults {
    // Recommended deep-sleep wake source. Fully user-overridable, including to
    // kPinNotConnected to disable GPIO wake.
    // Рекомендуемый источник пробуждения. Полностью переопределяем пользователем.
    uint8_t wake_gpio;
    bool    wake_active_low;
};

// Touch servicing recommendation only. Controller identity, address and pins
// remain physical/effective TouchWiring; geometry remains touch/display contract.
// Только рекомендация обслуживания тача. Controller/address/pins остаются в
// TouchWiring, geometry — в touch/display contract.
enum class TouchServiceMode : uint8_t { Polling = 0, Interrupt = 1 };

struct TouchDefaults {
    TouchServiceMode service_mode;
    uint32_t         i2c_clock_hz;
};

// Host servicing recommendation. Panel geometry stays in PanelMode / BoardProfile.
// Рекомендация обслуживания хоста. Геометрия панели остаётся в PanelMode / BoardProfile.
enum class DisplayRenderMode : uint8_t { Partial = 0, Direct = 1, Full = 2 };

enum class DisplayBufferMemory : uint8_t { Internal = 0, Psram = 1 };

// Who allocates the RGB scanout buffer. PanelDriver is the panel driver;
// on the proven path that driver is esp_lcd. No esp_lcd header is included here.
// Кто выделяет буфер RGB scanout. PanelDriver — драйвер панели; на проверенном
// пути это esp_lcd. Заголовок esp_lcd сюда не включается.
enum class RgbFramebufferOwner : uint8_t { PanelDriver = 0 };

// Partial CPU blit of the damaged rectangle, then a full-framebuffer C2M writeback.
// Частичный CPU-блит испорченного прямоугольника, затем полный C2M всего кадра.
enum class DisplayFlush : uint8_t { PartialBlitThenFullWriteback = 0 };

struct DisplayDefaults {
    DisplayRenderMode    render_mode;
    uint8_t              draw_buffer_count;
    uint16_t             partial_draw_lines;
    DisplayBufferMemory  draw_buffer_memory;
    uint8_t              rgb_framebuffer_count;
    DisplayBufferMemory  rgb_framebuffer_memory;
    RgbFramebufferOwner  rgb_framebuffer_owner;
    bool                 double_framebuffer;
    // Tested bounce default in pixels. Not a ban on YORADIO_RGB_BOUNCE10=1.
    // Проверенный bounce по умолчанию, в пикселях. Это не запрет YORADIO_RGB_BOUNCE10=1.
    uint16_t             bounce_buffer_px;
    DisplayFlush         flush;
    uint32_t             startup_pclk_hz;
    uint32_t             runtime_pclk_hz;
    uint32_t             flash_guard_pclk_hz;
    bool                 promote_pclk_once_on_boot_to_main;
};

struct BoardDefaults {
    AudioDefaults   audio;
    PowerDefaults   power;
    DisplayDefaults display;
    TouchDefaults   touch;
};

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_CONTRACT_H
