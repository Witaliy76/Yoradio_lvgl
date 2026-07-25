/************************************************************************************************
   Display driver for UEDX48480021-MD80ET (480x480) ST7701S RGB Panel
   Based on ST7701 implementation
   Adapted for UEDX48480021-MD80ET with ST7701S Type4 preset (BGR mode)
   Features: Round 2.1" display, Active LOW backlight, CST826 touch, RGB→BGR swap
************************************************************************************************/

// Adaptation: Witaliy76 - https://github.com/Witaliy76
#include "../core/options.h"
#if DSP_MODEL==DSP_UEDX48480021

#include "displayUEDX48480021.h"
#include "fonts/bootlogo.h"
#include "../core/spidog.h"
#include "../core/config.h"
#include "../core/network.h"
#include "../core/display.h"
#include "../i18n/i18n.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_task_wdt.h"
#include "esp_cpu.h"

#include "Arduino_GFX_Library.h"
#include "tools/GFX_Canvas_screen.h"

// ST7701 Type4 init sequence from Arduino_GFX (BGR mode for UEDX48480021)
extern const uint8_t st7701_type4_init_operations[];
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32-hal-ledc.h"
#endif

#include "esp_timer.h"
#include "esp_rom_sys.h"
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
#include "lvgl.h"
#endif

#include "driver/adc.h"
#include "esp_adc_cal.h"

#define TAKE_MUTEX() sdog.takeMutex()
#define GIVE_MUTEX() sdog.giveMutex()

// ST7701 post-commands always enabled (required for panel operation)

// Local RGB888 -> RGB565 helper (Arduino_GFX does not export free color565)
static inline uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// === Global objects for Canvas mode (RGB Panel) ===
static Arduino_DataBus *bus = nullptr;
static Arduino_ESP32RGBPanel *rgbpanel = nullptr;
static Arduino_RGB_Display *output_display = nullptr;
Arduino_Canvas *gfx = nullptr;

#ifndef BATTERY_OFF

  #ifndef ADC_PIN
    #define ADC_PIN 5
  #endif
  #if (ADC_PIN == 5)
    #define USER_ADC_CHAN ADC1_CHANNEL_4
  #endif

  #ifndef R1
    #define R1 33		// Resistor value on + side
  #endif
  #ifndef R2
    #define R2 100		// Resistor value on - side
  #endif
  #ifndef DELTA_BAT
    #define DELTA_BAT 0	// Battery voltage correction value
  #endif

  // Battery monitoring constants
  #define BATTERY_SAMPLES 100       // Number of samples for accuracy
  #define BATTERY_CHECK_INTERVAL 5000  // Check every 5 seconds
  #define BATTERY_VOLTAGE_THRESHOLD 0.01f  // Voltage change threshold for charging detection
  #define BATTERY_SMOOTH_COUNT 10  // Number of measurements for smoothing
  #define BATTERY_CHARGE_MIN_VOLTAGE 3.5f  // Minimum voltage for charging detection
  #define BATTERY_STABLE_THRESHOLD 0.002f  // Threshold for stable voltage detection

  float ADC_R1 = R1;        // Resistor value on + side
  float ADC_R2 = R2;        // Resistor value on - side
  float DELTA = DELTA_BAT;  // Battery voltage correction value

  uint8_t g, t = 1;         // Counters for blink and averaging
  bool Charging = false;    // Flag: charger connected

  float Volt = 0;           // Battery voltage
  float Volt1 = 0, Volt2 = 0, Volt3 = 0, Volt4 = 0, Volt5 = 0;  // Previous voltage readings
  float smoothBuffer[BATTERY_SMOOTH_COUNT] = {0};
  uint8_t smoothIndex = 0;
  float lastVolt = 0;       // Previous voltage for change calculation
  static esp_adc_cal_characteristics_t adc1_chars;

  uint8_t ChargeLevel;
  // Battery voltage array corresponding to remaining charge percentage
  float vs[22] = {2.60, 3.10, 3.20, 3.26, 3.29, 3.33, 3.37, 3.41, 3.46, 3.51, 3.56, 3.61, 3.65, 3.69, 3.72, 3.75, 3.78, 3.82, 3.88, 3.95, 4.03, 4.25};

  // Charging state structure
  struct {
    float lastVoltage = 0;
    uint32_t lastCheck = 0;
    uint8_t chargeCount = 0;    // Counter for consecutive voltage increases
    uint8_t dischargeCount = 0; // Counter for consecutive voltage drops
    uint8_t stableCount = 0;    // Counter for stable measurements
  } chargingState;

