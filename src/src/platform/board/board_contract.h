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
// NOT physical truth and NOT a restriction: every value here is overridable by
// the user in myoptions.h. Nothing in this struct may be treated as a hardware
// constraint. Display/touch/UI servicing policy has no safe owner yet and is
// deliberately absent (deferred to S04/S05/S06); the S01 PCLK constants stay in
// the display backend and are not duplicated here.
// BoardDefaults — проверенные рекомендации YoRadio для одной платы.
// Это НЕ физическая правда и НЕ запрет: любое значение переопределяемо в
// myoptions.h. Политика обслуживания дисплея/тача/UI пока без владельца и
// намеренно отсутствует (S04/S05/S06); константы PCLK S01 здесь не дублируются.
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

struct BoardDefaults {
    AudioDefaults audio;
    PowerDefaults power;
};

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_CONTRACT_H
