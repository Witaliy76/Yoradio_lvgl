#include "options.h"

#include <cstdarg>
#include "esp_heap_caps.h"
#include "WiFi.h"
#include "time.h"
#include "display.h"
#include "player.h"
#include "network.h"
#include "../displays/tools/GFX_Canvas_screen.h"
#include "../core/spidog.h"
#include "../lvgl_ui/lvgl_ui.h"
#include "../lvgl_ui/lv_screensaver.h"
#include "../lvgl_ui/lv_ui_events.h"
#include "../ai/ai_subsystem.h"
extern Arduino_Canvas* gfx;

// Глобальный флаг "кадр грязный" для dirty-based flush
static volatile bool g_frameDirty = false;

// Block 8-E1: panel flush stats for one-shot diag (updated only when gfxFlushScreen runs).
// Block 8-E1: счётчики panel flush для diag (только при реальном gfxFlushScreen).
static uint32_t s_panel_gfx_flush_count = 0;
static uint32_t s_panel_last_gfx_flush_ms = 0;

// Helper-функция для установки флага dirty (вызывается из функций рисования)
void markFrameDirty() {
    g_frameDirty = true;
}

// Block 8-E3: panel flush counter when LVGL bypasses Canvas (ST7701 direct path).
// Block 8-E3: счётчик panel flush при прямом LVGL→output_display (без g_frameDirty).
void lvgl_ui::recordLvglDirectPanelFlush() {
    s_panel_gfx_flush_count++;
    s_panel_last_gfx_flush_ms = millis();
}

Display display;
#ifdef USE_NEXTION
Nextion nextion;
#endif

#ifndef DUMMYDISPLAY
//============================================================================================================================

namespace {
// Set when either legacy or LVGL boot UI was created (prevents duplicate boot in Display::init tail).
// Установлено при создании legacy или LVGL boot (не дублировать boot в хвосте Display::init).
bool s_any_boot_ui_shown = false;
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
// Pager::removePage() default calls dsp.clearDsp() — full Canvas fill; LVGL draws into the same buffer.
// Skipping clear avoids theme-colored wipe over LVGL Boot/Main during DSP_START / AP handoff.
// removePage по умолчанию вызывает dsp.clearDsp() — полная заливка Canvas; LVGL рисует в тот же буфер.
// Без clear не затираем LVGL Boot/Main цветом темы при DSP_START / переходе в AP.
static bool lvgl_player_uses_same_canvas() {
  return lvgl_ui::getPreferredBackend(PLAYER) == lvgl_ui::UiBackend::Lvgl;
}
#endif
} // namespace

DspCore dsp;

Page *pages[] = { new Page(), new Page(), new Page(), new Page() };

#ifndef DSQ_SEND_DELAY
  #define DSQ_SEND_DELAY portMAX_DELAY
#endif

#ifndef CORE_STACK_SIZE
  // LVGL path needs larger DspTask stack (fonts/layout/events + logging overhead).
  // Для ветки LVGL нужен больший стек DspTask (шрифты/layout/events + накладные логи).
  #if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    #define CORE_STACK_SIZE  (1024*6)
  #else
    #define CORE_STACK_SIZE  (1024*3)
  #endif
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
  
  // Защита от повторного вызова / Idempotent boot UI
  if (s_any_boot_ui_shown) {
    Serial.println("[Display] _bootScreen already created, skipping");
    return;
  }
  s_any_boot_ui_shown = true;
  
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

bool Display::_legacyWidgetsAvailable() const {
  // LVGL PLAYER no longer builds legacy widgets; legacy Canvas updates become no-ops.
  // LVGL PLAYER больше не строит legacy widgets; legacy Canvas updates становятся заглушкой.
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  if (_lvgl_player_handoff_pending || _activeBackend == lvgl_ui::UiBackend::Lvgl) {
    return false;
  }
#endif
  return _legacyPlayerWidgetsBuilt;
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
  _legacyPlayerWidgetsBuilt = true;
}