#endif

DspCore::DspCore() {
#ifndef BATTERY_OFF
  // Initialize ADC for battery monitoring
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(USER_ADC_CHAN, ADC_ATTEN_DB_12);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &adc1_chars);
#endif
  _scrollid = nullptr;
  _lastScroller = nullptr;
  _lastReleaseTime = 0;
  _lastScrollIndex = -1;
  _oldClockTop = 0;
}

#include "tools/utf8RusGFX.h"
///////////////////////////////////////////////////////////////

// Day of week uppercase (can be overridden in myoptions.h)
#ifndef DOW_UPPERCASE
#define DOW_UPPERCASE true
#endif

void DspCore::initDisplay() {
  Serial.println("[UEDX48480021] initDisplay start");
  
  // --- RGB PANEL INITIALIZATION BLOCK START ---
  
  // 0. Initialize RST pin according to UEDX48480021 guide
  pinMode(ST7701_RST, OUTPUT);
  digitalWrite(ST7701_RST, HIGH);  // RST HIGH (inactive)
  delay(10);
  digitalWrite(ST7701_RST, LOW);   // RST LOW (reset)
  delay(10);
  digitalWrite(ST7701_RST, HIGH);  // RST HIGH (active)
  delay(50);  // Post-reset delay
  Serial.println("[UEDX48480021] RST pin initialized");
  
  // 1. Create SPI bus (required for Arduino_GFX, even if only used for CS/RESET)
  if (!bus) {
    Serial.println("[UEDX48480021] Initializing bus...");
    bus = new Arduino_SWSPI(
        GFX_NOT_DEFINED /* DC */, ST7701_CS /* CS */,
        ST7701_SCK /* SCK */, ST7701_SDA /* MOSI */, GFX_NOT_DEFINED /* MISO */);
    if (!bus) {
      Serial.println("[UEDX48480021] Failed to initialize bus!");
      return;
    }
    
    // Initialize bus according to UEDX48480021 guide
    if (!bus->begin()) {
      Serial.println("[UEDX48480021] ERROR: Bus initialization failed!");
      return;
    }
    Serial.println("[UEDX48480021] Bus initialized successfully");
  }

  // 2. Create RGB panel object with all pins and timings for UEDX48480021
  if (!rgbpanel) {
    Serial.println("[UEDX48480021] Initializing RGB panel...");
    rgbpanel = new Arduino_ESP32RGBPanel(
        ST7701_DE /* DE */, ST7701_VSYNC /* VSYNC */, ST7701_HSYNC /* HSYNC */, ST7701_PCLK /* PCLK */,
        // RED channel pins (R0-R4) - SWAPPED with blue per UEDX48480021 guide
        ST7701_R0, ST7701_R1, ST7701_R2, ST7701_R3, ST7701_R4,
        // GREEN channel pins (G0-G5)
        ST7701_G0, ST7701_G1, ST7701_G2, ST7701_G3, ST7701_G4, ST7701_G5,
        // BLUE channel pins (B0-B4) - SWAPPED with red per UEDX48480021 guide
        ST7701_B0, ST7701_B1, ST7701_B2, ST7701_B3, ST7701_B4,
        // Horizontal timing parameters - CRITICAL for UEDX48480021: hfp=50, hsync=8, hbp=10
        /* hsync_polarity */ 1, /* hsync_front_porch */ 50, /* hsync_pulse_width */ 8, /* hsync_back_porch */ 10,
        // Vertical timing parameters - CRITICAL for UEDX48480021: vfp=8, vsync=2, vbp=18
        /* vsync_polarity */ 1, /* vsync_front_porch */ 8, /* vsync_pulse_width */ 2, /* vsync_back_porch */ 18,
        // Clock and data format parameters - CRITICAL for UEDX48480021: 8MHz, pclk_active_neg=0
        /* pclk_active_neg */ 0, /* prefer_speed */ 6000000UL, /* big endian */ false,
        /* de_idle_high */ 0, /* pclk_idle_high */ 0, /* bounce_buffer_size_px */ 0);
    if (!rgbpanel) {
      Serial.println("[UEDX48480021] Failed to initialize RGB panel!");
      return;
    }
  }

  // 3. Create final display object
  if (!output_display) {
    Serial.println("[UEDX48480021] Initializing RGB display...");
    
    // CRITICAL: Use ST7701 Type4 init sequence (BGR mode) for UEDX48480021
    output_display = new Arduino_RGB_Display(
        480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, false /* auto flush */,
        bus, ST7701_RST /* RST */, st7701_type4_init_operations, sizeof(st7701_type4_init_operations));
    if (!output_display) {
      Serial.println("[UEDX48480021] Failed to initialize RGB display!");
      return;
    }
    
  }

  // 4. Initialize Canvas
  // IMPORTANT: don't call begin() on output_display manually,
  // gfx->begin() will do it. Double begin causes RGB panel slot panic.
  if (!gfx) {
    Serial.println("[UEDX48480021] Initializing canvas...");
    gfx = new Arduino_Canvas(480, 480, output_display);
    if (!gfx) {
      Serial.println("[UEDX48480021] Failed to create canvas!");
      delete gfx;
      gfx = nullptr;
      return;
    }
    
    if (!gfx->begin()) {
      Serial.println("[UEDX48480021] Failed to begin canvas!");
      delete gfx;
      gfx = nullptr;
      return;
    }
    Serial.println("[UEDX48480021] Canvas initialized successfully");
    
    // Delay for display stabilization
    delay(100);

    // ST7701S post-commands for pixel format, inversion and MADCTL
    if (bus) {
      // Pixel format: 18-bit (0x66)
      bus->sendCommand(0x3A);
      bus->sendData(0x66);
      delay(2);

      // Inversion off
      bus->sendCommand(0x20);
      delay(2);

      // Page 0x10: scan setup (C7=0x00 normal)
      bus->beginWrite();
      bus->writeCommand(0xFF); bus->write(0x77); bus->write(0x01); bus->write(0x00); bus->write(0x00); bus->write(0x10);
      bus->writeCommand(0xC7); bus->write(0x00);
      // Return to page 0x00 and set MADCTL = 0x00 (RGB order)
      bus->writeCommand(0xFF); bus->write(0x77); bus->write(0x01); bus->write(0x00); bus->write(0x00); bus->write(0x00);
      bus->writeCommand(0x36); bus->write(0x00);
      bus->endWrite();
      delay(5);
    }
  }

  // 5. Enable backlight - CRITICAL: Active LOW for UEDX48480021
  pinMode(ST7701_BL, OUTPUT);
  digitalWrite(ST7701_BL, LOW);  // LOW = on for UEDX48480021
  delay(50); // Backlight stabilization delay
  
  Serial.println("[UEDX48480021] Backlight enabled (Active LOW)");
  
  // --- RGB PANEL INITIALIZATION BLOCK END ---

  Serial.print("[UEDX48480021] Canvas ptr: "); Serial.println((uintptr_t)gfx, HEX);

  plItemHeight = playlistConf.widget.textsize*(CHARHEIGHT-1)+playlistConf.widget.textsize*4;
  plTtemsCount = round((float)height()/plItemHeight);
  if(plTtemsCount%2==0) plTtemsCount++;
  plCurrentPos = plTtemsCount/2;
  plYStart = (height() / 2 - plItemHeight / 2) - plItemHeight * (plTtemsCount - 1) / 2 + playlistConf.widget.textsize*2;
  
  Serial.println("[UEDX48480021] initDisplay completed successfully");
  Serial.println("[UEDX48480021] Display ready for normal operation");
}

