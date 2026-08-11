#ifndef display_h
#define display_h
#include "options.h"

#include "Arduino.h"
#include <Ticker.h>
#include "config.h"
#include "common.h"
#include "../displays/dspcore.h"

#ifndef DUMMYDISPLAY
  #include "../lvgl_ui/lvgl_ui.h"
  void loopDspTask(void * pvParameters);

// Forward decl for DspCore scroll helpers (8-E16.2B-1 stub returns nullptr until E16.2B-2/E17).
// Forward decl для scroll-хелперов DspCore (заглушка nullptr до E16.2B-2/E17).
class Page;

class Display {
  public:
    uint16_t currentPlItem;
    uint16_t numOfNextStation;
    displayMode_e _mode;
    bool _isStationsChanging = false;  // Флаг смены режима станций
    bool _isVolumeChanging = false;    // Флаг смены режима громкости
  public:
    Display() {};
    displayMode_e mode() { return _mode; }
    void mode(displayMode_e m) { _mode=m; }
    bool isStationsChanging() { return _isStationsChanging; }
    bool isVolumeChanging() { return _isVolumeChanging; }
    void init();
    void loop();
    void _start();
    bool ready() { return _bootStep==2; }
    void resetQueue();
    void putRequest(displayRequestType_e type, int payload=0);
    // Request one RGB scanout resynchronization after a completed runtime LittleFS mutation
    // transaction (or the startup hazard window). Safe from ANY task: on DspTask it executes
    // immediately, elsewhere it is serialized through the display queue. The only resync entry
    // point that non-display subsystems may call.
    // Запрос одного ресинхрона RGB scanout после завершённой runtime-транзакции записи в
    // LittleFS (или стартового окна). Безопасен из ЛЮБОЙ задачи: на DspTask выполняется сразу,
    // иначе сериализуется через очередь дисплея. Единственная точка входа для не-display кода.
    void requestRgbResync();
    void flip();
    void invert();
    bool deepsleep();
    void wakeup();
    void setContrast();
    void setAIInterpretation(const String& text);  // AI interpretation widget / Виджет AI интерпретации
    // Stage 6.1C: read-only snapshot for LVGL Main AI line (DspTask); no AI logic change.
    // 6.1C: снимок строки для LVGL Main — только чтение, логика AI не трогается.
    void copyAIInterpretationForLvgl(char* buf, size_t cap) const;
    // Block 8 / 8-E1: one-shot display diagnostics (telnet "diag display"); no heap alloc.
    // Block 8 / 8-E1: однократный снимок дисплея (telnet); без выделения heap.
    size_t diagSnapshot(char* out, size_t len) const;
    // Block 8-E16.2B-2: stub for widgets.cpp; RGB drivers no longer call (scroll helpers inert).
    // Block 8-E16.2B-2: заглушка для widgets.cpp; RGB-драйверы не вызывают.
    Page* getActivePage() const { return nullptr; }
  private:
    // AI interpretation pending buffer for LVGL Main (DspTask read via copyAIInterpretationForLvgl).
    // Буфер AI для LVGL Main (чтение на DspTask через copyAIInterpretationForLvgl).
    bool _aiPending = false;
    char _aiPendingText[256];
    Ticker _returnTicker;
    uint8_t _bootStep;
    bool _suspendFlush;
    bool _lvgl_player_handoff_pending = false;
    void _tryCompleteLvglPlayerHandoff();
    // Wi‑Fi 5A: Boot fail → LVGL Recovery handoff (dwell + "Opening…" pause).
    bool _lvgl_wifi_recovery_handoff_pending = false;
    uint8_t _lvgl_wifi_recovery_handoff_phase = 0; // 0=min dwell; 1=Opening msg, wait short delay
    uint32_t _lvgl_wifi_recovery_phase_started_ms = 0;
    void _tryCompleteLvglWifiRecoveryHandoff();
    // S6V8A: runtime LOST → Recovery escalation timer (Display / DspTask owned).
    // S6V8A: таймер эскалации runtime LOST → Recovery (владелец — Display / DspTask).
    bool     _lost_escalation_armed      = false;
    uint32_t _lost_started_ms            = 0;
    uint8_t  _lost_escalation_milestone  = 0; // 0=initial text pending; 1=set; 2=30s; 3=50s
    void _tryCompleteLostEscalation();
    // Sole policy-layer execution point for an RGB scanout resync. DspTask context only.
    // Единственная точка выполнения ресинхрона на уровне политики. Только контекст DspTask.
    void _performRgbResync();
    void _title();
    void _switchMode(displayMode_e newmode);
    void _createDspTask();
    void _setReturnTicker(uint8_t time_s);
};

#else

class Display {
  public:
    uint16_t currentPlItem;
    uint16_t numOfNextStation;
    displayMode_e _mode;
  public:
    Display() {};
    displayMode_e mode() { return _mode; }
    void mode(displayMode_e m) { _mode=m; }
    void init();
    void _start();
    void putRequest(displayRequestType_e type, int payload=0);
    void requestRgbResync(){}
    void loop(){}
    bool ready() { return true; }
    void resetQueue(){}
    void centerText(const char* text, uint8_t y, uint16_t fg, uint16_t bg){}
    void rightText(const char* text, uint8_t y, uint16_t fg, uint16_t bg){}
    void flip(){}
    void invert(){}
    void setContrast(){}
    bool deepsleep(){return true;}
    void wakeup(){}
    void setAIInterpretation(const String& text){}  // AI interpretation widget / Виджет AI интерпретации
    void copyAIInterpretationForLvgl(char* buf, size_t cap) const {
        if (buf && cap) buf[0] = '\0';
    }
};

#endif

extern Display display;

// One-shot RGB resync for a completed runtime LittleFS mutation transaction.
//
// Scope this to a LOGICAL transaction (one user/application filesystem operation), never to a
// filesystem primitive — a per-chunk or per-remove guard would fire many times inside a single
// operation, which is exactly what the policy forbids. Declare it where the transaction ends,
// call markFsMutated() once LittleFS has actually been touched, and the resync is issued on
// every exit path — success or failure — because a mutation that failed can still have
// disturbed the RGB scanout.
//
// Call deferToDisplayEvent() when the operation queues a Display completion event
// (MAIN_BG_FS_UPDATED / ART_FS_UPDATED / CUSTOM_THEME_FILE_UPDATED): that handler reloads the
// committed file and performs the resync at the end of its own work, so issuing one here too
// would give the operation two.
//
// Одноразовый ресинхрон RGB после завершённой runtime-транзакции записи в LittleFS.
// Привязывать к ЛОГИЧЕСКОЙ транзакции, а не к примитиву ФС. markFsMutated() — когда LittleFS
// действительно изменён; ресинхрон сработает на любом выходе, включая ошибочный.
// deferToDisplayEvent() — если операция поставила событие Display, которое сделает ресинхрон
// после перезагрузки файла.
class RgbResyncTransaction {
  public:
    RgbResyncTransaction() = default;
    RgbResyncTransaction(const RgbResyncTransaction&) = delete;
    RgbResyncTransaction& operator=(const RgbResyncTransaction&) = delete;

    void markFsMutated() { _mutated = true; }
    void deferToDisplayEvent() { _deferred = true; }

    ~RgbResyncTransaction() {
      if (_mutated && !_deferred) display.requestRgbResync();
    }

  private:
    bool _mutated = false;
    bool _deferred = false;
};

#endif