void Display::_apScreen() {
  _suspendFlush = false;  // Разрешаем обновления экрана в AP режиме / Enable screen updates in AP mode
  Serial.println("[Display] _apScreen() called");
  Serial.printf("[Display] _suspendFlush = %s\n", _suspendFlush ? "true" : "false");
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  if (_boot) _pager.removePage(_boot, !lvgl_player_uses_same_canvas());
#else
  if (_boot) _pager.removePage(_boot);
#endif
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
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  if (_boot) _pager.removePage(_boot, !lvgl_player_uses_same_canvas());
#else
  if (_boot) _pager.removePage(_boot);
#endif
  #ifdef USE_NEXTION
    nextion.wake();
  #endif
  if (network.status != CONNECTED && network.status != SDREADY) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    // Wi‑Fi 5A: LVGL Boot + Wi‑Fi fail → Recovery shell (AP still raised in network.begin hybrid C).
    // Wi‑Fi 5A: при активном LVGL Boot не показывать legacy AP как основной экран.
    if (lvgl_ui::isLvglBootActive()) {
      _mode = PLAYER; // coherent nominal mode until WIFI takes over / номинальный режим до перехода в WIFI
      lvgl_ui::bootScreenSetStatusUtf8(
          (config.ssidsCount == 0) ? "No saved Wi-Fi networks" : "Could not connect to saved networks");
      lvgl_ui::bootScreenNotifyBootSignal();
      _lvgl_wifi_recovery_handoff_pending   = true;
      _lvgl_wifi_recovery_handoff_phase     = 0;
      _lvgl_wifi_recovery_phase_started_ms  = 0;
      _suspendFlush                         = false;
      Serial.println("[Display] Wi-Fi 5A: LVGL Recovery handoff pending");
      return;
    }
    lvgl_ui::dismissBootForApLegacyHandoff(); // blank LVGL screen; AP uses legacy pager / пустой LVGL, AP на legacy
#endif
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
  lvgl_ui::UiBackend startBackend = lvgl_ui::getPreferredBackend(PLAYER);
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  if (startBackend == lvgl_ui::UiBackend::Lvgl) {
    Serial.println("[Display] Skipping legacy _buildPager for LVGL PLAYER backend");
    _legacyPlayerWidgetsBuilt = false;
  } else
#endif
  {
    _buildPager();
  }
  _mode = PLAYER;
  config.setTitle(const_PlReady);

  // Stage 5.5 guard: if PLAYER backend is LVGL, skip legacy player page setup.
  // Guard 5.5: если backend PLAYER = LVGL, пропускаем legacy подготовку страницы плейера.
  if (startBackend == lvgl_ui::UiBackend::Lvgl) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    // Defer dismiss + Main until min Boot dwell (logo + indeterminate bar); completed in loop().
    // Откладываем переход на Main до мин. времени Boot; завершение в loop().
    _lvgl_player_handoff_pending = true;
    return;
#else
    _activeBackend = startBackend;
    lvgl_ui::onModeChanged(PLAYER, startBackend, PLAYER);
    _deactivateLegacyPagerForLvgl();
    _bootStep = 2;
    _suspendFlush = false;
    return;
#endif
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
}

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
void Display::_tryCompleteLvglWifiRecoveryHandoff() {
  if (!_lvgl_wifi_recovery_handoff_pending) return;
  constexpr uint32_t kOpeningMsgHoldMs = 450;

  if (_lvgl_wifi_recovery_handoff_phase == 0) {
    if (!lvgl_ui::isLvglBootMinDwellElapsed()) return;
    lvgl_ui::bootScreenSetStatusUtf8("Opening Wi-Fi Setup…");
    lvgl_ui::bootScreenNotifyBootSignal();
    _lvgl_wifi_recovery_handoff_phase    = 1;
    _lvgl_wifi_recovery_phase_started_ms = millis();
    return;
  }
  if (_lvgl_wifi_recovery_handoff_phase == 1) {
    if ((uint32_t)(millis() - _lvgl_wifi_recovery_phase_started_ms) < kOpeningMsgHoldMs) return;
    lvgl_ui::notifyWifiRecoveryEnteredFromBootFailure();
    // Wi‑Fi 5B: must not use dismissBootForMainHandoff() — it enters Main and preloads background offline → crash / см. 5B.
    lvgl_ui::dismissBootForWifiRecoveryHandoff();
    _lvgl_wifi_recovery_handoff_pending  = false;
    _lvgl_wifi_recovery_handoff_phase    = 0;
    const displayMode_e prev_mode        = _mode;
    _mode                                = WIFI;
    lvgl_ui::UiBackend backend           = lvgl_ui::getPreferredBackend(WIFI);
    _activeBackend                       = backend;
    lvgl_ui::onModeChanged(WIFI, backend, prev_mode);
    if (backend == lvgl_ui::UiBackend::Lvgl) {
      _deactivateLegacyPagerForLvgl();
    }
    _bootStep       = 2;
    _suspendFlush = false;
    // Wi‑Fi 5B: recovery handoff stays WIFI-only (no legacy PLAYER page setup here).
    // Wi‑Fi 5B: handoff Recovery остаётся в WIFI — без legacy PLAYER page setup.
  }
}