void DspCore::displayOn() {
  if (output_display) {
    // Turn on RGB display
    Serial.println("[UEDX48480021] Display ON");
  }
}

void DspCore::displayOff() {
  if (output_display) {
    // Turn off RGB display
    Serial.println("[UEDX48480021] Display OFF");
  }
}

void DspCore::drawLogo(uint16_t top) 
{ 
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    // Block 8-E11: LVGL Boot uses scr_boot assets; never paint bootlogo2 into shared Canvas.
    // Block 8-E11: при LVGL Boot / lv_disp — не пишем в Canvas (E5C shared buffer).
    if (lvgl_ui::isLvglBootActive() || lv_disp_get_default() != nullptr) {
        return;
    }
#endif
    Serial.println("[UEDX48480021] drawLogo call");
    
    if (!gfx) {
        Serial.println("[UEDX48480021] drawLogo: gfx is nullptr!");
        return;
    }
    
    // Clear area for logo
    gfxFillRect(gfx, 0, top, width(), 88, config.theme.background);
    
    // Draw logo
    gfxDrawBitmap(gfx, (width() - 134) / 2, top, bootlogo2, 134, 88);
    
    delay(100);
  // Force screen update after drawing logo
  if (gfx) gfx->flush();
}

// Calculate string width for standard Adafruit_GFX font
uint16_t DspCore::textWidthGFX(const char *txt, uint8_t textsize) {
  return strlen(txt) * CHARWIDTH * textsize;
}

