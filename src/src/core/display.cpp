#include "options.h"

#include "WiFi.h"
#include "time.h"
#include "display.h"
#include "player.h"
#include "network.h"
#include "../displays/tools/GFX_Canvas_screen.h"
#include "../core/spidog.h"
#include "../lvgl_ui/lvgl_ui.h"
#include "../lvgl_ui/lv_ui_events.h"
extern Arduino_Canvas* gfx;

// Глобальный флаг "кадр грязный" для dirty-based flush
static volatile bool g_frameDirty = false;

// Helper-функция для установки флага dirty (вызывается из функций рисования)
void markFrameDirty() {
    g_frameDirty = true;
}

Display display;
#ifdef USE_NEXTION
Nextion nextion;
#endif

#ifndef DUMMYDISPLAY
//============================================================================================================================
DspCore dsp;

Page *pages[] = { new Page(), new Page(), new Page(), new Page() };

#ifndef DSQ_SEND_DELAY
  #define DSQ_SEND_DELAY portMAX_DELAY
#endif

#ifndef CORE_STACK_SIZE
  #define CORE_STACK_SIZE  1024*3
#endif
#ifndef DSP_TASK_DELAY
  #define DSP_TASK_DELAY  pdMS_TO_TICKS(5)
#endif
#if !((DSP_MODEL==DSP_ST7735 && DTYPE==INITR_BLACKTAB) || DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7789_170 || DSP_MODEL==DSP_ST7796 || DSP_MODEL==DSP_ILI9488 || DSP_MODEL==DSP_ILI9486 || DSP_MODEL==DSP_ILI9341 || DSP_MODEL==DSP_ILI9225 || DSP_MODEL==DSP_AXS15231B || DSP_MODEL==DSP_ST7701 || DSP_MODEL==DSP_UEDX48480021)
  #undef  BITRATE_FULL
  #define BITRATE_FULL     false
#endif
TaskHandle_t DspTask;
QueueHandle_t displayQueue;

void returnPlayer(){
  display.putRequest(NEWMODE, PLAYER);
}

void Display::_createDspTask(){
  // Display Task on Core 0 for better touchscreen response
  // Display Task на ядре 0 для лучшей отзывчивости touchscreen
  // Priority lowered to 3 to give Audio Task more CPU time
  // Приоритет снижен до 3 для предоставления Audio Task больше времени CPU
  xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE,  NULL,  3, &DspTask, 0);
}

void loopDspTask(void * pvParameters){
  while(true){
    if(displayQueue==NULL) break;
    display.loop();
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2) && defined(LVGL_DEBUG_STACK)
    // Optional one-time watermark print for DspTask stack.
    // Необязательный однократный вывод watermark стека DspTask.
    static bool s_printed = false;
    if (!s_printed) {
      UBaseType_t watermark = uxTaskGetStackHighWaterMark(nullptr);
      Serial.print("[LVGL] DspTask stack high watermark: ");
      Serial.println(watermark);
      s_printed = true;
    }
#endif
    vTaskDelay(DSP_TASK_DELAY);
  }
  vTaskDelete( NULL );
  DspTask=NULL;
}

void Display::init() {
  Serial.print("##[BOOT]#\tdisplay.init\t");
  
#ifdef USE_NEXTION
  nextion.begin();
#endif

#if LIGHT_SENSOR!=255
  analogSetAttenuation(ADC_0db);
#endif

  _bootStep = 0;
  _suspendFlush = true; // блокируем flush до полной сборки страниц
  
  // Инициализация дисплея
  dsp.initDisplay();
  // Жёстко отключим любые метры до создания страниц (во избежание стартовых артефактов)
  _deactivateAllMeters();
  
  // Инициализация Spectrum Analyzer (всегда)
  if (!spectrumAnalyzer.init()) {
    Serial.println("[Display] Failed to initialize Spectrum Analyzer!");
  } else {
    Serial.println("[Display] Spectrum Analyzer initialized successfully");
  }
  
  // Проверяем, что gfx инициализирован
  if (!gfx) {
    Serial.println("[Display] Failed to initialize display!");
    return;
  }

  lvgl_ui::initRuntime();
  lvgl_ui::initTick();
  lvgl_ui::initDisplayDriver(dsp.width(), dsp.height());
  lvgl_ui::createTestOverlay();

  // Создаем очередь для дисплея
  displayQueue = xQueueCreate(5, sizeof(requestParams_t));
  if (!displayQueue) {
    Serial.println("[Display] Failed to create display queue!");
    return;
  }
  
  // Создаем задачу для дисплея
  _createDspTask();
  
  // Ждем завершения инициализации
  while(_bootStep == 0) { 
    delay(10); 
  }
  
  // Создаем загрузочный экран
  _bootScreen();
  
  Serial.println("done");
}
void Display::_applyPendingAI() {
  // Apply pending AI interpretation when returning to PG_PLAYER page
  if (!_aiPending || !_ai_interpretation) return;
  
  if (strlen(_aiPendingText) == 0) {
    // Empty text - hide widget
    _ai_interpretation->setText("");
    _ai_interpretation->setActive(false, true);
  } else {
    // Non-empty text - show widget
    _ai_interpretation->setText(_aiPendingText);
    _ai_interpretation->setActive(true);
  }
  _aiPending = false;  // Clear pending flag after applying
}

