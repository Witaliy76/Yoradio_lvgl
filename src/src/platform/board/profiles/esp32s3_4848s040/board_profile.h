// Immutable physical profile of the ESP32-S3 4848S040 (ST7701 480x480 RGB, GT911).
// Неизменяемый физический профиль ESP32-S3 4848S040 (ST7701 480x480 RGB, GT911).
// Ownership: platform/board. Selected by board_select.h; never included directly by
// subsystems. Must not #define or #undef any user-configurable macro.
// Владелец: platform/board. Выбирается board_select.h; подсистемы его напрямую не
// включают. Не должен определять или отменять пользовательские макросы.
//
// S02-5: the 28 migrated GPIO numbers (24 display + 4 touch) live only in the
// constexpr BoardProfile below. They are not preprocessor macros, and they are
// not copied into a second public macro set. The signal name sits in a comment
// beside each RGB value. Drivers do not read this struct directly: board_select.h
// publishes kWiring (identical to this profile while Custom Wiring is absent),
// and compat/legacy_macros.h projects kWiring onto the temporary ST7701_* / TS_* names.
// S02-5: 28 перенесённых GPIO живут только в constexpr BoardProfile ниже.
// Это не макросы препроцессора и не второй публичный набор имён. Имя сигнала
// стоит в комментарии рядом со значением. Драйверы читают kWiring через временные
// имена ST7701_* / TS_*.
#ifndef YORADIO_PLATFORM_BOARD_PROFILE_ESP32S3_4848S040_H
#define YORADIO_PLATFORM_BOARD_PROFILE_ESP32S3_4848S040_H

#include "../../board_id.h"
#include "../../board_contract.h"

// ---------------------------------------------------------------------------
// Section 1 — facts that are not the 28 migrated driver GPIO.
// The 28 GPIO numbers themselves are only in the section 2 initializer.
// Секция 1 — факты вне 28 GPIO драйверов. Сами эти 28 чисел — только
// в инициализаторе секции 2.
// ---------------------------------------------------------------------------

// N16R8 module: 16 MB flash, 8 MB octal PSRAM. The production partition table
// partition_16MB_ota_largefs.csv does not fit a smaller part.
// Модуль N16R8: 16 МБ flash, 8 МБ octal PSRAM. Боевая таблица разделов меньше не влезает.
#define YORADIO_BP_4848S040_MIN_FLASH_BYTES  (16u * 1024u * 1024u)

// Non-GPIO facts still named here. The 28 migrated GPIO are not: their only
// numeric source is the BoardProfile initializer in section 2.
// Здесь остаются факты, которые не являются теми 28 GPIO. Сами 28 GPIO
// записаны один раз в инициализаторе BoardProfile в секции 2.
#define YORADIO_BP_4848_DISP_W         480
#define YORADIO_BP_4848_DISP_H         480
#define YORADIO_BP_4848_TOUCH_ADDR     0x5D

// --- Power: stock BOOT button, hardwired to GND ---
#define YORADIO_BP_4848_BOOT_BTN_GPIO   0

// --- USB: pin identity is fixed by the ESP32-S3 itself, not by this board ---
#define YORADIO_BP_4848_USB_DM_GPIO    19
#define YORADIO_BP_4848_USB_DP_GPIO    20

// ---------------------------------------------------------------------------
// Section 2 — constexpr typed view
// Секция 2 — типизированный constexpr-вид
// ---------------------------------------------------------------------------

#ifdef __cplusplus

