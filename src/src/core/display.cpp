#include "options.h"

#include <cstdarg>
#include "esp_heap_caps.h"
#include "WiFi.h"
#include "time.h"
#include "display.h"
#include "autodim.h"
#include "sleep_timer.h"
#include "player.h"
#include "network.h"
#include "../core/spidog.h"
#include "../displays/display_port.h"
#include "../lvgl_ui/lvgl_ui.h"
#include "../lvgl_ui/lv_screensaver.h"
#include "../lvgl_ui/lv_ui_events.h"
#include "../ai/ai_subsystem.h"
#include "../i18n/i18n.h"
#if YORADIO_PPM_PCM_TELEMETRY_DIAG
#include "ppm_pcm_telemetry.h"
#endif

// Block 8-E1/E18D: panel flush stats for diag (LVGL direct path via recordLvglDirectPanelFlush).
// Block 8-E1/E18D: счётчики panel flush для diag (прямой LVGL path).
static uint32_t s_panel_flush_count = 0;
static uint32_t s_panel_last_flush_ms = 0;

// Block 8-E18D: legacy markFrameDirty retained as no-op for any stale references.
// Block 8-E18D: markFrameDirty — no-op (legacy Canvas dirty flush removed).
void markFrameDirty() {}

// Block 8-E3/8.1H-H: panel flush counter on the LVGL → DisplayPort path.
// Block 8-E3/8.1H-H: счётчик panel flush на пути LVGL → DisplayPort.
void lvgl_ui::recordLvglDirectPanelFlush() {
    s_panel_flush_count++;
    s_panel_last_flush_ms = millis();
}

Display display;

namespace {

// Format catalog text with one unsigned arg; fallback is error recovery only.
// Формат с одним unsigned; fallback только при ошибке/truncation.
bool formatLocalizedTextU(char* destination, size_t capacity,
                            i18n::TextId format_id, i18n::TextId fallback_id,
                            unsigned int value) {
  if (!destination || capacity == 0) return false;

  const int written =
      snprintf(destination, capacity, i18n::text(format_id), value);
  if (written >= 0 && static_cast<size_t>(written) < capacity) return true;
  strlcpy(destination, i18n::text(fallback_id), capacity);
  return false;
}

// Format catalog text with one string arg; fallback is error recovery only.
// Формат с одной строкой; fallback только при ошибке/truncation.
bool formatLocalizedTextS(char* destination, size_t capacity,
                          i18n::TextId format_id, i18n::TextId fallback_id,
                          const char* string_arg) {
  if (!destination || capacity == 0) return false;

  const char* arg = string_arg ? string_arg : "";
  const int written =
      snprintf(destination, capacity, i18n::text(format_id), arg);
  if (written >= 0 && static_cast<size_t>(written) < capacity) return true;
  strlcpy(destination, i18n::text(fallback_id), capacity);
  return false;
}

}  // namespace

#ifndef DUMMYDISPLAY
//============================================================================================================================


DspCore dsp;

// BASE-LVGL9-MIGRATION EXEC-01B: raised 6 KiB -> 10 KiB for LVGL 9.
// LVGL 9's software draw path is deeper than 8.3's: measured DspTask peak usage
// during Main construction is ~6760 B, which overflowed the old 6144 B stack
// (canary panic). 10240 B leaves ~3.4 KiB measured margin.
// EXEC-01B: 6 KiB -> 10 KiB для LVGL 9 (пик ~6760 B при построении Main).
#ifndef CORE_STACK_SIZE
  #if YORADIO_DSPTASK_12K
    #define CORE_STACK_SIZE  (1024*12)
  #else
    #define CORE_STACK_SIZE  (1024*10)
  #endif
#endif
#ifndef DSP_TASK_DELAY
  #define DSP_TASK_DELAY  pdMS_TO_TICKS(5)
#endif
#ifndef DSQ_SEND_DELAY
  #define DSQ_SEND_DELAY portMAX_DELAY
#endif
TaskHandle_t DspTask;
QueueHandle_t displayQueue;

void returnPlayer(){
  display.putRequest(NEWMODE, PLAYER);
}

void Display::_createDspTask(){
  xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE,  NULL,  3, &DspTask, 0);
}