void Display::_deactivateAllMeters(){
  sdog.takeMutex();
  if(_spectrumwidget) _spectrumwidget->setActive(false, true);
  if(_vuwidget) _vuwidget->lock(true);
  // Очистим обе области на всякий случай
  #if defined(spectrumConf)
  #endif
  sdog.giveMutex();
}

void Display::_bootScreen(){
  if (!gfx) {
    Serial.println("[Display] gfx is nullptr in _bootScreen!");
    return;
    delay(1000);
  }
  
  // Защита от повторного вызова
  static bool bootScreenCreated = false;
  if(bootScreenCreated) {
    Serial.println("[Display] _bootScreen already created, skipping");
    return;
  }
  bootScreenCreated = true;
  
  Serial.println("[Display] Creating boot screen");
  _boot = new Page();
  _boot->addWidget(new ProgressWidget(bootWdtConf, bootPrgConf, BOOT_PRG_COLOR, 0));
  _bootstring = (TextWidget*) &_boot->addWidget(new TextWidget(bootstrConf, 50, true, BOOT_TXT_COLOR, 0));
  _pager.addPage(_boot);
  _pager.setPage(_boot, true);
  dsp.drawLogo(bootLogoTop);
  _bootStep = 1;
  
  // Разрешаем flush для анимации bootScreen / Enable flush for bootScreen animation
  _suspendFlush = false;
  Serial.println("[Display] Boot screen created, flush enabled for animation");
}

void Display::_deactivateLegacyPagerForLvgl() {
  // Same audioinfo policy as legacy _start() — was skipped on Lvgl path.
  // Та же политика audioinfo, что в legacy _start() (на ветке Lvgl не вызывалась).
  if (_heapbar) {
    _heapbar->lock(!config.store.audioinfo);
  }
  for (unsigned i = 0; i < 4; ++i) {
    if (pages[i]) {
      pages[i]->setActive(false);
    }
  }
}

