/************************************************************************************************
   Файл для дисплея AXS15231B (320x480) с тачскрином
   W76W
   4pda.ru
************************************************************************************************/

#include "../core/options.h"
#if DSP_MODEL==DSP_AXS15231B

#include "displayAXS15231B.h"
#include "fonts/bootlogo.h"
#include "../core/spidog.h"
#include "../core/config.h"
#include "../core/network.h"
#include "../core/display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_task_wdt.h"
#include "esp_cpu.h"

#include "Arduino_GFX.h"
#include "tools/GFX_Canvas_screen.h"
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

// === Глобальные объекты для Canvas-режима ===
static Arduino_DataBus *bus = nullptr;
static Arduino_GFX *output_display = nullptr;
Arduino_Canvas *gfx = nullptr;

#ifndef BATTERY_OFF

  #ifndef ADC_PIN
    #define ADC_PIN 5
  #endif
  #if (ADC_PIN == 5)
    #define USER_ADC_CHAN ADC1_CHANNEL_4
  #endif

  #ifndef R1
    #define R1 33		// Номинал резистора на плюс (+)
  #endif
  #ifndef R2
    #define R2 100		// Номинал резистора на плюс (-)
  #endif
  #ifndef DELTA_BAT
    #define DELTA_BAT 0	// Величина коррекции напряжения батареи
  #endif

  // Константы для оптимизации
  #define BATTERY_SAMPLES 100       // Количество замеров для точности
  #define BATTERY_CHECK_INTERVAL 5000  // Проверка каждые 5 секунд
  #define BATTERY_VOLTAGE_THRESHOLD 0.01f  // Порог изменения напряжения для определения зарядки
  #define BATTERY_SMOOTH_COUNT 10  // Количество измерений для сглаживания
  #define BATTERY_CHARGE_MIN_VOLTAGE 3.5f  // Минимальное напряжение для определения зарядки
  #define BATTERY_STABLE_THRESHOLD 0.002f  // Порог для определения стабильного напряжения

  float ADC_R1 = R1;        // Номинал резистора на плюс (+)
  float ADC_R2 = R2;        // Номинал резистора на минус (-)
  float DELTA = DELTA_BAT;  // Величина коррекции напряжения батареи

  uint8_t g, t = 1;         // Счётчики для мигалок и осреднений
  bool Charging = false;    // Признак, что подключено зарядное устройство

  float Volt = 0;           // Напряжение на батарее
  float Volt1 = 0, Volt2 = 0, Volt3 = 0, Volt4 = 0, Volt5 = 0;  // Предыдущие замеры напряжения
  float smoothBuffer[BATTERY_SMOOTH_COUNT] = {0};
  uint8_t smoothIndex = 0;
  float lastVolt = 0;       // Предыдущее напряжение для расчета изменений
  static esp_adc_cal_characteristics_t adc1_chars;

  uint8_t ChargeLevel;
  // Массив напряжений на батарее, соответствующий проценту оставшегося заряда: 
  float vs[22] = {2.60, 3.10, 3.20, 3.26, 3.29, 3.33, 3.37, 3.41, 3.46, 3.51, 3.56, 3.61, 3.65, 3.69, 3.72, 3.75, 3.78, 3.82, 3.88, 3.95, 4.03, 4.25};

  // Структура для хранения состояния зарядки
  struct {
    float lastVoltage = 0;
    uint32_t lastCheck = 0;
    uint8_t chargeCount = 0;    // Счетчик последовательных увеличений напряжения
    uint8_t dischargeCount = 0; // Счетчик последовательных падений напряжения
    uint8_t stableCount = 0;    // Счетчик стабильных измерений
  } chargingState;

#endif

DspCore::DspCore(): Arduino_AXS15231B(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */) {
  _scrollid = nullptr;
  _lastScroller = nullptr;
  _lastReleaseTime = 0;
  _lastScrollIndex = -1;
  _oldClockTop = 0;
#ifndef BATTERY_OFF
  // Инициализация ADC для батареи
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(USER_ADC_CHAN, ADC_ATTEN_DB_12);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &adc1_chars);
#endif
}

#include "tools/utf8RusGFX.h"
///////////////////////////////////////////////////////////////

// Day of week uppercase (can be overridden in myoptions.h)
#ifndef DOW_UPPERCASE
#define DOW_UPPERCASE false
#endif