namespace yoradio {

inline constexpr BoardProfile kBoardProfile_Esp32S3_4848S040 = {
    .identity = {
        .id       = BoardId::Esp32S3_4848S040,
        .name     = "ESP32-S3 4848S040",
        .revision = "ST7701 480x480 RGB panel, GT911 touch",
        .build    = {
            .soc             = SocTarget::Esp32S3,
            .min_flash_bytes = YORADIO_BP_4848S040_MIN_FLASH_BYTES,
            .requires_psram  = true,
            // Octal (OPI) PSRAM: the module is qio_opi and the RGB framebuffer
            // plus the LVGL PSRAM pool depend on that bandwidth.
            // Octal (OPI) PSRAM: модуль qio_opi, RGB-фреймбуфер и пул LVGL
            // рассчитаны на эту пропускную способность.
            .psram_mode      = PsramMode::Octal,
        },
    },

    .display = {
        .controller    = PanelController::St7701,
        .transport     = DisplayTransport::Rgb565Parallel16,
        .native_width  = YORADIO_BP_4848_DISP_W,
        .native_height = YORADIO_BP_4848_DISP_H,
        // Slot order is load-bearing: [0..4]=B0..B4, [5..10]=G0..G5, [11..15]=R0..R4.
        // Порядок слотов критичен: [0..4]=B0..B4, [5..10]=G0..G5, [11..15]=R0..R4.
        .data_gpio = {
            4,   // B0
            5,   // B1
            6,   // B2
            7,   // B3
            15,  // B4
            8,   // G0
            20,  // G1  GPIO20 is also native USB DP; stock wiring uses it as G1
            3,   // G2
            46,  // G3
            9,   // G4
            10,  // G5
            11,  // R0
            12,  // R1
            13,  // R2
            14,  // R3
            0,   // R4  GPIO0 is also the stock BOOT button
        },
        .hsync_gpio       = 16,  // HSYNC
        .vsync_gpio       = 17,  // VSYNC
        .de_gpio          = 18,  // DE
        .pclk_gpio        = 21,  // PCLK
        .cmd_cs_gpio      = 39,  // command CS
        .cmd_sck_gpio     = 48,  // command SCK; also the documented SD SCK line
        .cmd_sda_gpio     = 47,  // command SDA; also the documented SD MOSI line
        .backlight_gpio   = 38,  // BL, one PWM line, no separate enable pin
        // Panel RESX is not routed to the ESP32-S3 on this board: the proven
        // backend configures disp_gpio_num = GPIO_NUM_NC and never drives a reset.
        // RESX панели на этой плате не заведён на ESP32-S3.
        .panel_reset_gpio = kPinNotConnected,
    },

    .touch = {
        .controller   = TouchController::Gt911,
        .i2c_sda_gpio = 19,  // SDA; GPIO19 is also native USB DM, used here as touch SDA
        .i2c_scl_gpio = 45,  // SCL
        // Neither INT nor RST is routed, so the driver polls. The value is the
        // contract sentinel, not a second spelling of 255. TAMC_GT911.cpp skips
        // its GPIO reset when both pins compare unequal to 255.
        // Ни INT, ни RST не разведены, драйвер работает опросом. Значение — сентинел
        // контракта. TAMC_GT911.cpp пропускает GPIO-сброс, если оба пина не равны 255.
        .int_gpio     = kPinNotConnected,  // INT
        .reset_gpio   = kPinNotConnected,  // RST
        .i2c_address  = YORADIO_BP_4848_TOUCH_ADDR,
    },

    .audio = {
        // A mono onboard path physically exists but is reached only by moving
        // hardware straps, so software can never select it. The external I2S
        // output remains the configurable, recommended path (BoardDefaults).
        // No chip model, control pin or further schematic is claimed here,
        // because none is confirmed by source or board documentation.
        // Встроенный моно-тракт физически есть, но включается только аппаратными
        // перемычками, поэтому программно его выбрать нельзя. Модель чипа,
        // control-пин и прочая схема здесь не заявляются — они не подтверждены.
        .onboard_output_present             = true,
        .onboard_output_requires_hw_rework  = true,
        .onboard_output_is_mono             = true,
        .onboard_output_note                = "mono path enabled by R21/R22/R23 straps; not software-selectable",
    },

    .storage = {
        // Onboard microSD slot exists. Its pin assignment is deliberately not a
        // field of this profile: see StorageHardware and S07.
        // Слот microSD на плате есть. Назначение пинов намеренно не поле профиля.
        .onboard_sd_present = true,
    },

    .usb = {
        // The SoC has native USB; the pin numbers are an ESP32-S3 fact.
        // У SoC есть native USB; номера пинов — факт ESP32-S3.
        .soc_has_native_usb    = true,
        .soc_native_usb_dm_gpio = YORADIO_BP_4848_USB_DM_GPIO,
        .soc_native_usb_dp_gpio = YORADIO_BP_4848_USB_DP_GPIO,
        // Stock wiring occupies both: GPIO19 is the touch I2C SDA and GPIO20 is
        // RGB G1. So native USB is unavailable with the factory wiring, which is
        // NOT the same as the SoC lacking USB. Custom Wiring could free them.
        // Заводская разводка занимает оба: GPIO19 — SDA тача, GPIO20 — RGB G1.
        // Это не то же самое, что отсутствие USB у SoC.
        .native_usb_pins_free_in_stock_wiring = false,
    },

    .network = {
        .native_wifi        = true,
        .native_bluetooth   = true,
        // No antenna-selection GPIO and no Ethernet PHY exist on this board.
        // На плате нет GPIO выбора антенны и нет Ethernet PHY.
        .antenna_select_gpio = kPinNotConnected,
    },

    .power = {
        // No battery sense circuit on this board.
        .battery_sense_present  = false,
        // Stock BOOT button, shorting GPIO0 to GND. Recording the button is a
        // factory fact; using it as a wake source is BoardDefaults/user policy.
        // GPIO0 is simultaneously RGB R4 above — a real lifecycle-shared pin,
        // already handled by sleep_wakeup_early_init() before display bring-up.
        // Штатная кнопка BOOT замыкает GPIO0 на GND. Фиксация кнопки — заводской
        // факт; её использование как источника пробуждения — политика.
        .boot_button_present    = true,
        .boot_button_gpio       = YORADIO_BP_4848_BOOT_BTN_GPIO,
        .boot_button_active_low = true,
    },
};

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_PROFILE_ESP32S3_4848S040_H