void Display::_buildPager(){
  Serial.println("[DEBUG] _buildPager() called");
  Serial.print("[DEBUG] usespectrum: ");
  Serial.println(config.store.usespectrum);
#ifdef HIDE_VU
  Serial.println("[DEBUG] HIDE_VU is defined");
#else
  Serial.println("[DEBUG] HIDE_VU is NOT defined");
#endif

  _meta.init("*", metaConf, config.theme.meta, config.theme.metabg);
  _title1.init("*", title1Conf, config.theme.title1, config.theme.background);
  _clock.init(clockConf, 0, 0);
  #if DSP_MODEL==DSP_NOKIA5110
    _plcurrent.init("*", playlistConf, 0, 1);
  #else
    _plcurrent.init("*", playlistConf, config.theme.plcurrent, config.theme.plcurrentbg);
  #endif
  #if !defined(DSP_LCD)
    _plcurrent.moveTo({TFT_FRAMEWDT, (uint16_t)(dsp.plYStart+dsp.plCurrentPos*dsp.plItemHeight), (int16_t)playlistConf.width});
  #endif
  #ifndef HIDE_TITLE2
    _title2 = new ScrollWidget("*", title2Conf, config.theme.title2, config.theme.background);
  #endif
  #if !defined(DSP_LCD) && DSP_MODEL!=DSP_NOKIA5110
    _plbackground = new FillWidget(playlBGConf, config.theme.plcurrentfill);
    #if DSP_INVERT_TITLE || defined(DSP_OLED)
      _metabackground = new FillWidget(metaBGConf, config.theme.metafill);
    #else
      _metabackground = new FillWidget(metaBGConfInv, config.theme.metafill);
    #endif
  #endif
  #if DSP_MODEL==DSP_NOKIA5110
    _plbackground = new FillWidget(playlBGConf, 1);
    //_metabackground = new FillWidget(metaBGConf, 1);
  #endif
  // Создаём оба виджета; их активность управляется в рантайме
  #if DSP_MODEL==DSP_AXS15231B
    // spectrumConf теперь определена в displayAXS15231Bconf.h
    _spectrumwidget = new SpectrumWidget(spectrumConf);
  #elif DSP_MODEL==DSP_ST7701
    // spectrumConf теперь определена в displayST7701conf.h
    _spectrumwidget = new SpectrumWidget(spectrumConf);
  #elif DSP_MODEL==DSP_UEDX48480021
    // spectrumConf определена в displayUEDX48480021conf.h
    _spectrumwidget = new SpectrumWidget(spectrumConf);
  #else
    _spectrumwidget = new SpectrumWidget();
  #endif
  #if !defined(HIDE_VU)
    _vuwidget = new VuWidget(vuConf, bandsConf, config.theme.vumax, config.theme.vumin, config.theme.background);
  #endif
  _usingSpectrum = config.store.usespectrum;
  // Сразу отключим оба, чтобы до первичной инициализации они не мигали
  if(_spectrumwidget) _spectrumwidget->setActive(false, true);
  if(_vuwidget) _vuwidget->lock(true);
  #ifndef HIDE_VOLBAR
    _volbar = new SliderWidget(volbarConf, config.theme.volbarin, config.theme.background, 254, config.theme.volbarout);
  #endif
  #ifndef HIDE_HEAPBAR
    _heapbar = new SliderWidget(heapbarConf, config.theme.buffer, config.theme.background, 655350);
  #endif
  #ifndef HIDE_VOL
    _voltxt = new TextWidget(voltxtConf, 10, false, config.theme.vol, config.theme.background);
  #endif
  #ifndef HIDE_IP
    _volip = new TextWidget(iptxtConf, 30, false, config.theme.ip, config.theme.background);
  #endif
  #ifndef HIDE_RSSI
    _rssi = new TextWidget(rssiConf, 20, false, config.theme.rssi, config.theme.background);
  #endif
  _nums.init(numConf, 10, false, config.theme.digit, config.theme.background);
  #ifndef HIDE_WEATHER
    _weather = new ScrollWidget("\007", weatherConf, config.theme.weather, config.theme.background);
  #endif
  // AI interpretation widget / Виджет AI интерпретации
  _ai_interpretation = new ScrollWidget(" ", interpretationConf, config.theme.interpretation, config.theme.background);
  _ai_interpretation->setActive(false);  // По умолчанию скрыт / Hidden by default
  
  if(_volbar)   _footer.addWidget( _volbar);
  if(_voltxt)   _footer.addWidget( _voltxt);
  if(_volip)    _footer.addWidget( _volip);
  if(_rssi)     _footer.addWidget( _rssi);
  if(_heapbar)  _footer.addWidget( _heapbar);
  
  if(_metabackground) pages[PG_PLAYER]->addWidget( _metabackground);
  pages[PG_PLAYER]->addWidget(&_meta);
  pages[PG_PLAYER]->addWidget(&_title1);
  if(_title2) pages[PG_PLAYER]->addWidget(_title2);
  if(_ai_interpretation) pages[PG_PLAYER]->addWidget(_ai_interpretation);
  if(_weather) pages[PG_PLAYER]->addWidget(_weather);
  #if BITRATE_FULL
    _fullbitrate = new BitrateWidget(fullbitrateConf, config.theme.bitrate, config.theme.background);
    pages[PG_PLAYER]->addWidget( _fullbitrate);
  #else
    _bitrate = new TextWidget(bitrateConf, 30, false, config.theme.bitrate, config.theme.background);
    pages[PG_PLAYER]->addWidget( _bitrate);
  #endif
  if(_vuwidget) pages[PG_PLAYER]->addWidget(_vuwidget);
  if(_spectrumwidget) pages[PG_PLAYER]->addWidget(_spectrumwidget);
  // И после добавления в страницу ещё раз гарантированно выключим оба
  _deactivateAllMeters();
  pages[PG_PLAYER]->addWidget(&_clock);
  pages[PG_SCREENSAVER]->addWidget(&_clock);
  pages[PG_PLAYER]->addPage(&_footer);

  if(_metabackground) pages[PG_DIALOG]->addWidget( _metabackground);
  pages[PG_DIALOG]->addWidget(&_meta);
  pages[PG_DIALOG]->addWidget(&_nums);
  
  #if !defined(DSP_LCD) && DSP_MODEL!=DSP_NOKIA5110
    pages[PG_DIALOG]->addPage(&_footer);
  #endif
  #if !defined(DSP_LCD)
  if(_plbackground) {
    pages[PG_PLAYLIST]->addWidget( _plbackground);
    _plbackground->setHeight(dsp.plItemHeight);
    _plbackground->moveTo({0,(uint16_t)(dsp.plYStart+dsp.plCurrentPos*dsp.plItemHeight-playlistConf.widget.textsize*2), (int16_t)playlBGConf.width});
  }
  #endif
  pages[PG_PLAYLIST]->addWidget(&_plcurrent);

#ifdef CPU_LOAD
  // Добавляем CPU виджет в страницу плеера
  dsp.cpuWidget.init(cpuConf, 20, false, config.theme.rssi, config.theme.background);
  dsp.cpuWidget.setActive(true);
#endif

  for(const auto& p: pages) _pager.addPage(p);
}

