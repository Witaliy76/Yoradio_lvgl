// ===============================================
// CONFIGURATION FOR JC3248W535C
// ===============================================
// Config for JC3248W535C (AXS15231B QSPI 320x480 3.5")
// Display: 3.5" IPS LCD 320x480 AXS15231B
// Features:
// - AXS15231B QSPI interface
// - Standard backlight (GPIO1)
// - Capacitive touch AXS15231B (I2C)
// - SD card support (HSPI)
// - Battery monitoring (ADC)
// - I2S pins: DOUT=41, BCLK=42, LRC=2
//   by W76W, 4pda.to
#ifndef myoptions_h
#define myoptions_h

/* Audio library debug level / Уровень отладки аудиобиблиотеки */
/* 0=no debug, 1=error, 2=warn, 3=info, 4=debug, 5=verbose */
#define CORE_DEBUG_LEVEL 3

/* ===============================================
   DISPLAY
   =============================================== */
#define DSP_MODEL DSP_AXS15231B              // Display model AXS15231B

// Display pins JC3248W535C (QSPI interface)
#define DSP_HSPI         false
#define TFT_CS           45                  // SPI CS pin
#define TFT_RST          -1                  // SPI RST pin (-1 = connected to ESP EN pin)
#define TFT_SCK          47                  // SPI DC/RS pin
#define TFT_D0           21                  // QSPI Data 0
#define TFT_D1           48                  // QSPI Data 1
#define TFT_D2           40                  // QSPI Data 2
#define TFT_D3           39                  // QSPI Data 3
// Backlight (standard)
#define GFX_BL           1                   // Backlight pin (GPIO1)

/* ===============================================
   I2S DAC (Audio output)
   =============================================== */
#define I2S_DOUT         41                  // I2S Data Out (GPIO41)
#define I2S_BCLK         42                  // I2S Bit Clock (GPIO42)
#define I2S_LRC          2                   // I2S Left/Right Clock (GPIO2)

/* ===============================================
   DISABLED FEATURES
   =============================================== */
#define VS1053_CS        255                 // Disable VS1053 (not used)
//#define MUTE_PIN       2                   // MUTE pin (GPIO2 can be shared with onboard LED)

/* ===============================================
   BRIGHTNESS CONTROL
   =============================================== */
#define ENABLE_BRIGHTNESS_CONTROL            // Enable brightness control in web interface

// Auto-dimming: use LVGL Settings → Display → Auto Dim (runtime, CONFIG v7+).

/* ===============================================
   GENERAL SETTINGS
   =============================================== */
#define PLAYER_FORCE_MONO true               // Mono mode (enabled)
#define L10N_LANGUAGE RU                     // Interface language (Russian)
#define CLOCKFONT_MONO    true               // Monospace font for clock
#define RSSI_DIGIT        true               // Display RSSI as digits instead of icon
// Block 8.1F-B: config.store.vumeter / usespectrum kept for future LVGL widgets (WebUI toggles).
// Block 8.1F-B: config.store.vumeter / usespectrum — будущие LVGL-виджеты (переключатели WebUI).

/* ===============================================
   ENCODER
   =============================================== */
// Encoder support (uncomment if used)
//#define ENC_BTNR              7            // Right rotation (S2, CLK)
//#define ENC_BTNL              15           // Left rotation (S1, DT)
//#define ENC_BTNB              16           // Encoder button (Key, SW)
//#define ENC2_BTNR             34           // Encoder-2 right rotation (S2, CLK)
//#define ENC2_BTNL             35           // Encoder-2 left rotation (S1, DT)
//#define ENC2_BTNB             39           // Encoder-2 button (Key, SW)
#define ENC_INTERNALPULLUP    true           // Enable internal pull-up resistors
#define ENC2_INTERNALPULLUP   true           // Enable internal pull-up resistors for encoder-2

/* ===============================================
   BUTTONS
   =============================================== */
