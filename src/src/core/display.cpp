#include "options.h"

#include <cstdarg>
#include "esp_heap_caps.h"
#include "WiFi.h"
#include "time.h"
#include "display.h"
#include "player.h"
#include "network.h"
#include "../core/spidog.h"
#include "../lvgl_ui/lvgl_ui.h"
#include "../lvgl_ui/lv_screensaver.h"
#include "../lvgl_ui/lv_ui_events.h"
#include "../ai/ai_subsystem.h"
extern Arduino_Canvas* gfx;

// Block 8-E1/E18D: panel flush stats for diag (LVGL direct path via recordLvglDirectPanelFlush).
// Block 8-E1/E18D: счётчики panel flush для diag (прямой LVGL path).
static uint32_t s_panel_gfx_flush_count = 0;
static uint32_t s_panel_last_gfx_flush_ms = 0;

// Block 8-E18D: legacy markFrameDirty retained as no-op for any stale references.
// Block 8-E18D: markFrameDirty — no-op (legacy Canvas dirty flush removed).
void markFrameDirty() {}

// Block 8-E3: panel flush counter when LVGL bypasses Canvas (ST7701 direct path).
// Block 8-E3: счётчик panel flush при прямом LVGL→output_display (без g_frameDirty).
void lvgl_ui::recordLvglDirectPanelFlush() {
    s_panel_gfx_flush_count++;
    s_panel_last_gfx_flush_ms = millis();
}

Display display;

#ifndef DUMMYDISPLAY
//============================================================================================================================


DspCore dsp;

#ifndef DSQ_SEND_DELAY
  #define DSQ_SEND_DELAY portMAX_DELAY
#endif

#ifndef CORE_STACK_SIZE
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
  xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE,  NULL,  3, &DspTask, 0);
}

void loopDspTask(void * pvParameters){
  while(true){
    if(displayQueue==NULL) break;
    display.loop();
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2) && defined(LVGL_DEBUG_STACK)
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

  dsp.initDisplay();

  // Block 8-E18D: SpectrumAnalyzer runtime disabled; files kept for future LVGL widget.
  // Block 8-E18D: SpectrumAnalyzer отключён в runtime; файлы — для будущего LVGL-виджета.

#if DSP_MODEL == DSP_ST7701
  // Block 8-E17: ST7701 LVGL product — panel via output_display, gfx stays nullptr.
  if (!dsp.getOutputDisplay()) {
    Serial.println("[Display] Failed to initialize display (no output_display)!");
    return;
  }
#else
  if (!gfx) {
    Serial.println("[Display] Failed to initialize display!");
    return;
  }
#endif

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

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  Serial.println("[Boot] LVGL product path; legacy Pager boot removed (8-E16.2B-1)");
#else
  #error "Legacy Canvas Display boot removed in Block 8-E16.2B-1; YORADIO_USE_LVGL required"
#endif

  Serial.println("done");
}