// S6V8A + S6V9I: runtime LOST escalation — status sub-label (Trying to reconnect + Recovery milestones); open Recovery after 60 s.
// S6V8A + S6V9I: эскалация LOST — текст статуса; открыть Recovery через 60 с.
void Display::_tryCompleteLostEscalation() {
  if (!_lost_escalation_armed) return;
  if (_mode != LOST) { _lost_escalation_armed = false; return; }

  // Race guard: if reconnect already succeeded (beginReconnect cleared by WiFiReconnected) — cancel.
  // Гард гонки: если reconnect уже успешен (beginReconnect сброшен) — отменить.
  if (!network.beginReconnect) {
    _lost_escalation_armed     = false;
    _lost_started_ms           = 0;
    _lost_escalation_milestone = 0;
    return;
  }

  const uint32_t elapsed = (uint32_t)(millis() - _lost_started_ms);

  // Milestone 0 → 1: set initial status text on first tick after LOST.
  // Milestone 0 → 1: установить начальный текст при первом тике после LOST.
  if (_lost_escalation_milestone == 0) {
    // S6V9I: explain reconnect attempt + Recovery countdown (two lines in one label). / Две строки в одном label.
    lvgl_ui::overlayLostSetStatusText("Trying to reconnect...\nWi-Fi Recovery in 60s");
    _lost_escalation_milestone = 1;
  }

  // Milestone 1 → 2: ~30 s.
  if (_lost_escalation_milestone < 2 && elapsed >= 30000U) {
    lvgl_ui::overlayLostSetStatusText("Trying to reconnect...\nWi-Fi Recovery in 30s");
    _lost_escalation_milestone = 2;
  }

  // Milestone 2 → 3: ~50 s.
  if (_lost_escalation_milestone < 3 && elapsed >= 50000U) {
    lvgl_ui::overlayLostSetStatusText("Trying to reconnect...\nOpening Wi-Fi Recovery...");
    _lost_escalation_milestone = 3;
  }

  // Timeout: 60 s → open LVGL Wi-Fi Recovery.
  // Таймаут: 60 с → открываем LVGL Wi-Fi Recovery.
  if (elapsed >= 60000U) {
    // Race guard again before committing. / Ещё раз гард перед переходом.
    if (!network.beginReconnect) {
      _lost_escalation_armed     = false;
      _lost_started_ms           = 0;
      _lost_escalation_milestone = 0;
      return;
    }
    // Disarm first — prevents repeated putRequest if loop re-enters before mode switch. / Сначала disarm.
    _lost_escalation_armed     = false;
    _lost_started_ms           = 0;
    _lost_escalation_milestone = 0;
    // S6V9E: after 60s — suspend ESP auto-reconnect + our beginReconnect; radio for Wi-Fi Setup (not before timeout).
    // S6V9E: после 60s — стоп auto-reconnect и флага; радио для Setup (до таймаута не трогаем).
    network.recoverySuspendReconnectForSetup();
    lvgl_ui::notifyWifiRecoveryEnteredFromRuntimeDisconnect();
    putRequest(NEWMODE, WIFI);
  }
}
#endif

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
void Display::_tryCompleteLvglPlayerHandoff() {
  if (!_lvgl_player_handoff_pending) return;
  if (!lvgl_ui::dismissBootForMainHandoffWhenDue()) return;
  _lvgl_player_handoff_pending = false;
  _activeBackend = lvgl_ui::getPreferredBackend(PLAYER);
  lvgl_ui::onModeChanged(PLAYER, _activeBackend, PLAYER);
  _deactivateLegacyPagerForLvgl();
  _bootStep = 2;
  _suspendFlush = false;
}
#endif