void loopDspTask(void * pvParameters){
  while(true){
    if(displayQueue==NULL) break;
    display.loop();
#if defined(LVGL_DEBUG_STACK)
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

#if LIGHT_SENSOR!=255
  analogSetAttenuation(ADC_0db);
#endif

  _bootStep = 0;
  _suspendFlush = true;

  const bool display_ok = dsp.initDisplay();
  sleep_timer_init();

  if (!display_ok) {
    Serial.println("[Display] Failed to initialize display backend!");
    return;
  }

  lvgl_ui::initRuntime();
  lvgl_ui::initTick();
  lvgl_ui::initDisplayDriver(dsp.width(), dsp.height());
  lvgl_ui::createTestOverlay();

  displayQueue = xQueueCreate(5, sizeof(requestParams_t));
  if (!displayQueue) {
    Serial.println("[Display] Failed to create display queue!");
    return;
  }

  _createDspTask();

  while(_bootStep == 0) {
    delay(10);
  }

  Serial.println("[Boot] LVGL product path (8-E19B)");
  Serial.println("done");
}

void Display::_start() {
  Serial.println("[Display] _start() called");
  Serial.printf("[Display] network.status = %d\n", network.status);
  if (network.status != CONNECTED && network.status != SDREADY) {
    if (lvgl_ui::isLvglBootActive()) {
      _mode = PLAYER;
      lvgl_ui::bootScreenSetStatusUtf8(
          i18n::text((config.ssidsCount == 0)
                         ? i18n::TextId::BootNoSavedWifiNetworks
                         : i18n::TextId::BootSavedWifiConnectionFailed));
      lvgl_ui::bootScreenNotifyBootSignal();
      _lvgl_wifi_recovery_handoff_pending   = true;
      _lvgl_wifi_recovery_handoff_phase     = 0;
      _lvgl_wifi_recovery_phase_started_ms  = 0;
      _suspendFlush                         = false;
      Serial.println("[Display] Wi-Fi 5A: LVGL Recovery handoff pending");
      return;
    }
    Serial.println("[AP] LVGL Wi-Fi Recovery handoff");
    lvgl_ui::notifyWifiRecoveryEnteredFromBootFailure();
    lvgl_ui::showWifiRecoveryFlowFromDisplayStart();
    {
      const displayMode_e prev_mode = _mode;
      _mode                         = WIFI;
      lvgl_ui::onModeChanged(WIFI, prev_mode);
    }
    _bootStep     = 2;
    _suspendFlush = false;
    return;
  }

  _mode = PLAYER;
  config.setTitle(i18n::text(i18n::TextId::PlayerReady));
  _lvgl_player_handoff_pending = true;
}

void Display::_tryCompleteLvglWifiRecoveryHandoff() {
  if (!_lvgl_wifi_recovery_handoff_pending) return;
  constexpr uint32_t kOpeningMsgHoldMs = 450;

  if (_lvgl_wifi_recovery_handoff_phase == 0) {
    if (!lvgl_ui::isLvglBootMinDwellElapsed()) return;
    lvgl_ui::bootScreenSetStatusUtf8(i18n::text(i18n::TextId::BootOpeningWifiSetup));
    lvgl_ui::bootScreenNotifyBootSignal();
    _lvgl_wifi_recovery_handoff_phase    = 1;
    _lvgl_wifi_recovery_phase_started_ms = millis();
    return;
  }
  if (_lvgl_wifi_recovery_handoff_phase == 1) {
    if ((uint32_t)(millis() - _lvgl_wifi_recovery_phase_started_ms) < kOpeningMsgHoldMs) return;
    lvgl_ui::notifyWifiRecoveryEnteredFromBootFailure();
    lvgl_ui::dismissBootForWifiRecoveryHandoff();
    _lvgl_wifi_recovery_handoff_pending  = false;
    _lvgl_wifi_recovery_handoff_phase    = 0;
    const displayMode_e prev_mode = _mode;
    _mode                         = WIFI;
    lvgl_ui::onModeChanged(WIFI, prev_mode);
    _bootStep     = 2;
    _suspendFlush = false;
  }
}

void Display::_tryCompleteLostEscalation() {
  if (!_lost_escalation_armed) return;
  if (_mode != LOST) { _lost_escalation_armed = false; return; }

  if (!network.beginReconnect) {
    _lost_escalation_armed     = false;
    _lost_started_ms           = 0;
    _lost_escalation_milestone = 0;
    return;
  }

  const uint32_t elapsed = (uint32_t)(millis() - _lost_started_ms);

  if (_lost_escalation_milestone == 0) {
    char line[128];
    formatLocalizedTextU(line, sizeof(line), i18n::TextId::BootRecoveryCountdownFormat,
                         i18n::TextId::BootReconnectStatus, 60U);
    lvgl_ui::overlayLostSetStatusText(line);
    _lost_escalation_milestone = 1;
  }

  if (_lost_escalation_milestone < 2 && elapsed >= 30000U) {
    char line[128];
    formatLocalizedTextU(line, sizeof(line), i18n::TextId::BootRecoveryCountdownFormat,
                         i18n::TextId::BootReconnectStatus, 30U);
    lvgl_ui::overlayLostSetStatusText(line);
    _lost_escalation_milestone = 2;
  }

  if (_lost_escalation_milestone < 3 && elapsed >= 50000U) {
    lvgl_ui::overlayLostSetStatusText(i18n::text(i18n::TextId::BootOpeningRecovery));
    _lost_escalation_milestone = 3;
  }

  if (elapsed >= 60000U) {
    if (!network.beginReconnect) {
      _lost_escalation_armed     = false;
      _lost_started_ms           = 0;
      _lost_escalation_milestone = 0;
      return;
    }
    _lost_escalation_armed     = false;
    _lost_started_ms           = 0;
    _lost_escalation_milestone = 0;
    network.recoverySuspendReconnectForSetup();
    lvgl_ui::notifyWifiRecoveryEnteredFromRuntimeDisconnect();
    putRequest(NEWMODE, WIFI);
  }
}

void Display::_tryCompleteLvglPlayerHandoff() {
  if (!_lvgl_player_handoff_pending) return;
  if (!lvgl_ui::dismissBootForMainHandoffWhenDue()) return;
  _lvgl_player_handoff_pending = false;
  lvgl_ui::onModeChanged(PLAYER, PLAYER);
  _bootStep = 2;
  _suspendFlush = false;
  // PageChain requests the late resync only after Main reports LV_EVENT_SCREEN_LOADED.
  // Do not request here: dismissBoot() may still be inside its fade and onModeChanged()
  // can immediately route through goTo(Main); the completion seam deduplicates that pair.
  // Поздний ресинхрон запрашивает PageChain только после фактической загрузки Main.
}

void Display::_setReturnTicker(uint8_t time_s){
  _returnTicker.detach();
  _returnTicker.once(time_s, returnPlayer);
}

void Display::_switchMode(displayMode_e newmode) {
  if (newmode == _mode) return;
  if (newmode == LOST && lvgl_ui::isWifiSetupFlowActive()) {
    return;
  }
  if (newmode == LOST) {
    _lost_escalation_armed     = true;
    _lost_started_ms           = millis();
    _lost_escalation_milestone = 0;
  } else {
    _lost_escalation_armed     = false;
    _lost_started_ms           = 0;
    _lost_escalation_milestone = 0;
  }
  if (network.status != CONNECTED && network.status != SDREADY && newmode != WIFI &&
      !(newmode == PLAYER && _mode == WIFI)) {
    return;
  }

  const displayMode_e prev_mode = _mode;

  _isStationsChanging = false;
  _isVolumeChanging = false;

  _mode = newmode;
  if (newmode == PLAYER) {
    numOfNextStation = 0;
    _returnTicker.detach();
    config.isScreensaver = false;

    if (prev_mode == SCREENBLANK) {
      config.setDspOn(config.store.dspon, false);
    }
  }
  if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
    config.isScreensaver = true;
    if (newmode == SCREENBLANK) {
      config.setDspOn(false, false);
    }
  } else {
    config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    config.isScreensaver = false;
  }
  if (newmode == STATIONS) {
    currentPlItem = config.lastStation();
  }

  lvgl_ui::onModeChanged(newmode, prev_mode);
}

