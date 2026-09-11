// ===============================================
// CONFIGURATION FOR ESP32-4848S040
// ===============================================
// Config for ESP32-4848S040 (ST7701 RGB 480x480 4.0")
// Display: 4.0" IPS LCD 480x480 ST7701S (square)
// Features:
// - ST7701S RGB Panel
// - Standard backlight (GPIO38)
// - I2S pins: DOUT=40, BCLK=1, LRC=2
//   Author: Witaliy76 - https://github.com/Witaliy76
#ifndef myoptions_h
#define myoptions_h

/* Audio library debug level / Уровень отладки аудиобиблиотеки */
/* 0=no debug, 1=error, 2=warn, 3=info, 4=debug, 5=verbose */
#define CORE_DEBUG_LEVEL 0

/* ===============================================
   DISPLAY
   =============================================== */
#define DSP_MODEL DSP_ST7701              // Display model ST7701

// Display pins ESP32-4848S040
// ST7701S command bus (SWSPI)
#define ST7701_CS   39       // Chip Select
#define ST7701_SCK  48       // Serial Clock
#define ST7701_SDA  47       // Serial Data
// Fixed panel orientation applied during ST7701 init, before the RGB stream starts.
// Runtime WebUI flip is not reliable on this panel; touch remains configured separately.
#define ST7701_BOOT_ORIENTATION_180 false
// RGB sync signals
#define ST7701_DE     18     // Data Enable
#define ST7701_VSYNC  17     // Vertical Sync
#define ST7701_HSYNC  16     // Horizontal Sync
#define ST7701_PCLK   21     // Pixel Clock
// RGB Data Pins (16-bit interface)
// Red data pins R0..R4 (LSB→MSB)
#define ST7701_R0   11       // R0 (DATA0)
#define ST7701_R1   12       // R1 (DATA1)
#define ST7701_R2   13       // R2 (DATA2)
#define ST7701_R3   14       // R3 (DATA3)
#define ST7701_R4   0        // R4 (DATA4)
// Green data pins G0..G5
#define ST7701_G0   8        // G0 (DATA5)
#define ST7701_G1   20       // G1 (DATA6)
#define ST7701_G2   3        // G2 (DATA7)
#define ST7701_G3   46       // G3 (DATA8)
#define ST7701_G4   9        // G4 (DATA9)
#define ST7701_G5   10       // G5 (DATA10)
// Blue data pins B0..B4 (LSB→MSB)
#define ST7701_B0   4        // B0 (DATA11)
#define ST7701_B1   5        // B1 (DATA12)
#define ST7701_B2   6        // B2 (DATA13)
#define ST7701_B3   7        // B3 (DATA14)
#define ST7701_B4   15       // B4 (DATA15)
// Backlight (standard)
#define ST7701_BL   38       // Backlight pin

/* ===============================================
   I2S DAC (Audio output)
   =============================================== */
#define I2S_DOUT     40      // I2S Data Out (GPIO40)
#define I2S_BCLK     1       // I2S Bit Clock (GPIO1)
#define I2S_LRC      2       // I2S Left/Right Clock (GPIO2)

/* ===============================================
   DISABLED FEATURES
   =============================================== */
#define VS1053_CS     255                 // Disable VS1053 (not used)

/* ===============================================
   PHYSICAL MUTE BUTTON (optional) - INPUT
   Momentary button to GND, internal pull-up (BTN_INTERNALPULLUP).
   255 disables the feature cleanly - no GPIO is claimed for the product build.
   Set to the GPIO actually wired on YOUR board before hardware testing.
   Кнопка MUTE на GND, внутренняя подтяжка. 255 выключает без побочных эффектов.
   Указать реальный GPIO вашей платы перед аппаратной проверкой.
   =============================================== */
#define BTN_MUTE      255                 // optional user input - not wired by default

/* ===============================================
   AMPLIFIER MUTE / ENABLE LINE (optional) - OUTPUT
   3.3 V GPIO output, driven by Player::setOutputPins() (see options.h for the polarity
   contract - MUTE_VAL/MUTE_LOCK are unchanged by this task). MUTE_VAL is the level written
   when the amplifier must be silent: stopped, connecting, or PLAYING + semantic MUTE.
   255 disables the feature cleanly - no GPIO is claimed, no digitalWrite() is issued.
   Not the same input as BTN_MUTE above - this is an OUTPUT to external amp hardware.
   3.3-В GPIO выход, управляется Player::setOutputPins(). MUTE_VAL - уровень, при котором
   усилитель должен молчать: стоп, подключение, либо PLAYING + семантический MUTE.
   255 выключает без побочных эффектов - GPIO не занимается, digitalWrite() не вызывается.
   Не путать со входом BTN_MUTE выше - это ВЫХОД на внешний усилитель.
   =============================================== */
#define MUTE_PIN      255                 // amplifier-enable output - not wired by default