void Display::_showDialog(const char *title){
  if (!_legacyWidgetsAvailable()) return;
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
  if (newmode == _mode) return;
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  // Wi‑Fi 4C: ignore LOST while LVGL Wi‑Fi Setup owns the screen — keeps display.mode() WIFI so
  // legacy LOST path (overlay + controls gating) does not cover the password/connect UI.
  // Wi‑Fi 4C: игнорировать LOST пока активен LVGL Wi‑Fi Setup — mode остаётся WIFI, без оверлея LOST.
  if (newmode == LOST && lvgl_ui::isWifiSetupFlowActive()) {
    return;
  }
  // S6V8A: arm or disarm runtime escalation timer based on new mode.
  // S6V8A: взводим или сбрасываем таймер эскалации в зависимости от нового режима.
  if (newmode == LOST) {
    _lost_escalation_armed     = true;
    _lost_started_ms           = millis();
    _lost_escalation_milestone = 0;
  } else {
    _lost_escalation_armed     = false;
    _lost_started_ms           = 0;
    _lost_escalation_milestone = 0;
  }
#endif
  // Wi-Fi 3A: allow WIFI while offline; allow return to PLAYER when leaving LVGL Wi-Fi shell.
  // Wi‑Fi 3A: WIFI офлайн; возврат в PLAYER при выходе из LVGL Wi‑Fi shell.
  if (network.status != CONNECTED && network.status != SDREADY && newmode != WIFI &&
      !(newmode == PLAYER && _mode == WIFI)) {
    return;
  }

  // Previous mode for lvgl_ui::onModeChanged (carousel preservation when leaving saver/blank).
  // Предыдущий режим для onModeChanged (сохранение карусели при выходе из saver/blank).
  const displayMode_e prev_mode = _mode;

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
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
      // SCREENBLANK called setDspOn(false); legacy PLAYER path always runs setDspOn(dspon) — LVGL path skipped it.
      // SCREENBLANK гасит панель; у legacy PLAYER есть setDspOn — у LVGL Main его не было → wakeup не вызывался.
      if (prev_mode == SCREENBLANK) {
        config.setDspOn(config.store.dspon, false);
      }
#endif
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
      _applyPendingAI();
      _layoutChange(player.isRunning());
    }
  }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  const bool lvgl_screensaver_path =
      (newmode == SCREENSAVER || newmode == SCREENBLANK) &&
      (lvgl_ui::getPreferredBackend(newmode) == lvgl_ui::UiBackend::Lvgl);
#else
  const bool lvgl_screensaver_path = false;
#endif
  // Stage 5.6: LVGL uses overlay, not legacy PG_SCREENSAVER page / LVGL — оверлей, не legacy-страница.
  if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
    config.isScreensaver = true;
    if (!lvgl_screensaver_path) {
      _pager.setPage(pages[PG_SCREENSAVER]);
    }
    if (newmode == SCREENBLANK) {
      // Legacy clearClock() gfxFillRects the shared Arduino_Canvas — wipes top ~250px over LVGL. Skip for LVGL UI.
      // Legacy clearClock() заливает общий Canvas — стирает верх ~250px поверх LVGL. Для LVGL UI не вызывать.
      if (!lvgl_screensaver_path) {
        dsp.clearClock();
      }
      config.setDspOn(false, false);
    }
  } else {
    config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    config.isScreensaver = false;
  }
  if (newmode == VOL) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    const bool lvgl_vol = (lvgl_ui::getPreferredBackend(VOL) == lvgl_ui::UiBackend::Lvgl);
#else
    const bool lvgl_vol = false;
#endif
    if (!lvgl_vol && _legacyWidgetsAvailable()) {
#ifndef HIDE_VOLPAGE
#ifndef HIDE_IP
        _showDialog(const_DlgVolume);
#else
        _showDialog(WiFi.localIP().toString().c_str());
#endif
#endif
      _nums.setText(config.store.volume, numtxtFmt);
    }
  }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  const bool lvgl_lost = (lvgl_ui::getPreferredBackend(LOST) == lvgl_ui::UiBackend::Lvgl);
  const bool lvgl_upd  = (lvgl_ui::getPreferredBackend(UPDATING) == lvgl_ui::UiBackend::Lvgl);
#else
  const bool lvgl_lost = false;
  const bool lvgl_upd  = false;
#endif
  if (newmode == LOST && !lvgl_lost)      _showDialog(const_DlgLost);
  if (newmode == UPDATING && !lvgl_upd)  _showDialog(const_DlgUpdate);
  if (newmode == SLEEPING)  _showDialog("SLEEPING");
  if (newmode == SDCHANGE)  _showDialog(const_waitForSD);
  if (newmode == INFO || newmode == SETTINGS || newmode == TIMEZONE) _showDialog(const_DlgNextion);
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  if (newmode == WIFI && lvgl_ui::getPreferredBackend(WIFI) != lvgl_ui::UiBackend::Lvgl) {
    _showDialog(const_DlgNextion);
  }
#else
  if (newmode == WIFI) _showDialog(const_DlgNextion);
#endif
  if (newmode == NUMBERS) _showDialog("");
  if (newmode == STATIONS && _legacyWidgetsAvailable()) {
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
  lvgl_ui::onModeChanged(newmode, backend, prev_mode);
  if (backend == lvgl_ui::UiBackend::Lvgl) {
    _deactivateLegacyPagerForLvgl();
  }
}

void Display::resetQueue(){
  if(displayQueue!=NULL) xQueueReset(displayQueue);
}

void Display::_drawPlaylist() {
  if (!_legacyWidgetsAvailable()) return;
  dsp.drawPlaylist(currentPlItem);
  _setReturnTicker(10);
}