void Display::_start() {
  Serial.println("[Display] _start() called");
  Serial.printf("[Display] network.status = %d\n", network.status);
  if (network.status != CONNECTED && network.status != SDREADY) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (lvgl_ui::isLvglBootActive()) {
      _mode = PLAYER;
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
    Serial.println("[AP] LVGL Wi-Fi Recovery handoff; legacy _apScreen removed (8-E16.2B-1)");
    lvgl_ui::notifyWifiRecoveryEnteredFromBootFailure();
    lvgl_ui::showWifiRecoveryFlowFromDisplayStart();
    {
      const displayMode_e prev_mode = _mode;
      _mode                         = WIFI;
      lvgl_ui::UiBackend backend    = lvgl_ui::getPreferredBackend(WIFI);
      _activeBackend                = backend;
      lvgl_ui::onModeChanged(WIFI, backend, prev_mode);
    }
    _bootStep     = 2;
    _suspendFlush = false;
    return;
#else
    #error "Legacy AP Canvas path removed in Block 8-E16.2B-1"
#endif
  }

#if !(YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2))
  #error "Legacy Canvas PLAYER _start removed in Block 8-E16.2B-1; LVGL required"
#endif
  lvgl_ui::UiBackend startBackend = lvgl_ui::getPreferredBackend(PLAYER);
  _mode = PLAYER;
  config.setTitle(const_PlReady);

  if (startBackend == lvgl_ui::UiBackend::Lvgl) {
    _lvgl_player_handoff_pending = true;
    return;
  }
  Serial.println("[Display] FATAL: non-LVGL PLAYER backend on LVGL product build");
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
    lvgl_ui::dismissBootForWifiRecoveryHandoff();
    _lvgl_wifi_recovery_handoff_pending  = false;
    _lvgl_wifi_recovery_handoff_phase    = 0;
    const displayMode_e prev_mode        = _mode;
    _mode                                = WIFI;
    lvgl_ui::UiBackend backend           = lvgl_ui::getPreferredBackend(WIFI);
    _activeBackend                       = backend;
    lvgl_ui::onModeChanged(WIFI, backend, prev_mode);
    _bootStep       = 2;
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
    lvgl_ui::overlayLostSetStatusText("Trying to reconnect...\nWi-Fi Recovery in 60s");
    _lost_escalation_milestone = 1;
  }

  if (_lost_escalation_milestone < 2 && elapsed >= 30000U) {
    lvgl_ui::overlayLostSetStatusText("Trying to reconnect...\nWi-Fi Recovery in 30s");
    _lost_escalation_milestone = 2;
  }

  if (_lost_escalation_milestone < 3 && elapsed >= 50000U) {
    lvgl_ui::overlayLostSetStatusText("Trying to reconnect...\nOpening Wi-Fi Recovery...");
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
#endif

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
void Display::_tryCompleteLvglPlayerHandoff() {
  if (!_lvgl_player_handoff_pending) return;
  if (!lvgl_ui::dismissBootForMainHandoffWhenDue()) return;
  _lvgl_player_handoff_pending = false;
  _activeBackend = lvgl_ui::getPreferredBackend(PLAYER);
  lvgl_ui::onModeChanged(PLAYER, _activeBackend, PLAYER);
  _bootStep = 2;
  _suspendFlush = false;
}
#endif

void Display::_setReturnTicker(uint8_t time_s){
  _returnTicker.detach();
  _returnTicker.once(time_s, returnPlayer);
}

void Display::_swichMode(displayMode_e newmode) {
  if (newmode == _mode) return;
  if (newmode == SDCHANGE) {
    return;
  }
  if (newmode == NUMBERS) {
    return;
  }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  if (newmode == TIMEZONE) {
    return;
  }
  if (newmode == SLEEPING) {
    return;
  }
#endif
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
#endif
  if (network.status != CONNECTED && network.status != SDREADY && newmode != WIFI &&
      !(newmode == PLAYER && _mode == WIFI)) {
    return;
  }

  const displayMode_e prev_mode = _mode;

  _isStationsChanging = false;
  _isVolumeChanging = false;

  _mode = newmode;
  dsp.setScrollId(NULL);
  if (newmode == PLAYER) {
    numOfNextStation = 0;
    _returnTicker.detach();
    config.isScreensaver = false;

    if (lvgl_ui::getPreferredBackend(PLAYER) == lvgl_ui::UiBackend::Lvgl) {
      if (prev_mode == SCREENBLANK) {
        config.setDspOn(config.store.dspon, false);
      }
    }
  }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  const bool lvgl_screensaver_path =
      (newmode == SCREENSAVER || newmode == SCREENBLANK) &&
      (lvgl_ui::getPreferredBackend(newmode) == lvgl_ui::UiBackend::Lvgl);
#else
  const bool lvgl_screensaver_path = false;
#endif
  if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
    config.isScreensaver = true;
    if (newmode == SCREENBLANK) {
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
  if (newmode == STATIONS) {
    currentPlItem = config.lastStation();
  }

  lvgl_ui::UiBackend backend = lvgl_ui::getPreferredBackend(newmode);
  _activeBackend = backend;
  lvgl_ui::onModeChanged(newmode, backend, prev_mode);
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
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
  static bool s_lvgl_boot_connected_latched = false;
#endif
  if(_bootStep==0) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (lvgl_ui::tryPresentLvglBootOnFirstDspLoop()) {
      _bootStep = 1;
      _suspendFlush = false;
    } else {
      static bool s_lvgl_boot_present_fail_logged = false;
      if (!s_lvgl_boot_present_fail_logged) {
        Serial.println("[Boot] LVGL Boot presentation failed; legacy Canvas boot disabled");
        s_lvgl_boot_present_fail_logged = true;
      }
    }
#else
    #error "Legacy boot loop removed in Block 8-E16.2B-1"
#endif
  }
  if(displayQueue==NULL && _bootStep!=1) return;
  requestParams_t request;
  if(xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)){
    switch (request.type){
        case NEWMODE: _swichMode((displayMode_e)request.payload); break;
        case CLOCK:
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (_mode == SCREENSAVER && _activeBackend == lvgl_ui::UiBackend::Lvgl) {
            lvgl_ui::screensaverRefreshClock();
          }
#endif
          break;
        case NEWTITLE: _title(); break;
        case NEWSTATION:
          config.vuThreshold = 0;
          break;
        case NEXTSTATION:
          break;
        case DRAWPLAYLIST:
          break;
        case DRAWVOL:
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (_activeBackend == lvgl_ui::UiBackend::Lvgl &&
              (_mode == PLAYER || _mode == VOL)) {
            lvgl_ui::refreshMainScreen();
          }
#endif
          break;
        case DBITRATE:
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          if (_activeBackend == lvgl_ui::UiBackend::Lvgl && _mode == PLAYER) {
            lvgl_ui::refreshMainScreen();
          }
#endif
          break;
        case AUDIOINFO:
          break;
        case SHOWVUMETER:
          break;
        case SHOWWEATHER:
          break;
        case NEWWEATHER:
          break;
        case BOOTSTRING: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          const bool lvgl_boot_active_now = lvgl_ui::isLvglBootActive();
          if (lvgl_boot_active_now) {
            if (s_lvgl_boot_connected_latched) break;
            char line[96];
            snprintf(line, sizeof(line), bootstrFmt, config.ssids[request.payload].ssid);
            lvgl_ui::bootScreenSetStatusUtf8(line);
            lvgl_ui::bootScreenNotifyBootSignal();
          }
#endif
          break;
        }
        case WAITFORSD: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          const bool lvgl_boot_active_now = lvgl_ui::isLvglBootActive();
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
        case SDFILEINDEX:
          break;
        case DSPRSSI:
          break;
        case PSTART:
          break;
        case PSTOP:
          break;
        case DSP_START:
          Serial.println("[Display] Processing DSP_START request");
          _start();
          break;
        case NEWIP:
          break;
        case MAIN_BG_FS_UPDATED: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          lvgl_ui::onMainBackgroundSlotCommitted(static_cast<uint8_t>(request.payload));
#endif
          break;
        }
        case ART_FS_UPDATED: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          lvgl_ui::onStationArtCommitted();
#endif
          break;
        }
        case SET_THEME_PRESET: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          lvgl_ui::onThemePresetChanged(static_cast<uint8_t>(request.payload));