/* ===============================================
   DEEP-SLEEP WAKE PIN (optional) - INPUT
   Compile-time integrator option. Not a Settings control and not a runtime button.
   255 disables GPIO wake; existing sleep-timer deep sleep is unchanged.
   ESP32-S3 EXT0 requires an RTC GPIO (0-21). This 4848S040 RGB/I2S/touch map occupies
   that range, so the production default stays 255. Do not pick a pin that is driven
   after boot, and do not use GPIO0 with WAKE_LEVEL=LOW (held LOW through reset can
   enter download mode). External pull required: pull-up for LOW, pull-down for HIGH.
   Опция платы на этапе компиляции. Не пункт Settings и не runtime-кнопка.
   255 выключает GPIO-wake; текущий deep sleep по sleep timer не меняется.
   ESP32-S3 EXT0 — только RTC GPIO 0-21. На 4848S040 этот диапазон занят RGB/I2S/touch,
   поэтому production-значение 255. Не брать пин, который после boot снова станет выходом,
   и не использовать GPIO0 + WAKE_LEVEL=LOW. Нужна внешняя подтяжка: вверх для LOW,
   вниз для HIGH.
   =============================================== */
#define WAKE_PIN      255                 // deep-sleep wake GPIO - disabled by default
#define WAKE_LEVEL    LOW                 // LOW or HIGH; LOW is the historical default

/* ===============================================
   BRIGHTNESS CONTROL
   =============================================== */
#define BRIGHTNESS_PIN 255                // Don't use standard brightness pin
#define ENABLE_BRIGHTNESS_CONTROL         // Enable brightness control in web interface

/* ===============================================
   GENERAL SETTINGS
   =============================================== */
#define PLAYER_FORCE_MONO false           // Mono mode (disabled)

// Compile-time interface language: RU, EN, PL or SK.
// Язык интерфейса во время компиляции: RU, EN, PL или SK.
#define L10N_LANGUAGE RU

// Block 8.1F-B: config.store.vumeter / usespectrum kept for future LVGL widgets (WebUI toggles).
// Block 8.1F-B: config.store.vumeter / usespectrum — будущие LVGL-виджеты (переключатели WebUI).
#define EXT_WEATHER       false           // Extended weather (network.cpp)

/* ===============================================
   SYSTEM & DEBUG
   =============================================== */
#define BATTERY_OFF                       // Disable battery display
#define WDT_TIMEOUT 30                    // Watchdog timeout (seconds)
/* MemWatchdog: автоперезапуск при деградации RAM (TLS). Отключить: закомментировать. */
#define MEM_WATCHDOG_AUTOREBOOT

/* ===============================================
   SD CARD
   =============================================== */
// SD card support DISABLED for ESP32-4848S040
// SD card did not work properly on this board
// SD pins according to ESP32-4848S040 schematic:
//   io42 - TF(D3) - Chip Select
//   io47 - SPICLK_P - MOSI (Master Out Slave In)
//   io48 - SPICLK_N - SCK (Clock)
//   io41 - TF(D1) - MISO (Master In Slave Out)
// Note: SD card functionality is not implemented for this board
//#define USE_SD                              // SD card support disabled
//#define SDC_CS        42                    // Chip Select
//#define SD_SCK        48                    // SCK pin
//#define SD_MISO       41                    // MISO pin
//#define SD_MOSI       47                    // MOSI pin
//#define SD_HSPI       true                  // Use HSPI to avoid conflicts
//#define SD_DEBUG_ENABLED true               // SD card debug
//#define SDSPISPEED    20000000              // SPI speed (20 MHz)

/* ===============================================
   TOUCHSCREEN
   =============================================== */
// ESP32-4848S040 uses GT911 Capacitive I2C
#define TS_MODEL              TS_MODEL_GT911   // GT911 Capacitive I2C
#define TS_SDA                19               // GPIO19
#define TS_SCL                45               // GPIO45
#define TS_INT                255              // Not used
#define TS_RST                255              // Not used

// Touchscreen calibration (coordinates):
#define TS_X_MIN              0                  // X minimum
#define TS_X_MAX              480                // X maximum
#define TS_Y_MIN              0                  // Y minimum
#define TS_Y_MAX              480                // Y maximum


/* ===============================================
   DISPLAY OPTIONS
   =============================================== */
// Block 8-E5C: carousel PageChain slide (MOVE_LEFT/RIGHT). Boot fade unchanged.
// Block 8-E5C: slide-анимация карусели при свайпе. 0 = мгновенно (partial 4848); 1 = slide 300 ms.
// Future Settings / ESP32-P4 may enable 1 at runtime; not stored in NVS yet.
// Будущие Settings / быстрые платы — runtime; в NVS пока не сохраняется.
#define YORADIO_LVGL_PAGE_TRANSITION_ANIM_DEFAULT 0

// 1: paged Station list (accepted production renderer); 0/undefined: legacy continuous scroll.
// 1: постраничный список Station (принятый production renderer); 0/не задано: старый непрерывный скролл.
#define STATION_LIST_SIMPLE_PAGED 1

// Day of week uppercase (true = uppercase, false = lowercase)
//#define DOW_UPPERCASE true                     // Uncomment and set to true/false if needed

/* ===============================================
   DEBUG
   =============================================== */
//#define DEBUG_DISPLAY                          // Display debug

/* AI Layer debug logs / Логи отладки AI Layer */
// 0 = release (only important logs), 1 = debug (all logs)
// 0 = релиз (только важные логи), 1 = отладка (все логи)
#define AI_LAYER_DEBUG 0

#endif