void DspCore::initDisplay() {
  Serial.println("[AXS15231B] initDisplay start");
  
  // Инициализация подсветки (LEDC PWM)
  pinMode(GFX_BL, OUTPUT);
  // Инициализация подсветки: первичная привязка через analogWrite (ядро 3.x само настраивает LEDC)
  analogWrite(GFX_BL, map(config.store.brightness, 0, 100, 0, 255));

  // Проверяем и инициализируем шину
  if (!bus) {
    Serial.println("[AXS15231B] Initializing bus...");
    bus = new Arduino_ESP32QSPI(TFT_CS, TFT_SCK, TFT_D0, TFT_D1, TFT_D2, TFT_D3);
    if (!bus) {
      Serial.println("[AXS15231B] Failed to initialize bus!");
      return;
    }
  }

  // Проверяем и инициализируем дисплей
  if (!output_display) {
    Serial.println("[AXS15231B] Initializing display...");
    output_display = new Arduino_AXS15231B(bus, TFT_RST, 0, false, 320, 480, 0, 0, 0, 0);
    if (!output_display) {
      Serial.println("[AXS15231B] Failed to initialize display!");
      return;
    }
  }

  // Проверяем и инициализируем canvas
  if (!gfx) {
    Serial.println("[AXS15231B] Initializing canvas...");
    gfx = new Arduino_Canvas(320, 480, output_display);
    if (!gfx) {
      Serial.println("[AXS15231B] Failed to create canvas!");
      return;
    }
    
    if (!gfx->begin()) {
      Serial.println("[AXS15231B] Failed to begin canvas!");
      delete gfx;
      gfx = nullptr;
      return;
    }
    Serial.println("[AXS15231B] Canvas initialized successfully");
  }

  Serial.print("[AXS15231B] Canvas ptr: "); Serial.println((uintptr_t)gfx, HEX);

  plItemHeight = playlistConf.widget.textsize*(CHARHEIGHT-1)+playlistConf.widget.textsize*4;
  plTtemsCount = round((float)height()/plItemHeight);
  if(plTtemsCount%2==0) plTtemsCount++;
  plCurrentPos = plTtemsCount/2;
  plYStart = (height() / 2 - plItemHeight / 2) - plItemHeight * (plTtemsCount - 1) / 2 + playlistConf.widget.textsize*2;
  
  Serial.println("[AXS15231B] initDisplay completed successfully");
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
    Serial.println("[AXS15231B] drawLogo call");
    
    if (!gfx) {
        Serial.println("[AXS15231B] drawLogo: gfx is nullptr!");
        return;
    }
    
    // Временно гасим подсветку на время отрисовки логотипа
    ledcWrite(GFX_BL, 0);
    
    // Очищаем область под логотип
    gfxFillRect(gfx, 0, top, width(), 88, config.theme.background);
    
    // Рисуем логотип
    gfxDrawBitmap(gfx, (width() - 134) / 2, top, bootlogo2, 134, 88);
    
    // Возвращаем яркость согласно настройке
    ledcWrite(GFX_BL, map(config.store.brightness, 0, 100, 0, 255));
    
    // Уменьшаем задержку
    delay(100);
}

// Функция для вычисления ширины строки для стандартного шрифта Adafruit_GFX
uint16_t DspCore::textWidthGFX(const char *txt, uint8_t textsize) {
  return strlen(txt) * CHARWIDTH * textsize;
}