void Display::_drawNextStationNum(uint16_t num) {
  if (!_legacyWidgetsAvailable()) return;
  _setReturnTicker(10);
  _meta.setText(config.stationByNum(num));
  _nums.setText(num, "%d");
}

void Display::printPLitem(uint8_t pos, const char* item, bool uppercase){
  if (!_legacyWidgetsAvailable()) return;
  dsp.printPLitem(pos, item, _plcurrent, uppercase);
}

void Display::setAIInterpretation(const String& text) {
  // AI interpretation: LVGL Main reads _aiPendingText via copyAIInterpretationForLvgl (DspTask).
  // S6V10I-DIAG3: always refresh pending buffer when AI enabled; legacy Canvas only if built.
  // AI: LVGL Main читает _aiPendingText; буфер всегда обновляем, Canvas — только если есть виджеты.
  if (!config.store.ai_enabled) {
    _aiPending = false;
    _aiPendingText[0] = '\0';
    if (_ai_interpretation && _legacyWidgetsAvailable()) {
      _ai_interpretation->setText("");
      _ai_interpretation->setActive(false, true);
    }
    return;
  }

  const char* txt = (text.isEmpty() || text.c_str() == nullptr) ? "" : text.c_str();
  strlcpy(_aiPendingText, txt, sizeof(_aiPendingText));
  _aiPending = true;

  // No legacy PLAYER pager/widgets (e.g. LVGL PLAYER skipped _buildPager): data-only for LVGL
  if (!_legacyWidgetsAvailable() || !_ai_interpretation) {
    return;
  }

  Page* activePage = _pager.getActivePage();
  const bool isOnPlayerPage = (activePage == pages[PG_PLAYER]);
  if (!isOnPlayerPage) {
    return;
  }

  if (text.isEmpty()) {
    _ai_interpretation->setText("");
    _ai_interpretation->setActive(false, true);
  } else {
    _ai_interpretation->setText(text.c_str());
    _ai_interpretation->setActive(true);
  }
  _aiPending = false;
}