void Display::resetQueue(){
  if(displayQueue!=NULL) xQueueReset(displayQueue);
}

void Display::setAIInterpretation(const String& text) {
  if (!config.store.ai_enabled) {
    _aiPending = false;
    _aiPendingText[0] = '\0';
    return;
  }

  const char* txt = (text.isEmpty() || text.c_str() == nullptr) ? "" : text.c_str();
  strlcpy(_aiPendingText, txt, sizeof(_aiPendingText));
  _aiPending = true;
}

void Display::copyAIInterpretationForLvgl(char* buf, size_t cap) const {
    if (!buf || cap == 0) return;
    if (!config.store.ai_enabled) {
        buf[0] = '\0';
        return;
    }
    strlcpy(buf, _aiPendingText, cap);
}

void Display::_performRgbResync() {
  // Sole policy-layer caller of the DisplayPort resync request. The backend forwards it to
  // esp_lcd, which consumes one pending flag at the next VSYNC; repeat requests coalesce.
  // Safe no-op while the panel is not ready.
  // Единственный вызов запроса ресинхрона на уровне политики. Backend передаёт его в esp_lcd,
  // который снимает один флаг на следующем VSYNC; повторные запросы схлопываются.
  DisplayPort::restartRgbScanout();
}

void Display::requestRgbResync() {
  // Context-aware so every caller can use one API. DspTask-owned callers (LVGL screens, storage
  // adapters running under LVGL) execute the policy directly: enqueuing from DspTask would add a
  // pointless round-trip and, with a full queue, would block DspTask on a queue only DspTask
  // drains. Every other task serializes through the display queue as before.
  // Контекстно-зависимо, чтобы у всех вызывающих был один API. Вызовы из DspTask выполняются
  // сразу: постановка в очередь из DspTask — лишний круг, а на полной очереди это заблокировало
  // бы DspTask на очереди, которую разбирает только он сам. Прочие задачи — через очередь.
  if (xTaskGetCurrentTaskHandle() == DspTask) {
    _performRgbResync();
    return;
  }
  putRequest(RGB_RESYNC);
}