void Display::_apScreen() {
  _suspendFlush = false;  // Разрешаем обновления экрана в AP режиме / Enable screen updates in AP mode
  Serial.println("[Display] _apScreen() called");
  Serial.printf("[Display] _suspendFlush = %s\n", _suspendFlush ? "true" : "false");
  if(_boot) _pager.removePage(_boot);
  #ifndef DSP_LCD
    _boot = new Page();
    #if DSP_MODEL!=DSP_NOKIA5110
      #if DSP_INVERT_TITLE || defined(DSP_OLED)
      _boot->addWidget(new FillWidget(metaBGConf, config.theme.metafill));
      #else
      _boot->addWidget(new FillWidget(metaBGConfInv, config.theme.metafill));
      #endif
    #endif
    ScrollWidget *bootTitle = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*", apTitleConf, config.theme.meta, config.theme.metabg));
    bootTitle->setText("ёRadio AP Mode");
    TextWidget *apname = (TextWidget*) &_boot->addWidget(new TextWidget(apNameConf, 30, false, config.theme.title1, config.theme.background));
    apname->setText(apNameTxt);
    TextWidget *apname2 = (TextWidget*) &_boot->addWidget(new TextWidget(apName2Conf, 30, false, config.theme.clock, config.theme.background));
    apname2->setText(apSsid);
    TextWidget *appass = (TextWidget*) &_boot->addWidget(new TextWidget(apPassConf, 30, false, config.theme.title1, config.theme.background));
    appass->setText(apPassTxt);
    TextWidget *appass2 = (TextWidget*) &_boot->addWidget(new TextWidget(apPass2Conf, 30, false, config.theme.clock, config.theme.background));
    appass2->setText(apPassword);
    ScrollWidget *bootSett = (ScrollWidget*) &_boot->addWidget(new ScrollWidget("*", apSettConf, config.theme.title2, config.theme.background));
    bootSett->setText(WiFi.softAPIP().toString().c_str(), apSettFmt);
    _pager.addPage(_boot);
    _pager.setPage(_boot, false);
    
    // Принудительно обновляем экран после создания AP screen
    if(gfx) {
      sdog.takeMutex();
      gfxFlushScreen(gfx);
      sdog.giveMutex();
      Serial.println("[Display] AP screen flushed to display");
    }
  #else
    dsp.apScreen();
  #endif
}

void Display::_start() {
  Serial.println("[Display] _start() called");
  Serial.printf("[Display] network.status = %d\n", network.status);
  if(_boot) _pager.removePage(_boot);
  #ifdef USE_NEXTION
    nextion.wake();
  #endif
  if (network.status != CONNECTED && network.status != SDREADY) {
    _suspendFlush = false; // разрешаем flush в AP режиме / enable flush in AP mode
    Serial.println("[Display] Going to AP mode");
    _apScreen();
    #ifdef USE_NEXTION
      nextion.apScreen();
    #endif
    _bootStep = 2;
    Serial.println("[Display] _bootStep set to 2 in AP mode");
    return;
  }
  #ifdef USE_NEXTION
    //nextion.putcmd("page player");
    nextion.start();
  #endif
  _buildPager();
  _mode = PLAYER;
  config.setTitle(const_PlReady);

  // Stage 5.5 guard: if PLAYER backend is LVGL, skip legacy player page setup.
  // Guard 5.5: если backend PLAYER = LVGL, пропускаем legacy подготовку страницы плейера.
  lvgl_ui::UiBackend startBackend = lvgl_ui::getPreferredBackend(PLAYER);
  if (startBackend == lvgl_ui::UiBackend::Lvgl) {
    _activeBackend = startBackend;
    lvgl_ui::onModeChanged(PLAYER, startBackend);
    _deactivateLegacyPagerForLvgl();
    _bootStep = 2;
    _suspendFlush = false;
    pm.on_display_player();
    return;
  }

  // Perform deferred AI widget clear if needed (after widgets are initialized)
  // Выполнить отложенную очистку AI виджета если нужно (после инициализации виджетов)
  extern void aiPerformDeferredClearIfNeeded();
  aiPerformDeferredClearIfNeeded();
  // Перед первой отрисовкой гарантированно погасим оба виджета
  _deactivateAllMeters();
  // Перед первой отрисовкой гарантированно погасим оба виджета
  _deactivateAllMeters();
  
  if(_heapbar)  _heapbar->lock(!config.store.audioinfo);
  
  if(_weather)  _weather->lock(!config.store.showweather);
  if(_weather && config.store.showweather)  _weather->setText(const_getWeather);

  if(_rssi)     _setRSSI(WiFi.RSSI());
  #ifndef HIDE_IP
    if(_volip && network.status == CONNECTED) _volip->setText(WiFi.localIP().toString().c_str(), iptxtFmt);
  #endif
  _pager.setPage( pages[PG_PLAYER]);
  // Применяем pending AI интерпретацию при старте на странице плейера
  _applyPendingAI();
  // Применяем состояние метров ТОЛЬКО после установки страницы, чтобы Pager не переактивировал виджеты
  _deactivateAllMeters();
  // На старте активируем/деактивируем метры строго по состоянию плеера
  _layoutChange(player.isRunning());
  _volume();
  _station();
  _time(false);
  _bootStep = 2;
  _suspendFlush = false; // разрешаем flush после полной подготовки
  pm.on_display_player();
}