void Display::copyAIInterpretationForLvgl(char* buf, size_t cap) const {
    if (!buf || cap == 0) return;
    if (!config.store.ai_enabled) {
        buf[0] = '\0';
        return;
    }
    strlcpy(buf, _aiPendingText, cap);
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
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  // Guard: when PLAYER runs on LVGL, legacy widget moves/clears touch the shared Canvas and wipe the screen.
  // Гвард: при PLAYER на LVGL, legacy moveBack/setActive стирают общий Canvas — пропускаем.
  if (lvgl_player_uses_same_canvas()) return;
#endif
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
  // Stage 5.4 polish: latch Wi-Fi connected status on LVGL Boot screen.
  // Этап 5.4: при реальном подключении Wi-Fi зафиксировать статус на Boot (до перехода на Main).
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  static bool s_lvgl_boot_connected_latched = false;
#endif
  if(_bootStep==0) {
    _pager.begin();
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    // Stage 5.4: LVGL Boot from DspTask only (preflight); legacy boot if LVGL display missing.
    // Этап 5.4: LVGL Boot только из DspTask; legacy boot если дисплей LVGL не поднялся.
    if (lvgl_ui::tryPresentLvglBootOnFirstDspLoop()) {
      s_any_boot_ui_shown = true;
      _bootStep = 1;
      _suspendFlush = false;
    } else
#endif
    {
      _bootScreen();
    }
    // Не выходим сразу — нужен рендер (pager или LVGL taskHandler ниже).
    // Don't return immediately — rendering happens in _pager.loop() or lvgl taskHandler below.
  }
  // Разрешаем loop при bootStep==1 для анимации / Allow loop at bootStep==1 for animation
  if(displayQueue==NULL && _bootStep!=1) return;
  // Сначала обработаем входящие запросы рендера, затем нарисуем и выполнем flush
  requestParams_t request;
  if(xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)){
    switch (request.type){
        case NEWMODE: _swichMode((displayMode_e)request.payload); break;
        case CLOCK:
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (_mode == SCREENSAVER && _activeBackend == lvgl_ui::UiBackend::Lvgl) {
            lvgl_ui::screensaverRefreshClock();
            break;
          }
#endif
          if (_mode == PLAYER || _mode == SCREENSAVER) {
            _time();
          }
          /*#ifdef USE_NEXTION
            if(_mode==TIMEZONE) nextion.localTime(network.timeinfo);
            if(_mode==INFO)     nextion.rssi();
          #endif*/
          break;
        case NEWTITLE: _title(); break;
        case NEWSTATION: _station(); break;
        case NEXTSTATION: _drawNextStationNum(request.payload); break;
        case DRAWPLAYLIST: _drawPlaylist(); break;
        case DRAWVOL:
          _volume();
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (_activeBackend == lvgl_ui::UiBackend::Lvgl &&
              (_mode == PLAYER || _mode == VOL)) {
            lvgl_ui::refreshMainScreen();
          }
#endif
          break;
        case DBITRATE: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
            if (!_legacyWidgetsAvailable()) {
              if (_activeBackend == lvgl_ui::UiBackend::Lvgl && _mode == PLAYER) {
                lvgl_ui::refreshMainScreen();
              }
              break;
            }
#endif
            char buf[20]; 
            snprintf(buf, 20, bitrateFmt, config.station.bitrate); 
            if(_bitrate) { _bitrate->setText(config.station.bitrate==0?"":buf); } 
            if(_fullbitrate) { 
              _fullbitrate->setBitrate(config.station.bitrate); 
              _fullbitrate->setFormat(config.configFmt); 
            } 
          }
          break;
        case AUDIOINFO:
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (!_legacyWidgetsAvailable()) break;
#endif
          if(_heapbar)  { _heapbar->lock(!config.store.audioinfo); _heapbar->setValue(player.inBufferFilled()); }
          break;
        case SHOWVUMETER: {
          // Переключение виджетов выполняется в _layoutChange()
          _layoutChange(player.isRunning());
          break;
        }
        case SHOWWEATHER: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (!_legacyWidgetsAvailable()) break;
#endif
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
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (!_legacyWidgetsAvailable()) break;
#endif
          if(_weather && network.weatherBuf) _weather->setText(network.weatherBuf);
          break;
        }
        case BOOTSTRING: {
          bool lvgl_boot_active_now = false;
          bool block_legacy_boot_canvas = false;
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          lvgl_boot_active_now = lvgl_ui::isLvglBootActive();
          block_legacy_boot_canvas =
              lvgl_boot_active_now ||
              _lvgl_player_handoff_pending ||
              (_activeBackend == lvgl_ui::UiBackend::Lvgl);
#endif
          // Legacy boot text writes directly to Canvas (gfxFillRect in TextWidget::setText).
          // During LVGL Boot this causes visible black wipe artifacts, so skip legacy writes.
          // Legacy-текст Boot пишет прямо в Canvas (gfxFillRect в TextWidget::setText).
          // Во время LVGL Boot это даёт чёрные «протирки», поэтому legacy-ветку пропускаем.
          if (!block_legacy_boot_canvas && _bootstring) {
            _bootstring->setText(config.ssids[request.payload].ssid, bootstrFmt);
          }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (lvgl_boot_active_now) {
            // Once we show "Connected...", keep it stable until handoff.
            // После "Connected..." статус держим стабильным до перехода на Main.
            if (s_lvgl_boot_connected_latched) break;
            char line[96];
            snprintf(line, sizeof(line), bootstrFmt, config.ssids[request.payload].ssid);
            lvgl_ui::bootScreenSetStatusUtf8(line);
            lvgl_ui::bootScreenNotifyBootSignal();
          }
#endif
          /*#ifdef USE_NEXTION
            char buf[50];
            snprintf(buf, 50, bootstrFmt, config.ssids[request.payload].ssid);
            nextion.bootString(buf);
          #endif*/
          break;
        }
        case WAITFORSD: {
          bool lvgl_boot_active_now = false;
          bool block_legacy_boot_canvas = false;
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          lvgl_boot_active_now = lvgl_ui::isLvglBootActive();
          block_legacy_boot_canvas =
              lvgl_boot_active_now ||
              _lvgl_player_handoff_pending ||
              (_activeBackend == lvgl_ui::UiBackend::Lvgl);
#endif
          if (!block_legacy_boot_canvas && _bootstring) {
            _bootstring->setText(const_waitForSD);
          }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (lvgl_boot_active_now) {
            if (s_lvgl_boot_connected_latched) break;
            char line[64];
            strncpy_P(line, const_waitForSD, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            lvgl_ui::bootScreenSetStatusUtf8(line);
            lvgl_ui::bootScreenNotifyBootSignal();
          }
#endif
          break;
        }
        case SDFILEINDEX: {
          if (!_legacyWidgetsAvailable()) break;
          if(_mode == SDCHANGE) _nums.setText(request.payload, "%d");
          break;
        }
        case DSPRSSI:
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (!_legacyWidgetsAvailable()) break;
#endif
          if(_rssi){ _setRSSI(request.payload); } if (_heapbar && config.store.audioinfo) _heapbar->setValue(player.isRunning()?player.inBufferFilled():0);
          break;
        case PSTART: _layoutChange(true);   break;
        case PSTOP:  _layoutChange(false);  break;
        case DSP_START: 
          Serial.println("[Display] Processing DSP_START request");
          _start();  
          break;
        case NEWIP: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (!_legacyWidgetsAvailable()) break;
#endif
          #ifndef HIDE_IP
            if(_volip && network.status == CONNECTED) _volip->setText(WiFi.localIP().toString().c_str(), iptxtFmt);
          #endif
          break;
        }
        case MAIN_BG_FS_UPDATED: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          // NetServer after successful /upload_bg; LVGL only on DspTask / После commit upload — только здесь lv_*.
          lvgl_ui::onMainBackgroundSlotCommitted(static_cast<uint8_t>(request.payload));