void DspCore::printPLitem(uint8_t pos, const char* item, ScrollWidget& current, bool uppercase){
  if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (printPLitem)"); return; }
  if (pos == plCurrentPos) {
    current.setText(item);
  } else {
    uint8_t plColor = (abs(pos - plCurrentPos)-1)>4?4:abs(pos - plCurrentPos)-1;
    gfxFillRect(gfx, 0, plYStart + pos * plItemHeight - 1, width(), plItemHeight - 2, config.theme.background);
    // Обрезка строки по ширине без троеточия
    const char* rus = utf8Rus(item, true);
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
  if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (drawPlaylist)"); return; }
  uint8_t lastPos = config.fillPlMenu(currentItem - plCurrentPos, plTtemsCount);
  if(lastPos<plTtemsCount){
    gfxFillRect(gfx, 0, lastPos*plItemHeight+plYStart, width(), height()/2, config.theme.background);
  }
}

void DspCore::clearDsp(bool black) { if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (clearDsp)"); return; } gfxClearScreen(gfx, black?0:config.theme.background); }

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
  if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (_clockSeconds)"); return; }
  // Секунды
  char secbuf[8];
  snprintf(secbuf, sizeof(secbuf), "%02d", network.timeinfo.tm_sec);
  gfxDrawText(
    gfx,
    width()  - clockRightSpace - CHARWIDTH*4*2-10,
    clockTop-clockTimeHeight+48,
    secbuf,
    config.theme.seconds,
    config.theme.background,
    4
  );
  
  // Очищаем область под двоеточием (только область самого двоеточия) - для старого шрифта
  gfxFillRect(gfx, _timeleft+_dotsLeft+5, clockTop-CHARHEIGHT+5, 15, 65, config.theme.background);
  
  // Двоеточие с прозрачным фоном
  gfxDrawText(
    gfx,
    _timeleft+_dotsLeft +1,
    clockTop-CHARHEIGHT+75,
    ":",
    (network.timeinfo.tm_sec % 2 == 0) ? config.theme.clock : (CLOCKFONT_MONO?config.theme.clockbg:config.theme.background),
    (network.timeinfo.tm_sec % 2 == 0) ? config.theme.clock : (CLOCKFONT_MONO?config.theme.clockbg:config.theme.background),
    1,
    &DS_DIGI56pt7b
  );
#ifndef BATTERY_OFF
  if(!config.isScreensaver) {
    // Мигалки и батарейка
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
      // Статическая батарейка
      if (Volt >= 3.82)      { batcolor = color565(100, 255, 150); strcpy(batbuf, "\xA0\xA2\xA2\xA3"); }
      else if (Volt >= 3.72) { batcolor = color565(50, 255, 100);  strcpy(batbuf, "\x9D\xA2\xA2\xA3"); }
      else if (Volt >= 3.61) { batcolor = color565(0, 255, 0);     strcpy(batbuf, "\x9D\xA1\xA2\xA3"); }
      else if (Volt >= 3.46) { batcolor = color565(75, 255, 0);    strcpy(batbuf, "\x9D\x9E\xA2\xA3"); }
      else if (Volt >= 3.33) { batcolor = color565(150, 255, 0);   strcpy(batbuf, "\x9D\x9E\xA1\xA3"); }
      else if (Volt >= 3.20) { batcolor = color565(255, 255, 0);   strcpy(batbuf, "\x9D\x9E\x9E\xA3"); }
      else if (Volt >= 2.8)  { batcolor = color565(255, 0, 0);     strcpy(batbuf, "\x9D\x9E\x9E\x9F"); }
    }
    gfxDrawText(gfx, BatX, BatY, batbuf, batcolor, config.theme.background, BatFS);
#ifndef HIDE_VOLT
    char voltbuf[16];
    snprintf(voltbuf, sizeof(voltbuf), "%.3fv", Volt);
    gfxDrawText(gfx, VoltX, VoltY, voltbuf, batcolor, config.theme.background, VoltFS);
#endif
    char procbuf[8];
    snprintf(procbuf, sizeof(procbuf), "%3i%%", ChargeLevel);
    gfxDrawText(gfx, ProcX, ProcY, procbuf, batcolor, config.theme.background, ProcFS);
  }
#endif
}

void DspCore::_clockDate(){
  if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (_clockDate)"); return; }
  if(_olddateleft>0)
    gfxFillRect(gfx, _olddateleft,  clockTop+78, _olddatewidth, CHARHEIGHT*2, config.theme.background); //очистка надписи даты
  gfxDrawText1b(gfx, _dateleft, clockTop+78, _dateBuf, config.theme.date, config.theme.background, 2);
  strlcpy(_oldDateBuf, _dateBuf, sizeof(_dateBuf));
  _olddatewidth = _datewidth;
  _olddateleft = _dateleft;
  // День недели
  gfxDrawText(
    gfx,
    width() - clockRightSpace - CHARWIDTH*4*2+13-20,
    clockTop-CHARHEIGHT+44,
    dow[network.timeinfo.tm_wday],
    config.theme.dow,
    config.theme.background,
    3,
    nullptr,
    DOW_UPPERCASE
  );
}

void DspCore::_clockTime(){
  if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (_clockTime)"); return; }
  if(_oldtimeleft>0 && !CLOCKFONT_MONO)
    gfxFillRect(gfx, _oldtimeleft, clockTop-clockTimeHeight+1, _oldtimewidth, clockTimeHeight, config.theme.background);
  _timeleft = width()-clockRightSpace-CHARWIDTH*4*2-24-_timewidth;
  // Время
  gfxDrawText(
    gfx,
    _timeleft,
    clockTop + 63,
    _timeBuf,
    config.theme.clock,
    config.theme.background,
    1,
    &DS_DIGI56pt7b
  );
  strlcpy(_oldTimeBuf, _timeBuf, sizeof(_timeBuf));
  _oldtimewidth = _timewidth;
  _oldtimeleft = _timeleft;
  // Вертикальный разделитель
  //gfxDrawLine(gfx, width()-clockRightSpace-CHARWIDTH*4*2-25, clockTop +5, width()-clockRightSpace-CHARWIDTH*4*2-25, clockTop +5 + clockTimeHeight-3, config.theme.div);
  // Горизонтальный разделитель
  //gfxDrawLine(gfx, width()-clockRightSpace-CHARWIDTH*4*2+10, clockTop+32, width()-clockRightSpace-CHARWIDTH*4*2+10+62-1, clockTop+32, config.theme.div);
  sprintf(_buffordate, "%2d %s %d", network.timeinfo.tm_mday,mnths[network.timeinfo.tm_mon], network.timeinfo.tm_year+1900);
  strlcpy(_dateBuf, utf8Rus(_buffordate, true), sizeof(_dateBuf));
  _datewidth = strlen(_dateBuf) * CHARWIDTH*2;
  _dateleft = width() - clockRightSpace - _datewidth - 80;
}

