// Immutable physical profile of the VIEWE UEDX48480040E-WB-A
// (ESP32-S3 N16R8, GC9503 480x480 RGB, FT6336U touch).
// Неизменяемый физический профиль VIEWE UEDX48480040E-WB-A
// (ESP32-S3 N16R8, GC9503 480x480 RGB, тач FT6336U).
// Ownership: platform/board. Selected by board_select.h; never included directly by
// subsystems. Must not #define or #undef any user-configurable macro.
// Владелец: platform/board. Выбирается board_select.h; подсистемы его не включают.
//
// Evidence: vendor schematic UEDX48480040E-WB-A V1.3 (SCHEMATIC CONFIRMED for every
// GPIO below). Not yet device-verified on YoRadio (S07_bis-8/-9).
// Доказательства: схема вендора V1.3 (каждый GPIO ниже — SCHEMATIC CONFIRMED).
// На YoRadio ещё не проверено на устройстве (S07_bis-8/-9).
#ifndef YORADIO_PLATFORM_BOARD_PROFILE_ESP32S3_UEDX48480040_H
#define YORADIO_PLATFORM_BOARD_PROFILE_ESP32S3_UEDX48480040_H

#include "../../board_id.h"
#include "../../board_contract.h"

// ---------------------------------------------------------------------------
// Section 1 — preprocessor-visible facts that are not driver GPIO.
// Секция 1 — факты для препроцессора, не GPIO драйверов.
// ---------------------------------------------------------------------------
#define YORADIO_BP_UEDX_MIN_FLASH_BYTES  (16u * 1024u * 1024u)
#define YORADIO_BP_UEDX_DISP_W           480
#define YORADIO_BP_UEDX_DISP_H           480
// FT6336U, FocalTech FT5x06 register map, fixed I2C address 0x38.
#define YORADIO_BP_UEDX_TOUCH_ADDR       0x38
#define YORADIO_BP_UEDX_HAS_TOUCH        1
#define YORADIO_BP_UEDX_BOOT_BTN_GPIO    0
#define YORADIO_BP_UEDX_USB_DM_GPIO      19
#define YORADIO_BP_UEDX_USB_DP_GPIO      20

// ---------------------------------------------------------------------------
// Section 2 — constexpr typed view
// Секция 2 — типизированный constexpr-вид
// ---------------------------------------------------------------------------
#ifdef __cplusplus