void Display::_showDialog(const char *title){
  dsp.setScrollId(NULL);
  _pager.setPage( pages[PG_DIALOG]);
  // Принудительно перерисовываем фон мета ПОСЛЕ переключения страницы (чтобы не затерся clearDsp)
  if (_metabackground) {
    _metabackground->setActive(true, true); // true, true означает очистку и перерисовку
  }
  #ifdef META_MOVE
    _meta.moveTo(metaMove);
  #endif
  _meta.setAlign(WA_CENTER);
  _meta.setText(title);
}

void Display::_setReturnTicker(uint8_t time_s){
  _returnTicker.detach();
  _returnTicker.once(time_s, returnPlayer);
}

void Display::_swichMode(displayMode_e newmode) {
  #ifdef USE_NEXTION
    //nextion.swichMode(newmode);
    nextion.putRequest({NEWMODE, newmode});
  #endif
  if (newmode == _mode || (network.status != CONNECTED && network.status != SDREADY)) return;
  
  // Сбрасываем флаги смены режимов
  _isStationsChanging = false;
  _isVolumeChanging = false;
  
  _mode = newmode;
  dsp.setScrollId(NULL);
  if (newmode == PLAYER) {
    // Common state resets (needed for both legacy and LVGL backends).
    // Общий сброс состояния (нужен для обоих backend'ов).
    numOfNextStation = 0;
    _returnTicker.detach();
    config.isScreensaver = false;

    // Stage 5.5 guard: LVGL Main screen — skip legacy PLAYER page setup.
    // Guard 5.5: LVGL Main — пропускаем legacy подготовку страницы плейера.
    if (lvgl_ui::getPreferredBackend(PLAYER) == lvgl_ui::UiBackend::Lvgl) {
      pm.on_display_player();
      // backend + onModeChanged handled at end of _swichMode (common tail).
    } else {
      if(player.isRunning())
        _clock.moveTo(clockMove);
      else
        _clock.moveBack();
      #ifdef DSP_LCD
        dsp.clearDsp();
      #endif
      #ifdef META_MOVE
        _meta.moveBack();
      #endif
      _meta.setAlign(metaConf.widget.align);
      _nums.setText("");
      _pager.setPage( pages[PG_PLAYER]);
      if (_metabackground) {
        _metabackground->setActive(true, true);
      }
      _meta.setText(config.station.name);
      config.setDspOn(config.store.dspon, false);
      pm.on_display_player();
      _applyPendingAI();
      _layoutChange(player.isRunning());
    }
  }
  if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
    config.isScreensaver = true;
    _pager.setPage( pages[PG_SCREENSAVER]);
    if (newmode == SCREENBLANK) {
      dsp.clearClock();
      config.setDspOn(false, false);
    }
  }else{
    config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    config.isScreensaver = false;
  }
  if (newmode == VOL) {
    #ifndef HIDE_VOLPAGE
      #ifndef HIDE_IP
        _showDialog(const_DlgVolume);
      #else
        _showDialog(WiFi.localIP().toString().c_str());
      #endif
    #endif
    _nums.setText(config.store.volume, numtxtFmt);
  }
  if (newmode == LOST)      _showDialog(const_DlgLost);
  if (newmode == UPDATING)  _showDialog(const_DlgUpdate);
  if (newmode == SLEEPING)  _showDialog("SLEEPING");
  if (newmode == SDCHANGE)  _showDialog(const_waitForSD);
  if (newmode == INFO || newmode == SETTINGS || newmode == TIMEZONE || newmode == WIFI) _showDialog(const_DlgNextion);
  if (newmode == NUMBERS) _showDialog("");
  if (newmode == STATIONS) {
    _pager.setPage( pages[PG_PLAYLIST]);
    _plcurrent.setText("");
    currentPlItem = config.lastStation();
    _drawPlaylist();
  }

  // Stage 4.1: store active backend for current mode (metadata only; no hot-path change).
  lvgl_ui::UiBackend backend = lvgl_ui::getPreferredBackend(newmode);
  // При переходе INFO -> PLAYER очистка и отрисовка уже выполнены в setPage(pages[PG_PLAYER])
  // выше; повторный clearDsp() здесь затирал бы нарисованное (баг исправлен).
  _activeBackend = backend;
  lvgl_ui::onModeChanged(newmode, backend);
  if (backend == lvgl_ui::UiBackend::Lvgl) {
    _deactivateLegacyPagerForLvgl();
  }
}

void Display::resetQueue(){
  if(displayQueue!=NULL) xQueueReset(displayQueue);
}

void Display::_drawPlaylist() {
  dsp.drawPlaylist(currentPlItem);
  _setReturnTicker(10);
}

void Display::_drawNextStationNum(uint16_t num) {
  _setReturnTicker(10);
  _meta.setText(config.stationByNum(num));
  _nums.setText(num, "%d");
}

void Display::printPLitem(uint8_t pos, const char* item, bool uppercase){
  dsp.printPLitem(pos, item, _plcurrent, uppercase);
}