#endif
          break;
        }
        case ART_FS_UPDATED: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          // NetServer after successful /upload_art or /remove_art — reload art on Main (DspTask only).
          // NetServer после /upload_art или /remove_art — перезагрузить арт на Main (только DspTask).
          lvgl_ui::onStationArtCommitted();
#endif
          break;
        }
        case SET_THEME_PRESET: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          // Stage 6.6R-B: NetServer enqueued /set_theme request — apply on DspTask only (LVGL thread-safe).
          // Этап 6.6R-B: NetServer поставил запрос /set_theme в очередь — только DspTask.
          lvgl_ui::onThemePresetChanged(static_cast<uint8_t>(request.payload));
#endif
          break;
        }
        case CUSTOM_THEME_FILE_UPDATED: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          // Stage 6.6R-F1: /upload_theme or /remove_theme — reload palette + live UI if Custom active.
          // Этап 6.6R-F1: обновление theme_custom.txt — только DspTask, без lv_* в NetServer.
          lvgl_ui::onCustomThemeFileUpdated();
#endif
          break;
        }
        default: break;
      }
    DisplayEvent evt = { request.type, &request, _mode };
    lvgl_ui::onDisplayEvent(evt);
  }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  _tryCompleteLvglWifiRecoveryHandoff();
  _tryCompleteLvglPlayerHandoff();
  // S6V8A: runtime LOST escalation polling (DspTask; millis-based milestones). / Polling эскалации LOST.
  _tryCompleteLostEscalation();
#endif
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  if (lvgl_ui::isLvglBootActive()) {
    if (!s_lvgl_boot_connected_latched && WiFi.status() == WL_CONNECTED) {
      const String ssid = WiFi.SSID();
      char line[96];
      snprintf(line, sizeof(line), "Connected to %s", ssid.length() ? ssid.c_str() : "WiFi");
      lvgl_ui::bootScreenSetStatusUtf8(line);
      s_lvgl_boot_connected_latched = true;
    }
  } else {
    s_lvgl_boot_connected_latched = false;
  }
#endif
  // Throttled refresh of LVGL screens (~1 Hz); pattern symmetric for INFO and Main.
  // Throttled обновление LVGL экранов (~1 Гц); симметричный паттерн для INFO и Main.
  // SCREENBLANK/SCREENSAVER: do not refresh underlying pages (carousel Info still reports Info slot during blank).
  // SCREENBLANK/SCREENSAVER: не обновлять страницы под оверлеем (на blank карусель всё ещё на Info).
  if (_activeBackend == lvgl_ui::UiBackend::Lvgl && _mode != SCREENBLANK && _mode != SCREENSAVER) {
    // INFO labels: classic path _mode==INFO, or carousel on Info while still PLAYER (Stage 5.3 swipe).
    // Подписи INFO: обычный INFO или карусель на Info при mode PLAYER (свайп 5.3).
    if (_mode == INFO || lvgl_ui::isLvglCarouselOnInfoSlot()) {
      static uint32_t lastInfoRefresh = 0;
      if (millis() - lastInfoRefresh >= 1000) {
        lvgl_ui::refreshInfoScreen();
        lastInfoRefresh = millis();
      }
    }
    if (_mode == PLAYER || _mode == VOL) {
      static uint32_t lastMainRefresh = 0;
      if (millis() - lastMainRefresh >= 1000) {
        lvgl_ui::refreshMainScreen();
        lastMainRefresh = millis();
      }
    }
  }
  // Stage 2 + 5.4: legacy pager unless LVGL owns mode or LVGL Boot is active.
  // Этап 2 + 5.4: legacy pager, кроме режимов LVGL и активного LVGL Boot.
  const bool lvgl_boot_active = lvgl_ui::isLvglBootActive();
  if (_activeBackend == lvgl_ui::UiBackend::LegacyCanvas && !lvgl_boot_active) _pager.loop();
  if (_activeBackend == lvgl_ui::UiBackend::Lvgl || lvgl_boot_active) lvgl_ui::taskHandler();
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
      s_panel_gfx_flush_count++;
      s_panel_last_gfx_flush_ms = lastFlushMs;
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
  if (!_legacyWidgetsAvailable()) return;
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
  if (_legacyWidgetsAvailable()) {
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
  }
  if (player_on_track_change) player_on_track_change();
  aiSubsystem.onTrackChange();
}