void DspCore::printPLitem(uint8_t pos, const char* item, ScrollWidget& current, bool uppercase){
  if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (printPLitem)"); return; }
  if (pos == plCurrentPos) {
    current.setText(item);
  } else {
    uint8_t plColor = (abs(pos - plCurrentPos)-1)>4?4:abs(pos - plCurrentPos)-1;
    gfxFillRect(gfx, 0, plYStart + pos * plItemHeight - 1, width(), plItemHeight - 2, config.theme.background);
    // Trim string to width without ellipsis
    const char* rus = utf8Rus(item, uppercase);
    int len = strlen(rus);
    char buf[128];
    int maxWidth = playlistConf.width;
    uint8_t textsize = playlistConf.widget.textsize;
    if (textWidthGFX(rus, textsize) <= maxWidth) {
      strncpy(buf, rus, sizeof(buf)-1);
      buf[sizeof(buf)-1] = 0;
    } else {
      int cut = len;
      while (cut > 0) {
        char tmp[128];
        strncpy(tmp, rus, cut);
        tmp[cut] = 0;
        if (textWidthGFX(tmp, textsize) <= maxWidth) break;
        cut--;
      }
      strncpy(buf, rus, cut);
      buf[cut] = 0;
    }
    // Используем gfxDrawText1b() так как строка уже сконвертирована в CP1251 через utf8Rus()
    // Use gfxDrawText1b() because string is already converted to CP1251 via utf8Rus()
    // gfxDrawText() вызывает utf8Rus() внутри, что приводит к двойной конвертации
    // gfxDrawText() calls utf8Rus() internally, causing double conversion
    gfxDrawText1b(
      gfx,
      TFT_FRAMEWDT,
      plYStart + pos * plItemHeight,
      buf,
      config.theme.playlist[plColor],
      config.theme.background,
      textsize,
      nullptr,
      uppercase
    );
  }
}

void DspCore::drawPlaylist(uint16_t currentItem) {
  if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (drawPlaylist)"); return; }
  uint8_t lastPos = config.fillPlMenu(currentItem - plCurrentPos, plTtemsCount);
  if(lastPos<plTtemsCount){
    gfxFillRect(gfx, 0, lastPos*plItemHeight+plYStart, width(), height()/2, config.theme.background);
  }
}