void Display::setAIInterpretation(const String& text) {
  // AI interpretation widget / Виджет AI интерпретации
  // Draw only on PG_PLAYER page, otherwise save to pending
  if (!_ai_interpretation) return;
  
  // Runtime gate: если AI выключен, очищаем виджет и выходим до записи pending
  // Runtime gate: if AI is disabled, clear widget and exit before writing pending
  if (!config.store.ai_enabled) {
    _ai_interpretation->setText("");
    _ai_interpretation->setActive(false, true);
    _aiPending = false;
    _aiPendingText[0] = '\0';
    return;
  }
  
  // Always update pending buffer
  const char* txt = (text.isEmpty() || text.c_str() == nullptr) ? "" : text.c_str();
  strlcpy(_aiPendingText, txt, sizeof(_aiPendingText));
  _aiPending = true;
  
  // Check if we're on PG_PLAYER page
  Page* activePage = _pager.getActivePage();
  bool isOnPlayerPage = (activePage == pages[PG_PLAYER]);
  
  if (!isOnPlayerPage) {
    // Not on player page - just save to pending, don't draw
    return;
  }
  
  // On player page - apply text
  if (text.isEmpty()) {
    // Clear text and hide widget
    _ai_interpretation->setText("");
    _ai_interpretation->setActive(false, true);  // clr=true for explicit area clearing
  } else {
    _ai_interpretation->setText(text.c_str());
    _ai_interpretation->setActive(true);
  }
  _aiPending = false;  // Clear pending flag after applying
}

void Display::putRequest(displayRequestType_e type, int payload){
  if(displayQueue==NULL) return;
  requestParams_t request;
  request.type = type;
  request.payload = payload;
  
  // Для DSP_START используем блокирующую отправку
  if(type == DSP_START) {
    Serial.println("[Display] Sending DSP_START request");
    xQueueSend(displayQueue, &request, portMAX_DELAY);
    return;
  }
  
  if(type == NEWMODE) {
    if(payload == STATIONS) {
      _isStationsChanging = true;
    } else if(payload == VOL) {
      _isVolumeChanging = true;
    }
  }
  xQueueSend(displayQueue, &request, DSQ_SEND_DELAY);
  #ifdef USE_NEXTION
    nextion.putRequest(request);
  #endif
}

void Display::_layoutChange(bool played){
  if(config.store.vumeter){
    // Очистим состояние: отключим оба перед переключением
    if(_spectrumwidget) _spectrumwidget->setActive(false, true);
    if(_vuwidget) _vuwidget->lock(true);
    if(played){
      if(config.store.usespectrum){
        if(_spectrumwidget) _spectrumwidget->setActive(true);
      }else{
        if(_vuwidget) _vuwidget->unlock();
      }
      _clock.moveTo(clockMove);
      if(_weather) _weather->moveTo(weatherMoveVU);
    }else{
      // В режиме Stop отключаем оба визуальных метра
      if(_spectrumwidget) _spectrumwidget->setActive(false, true);
      if(_vuwidget) if(!_vuwidget->locked()) _vuwidget->lock();
      _clock.moveBack();
      if(_weather) _weather->moveBack();
    }
  }else{
    // При выключенном vumeter принудительно отключаем оба виджета
    if(_spectrumwidget) _spectrumwidget->setActive(false, true);
    if(_vuwidget) _vuwidget->lock(true);
    if(played){
      if(_weather) _weather->moveTo(weatherMove);
      _clock.moveBack();
    }else{
      if(_weather) _weather->moveBack();
      _clock.moveBack();
    }
  }
}
#ifndef DSP_QUEUE_TICKS
  #define DSP_QUEUE_TICKS pdMS_TO_TICKS(10)