void Display::_time(bool redraw) {
  if (!_legacyWidgetsAvailable()) return;
  
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
  if (!_legacyWidgetsAvailable()) return;
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
#elif DSP_MODEL == DSP_ST7701 || DSP_MODEL == DSP_UEDX48480021 || DSP_MODEL == DSP_AXS15231B
  // RGB panels: myoptions often sets BRIGHTNESS_PIN 255; backlight is gated in dsp.sleep() (e.g. ST7701_BL).
  // RGB: в myoptions часто BRIGHTNESS_PIN 255; подсветка гасится в dsp.sleep() (напр. ST7701_BL).
  dsp.sleep();
  return true;
#else
  return false;
#endif
}

void Display::wakeup(){
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.wake();
#elif DSP_MODEL == DSP_ST7701 || DSP_MODEL == DSP_UEDX48480021 || DSP_MODEL == DSP_AXS15231B
  dsp.wake();
#endif
}

namespace {

const char* displayModeName(displayMode_e mode) {
  switch (mode) {
    case PLAYER: return "PLAYER";
    case VOL: return "VOL";
    case STATIONS: return "STATIONS";
    case NUMBERS: return "NUMBERS";
    case LOST: return "LOST";
    case UPDATING: return "UPDATING";
    case INFO: return "INFO";
    case SETTINGS: return "SETTINGS";
    case TIMEZONE: return "TIMEZONE";
    case WIFI: return "WIFI";
    case CLEAR: return "CLEAR";
    case SLEEPING: return "SLEEPING";
    case SDCHANGE: return "SDCHANGE";
    case SCREENSAVER: return "SCREENSAVER";
    case SCREENBLANK: return "SCREENBLANK";
    default: return "UNKNOWN";
  }
}

const char* uiBackendName(lvgl_ui::UiBackend backend) {
  return (backend == lvgl_ui::UiBackend::Lvgl) ? "Lvgl" : "LegacyCanvas";
}

} // namespace

size_t Display::diagSnapshot(char* out, size_t len) const {
  if (!out || len == 0) return 0;
  out[0] = '\0';
  size_t off = 0;
  bool snap_truncated = false;

  auto append_line = [&](const char* fmt, ...) -> bool {
    if (off >= len) {
      snap_truncated = true;
      return false;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out + off, len - off, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    if ((size_t)n >= len - off) {
      snap_truncated = true;
      off = len - 1;
      out[off] = '\0';
      return false;
    }
    off += (size_t)n;
    return true;
  };

  append_line("heap.free: %u\n", (unsigned)ESP.getFreeHeap());
  append_line("heap.internal_largest: %u\n",
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  append_line("heap.psram_free: %u\n", (unsigned)ESP.getFreePsram());
  append_line("heap.psram_largest: %u\n",
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

  off = lvgl_ui::appendDisplayDiag(out, len, off, &snap_truncated);

  append_line("display.mode: %s\n", displayModeName(_mode));
  append_line("display.backend_active: %s\n", uiBackendName(_activeBackend));
  append_line("display.backend_preferred_player: %s\n",
              uiBackendName(lvgl_ui::getPreferredBackend(PLAYER)));

  append_line("display.legacy_widgets_available: %d\n",
              _legacyWidgetsAvailable() ? 1 : 0);
  append_line("display.legacy_player_widgets_built: %d\n",
              _legacyPlayerWidgetsBuilt ? 1 : 0);
  append_line("display.suspend_flush: %d\n", _suspendFlush ? 1 : 0);
  append_line("g_frameDirty: %d\n", g_frameDirty ? 1 : 0);

  const uint32_t now = millis();
  append_line("panel.gfx_flush_count: %lu\n", (unsigned long)s_panel_gfx_flush_count);
  if (s_panel_last_gfx_flush_ms != 0) {
    append_line("panel.last_gfx_flush_ms_ago: %lu\n",
                (unsigned long)(now - s_panel_last_gfx_flush_ms));
  } else {
    append_line("panel.last_gfx_flush_ms_ago: never\n");
  }

  if (DspTask != nullptr) {
    append_line("dsp_task.stack_hwm_words: %u\n",
                (unsigned)uxTaskGetStackHighWaterMark(DspTask));
  }

  append_line("diag.truncated: %d\n", snap_truncated ? 1 : 0);

  return off;
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