void DspCore::clearDsp(bool black) { if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (clearDsp)"); return; } gfxClearScreen(gfx, black?0:config.theme.background); }

uint8_t DspCore::_charWidth(unsigned char c){
  GFXglyph *glyph = pgm_read_glyph_ptr(&DS_DIGI56pt7b, c - 0x20);
  return pgm_read_byte(&glyph->xAdvance);
}

uint16_t DspCore::textWidth(const char *txt){
  uint16_t w = 0, l=strlen(txt);
  for(uint16_t c=0;c<l;c++) w+=_charWidth(txt[c]);
  return w;
}

// Функция для вычисления ширины первых N символов строки
uint16_t DspCore::textWidthN(const char *txt, int n) {
  uint16_t w = 0;
  for(int c=0; c<n && txt[c]; c++) w+=_charWidth(txt[c]);
  return w;
}

void DspCore::_getTimeBounds() {
  _timewidth = textWidth(_timeBuf);
  char buf[4];
  strftime(buf, 4, "%H", &network.timeinfo);
  _dotsLeft=textWidth(buf);
}

void DspCore::_clockSeconds(){
  if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (_clockSeconds)"); return; }
  // Seconds display
  char secbuf[8];
  snprintf(secbuf, sizeof(secbuf), "%02d", network.timeinfo.tm_sec);
  gfxDrawText(
    gfx,
    width()  - clockRightSpace - CHARWIDTH*4*2-10,
    clockTop-clockTimeHeight+48,
    secbuf,
    config.theme.seconds,
    config.theme.background,
    4,
    nullptr,
    false
  );
  
  // Clear colon area (for old font compatibility)
  gfxFillRect(gfx, _timeleft+_dotsLeft+5, clockTop-CHARHEIGHT+5, 15, 65, config.theme.background);
  
  // Blinking colon
  gfxDrawText(
    gfx,
    _timeleft+_dotsLeft +1,
    clockTop-CHARHEIGHT+75,
    ":",
    (network.timeinfo.tm_sec % 2 == 0) ? config.theme.clock : (CLOCKFONT_MONO?config.theme.clockbg:config.theme.background),
    (network.timeinfo.tm_sec % 2 == 0) ? config.theme.clock : (CLOCKFONT_MONO?config.theme.clockbg:config.theme.background),
    1,
    &DS_DIGI56pt7b,
    false
  );
#ifndef BATTERY_OFF
  if(!config.isScreensaver) {
    // Battery indicator with animations
    char batbuf[8] = "";
    uint16_t batcolor = 0;
    if (Charging) {
      batcolor = color565(0, 255, 255);
      if (g == 1) strcpy(batbuf, "\xA0\xA2\x9E\x9F");
      if (g == 2) strcpy(batbuf, "\xA0\x9E\x9E\xA3");
      if (g == 3) strcpy(batbuf, "\x9D\x9E\xA2\xA3");
      if (g >= 4) {g = 0; strcpy(batbuf, "\x9D\xA2\xA2\x9F");}
      g++;
    } else if (Volt < 2.8) {
      batcolor = color565(255, 0, 0);
      if (g == 1) strcpy(batbuf, "\xA0\xA2\xA2\xA3");
      if (g >= 2) {g = 0; strcpy(batbuf, "\x9D\x9E\x9E\x9F");}
      g++;
    } else {
      // Static battery icon
      if (Volt >= 3.82)      { batcolor = color565(100, 255, 150); strcpy(batbuf, "\xA0\xA2\xA2\xA3"); }
      else if (Volt >= 3.72) { batcolor = color565(50, 255, 100);  strcpy(batbuf, "\x9D\xA2\xA2\xA3"); }
      else if (Volt >= 3.61) { batcolor = color565(0, 255, 0);     strcpy(batbuf, "\x9D\xA1\xA2\xA3"); }
      else if (Volt >= 3.46) { batcolor = color565(75, 255, 0);    strcpy(batbuf, "\x9D\x9E\xA2\xA3"); }
      else if (Volt >= 3.33) { batcolor = color565(150, 255, 0);   strcpy(batbuf, "\x9D\x9E\xA1\xA3"); }
      else if (Volt >= 3.20) { batcolor = color565(255, 255, 0);   strcpy(batbuf, "\x9D\x9E\x9E\xA3"); }
      else if (Volt >= 2.8)  { batcolor = color565(255, 0, 0);     strcpy(batbuf, "\x9D\x9E\x9E\x9F"); }
    }
    gfxDrawText(gfx, BatX, BatY, batbuf, batcolor, config.theme.background, BatFS, nullptr, false);
#ifndef HIDE_VOLT
    char voltbuf[16];
    snprintf(voltbuf, sizeof(voltbuf), "%.3fv", Volt);
    gfxDrawText(gfx, VoltX, VoltY, voltbuf, batcolor, config.theme.background, VoltFS, nullptr, false);
#endif
    char procbuf[8];
    snprintf(procbuf, sizeof(procbuf), "%3i%%", ChargeLevel);
    gfxDrawText(gfx, ProcX, ProcY, procbuf, batcolor, config.theme.background, ProcFS, nullptr, false);
  }
#endif
}

void DspCore::_clockDate(){
  if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (_clockDate)"); return; }
  if(_olddateleft>0)
    gfxFillRect(gfx, _olddateleft,  clockTop+78, _olddatewidth, CHARHEIGHT*2, config.theme.background); // clear old date
  gfxDrawText1b(gfx, _dateleft, clockTop+78, _dateBuf, config.theme.date, config.theme.background, 2, nullptr, true);
  strlcpy(_oldDateBuf, _dateBuf, sizeof(_dateBuf));
  _olddatewidth = _datewidth;
  _olddateleft = _dateleft;
  // Day of week
  gfxDrawText(
    gfx,
    width() - clockRightSpace - CHARWIDTH*4*2+13-20,
    clockTop-CHARHEIGHT+44,
    i18n::dayShort(static_cast<uint8_t>(network.timeinfo.tm_wday)),
    config.theme.dow,
    config.theme.background,
    3,
    nullptr,
    DOW_UPPERCASE
  );
}

void DspCore::_clockTime(){
  if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (_clockTime)"); return; }
  if(_oldtimeleft>0 && !CLOCKFONT_MONO)
    gfxFillRect(gfx, _oldtimeleft, clockTop-clockTimeHeight+1, _oldtimewidth, clockTimeHeight, config.theme.background);
  _timeleft = width()-clockRightSpace-CHARWIDTH*4*2-24-_timewidth;
  // Time display
  gfxDrawText(
    gfx,
    _timeleft,
    clockTop + 63,
    _timeBuf,
    config.theme.clock,
    config.theme.background,
    1,
    &DS_DIGI56pt7b,
    false
  );
  strlcpy(_oldTimeBuf, _timeBuf, sizeof(_timeBuf));
  _oldtimewidth = _timewidth;
  _oldtimeleft = _timeleft;
  // Vertical divider (disabled)
  //gfxDrawLine(gfx, width()-clockRightSpace-CHARWIDTH*4*2-25, clockTop +5, width()-clockRightSpace-CHARWIDTH*4*2-25, clockTop +5 + clockTimeHeight-3, config.theme.div);
  // Horizontal divider (disabled)
  //gfxDrawLine(gfx, width()-clockRightSpace-CHARWIDTH*4*2+10, clockTop+32, width()-clockRightSpace-CHARWIDTH*4*2+10+62-1, clockTop+32, config.theme.div);
  sprintf(_buffordate, "%2d %s %d", network.timeinfo.tm_mday,i18n::monthName(static_cast<uint8_t>(network.timeinfo.tm_mon)), network.timeinfo.tm_year+1900);
  strlcpy(_dateBuf, utf8Rus(_buffordate, true), sizeof(_dateBuf));
  _datewidth = strlen(_dateBuf) * CHARWIDTH*2;
  _dateleft = width() - clockRightSpace - _datewidth - 80;
}

void DspCore::printClock(uint16_t top, uint16_t rightspace, uint16_t timeheight, bool redraw){
  // Limit top boundary
  if(top < TFT_FRAMEWDT) {
    top = TFT_FRAMEWDT;
  }
  
  // Do not clamp bottom: when Y>310 forced top=302 → two positions → ghosting. Use requested Y.
  if (_oldClockTop != 0 && _oldClockTop != top && gfx) {
    int16_t oldY = (int16_t)_oldClockTop - (int16_t)timeheight - 20;
    if (oldY < 0) oldY = 0;
    gfxFillRect(gfx, TFT_FRAMEWDT, (uint16_t)oldY, MAX_WIDTH, 250, config.theme.background);
  }
  clockTop = top;
  clockRightSpace = rightspace;
  clockTimeHeight = timeheight;
  _oldClockTop = top;
  strftime(_timeBuf, sizeof(_timeBuf), "%H:%M", &network.timeinfo);
  if(strcmp(_oldTimeBuf, _timeBuf)!=0 || redraw){
    _getTimeBounds();
    _clockTime();
    _clockDate();
  }
  _clockSeconds();
}

void DspCore::clearClock(){
  if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (clearClock)"); return; }
  int16_t curY = (int16_t)clockTop - (int16_t)clockTimeHeight - 20;
  if (curY < 0) curY = 0;
  gfxFillRect(gfx, TFT_FRAMEWDT, (uint16_t)curY, MAX_WIDTH, 250, config.theme.background);
  if (_oldClockTop != 0 && _oldClockTop != clockTop) {
    int16_t oldY = (int16_t)_oldClockTop - (int16_t)clockTimeHeight - 20;
    if (oldY < 0) oldY = 0;
    gfxFillRect(gfx, TFT_FRAMEWDT, (uint16_t)oldY, MAX_WIDTH, 250, config.theme.background);
  }
  _oldClockTop = clockTop;
}

void DspCore::startWrite(void) {
  TAKE_MUTEX();
  //ILI9486_SPI::startWrite();
}

void DspCore::endWrite(void) { 
  //ILI9486_SPI::endWrite();
  GIVE_MUTEX();
}
  
void DspCore::loop(bool force) {
#ifndef BATTERY_OFF
    static uint32_t lastBatteryUpdate = 0;
    if (millis() - lastBatteryUpdate >= 1000) { // Update every second
        readBattery();
        lastBatteryUpdate = millis();
    }
#endif
}

void DspCore::charSize(uint8_t textsize, uint8_t& width, uint16_t& height){
  width = textsize * CHARWIDTH;
  height = textsize * CHARHEIGHT;
}

void DspCore::setTextSize(uint8_t s){
  if (!gfx) { Serial.println("[UEDX48480021] gfx is nullptr! (setTextSize)"); return; }
  gfx->setTextSize(s);
}

void DspCore::flip(){
  TAKE_MUTEX();
  if (output_display) output_display->setRotation(config.store.flipscreen ? 2 : 0);
  GIVE_MUTEX();
}

void DspCore::invert(){
  TAKE_MUTEX();
  if (bus) {
    // Hardware inversion control for ST7701: 0x21 (INVON), 0x20 (INVOFF)
    bus->sendCommand(config.store.invertdisplay ? 0x21 : 0x20);
  }
  GIVE_MUTEX();
}

void DspCore::sleep(void) { 
  Serial.println("[UEDX48480021] sleep");
  TAKE_MUTEX();
  displayOff();
  // Turn off RGB panel backlight - Active LOW: HIGH = off
  digitalWrite(ST7701_BL, HIGH);
  GIVE_MUTEX();
}

void DspCore::wake(void) {
  Serial.println("[UEDX48480021] wake");
  TAKE_MUTEX();
  displayOn();
  // Turn on RGB panel backlight - Active LOW: use PWM for initial brightness
  #if defined(ENABLE_BRIGHTNESS_CONTROL)
    analogWrite(ST7701_BL, map(config.store.brightness, 0, 100, 255, 0));
  #else
    analogWrite(ST7701_BL, 0);  // Max brightness for Active LOW
  #endif
  GIVE_MUTEX();
}

void DspCore::setBrightness(uint8_t brightness) {
  TAKE_MUTEX();
  // UEDX48480021: Active LOW backlight - use PWM for smooth control
  // For Active LOW: 0 = max brightness, 255 = min brightness
  if (brightness == 0) {
    digitalWrite(ST7701_BL, HIGH);  // Fully off
  } else {
    // Invert value for Active LOW: 100% -> 0, 0% -> 255
    uint8_t pwm_value = map(brightness, 0, 100, 255, 0);
    analogWrite(ST7701_BL, pwm_value);
  }
  GIVE_MUTEX();
}

void DspCore::writePixel(int16_t x, int16_t y, uint16_t color) {
    if(_clipping){
        if ((x < _cliparea.left) || (x > _cliparea.left+_cliparea.width) || (y < _cliparea.top) || (y > _cliparea.top + _cliparea.height)) return;
    }
    gfxDrawPixel(gfx, x, y, color);
}

void DspCore::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if(_clipping){
        if ((x < _cliparea.left) || (x >= _cliparea.left+_cliparea.width) || (y < _cliparea.top) || (y > _cliparea.top + _cliparea.height))  return;
    }
    gfxFillRect(gfx, x, y, w, h, color);
}