#endif
void Display::loop() {
  if(_bootStep==0) {
    _pager.begin();
    _bootScreen();
    // Не выходим сразу - нужно вызвать _pager.loop() для рендеринга виджетов
    // Don't return immediately - need to call _pager.loop() for widget rendering
  }
  // Разрешаем loop при bootStep==1 для анимации / Allow loop at bootStep==1 for animation
  if(displayQueue==NULL && _bootStep!=1) return;
  // Сначала обработаем входящие запросы рендера, затем нарисуем и выполнем flush
  requestParams_t request;
  if(xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)){
    bool pm_result = true;
    pm.on_display_queue(request, pm_result);
    if(pm_result)
      switch (request.type){
        case NEWMODE: _swichMode((displayMode_e)request.payload); break;
        case CLOCK: 
          if(_mode==PLAYER || _mode==SCREENSAVER) _time(); 
          /*#ifdef USE_NEXTION
            if(_mode==TIMEZONE) nextion.localTime(network.timeinfo);
            if(_mode==INFO)     nextion.rssi();
          #endif*/
          break;
        case NEWTITLE: _title(); break;
        case NEWSTATION: _station(); break;
        case NEXTSTATION: _drawNextStationNum(request.payload); break;
        case DRAWPLAYLIST: _drawPlaylist(); break;
        case DRAWVOL: _volume(); break;
        case DBITRATE: {
            char buf[20]; 
            snprintf(buf, 20, bitrateFmt, config.station.bitrate); 
            if(_bitrate) { _bitrate->setText(config.station.bitrate==0?"":buf); } 
            if(_fullbitrate) { 
              _fullbitrate->setBitrate(config.station.bitrate); 
              _fullbitrate->setFormat(config.configFmt); 
            } 
          }
          break;
        case AUDIOINFO: if(_heapbar)  { _heapbar->lock(!config.store.audioinfo); _heapbar->setValue(player.inBufferFilled()); } break;
        case SHOWVUMETER: {
          // Переключение виджетов выполняется в _layoutChange()
          _layoutChange(player.isRunning());
          break;
        }
        case SHOWWEATHER: {
          if(_weather) _weather->lock(!config.store.showweather);
          if(!config.store.showweather){
            #ifndef HIDE_IP
            if(_volip && network.status == CONNECTED) _volip->setText(WiFi.localIP().toString().c_str(), iptxtFmt);
            #endif
          }else{
            if(_weather) _weather->setText(const_getWeather);
          }
          break;
        }
        case NEWWEATHER: {
          if(_weather && network.weatherBuf) _weather->setText(network.weatherBuf);
          break;
        }
        case BOOTSTRING: {
          if(_bootstring) _bootstring->setText(config.ssids[request.payload].ssid, bootstrFmt);
          /*#ifdef USE_NEXTION
            char buf[50];
            snprintf(buf, 50, bootstrFmt, config.ssids[request.payload].ssid);
            nextion.bootString(buf);
          #endif*/
          break;
        }
        case WAITFORSD: {
          if(_bootstring) _bootstring->setText(const_waitForSD);
          break;
        }
        case SDFILEINDEX: {
          if(_mode == SDCHANGE) _nums.setText(request.payload, "%d");
          break;
        }
        case DSPRSSI: if(_rssi){ _setRSSI(request.payload); } if (_heapbar && config.store.audioinfo) _heapbar->setValue(player.isRunning()?player.inBufferFilled():0); break;
        case PSTART: _layoutChange(true);   break;
        case PSTOP:  _layoutChange(false);  break;
        case DSP_START: 
          Serial.println("[Display] Processing DSP_START request");
          _start();  
          break;
        case NEWIP: {
          #ifndef HIDE_IP
            if(_volip && network.status == CONNECTED) _volip->setText(WiFi.localIP().toString().c_str(), iptxtFmt);
          #endif
          break;
        }
        default: break;
      }
    DisplayEvent evt = { request.type, &request, _mode };
    lvgl_ui::onDisplayEvent(evt);
  }
  // Throttled refresh of LVGL screens (~1 Hz); pattern symmetric for INFO and Main.
  // Throttled обновление LVGL экранов (~1 Гц); симметричный паттерн для INFO и Main.
  if (_activeBackend == lvgl_ui::UiBackend::Lvgl) {
    if (_mode == INFO) {
      static uint32_t lastInfoRefresh = 0;
      if (millis() - lastInfoRefresh >= 1000) {
        lvgl_ui::refreshInfoScreen();
        lastInfoRefresh = millis();
      }
    }
    if (_mode == PLAYER) {
      static uint32_t lastMainRefresh = 0;
      if (millis() - lastMainRefresh >= 1000) {
        lvgl_ui::refreshMainScreen();
        lastMainRefresh = millis();
      }
    }
  }
  // Stage 2 order: legacy pager first. LVGL только в режиме INFO — иначе затирает boot и плейер.
  if (_activeBackend == lvgl_ui::UiBackend::LegacyCanvas) _pager.loop();
  if (_activeBackend == lvgl_ui::UiBackend::Lvgl) lvgl_ui::taskHandler();
  // Dirty-based flush: flush только если был реальный рендеринг и прошло >=16мс
  if(!_suspendFlush){
    static uint32_t lastFlushMs = 0;
    // Flush только если кадр грязный И прошло >=16мс (throttle ~60 FPS)
    if(g_frameDirty && (millis() - lastFlushMs >= 16)){
      sdog.takeMutex();
      gfxFlushScreen(gfx);
      sdog.giveMutex();
      g_frameDirty = false; // Сбрасываем флаг после flush
      lastFlushMs = millis();
    }
  }
#ifdef USE_NEXTION
  nextion.loop();
#endif
  dsp.loop();
  #if I2S_DOUT==255
  player.computeVUlevel();
  #endif
}