namespace yoradio {

inline constexpr BoardProfile kBoardProfile_Esp32S3_Uedx48480040 = {
    .identity = {
        .id       = BoardId::Esp32S3_Uedx48480040,
        .name     = "VIEWE UEDX48480040E-WB-A",
        .revision = "GC9503 480x480 RGB panel, FT6336U touch",
        .build    = {
            .soc             = SocTarget::Esp32S3,
            .min_flash_bytes = YORADIO_BP_UEDX_MIN_FLASH_BYTES,
            .requires_psram  = true,
            // N16R8 module: octal PSRAM carries the RGB framebuffer and the LVGL pool.
            // Модуль N16R8: на octal PSRAM лежат RGB-фреймбуфер и пул LVGL.
            .psram_mode      = PsramMode::Octal,
        },
    },

    .display = {
        .controller    = PanelController::Gc9503,
        .transport     = DisplayTransport::Rgb565Parallel16,
        .native_width  = YORADIO_BP_UEDX_DISP_W,
        .native_height = YORADIO_BP_UEDX_DISP_H,
        // Slot order is load-bearing: [0..4]=B0..B4, [5..10]=G0..G5, [11..15]=R0..R4.
        // Nets IO15-B0 .. IO0-R4 on the V1.3 schematic (vendor pin table).
        // Порядок слотов критичен; цепи IO15-B0 .. IO0-R4 по схеме V1.3.
        .data_gpio = {
            15,  // B0
            14,  // B1
            13,  // B2
            12,  // B3
            11,  // B4
            10,  // G0
            9,   // G1
            8,   // G2
            7,   // G3
            6,   // G4
            5,   // G5
            4,   // R0
            3,   // R1
            2,   // R2
            1,   // R3
            0,   // R4  GPIO0 is also the stock BOOT button (as on 4848S040)
        },
        .hsync_gpio       = 16,  // HSYNC
        .vsync_gpio       = 17,  // VSYNC
        .de_gpio          = 18,  // DE
        .pclk_gpio        = 21,  // PCLK
        .cmd_cs_gpio      = 39,  // command CS
        .cmd_sck_gpio     = 48,  // command SCK; recommended I2S WS after init (see .shared)
        .cmd_sda_gpio     = 47,  // command SDA; also TF CS through R17 0R (see .shared)
        .backlight_gpio   = 38,  // LCD-BL-EN -> LP3321 PWM
        // LCM RESET is a passive RC (R27/C14), not routed to the ESP32-S3.
        // LCM RESET — пассивная RC-цепь, на ESP32-S3 не заведена.
        .panel_reset_gpio = kPinNotConnected,
    },

    .touch = {
        .controller   = TouchController::Ft5x06,
        .i2c_sda_gpio = 40,  // SDA, 4.7k pull-up R5
        .i2c_scl_gpio = 41,  // SCL, 4.7k pull-up R6
        // INT is not routed; TP_RST goes to LCM-RESET through R32 0R, not to the ESP.
        // INT не разведён; TP_RST идёт на LCM-RESET через R32, не на ESP.
        .int_gpio     = kPinNotConnected,
        .reset_gpio   = kPinNotConnected,
        .i2c_address  = YORADIO_BP_UEDX_TOUCH_ADDR,
    },

    .audio = {
        // No onboard audio output; sound needs an external I2S DAC (BoardDefaults).
        // Встроенного аудиовыхода нет; звук — через внешний I2S ЦАП (BoardDefaults).
        .onboard_output_present             = false,
        .onboard_output_requires_hw_rework  = false,
        .onboard_output_is_mono             = false,
        .onboard_output_note                = "no onboard audio output; external I2S DAC",
    },

    .storage = {
        // TF slot, SPI mode, from the V1.3 schematic; not measured: SchematicCandidate.
        // CS (DAT3) is the panel command SDA line through R17 0R (see .shared); CLK 45
        // and DAT0 46 are strapping pins with the vendor's own pulls; CMD 42 also
        // drives the WS2812 DIN through D4. SD-DET is not routed: no CD GPIO.
        // DAT1/DAT2 are not routed to the ESP32-S3: no 4-bit SDMMC.
        // Слот TF в режиме SPI по схеме V1.3, без измерений: SchematicCandidate.
        // CS — линия SDA команд панели через R17 (см. .shared); CLK 45 и DAT0 46 —
        // strapping-пины; CMD 42 также идёт на DIN WS2812. CD-пина нет; 4-bit нет.
        .onboard_sd_present     = true,
        .sd_pin_evidence        = PinEvidence::SchematicCandidate,
        .sd_clk_gpio            = 45,
        .sd_cmd_gpio            = 42,
        .sd_dat0_gpio           = 46,
        .sd_cs_gpio             = 47,
        .sd_card_detect_present = false,
        .sd_supports_sdmmc_4bit = false,
    },

    .usb = {
        .soc_has_native_usb     = true,
        .soc_native_usb_dm_gpio = YORADIO_BP_UEDX_USB_DM_GPIO,
        .soc_native_usb_dp_gpio = YORADIO_BP_UEDX_USB_DP_GPIO,
        // The board routes GPIO19/20 only to the "USB" connector and J1: free in stock
        // wiring. VBUS2 goes in through D2, so that port cannot supply 5 V (no OTG host
        // power). YoRadio's default I2S uses 19/20 (BoardDefaults), so leave that port
        // unplugged; logs and flashing use the CH340 "UART" port.
        // GPIO19/20 разведены только на разъём «USB» и J1 — в заводской разводке свободны.
        // VBUS2 идёт через D2: 5 В на разъём не подаётся. I2S по умолчанию занимает 19/20,
        // поэтому этот разъём не подключать; логи и прошивка — через CH340 («UART»).
        .native_usb_pins_free_in_stock_wiring = true,
    },

    .network = {
        .native_wifi         = true,
        .native_bluetooth    = true,
        .antenna_select_gpio = kPinNotConnected,
    },

    .power = {
        .battery_sense_present  = false,
        // K1 shorts GPIO0 to GND (pull-up R18); GPIO0 is also RGB R4, as on 4848S040.
        // K1 замыкает GPIO0 на GND; GPIO0 одновременно RGB R4, как на 4848S040.
        .boot_button_present    = true,
        .boot_button_gpio       = YORADIO_BP_UEDX_BOOT_BTN_GPIO,
        .boot_button_active_low = true,
    },

    // Lines with more than one role. The panel owns the 3-wire command bus only while
    // its init recipe runs (before scanout); afterwards:
    //   GPIO47 SDA -> TF CS (wired through R17 0R; SD uses it after the panel init);
    //   GPIO48 SCK -> I2S WS (board default wiring of the external DAC, UEDX-0 decision).
    // Command CS (39) is never shared.
    // Линии с несколькими ролями. Панель владеет 3-wire шиной только пока исполняется
    // её рецепт (до scanout); затем GPIO47 — CS карты, GPIO48 — WS I2S. CS 39 не делится.
    .shared = {
        .pins = {
            { .gpio = 47, .kind = ResourceShare::LifecycleShared,
              .owner = PinRole::DisplayCommandSda, .followers = pinRoleBit(PinRole::SdCs) },
            { .gpio = 48, .kind = ResourceShare::LifecycleShared,
              .owner = PinRole::DisplayCommandSck, .followers = pinRoleBit(PinRole::I2sWs) },
        },
        .count = 2,
    },
};

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_PROFILE_ESP32S3_UEDX48480040_H
