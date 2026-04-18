// ===============================================
// CONFIGURATION FOR ESP32-4848S040
// ===============================================
// Config for ESP32-4848S040 (ST7701 RGB 480x480 4.0")
// Display: 4.0" IPS LCD 480x480 ST7701S (square)
// Features:
// - ST7701S RGB Panel
// - Standard backlight (GPIO38)
// - I2S pins: DOUT=40, BCLK=1, LRC=2
//   by W76W, 4pda.to
#ifndef myoptions_h
#define myoptions_h

/* 0 = normal NVS persistence (default). 1 = suppress EEPROM.put/commit in Config::saveValue/eepromWrite for local T1.3-style diagnosis only. anti_glitch diagnostic branch closed with 0. */
#define DEBUG_GLITCH_SUSPEND_NVS_WRITES 0

/* 1 = concise Serial traces for SaveManager (init, owe, timer, worker, EEPROM commit + readback). Revert to 0 after diagnosis. */
#define SM_DIAG_PERSIST 1
#define SM_V2_ENABLED 1
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
//#define MUTE_PIN    255                 // Disable MUTE (not used)

/* ===============================================
   BRIGHTNESS CONTROL
   =============================================== */
#define BRIGHTNESS_PIN 255                // Don't use standard brightness pin
#define ENABLE_BRIGHTNESS_CONTROL         // Enable brightness control in web interface

// Auto-dimming (optional, see main.cpp for implementation)
// Uncomment to enable auto-dimming backlight:
//#define AUTOBACKLIGHT(x)    *function*    // Autobacklight function. See options.h for example
//#define AUTOBACKLIGHT_MAX     2500
//#define AUTOBACKLIGHT_MIN     12
//#define DOWN_LEVEL           50      // Lowest brightness level (0-255, default 2)
//#define DOWN_INTERVAL        60      // Interval for backlight dimming in seconds (default 60)
//#define GFX_BL      ST7701_BL  // Alias for autobacklight functions

/* ===============================================
   GENERAL SETTINGS
   =============================================== */
#define PLAYER_FORCE_MONO false           // Mono mode (disabled)
#define L10N_LANGUAGE RU                  // Interface language (Russian)
#define BITRATE_FULL  true                // Full bitrate widget

/* ===============================================
   SPECTRUM ANALYZER
   =============================================== */
// Switch between VU-meter and spectrum analyzer in settings (usespectrum)
#define SPECTRUM_USE_PSRAM     true       // Use PSRAM for FFT buffers
#define SPECTRUM_BANDS         15         // Number of spectrum bands
#define SPECTRUM_FFT_SIZE      64         // FFT size
#define SPECTRUM_SMOOTHING     0.90f      // Smoothing (0.0-1.0, higher = smoother)
#define SPECTRUM_PEAK_HOLD_TIME 300.0f    // Peak hold time (ms)
#define SPECTRUM_LOGARITHMIC   false      // Logarithmic frequency scale
#define SPECTRUM_STEREO        false       // Stereo mode
#define SPECTRUM_GAIN          0.03f      // Overall spectrum gain (1.0 = no gain)
#define SPECTRUM_GRADIENT      true       // Gradient fill (true = smooth gradients for RGB Panel)

/* ===============================================
   VU METER & DISPLAY WIDGETS
   =============================================== */
//#define HIDE_VU                         // Hide VU-meter (uncomment to enable)
#define BOOMBOX_STYLE                     // VU-meter style (bars instead of strips)
//#define CPU_LOAD                          // CPU load widget
//#define WAKE_PIN              255       // Wake pin (not used)
#define EXT_WEATHER       false

/* ===============================================
   SYSTEM & DEBUG
   =============================================== */
#define BATTERY_OFF                       // Disable battery display
//#define PERFMON_DISABLED                // Disable performance monitoring
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