void Display::_setRSSI(int rssi) {
  if(!_rssi) return;
#if RSSI_DIGIT
  _rssi->setText(rssi, rssiFmt);
  return;
#endif
  char rssiG[3];
  int rssi_steps[] = {RSSI_STEPS};
  if(rssi >= rssi_steps[0]) strlcpy(rssiG, "\004\006", 3);
  if(rssi >= rssi_steps[1] && rssi < rssi_steps[0]) strlcpy(rssiG, "\004\005", 3);
  if(rssi >= rssi_steps[2] && rssi < rssi_steps[1]) strlcpy(rssiG, "\004\002", 3);
  if(rssi >= rssi_steps[3] && rssi < rssi_steps[2]) strlcpy(rssiG, "\003\002", 3);
  if(rssi <  rssi_steps[3] || rssi >=  0) strlcpy(rssiG, "\001\002", 3);
  _rssi->setText(rssiG);
}

void Display::_station() {
  config.vuThreshold = 0;
  _meta.setAlign(metaConf.widget.align);
  _meta.setText(config.station.name);
/*#ifdef USE_NEXTION
  nextion.newNameset(config.station.name);
  nextion.bitrate(config.station.bitrate);
  nextion.bitratePic(ICON_NA);
#endif*/
}

char *split(char *str, const char *delim) {
  char *dmp = strstr(str, delim);
  if (dmp == NULL) return NULL;
  *dmp = '\0'; 
  return dmp + strlen(delim);
}

void Display::_title() {
  if (strlen(config.station.title) > 0) {
    char tmpbuf[strlen(config.station.title)+1];
    strlcpy(tmpbuf, config.station.title, strlen(config.station.title)+1);
    char *stitle = split(tmpbuf, " - ");
    if(stitle && _title2){
      _title1.setText(tmpbuf);
      _title2->setText(stitle);
    }else{
      _title1.setText(config.station.title);
      if(_title2) _title2->setText("");
    }
    /*#ifdef USE_NEXTION
      nextion.newTitle(config.station.title);
    #endif*/
    
  }else{
    _title1.setText("");
    if(_title2) _title2->setText("");
  }
  if (player_on_track_change) player_on_track_change();
  pm.on_track_change();
}

void Display::_time(bool redraw) {
  
#if LIGHT_SENSOR!=255
  if(config.store.dspon) {
    config.store.brightness = AUTOBACKLIGHT(analogRead(LIGHT_SENSOR));
    config.setBrightness();
  }
#endif
  if(config.isScreensaver && network.timeinfo.tm_sec % 20 == 0){
    uint16_t maxY = dsp.height() - dsp.plItemHeight - TFT_FRAMEWDT*2;
    uint16_t maxClockY = dsp.height() - 100;  // нижняя граница (380 при height 480)
    if (maxY > maxClockY) maxY = maxClockY;
    uint16_t minClockY = 100;  // верхняя граница — не меньше 100 px от верха
    #ifdef GXCLOCKFONT
      uint16_t minY = (TFT_FRAMEWDT > minClockY) ? TFT_FRAMEWDT : minClockY;
      uint16_t ft=static_cast<uint16_t>(random(minY, maxY));
    #else
      uint16_t minY = (TFT_FRAMEWDT+clockConf.textsize > minClockY) ? (TFT_FRAMEWDT+clockConf.textsize) : minClockY;
      uint16_t ft=static_cast<uint16_t>(random(minY, maxY));
    #endif
    if (minY < maxY) _clock.moveTo({clockConf.left, ft, 0});
  }
  _clock.draw();
  /*#ifdef USE_NEXTION
    nextion.printClock(network.timeinfo);
  #endif*/
}

void Display::_volume() {
  if(_volbar) _volbar->setValue(config.store.volume);
  #ifndef HIDE_VOL
    if(_voltxt) _voltxt->setText(config.store.volume, voltxtFmt);
  #endif
  if(_mode==VOL) {
    _setReturnTicker(3);
    _nums.setText(config.store.volume, numtxtFmt);
  }
  /*#ifdef USE_NEXTION
    nextion.setVol(config.store.volume, _mode == VOL);
  #endif*/
}

void Display::flip(){ dsp.flip(); }

void Display::invert(){ dsp.invert(); }

void  Display::setContrast(){
  #if DSP_MODEL==DSP_NOKIA5110
    dsp.setContrast(config.store.contrast);
  #endif
}

bool Display::deepsleep(){
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.sleep();
  return true;
#endif
  return false;
}

void Display::wakeup(){
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.wake();
#endif
}
//============================================================================================================================
#else // !DUMMYDISPLAY
//============================================================================================================================
void Display::init(){
  #ifdef USE_NEXTION
  nextion.begin(true);
  #endif
}
void Display::_start(){
  #ifdef USE_NEXTION
  //nextion.putcmd("page player");
  nextion.start();
  #endif
  config.setTitle(const_PlReady);
}
void Display::putRequest(displayRequestType_e type, int payload){
  if(type==DSP_START) _start();
  #ifdef USE_NEXTION
    requestParams_t request;
    request.type = type;
    request.payload = payload;
    nextion.putRequest(request);
  #else
    if(type==NEWMODE) mode((displayMode_e)payload);
  #endif
}
//============================================================================================================================
#endif // DUMMYDISPLAY
