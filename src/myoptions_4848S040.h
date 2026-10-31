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

/* ===============================================
   BOARD SELECTION / ВЫБОР ПЛАТЫ
   Concrete board identity. The PlatformIO environment selects the build family
   (SoC, flash/PSRAM class, toolchain, partitions); this line selects the board.
   Known ids: src/src/platform/board/board_id.h
   Идентичность конкретной платы. Окружение PlatformIO задаёт build family
   (SoC, класс flash/PSRAM, тулчейн, разделы); эта строка задаёт плату.
   =============================================== */
#define YORADIO_BOARD YORADIO_BOARD_4848S040

/* Audio library debug level / Уровень отладки аудиобиблиотеки */
/* 0=no debug, 1=error, 2=warn, 3=info, 4=debug, 5=verbose */
#define CORE_DEBUG_LEVEL 0

/* ===============================================
   DISPLAY
   =============================================== */
#define DSP_MODEL DSP_ST7701              // Display model ST7701

// Display / touch GPIO of the selected board come from its BoardProfile.
// To re-pin any of the 28 lines, use the CUSTOM WIRING section below.
// GPIO дисплея/тача выбранной платы задаёт BoardProfile.
// Переназначение любого из 28 линий — в разделе CUSTOM WIRING ниже.

// Fixed panel orientation applied during ST7701 init, before the RGB stream starts.
// Runtime WebUI flip is not reliable on this panel; touch remains configured separately.
// This is display POLICY, not one of the 24 physical GPIO, so it stays a user option.
#define ST7701_BOOT_ORIENTATION_180 false

// ============================================================
// CUSTOM WIRING — GPIO overrides
// ============================================================
// 0 = заводская разводка выбранной платы
// 1 = разрешить индивидуальные переопределения GPIO
//
// Для изменения пина:
// 1. Установите YORADIO_CUSTOM_WIRING = 1
// 2. Раскомментируйте нужную строку
// 3. Укажите свой GPIO
//
// Значения ниже — штатные GPIO платы ESP32-S3 4848S040.
// Для других плат заводские значения определяются их профилем.
// I2S_DOUT/BCLK/LRC, WAKE_PIN, MUTE_PIN, IR и SD здесь не дублируются —
// они остаются в своих разделах myoptions.h.
// ============================================================

#define YORADIO_CUSTOM_WIRING 0

// RGB DISPLAY — BLUE
// #define DISP_B0      4
// #define DISP_B1      5
// #define DISP_B2      6
// #define DISP_B3      7
// #define DISP_B4     15

// RGB DISPLAY — GREEN
// #define DISP_G0      8
// #define DISP_G1     20
// #define DISP_G2      3
// #define DISP_G3     46
// #define DISP_G4      9
// #define DISP_G5     10

// RGB DISPLAY — RED
// #define DISP_R0     11
// #define DISP_R1     12
// #define DISP_R2     13
// #define DISP_R3     14
// #define DISP_R4      0

// RGB DISPLAY — SYNCHRONIZATION
// #define DISP_HSYNC  16
// #define DISP_VSYNC  17
// #define DISP_DE     18
// #define DISP_PCLK   21

// DISPLAY — COMMAND BUS / BACKLIGHT
// #define DISP_CS     39
// #define DISP_SCK    48
// #define DISP_SDA    47
// #define DISP_BL     38

// GT911 TOUCH
// #define TOUCH_SDA   19
// #define TOUCH_SCL   45
// #define TOUCH_INT  255  // Not connected
// #define TOUCH_RST  255  // Not connected

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
   contract - MUTE_VAL/MUTE_LOCK). MUTE_VAL is the level written when the amplifier must
   be silent: stopped, connecting, or PLAYING + semantic MUTE.
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
   ESP32-4848S040: the stock BOOT button sits on GPIO0 and shorts it to GND, so the
   board wakes on WAKE_PIN=0 with WAKE_LEVEL=LOW - no extra wiring and no external pull.
   GPIO0 is shared with the RGB panel as ST7701_R4, so it is only safe to press BOOT
   once the device has fully entered Deep Sleep (display and backlight already off).
   Press BOOT briefly and release; after wake sleep_wakeup_early_init() returns the pin
   from RTC IO to digital GPIO before display init. Do NOT press BOOT while the display
   is running, and do NOT hold BOOT through a hardware Reset - that enters download mode.
   Other boards: pick a free RTC GPIO (ESP32-S3: 0-21) with a stable hardware pull -
   pull-up for WAKE_LEVEL=LOW, pull-down for WAKE_LEVEL=HIGH.
   Опция платы на этапе компиляции. Не пункт Settings и не runtime-кнопка.
   255 выключает GPIO-wake; текущий deep sleep по sleep timer не меняется.
   ESP32-4848S040: штатная кнопка BOOT сидит на GPIO0 и замыкает его на GND, поэтому
   пробуждение работает при WAKE_PIN=0 и WAKE_LEVEL=LOW - без доп. проводов и подтяжки.
   GPIO0 одновременно используется RGB-панелью как ST7701_R4, поэтому нажимать BOOT
   можно только после полного входа в Deep Sleep (экран и подсветка уже выключены).
   Нажать BOOT коротко и отпустить; после пробуждения sleep_wakeup_early_init()
   возвращает пин из RTC IO в digital GPIO до инициализации дисплея. НЕ нажимать
   BOOT при работающем дисплее и НЕ удерживать его во время аппаратного Reset -
   это включает download mode.
   Другие платы: взять свободный RTC GPIO (ESP32-S3: 0-21) со стабильной аппаратной
   подтяжкой: вверх для WAKE_LEVEL=LOW, вниз для WAKE_LEVEL=HIGH.
   =============================================== */