void Display::putRequest(displayRequestType_e type, int payload){
  if(displayQueue==NULL) return;
  requestParams_t request;
  request.type = type;
  request.payload = payload;

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
}

#ifndef DSP_QUEUE_TICKS
  #define DSP_QUEUE_TICKS pdMS_TO_TICKS(10)
#endif
void Display::loop() {
  static bool s_lvgl_boot_connected_latched = false;
  if(_bootStep==0) {
    if (lvgl_ui::tryPresentLvglBootOnFirstDspLoop()) {
      _bootStep = 1;
      _suspendFlush = false;
      // EARLY startup resync: the RGB panel finished init (and has been streaming) since
      // DisplayPort::begin() succeeded, well before DspTask was even created — this is the
      // first DspTask loop iteration, so it is the latest point still strictly before the
      // Boot screen's first lv_timer_handler() flush later in this same loop() call. Closes
      // the true-cold-boot window where scanout phase can start desynchronized. The LATE
      // resync at completed Boot->Main is owned by PageChain and still covers the
      // config/playlist/NVS tail that runs after this point; the two remain distinct.
      // РАННИЙ стартовый ресинхрон: RGB-панель завершила init (и уже стримит) с момента
      // успешного DisplayPort::begin(), задолго до создания DspTask — это первая итерация
      // цикла DspTask, т.е. последняя точка, ещё строго до первого lv_timer_handler()-flush
      // экрана Boot дальше в этом же вызове loop(). Закрывает окно истинного холодного
      // старта, где scanout может начаться рассинхронизированным. ПОЗДНИЙ ресинхрон после
      // завершённого Boot->Main теперь принадлежит PageChain и покрывает последующий хвост.
      _performRgbResync();
    } else {
      static bool s_lvgl_boot_present_fail_logged = false;
      if (!s_lvgl_boot_present_fail_logged) {
        Serial.println("[Boot] LVGL Boot presentation failed");
        s_lvgl_boot_present_fail_logged = true;
      }
    }
  }
  if(displayQueue==NULL && _bootStep!=1) return;
  requestParams_t request;
  if(xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)){
    switch (request.type){
        case NEWMODE: _switchMode((displayMode_e)request.payload); break;
        // 8.1H-I-B: explicit Main redraw from WebUI settings/reset; no mode change (replaces CLEAR;PLAYER).
        // payload!=0 = force full redraw (e.g. flipscreen orientation change).
        // 8.1H-I-B: явная перерисовка Main из настроек/сброса WebUI; без смены режима (замена CLEAR;PLAYER).
        // payload!=0 = принудительная полная перерисовка (например, смена ориентации flipscreen).
        case REFRESH_MAIN: lvgl_ui::refreshMainScreenFromSettings(request.payload != 0); break;
        case CLOCK:
          if (_mode == SCREENSAVER) {
            lvgl_ui::screensaverRefreshClock();
          }
          break;
        case NEWTITLE: _title(); break;
        case NEWSTATION:
          config.vuThreshold = 0;
          break;
        case DRAWVOL:
          if (_mode == PLAYER || _mode == VOL) {
            lvgl_ui::refreshMainScreen();
          }
          break;
        case DBITRATE:
          if (_mode == PLAYER) {
            lvgl_ui::refreshMainScreen();
          }
          break;
        case BOOTSTRING: {
          if (lvgl_ui::isLvglBootActive()) {
            if (s_lvgl_boot_connected_latched) break;
            char line[96];
            formatLocalizedTextS(line, sizeof(line), i18n::TextId::BootConnectFormat,
                                 i18n::TextId::BootStarting,
                                 config.ssids[request.payload].ssid);
            lvgl_ui::bootScreenSetStatusUtf8(line);
            lvgl_ui::bootScreenNotifyBootSignal();
          }
          break;
        }
        case WAITFORSD: {
          if (lvgl_ui::isLvglBootActive()) {
            if (s_lvgl_boot_connected_latched) break;
            char line[64];
            strlcpy(line, i18n::text(i18n::TextId::WaitForSd), sizeof(line));
            lvgl_ui::bootScreenSetStatusUtf8(line);
            lvgl_ui::bootScreenNotifyBootSignal();
          }
          break;
        }
        case DSP_START:
          Serial.println("[Display] Processing DSP_START request");
          _start();
          break;
        // Asset completion handlers below reload/decode the committed file from LittleFS into
        // PSRAM before the operation is really finished. The resync therefore belongs after the
        // reload, not at the web-side write — one request covers write + commit + read + apply.
        // Обработчики ниже дочитывают/декодируют закоммиченный файл из LittleFS в PSRAM, и лишь
        // тогда операция завершена. Ресинхрон идёт после reload, а не на стороне записи — один
        // запрос покрывает write + commit + read + apply.
        case MAIN_BG_FS_UPDATED: {
          lvgl_ui::onMainBackgroundSlotCommitted(static_cast<uint8_t>(request.payload));
          _performRgbResync();
          break;
        }
        case ART_FS_UPDATED: {
          lvgl_ui::onStationArtCommitted();
          _performRgbResync();
          break;
        }
        case SET_THEME_PRESET: {
          // Writes /data/theme.dat and reapplies the palette across created pages.
          // Пишет /data/theme.dat и переприменяет палитру на созданных страницах.
          lvgl_ui::onThemePresetChanged(static_cast<uint8_t>(request.payload));
          _performRgbResync();
          break;
        }
        case CUSTOM_THEME_FILE_UPDATED: {
          lvgl_ui::onCustomThemeFileUpdated();
          _performRgbResync();
          break;
        }
        // Queued resync from a non-display task (AI config/prompt persistence, playlist import).
        // Ресинхрон из не-display задачи (persistence AI config/prompt, импорт плейлиста).
        case RGB_RESYNC: {
          _performRgbResync();
          break;
        }
        default: break;
      }
    DisplayEvent evt = { request.type, &request, _mode };
    lvgl_ui::onDisplayEvent(evt);
  }
  _tryCompleteLvglWifiRecoveryHandoff();
  _tryCompleteLvglPlayerHandoff();
  _tryCompleteLostEscalation();
  if (lvgl_ui::isLvglBootActive()) {
    if (!s_lvgl_boot_connected_latched && WiFi.status() == WL_CONNECTED) {
      const String ssid = WiFi.SSID();
      char line[96];
      formatLocalizedTextS(line, sizeof(line), i18n::TextId::BootConnectedToFormat,
                           i18n::TextId::BootStarting,
                           ssid.length() ? ssid.c_str()
                                         : i18n::text(i18n::TextId::BootWifiFallbackName));
      lvgl_ui::bootScreenSetStatusUtf8(line);
      s_lvgl_boot_connected_latched = true;
      Serial.printf("[MAIN_BG] connected_ms=%u\n", (unsigned)millis());
    } else if (s_lvgl_boot_connected_latched && _lvgl_player_handoff_pending) {
      // Next loops: Loading background... then JPEG cache for the active theme.
      // Следующие циклы: «Загрузка фона...» и кэш JPEG активной темы.
      lvgl_ui::bootPrepareActiveMainBackgroundIfNeeded();
    }
  } else {
    s_lvgl_boot_connected_latched = false;
  }
  lvgl_ui::mainBgPollRuntimeApply();
  if (_mode != SCREENBLANK && _mode != SCREENSAVER) {
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
    // Weather W2: refresh Weather page while its carousel slot is active (reached via swipe; no
    // dedicated display mode). Same ~1 Hz throttle as Info/Main; reads WeatherState only.
    // Weather W2: обновление страницы погоды, пока активен её слот карусели (доступ свайпом).
    if (lvgl_ui::isLvglCarouselOnWeatherSlot()) {
      static uint32_t lastWeatherRefresh = 0;
      if (millis() - lastWeatherRefresh >= 1000) {
        lvgl_ui::refreshWeatherScreen();
        lastWeatherRefresh = millis();
      }
    }
    // E33: refresh Station status line (clock/RSSI/weather glance) while Station slot is active.
    // No list/focus/scroll work — LvglStationPage::update() only calls wgt_status_line::update().
    // E33: обновление status line Station (~1 Гц); только clock/RSSI/погода — без rebuild списка.
    if (lvgl_ui::isLvglCarouselOnStationSlot()) {
      static uint32_t lastStationRefresh = 0;
      if (millis() - lastStationRefresh >= 1000) {
        lvgl_ui::refreshStationScreen();
        lastStationRefresh = millis();
      }
    }
    // E6C1: refresh Visual status row + metadata while Visual slot is active (~1 Hz).
    // E6C1: обновление status row и metadata на слоте Visual (~1 Гц).
    if (lvgl_ui::isLvglCarouselOnVisualSlot()) {
      static uint32_t lastVisualRefresh = 0;
      if (millis() - lastVisualRefresh >= 1000) {
        lvgl_ui::refreshVisualScreen();
        lastVisualRefresh = millis();
      }
    }
    // 6.7S1a: refresh Settings status line while Settings slot is active (~1 Hz).
    // Same policy as Station/Weather — LvglSettingsPage::update() is status line only.
    // 6.7S1a: обновление status line Settings на слоте Settings (~1 Гц); только status line.
    if (lvgl_ui::isLvglCarouselOnSettingsSlot()) {
      static uint32_t lastSettingsRefresh = 0;
      if (millis() - lastSettingsRefresh >= 1000) {
        lvgl_ui::refreshSettingsScreen();
        lastSettingsRefresh = millis();
      }
    }
  }
  autodim_loop();
  sleep_timer_loop();
  lvgl_ui::taskHandler();
