// ===============================================
// CONFIGURATION FOR UEDX48480021-MD80ET
// ===============================================
// Config for UEDX48480021-MD80ET (ST7701S RGB 480x480 2.1")
// Display: 2.1" IPS LCD 480x480 ST7701S
// Features:
// - ST7701S Type4 preset (BGR mode)
// - RGB→BGR pin swap in Arduino_ESP32RGBPanel
// - Active LOW backlight (GPIO7)
// - I2S pins: DOUT=43, BCLK=44, LRC=4 (GPIO4 requires removing capacitor C9)
//   by W76W, 4pda.to
#ifndef myoptions_h
#define myoptions_h

/* Audio library debug level / Уровень отладки аудиобиблиотеки */
/* 0=no debug, 1=error, 2=warn, 3=info, 4=debug, 5=verbose */
#define CORE_DEBUG_LEVEL 0

/* Display */
// New module UEDX48480021-MD80ET (ST7701S RGB 480x480 2.1")
#define DSP_MODEL DSP_UEDX48480021

// Display pins UEDX48480021-MD80ET
// ST7701S command bus (SWSPI)
#define ST7701_CS   18
#define ST7701_SCK  13
#define ST7701_SDA  12
#define ST7701_RST  8
// RGB sync signals
#define ST7701_DE     17
#define ST7701_VSYNC  3
#define ST7701_HSYNC  46
#define ST7701_PCLK   9
// RGB Data Pins (16-bit interface) - CRITICAL SWAP for UEDX48480021
// Red data pins R0..R4 - SWAPPED with B pins
#define ST7701_R0   10   // B0 (DATA0)  → R0 (SWAPPED)
#define ST7701_R1   11   // B1 (DATA1)  → R1 (SWAPPED)
#define ST7701_R2   12   // B2 (DATA2)  → R2 (SWAPPED)
#define ST7701_R3   13   // B3 (DATA3)  → R3 (SWAPPED)
#define ST7701_R4   14   // B4 (DATA4)  → R4 (SWAPPED)
// Green data pins G0..G5
#define ST7701_G0   21   // G0 (DATA5)
#define ST7701_G1   47   // G1 (DATA6)
#define ST7701_G2   48   // G2 (DATA7)
#define ST7701_G3   45   // G3 (DATA8)
#define ST7701_G4   38   // G4 (DATA9)
#define ST7701_G5   39   // G5 (DATA10)
// Blue data pins B0..B4 - SWAPPED with R pins
#define ST7701_B0   40   // R0 (DATA11) → B0 (SWAPPED)
#define ST7701_B1   41   // R1 (DATA12) → B1 (SWAPPED)
#define ST7701_B2   42   // R2 (DATA13) → B2 (SWAPPED)
#define ST7701_B3   2    // R3 (DATA14) → B3 (SWAPPED)
#define ST7701_B4   1    // R4 (DATA15) → B4 (SWAPPED)
// Backlight - CRITICAL: Active LOW for UEDX48480021
#define ST7701_BL   7


/* ===============================================
   ENCODER
   =============================================== */
   #define ENC_BTNL              255           /*  Left rotation */
   #define ENC_BTNB              255           /*  Encoder button */
   #define ENC_BTNR              255           /*  Right rotation */
   #define ENC2_BTNL              6           // Left rotation
   #define ENC2_BTNB              0           // Encoder button
   #define ENC2_BTNR              5           // Right rotation
   #define ENC2_INTERNALPULLUP    false       // Internal pull-up resistors
   #define ENC2_HALFQUARD         true        // Half mode (experimental)
   
/* ===============================================
   DISPLAY OPTIONS
   =============================================== */
//#define RSSI_DIGIT            true        // Display RSSI as digits instead of icon

/* ===============================================
   I2S DAC (Audio output)
   =============================================== */
// IMPORTANT: To use GPIO4 (LRC) you must remove capacitor C9 from the board!
#define I2S_DOUT     43      // I2S Data Out (GPIO43, UART TX1 on board)
#define I2S_BCLK     44      // I2S Bit Clock (GPIO44, UART RX1 on board)
#define I2S_LRC      4       // I2S Left/Right Clock (GPIO4, requires C9 removal)


/* ===============================================
   DISABLED FEATURES
   =============================================== */
#define VS1053_CS     255                 // Disable VS1053 (not used)
//#define MUTE_PIN    255                 // Disable MUTE (not used)

/* ===============================================
   BRIGHTNESS CONTROL
   =============================================== */
// IMPORTANT: UEDX48480021 backlight is controlled via Active LOW on GPIO7
#define BRIGHTNESS_PIN 255                // Don't use standard brightness pin
#define ENABLE_BRIGHTNESS_CONTROL         // Enable brightness control in web interface

// Auto-dimming: use LVGL Settings → Display → Auto Dim (runtime, CONFIG v7+).

/* ===============================================
   GENERAL SETTINGS
   =============================================== */
#define PLAYER_FORCE_MONO true           // Mono mode (disabled)
// Block 8.1F-B: config.store.vumeter / usespectrum kept for future LVGL widgets (WebUI toggles).
// Block 8.1F-B: config.store.vumeter / usespectrum — будущие LVGL-виджеты (переключатели WebUI).
#define EXT_WEATHER       false           // Extended weather (network.cpp)

/* ===============================================
   SYSTEM & DEBUG
   =============================================== */
#define BATTERY_OFF                     // Disable battery display
#define WDT_TIMEOUT 30                    // Watchdog timeout (seconds)
/* MemWatchdog: автоперезапуск при деградации RAM (TLS). Отключить: закомментировать. */
#define MEM_WATCHDOG_AUTOREBOOT

/* ===============================================
   TOUCHSCREEN
   =============================================== */
// UEDX48480021-MD80ET uses CST826 


// CST826 for UEDX48480021-MD80ET:
#define TS_MODEL              TS_MODEL_CST826  // CST826 Capacitive I2C
#define TS_SDA                16               // GPIO16
#define TS_SCL                15               // GPIO15
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
//#define DEBUG_TOUCH                            // Touchscreen debug
//#define DEBUG_DISPLAY                          // Display debug

/* AI Layer debug logs / Логи отладки AI Layer */
// 0 = release (only important logs), 1 = debug (all logs)
// 0 = релиз (только важные логи), 1 = отладка (все логи)
#define AI_LAYER_DEBUG 0

#endif