// Button support (uncomment if used)
//#define BTN_LEFT              33           // VolDown, Prev
//#define BTN_CENTER            32           // Play, Stop, Show playlist
//#define BTN_RIGHT             21           // VolUp, Next
//#define BTN_UP                255          // Prev, Move Up
//#define BTN_DOWN              255          // Next, Move Down
//#define BTN_INTERNALPULLUP    false        // Enable weak pull-up resistors
//#define BTN_LONGPRESS_LOOP_DELAY  200      // Delay between DuringLongPress events
//#define BTN_CLICK_TICKS       300          // Click timing (ms)
//#define BTN_PRESS_TICKS       500          // Press timing (ms)

/* ===============================================
   SD CARD
   =============================================== */
// SD card support (HSPI)
#define SDC_CS           10                  // SD Card Chip Select IF you wont use SD Card - use SDC_CS=255
#define SD_SPIPINS       12, 13, 11          // SCK, MISO, MOSI
#define SD_HSPI          true                // Use HSPI (SCK=14, MISO=12, MOSI=13) instead of VSPI

/* ===============================================
   BATTERY MONITORING
   =============================================== */
// Battery voltage monitoring via ADC
//#define BATTERY_OFF                        // Uncomment to disable battery monitoring
//#define HIDE_BAT                           // Hide battery level and voltage display
//#define HIDE_VOLT                          // Hide only battery voltage display

#define ADC_PIN          5                   // ADC pin for battery voltage divider (GPIO5)
#define R1               68                  // Resistor value on + side (kOhm or Ohm)
#define R2               100                 // Resistor value on - side (kOhm or Ohm)
#define DELTA_BAT        0.127               // Battery voltage correction value (volts)

/* ===============================================
   TOUCHSCREEN
   =============================================== */
// JC3248W535C uses AXS15231B Capacitive I2C
#define TS_MODEL         TS_MODEL_AXS15231B  // AXS15231B Capacitive I2C
#define TS_SDA           4                   // GPIO4
#define TS_SCL           8                   // GPIO8
#define TS_INT           3                   // GPIO3
#define TS_RST           -1                  // Not used

// Touchscreen calibration (coordinates):
#define TS_X_MIN         0                   // X minimum
#define TS_X_MAX         320                 // X maximum
#define TS_Y_MIN         0                   // Y minimum
#define TS_Y_MAX         480                 // Y maximum


/* ===============================================
   OTHER SETTINGS
   =============================================== */
//#define HIDE_VOLPAGE                       // Hide volume page (use volume progress bar)
//#define HIDE_DATE                          // Hide date display
//#define ROTATE_90        false             // DOES NOT WORK for AXS15231B. Don't touch!

/* ===============================================
   WEATHER & LOCATION
   =============================================== */
#define GRND_HEIGHT      301                 // Ground height above sea level (meters) for pressure correction

/* ===============================================
   IR REMOTE CONTROL
   =============================================== */
// IR remote control (uncomment if used)
//#define IR_PIN           6                 // IR receiver pin
//#define IR_TIMEOUT       80                // IR timeout (see IRremoteESP8266 example)

/* ===============================================
   RTC MODULE
   =============================================== */
// Real-time clock module (uncomment if used)
//#define RTC_MODULE       DS3231            // DS3231 or DS1307
//#define RTC_SDA          21
//#define RTC_SCL          22

/* ===============================================
   SYSTEM & DEBUG
   =============================================== */
//#define WROOM_USED                         // Set if using WROOM (adds memory, aac_decoder must be replaced)
/* MemWatchdog: автоперезапуск при деградации RAM (TLS). Отключить: закомментировать. */
#define MEM_WATCHDOG_AUTOREBOOT
//#define LED_BUILTIN      2                 // Onboard LED pin
//#define LED_INVERT       false             // Invert onboard LED
//#define DSP_INVERT_TITLE false             // Invert station title colors
//#define EXT_WEATHER      false             // Extended weather display
//#define DEBUG_TOUCH                          // Enable touchscreen debug output
//#define DEBUG_DISPLAY                          // Display debug

// Day of week uppercase (true = uppercase, false = lowercase)
//#define DOW_UPPERCASE true                     // Uncomment and set to true/false if needed

/* AI Layer debug logs / Логи отладки AI Layer */
// 0 = release (only important logs), 1 = debug (all logs)
// 0 = релиз (только важные логи), 1 = отладка (все логи)
#define AI_LAYER_DEBUG 0

#endif