#if YORADIO_PPM_PCM_TELEMETRY_DIAG
  ppmPcmTelemetryConsumerService();
#endif
  dsp.loop();
  #if I2S_DOUT==255
  player.computeVUlevel();
  #endif
}

void Display::_title() {
  if (player_on_track_change) player_on_track_change();
  aiSubsystem.onTrackChange();
}

void Display::flip(){ dsp.flip(); }

void Display::invert(){ dsp.invert(); }

void  Display::setContrast(){
}

bool Display::deepsleep(){
  dsp.sleep();
  return true;
}

void Display::wakeup(){
  dsp.wake();
}

namespace {

const char* displayModeName(displayMode_e mode) {
  switch (mode) {
    case PLAYER: return "PLAYER";
    case VOL: return "VOL";
    case STATIONS: return "STATIONS";
    case LOST: return "LOST";
    case UPDATING: return "UPDATING";
    case INFO: return "INFO";
    case SETTINGS: return "SETTINGS";
    case WIFI: return "WIFI";
    case SCREENSAVER: return "SCREENSAVER";
    case SCREENBLANK: return "SCREENBLANK";
    default: return "UNKNOWN";
  }
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
  append_line("display.ui_path: lvgl_only\n");
  append_line("display.suspend_flush: %d\n", _suspendFlush ? 1 : 0);

  const uint32_t now = millis();
  // Block 8-E19G/8.1H-E: panel.flush_* — LVGL direct panel flush (canonical diag keys).
  // Block 8-E19G/8.1H-E: panel.flush_* — прямой LVGL→panel (канонические ключи diag).
  append_line("panel.flush_count: %lu\n", (unsigned long)s_panel_flush_count);
  if (s_panel_last_flush_ms != 0) {
    const unsigned long ms_ago = (unsigned long)(now - s_panel_last_flush_ms);
    append_line("panel.last_flush_ms_ago: %lu\n", ms_ago);
  } else {
    append_line("panel.last_flush_ms_ago: never\n");
  }

  if (DspTask != nullptr) {
    append_line("dsp_task.stack_hwm_words: %u\n",
                (unsigned)uxTaskGetStackHighWaterMark(DspTask));
  }

  append_line("diag.truncated: %d\n", snap_truncated ? 1 : 0);

  return off;
}

//============================================================================================================================
#else // DUMMYDISPLAY — DSP_DUMMY headless path removed with Nextion (Block 8-E15)
//============================================================================================================================
#error "DSP_DUMMY / DUMMYDISPLAY is unsupported in this LVGL RGB fork after Block 8-E15 Nextion removal"
#endif // DUMMYDISPLAY