#define WAKE_PIN      0                   // BOOT button on ESP32-4848S040; 255 disables GPIO wake
#define WAKE_LEVEL    LOW                 // LOW or HIGH; BOOT shorts GPIO0 to GND -> LOW

/* ===============================================
   BRIGHTNESS CONTROL
   =============================================== */
#define BRIGHTNESS_PIN 255                // Don't use standard brightness pin
#define ENABLE_BRIGHTNESS_CONTROL         // Enable brightness control in web interface

/* ===============================================
   GENERAL SETTINGS
   =============================================== */
#define PLAYER_FORCE_MONO false           // Mono mode (disabled)
#define L10N_LANGUAGE RU                  // Interface language (Russian)
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

// GT911 pins are owned by the BoardProfile. Re-pin them in CUSTOM WIRING below
// with TOUCH_SDA / TOUCH_SCL / TOUCH_INT / TOUCH_RST. Do not redefine TS_* here.
// TS_MODEL stays the touch-driver selector (not wiring).
// Пины GT911 принадлежат BoardProfile. Переназначение — в CUSTOM WIRING ниже
// именами TOUCH_*. Не определяйте TS_* здесь. TS_MODEL — селектор драйвера.

// Touchscreen calibration (coordinates):
#define TS_X_MIN              0                  // X minimum
#define TS_X_MAX              480                // X maximum
#define TS_Y_MIN              0                  // Y minimum
#define TS_Y_MAX              480                // Y maximum


/* ===============================================
   DISPLAY OPTIONS
   =============================================== */
// RGB panel bounce buffers (ESP32-S3 RGB transport):
//   1 = use two INTERNAL RGB565 bounce buffers, each panel_width x 10 pixels.
//       This improves scanout tolerance to short PSRAM bandwidth stalls, but
//       reserves about 19.2 KB of INTERNAL RAM on a 480-pixel-wide panel.
//   0 = disable bounce buffers and let RGB DMA read the framebuffer from PSRAM.
#ifndef YORADIO_RGB_BOUNCE10
#define YORADIO_RGB_BOUNCE10 0
#endif

// RGB flash guard (direct RGB without bounce buffers):
//   1 = before SaveManager writes settings to NVS, lower the RGB pixel clock
//       10 -> 6 MHz, write only after the change is confirmed, then return to
//       10 MHz after a short pause. Prevents the horizontal picture shift that
//       flash writes can cause. Prints [RGB-GUARD] / [SAVE] lines on Serial.
//   0 = write NVS at 10 MHz (diagnostic comparison only).
// Bounce buffers (YORADIO_RGB_BOUNCE10 1) bypass the guard.
#ifndef YORADIO_RGB_FLASH_GUARD
#define YORADIO_RGB_FLASH_GUARD 1
#endif

// DspTask stack size:
//   1 = 12 KB stack for additional runtime headroom.
//   0 = historical 10 KB stack.
// This option does not change the task core, priority, period, or entry point.
#ifndef YORADIO_DSPTASK_12K
#define YORADIO_DSPTASK_12K 1
#endif

// AsyncTCP worker stack size:
//   1 = accepted measured 16 KB stack; 0 = historical 32 KB fallback.
#ifndef YORADIO_ASYNCTCP_STACK_16K
#define YORADIO_ASYNCTCP_STACK_16K 1
#endif

// Block 8-E5C: carousel PageChain slide (MOVE_LEFT/RIGHT). Boot fade unchanged.
// Block 8-E5C: slide-анимация карусели при свайпе. 0 = мгновенно (partial 4848); 1 = slide 300 ms.
// Future Settings / ESP32-P4 may enable 1 at runtime; not stored in NVS yet.
// Будущие Settings / быстрые платы — runtime; в NVS пока не сохраняется.
// Не используется на 4848S040, но остаётся для совместимости.
#define YORADIO_LVGL_PAGE_TRANSITION_ANIM_DEFAULT 0

// 1: lightweight paged Station list; 0/undefined: legacy continuous scroll
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
#define YORADIO_PPM_PCM_TELEMETRY_DIAG 0
#endif