void DspCore::printClock(uint16_t top, uint16_t rightspace, uint16_t timeheight, bool redraw){
  // Ограничиваем верхнюю границу
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
  if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (clearClock)"); return; }
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
    if (millis() - lastBatteryUpdate >= 1000) { // Обновляем каждую секунду
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
  if (!gfx) { Serial.println("[AXS15231B] gfx is nullptr! (setTextSize)"); return; }
  gfx->setTextSize(s);
}

void DspCore::flip(){
  TAKE_MUTEX();
  if (output_display) output_display->setRotation(config.store.flipscreen ? 2 : 0);
  GIVE_MUTEX();
}

void DspCore::invert(){
  TAKE_MUTEX();
  if (output_display) output_display->invertDisplay(config.store.invertdisplay);
  GIVE_MUTEX();
}

void DspCore::sleep(void) { 
  Serial.println("DspCore::sleep");
  TAKE_MUTEX();
  displayOff();
  ledcWrite(GFX_BL, 0); // Выключаем подсветку через PWM
  GIVE_MUTEX();
}

void DspCore::wake(void) {
  Serial.println("DspCore::wake");
  TAKE_MUTEX();
  displayOn();
  ledcWrite(GFX_BL, map(config.store.brightness, 0, 100, 0, 255)); // Устанавливаем яркость через PWM
  GIVE_MUTEX();
}

void DspCore::setBrightness(uint8_t brightness) {
  TAKE_MUTEX();
  ledcWrite(GFX_BL, map(brightness, 0, 100, 0, 255));
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
  setFont(&DS_DIGI56pt7b);
  setTextSize(1);
  //  setFont(NULL);
  //  setTextSize(12);
}

uint16_t DspCore::width() {
    return 320;
}

uint16_t DspCore::height() {
    return 480;
}

#ifndef BATTERY_OFF
void DspCore::readBattery() {
    static uint32_t lastRead = 0;
    if (millis() - lastRead < 100) return; // Ограничиваем частоту чтений
    lastRead = millis();

    float tempmVolt = 0;
    for(uint8_t i = 0; i < BATTERY_SAMPLES; i++) {
        tempmVolt += esp_adc_cal_raw_to_voltage(adc1_get_raw(USER_ADC_CHAN), &adc1_chars);
    }
    float mVolt = (tempmVolt / BATTERY_SAMPLES) / 1000;
    float rawVolt = (mVolt + 0.0028 * mVolt * mVolt + 0.0096 * mVolt - 0.051) / (ADC_R2 / (ADC_R1 + ADC_R2)) + DELTA;
    if (rawVolt < 0) rawVolt = 0;

    // Первый уровень сглаживания - скользящее среднее по 5 последним измерениям
    Volt5 = Volt4;
    Volt4 = Volt3;
    Volt3 = Volt2;
    Volt2 = Volt1;
    Volt1 = rawVolt;
    float firstSmooth = (Volt1 + Volt2 + Volt3 + Volt4 + Volt5) / 5;

    // Второй уровень сглаживания - циклический буфер из 10 измерений
    smoothBuffer[smoothIndex] = firstSmooth;
    smoothIndex = (smoothIndex + 1) % BATTERY_SMOOTH_COUNT;
    
    float sum = 0;
    for(uint8_t i = 0; i < BATTERY_SMOOTH_COUNT; i++) {
        sum += smoothBuffer[i];
    }
    Volt = sum / BATTERY_SMOOTH_COUNT;

    // Определение зарядки на основе изменения напряжения
    if (millis() - chargingState.lastCheck >= BATTERY_CHECK_INTERVAL) {
        chargingState.lastCheck = millis();
        float voltDelta = Volt - chargingState.lastVoltage;
        
        if (abs(voltDelta) < BATTERY_STABLE_THRESHOLD) {
            // Напряжение стабильно
            chargingState.stableCount++;
            if (chargingState.stableCount >= 3) { // Если напряжение стабильно 3 раза подряд
                chargingState.chargeCount = 0;
                chargingState.dischargeCount = 0;
                Charging = false; // Нет зарядки при стабильном напряжении
            }
        } else {
            chargingState.stableCount = 0; // Сбрасываем счетчик стабильности
            
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

    // Расчет процента заряда
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