void DspCore::setClipping(clipArea ca){
  _cliparea = ca;
  _clipping = true;
}

void DspCore::clearClipping(){
  _clipping = false;
}

void DspCore::setNumFont(){
  // For Canvas mode, font switching is done in gfxDrawText functions
  // Leave empty here
}

uint16_t DspCore::width() {
    return 480;
}

uint16_t DspCore::height() {
    return 480;
}

#ifndef BATTERY_OFF
void DspCore::readBattery() {
    static uint32_t lastRead = 0;
    if (millis() - lastRead < 100) return; // Limit reading frequency
    lastRead = millis();

    float tempmVolt = 0;
    for(uint8_t i = 0; i < BATTERY_SAMPLES; i++) {
        tempmVolt += esp_adc_cal_raw_to_voltage(adc1_get_raw(USER_ADC_CHAN), &adc1_chars);
    }
    float mVolt = (tempmVolt / BATTERY_SAMPLES) / 1000;
    float rawVolt = (mVolt + 0.0028 * mVolt * mVolt + 0.0096 * mVolt - 0.051) / (ADC_R2 / (ADC_R1 + ADC_R2)) + DELTA;
    if (rawVolt < 0) rawVolt = 0;

    // First level smoothing - moving average of last 5 readings
    Volt5 = Volt4;
    Volt4 = Volt3;
    Volt3 = Volt2;
    Volt2 = Volt1;
    Volt1 = rawVolt;
    float firstSmooth = (Volt1 + Volt2 + Volt3 + Volt4 + Volt5) / 5;

    // Second level smoothing - circular buffer of 10 readings
    smoothBuffer[smoothIndex] = firstSmooth;
    smoothIndex = (smoothIndex + 1) % BATTERY_SMOOTH_COUNT;
    
    float sum = 0;
    for(uint8_t i = 0; i < BATTERY_SMOOTH_COUNT; i++) {
        sum += smoothBuffer[i];
    }
    Volt = sum / BATTERY_SMOOTH_COUNT;

    // Charging detection based on voltage change
    if (millis() - chargingState.lastCheck >= BATTERY_CHECK_INTERVAL) {
        chargingState.lastCheck = millis();
        float voltDelta = Volt - chargingState.lastVoltage;
        
        if (abs(voltDelta) < BATTERY_STABLE_THRESHOLD) {
            // Voltage is stable
            chargingState.stableCount++;
            if (chargingState.stableCount >= 3) { // If stable 3 times in a row
                chargingState.chargeCount = 0;
                chargingState.dischargeCount = 0;
                Charging = false; // No charging when stable
            }
        } else {
            chargingState.stableCount = 0; // Reset stability counter
            
            if (voltDelta > BATTERY_VOLTAGE_THRESHOLD && Volt >= BATTERY_CHARGE_MIN_VOLTAGE) {
                chargingState.chargeCount++;
                chargingState.dischargeCount = 0;
                if (chargingState.chargeCount >= 2) {
                    Charging = true;
                }
            } else if (voltDelta < -BATTERY_VOLTAGE_THRESHOLD) {
                chargingState.dischargeCount++;
                if (chargingState.dischargeCount >= 2) {
                    chargingState.chargeCount = 0;
                    chargingState.dischargeCount = 0;
                    Charging = false;
                }
            } else {
                chargingState.chargeCount = 0;
                chargingState.dischargeCount = 0;
            }
        }
        
        chargingState.lastVoltage = Volt;
    }

    // Calculate charge percentage
    uint8_t idx = 0;
    while (true) {
        if (Volt < vs[idx]) {ChargeLevel = 0; break;}
        if (Volt < vs[idx+1]) {
            mVolt = Volt - vs[idx];
            ChargeLevel = idx * 5 + round(mVolt /((vs[idx+1] - vs[idx]) / 5 ));
            break;
        }
        else {idx++;}
    }
         if (ChargeLevel < 0) ChargeLevel = 0;
     if (ChargeLevel > 100) ChargeLevel = 100;
 }
 #endif

Arduino_G* DspCore::getOutputDisplay() {
  return output_display;
}

// Block 8-E16.2B-2: legacy scroll fairness inert — no Pager/Page after Display member removal.
// Block 8-E16.2B-2: ротация ScrollWidget отключена; LVGL — единственный UI path на product.
int16_t DspCore::getScrollWidgetIndex(void* widget) {
  (void)widget;
  return -1;
}

void DspCore::normalizeScrollIndex() {
  _lastScrollIndex = -1;
}

void DspCore::advanceScrollIndex() {
  // no-op — no legacy active page / no Canvas scroll queue
}

#endif