#endif
          break;
        }
        case CUSTOM_THEME_FILE_UPDATED: {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
  if (_activeBackend == lvgl_ui::UiBackend::Lvgl && _mode != SCREENBLANK && _mode != SCREENSAVER) {
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
  const bool lvgl_boot_active = lvgl_ui::isLvglBootActive();
  if (_activeBackend == lvgl_ui::UiBackend::Lvgl || lvgl_boot_active) {
    lvgl_ui::taskHandler();
  }
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
  #if DSP_MODEL==DSP_NOKIA5110
    dsp.setContrast(config.store.contrast);
  #endif
}

bool Display::deepsleep(){
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN!=255
  dsp.sleep();
  return true;
#elif DSP_MODEL == DSP_ST7701 || DSP_MODEL == DSP_UEDX48480021 || DSP_MODEL == DSP_AXS15231B
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

  // Block 8-E16.2B-1: legacy Pager/widgets removed from Display.
  append_line("display.legacy_pager_widgets: 0\n");
  append_line("display.canvas_allocated: %d\n", gfx ? 1 : 0);
  append_line("display.legacy_canvas_flush: 0\n");
  append_line("display.suspend_flush: %d\n", _suspendFlush ? 1 : 0);
  append_line("g_frameDirty: 0\n");

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
#else // DUMMYDISPLAY — DSP_DUMMY headless path removed with Nextion (Block 8-E15)
//============================================================================================================================
#error "DSP_DUMMY / DUMMYDISPLAY is unsupported in this LVGL RGB fork after Block 8-E15 Nextion removal"
#endif // DUMMYDISPLAY
