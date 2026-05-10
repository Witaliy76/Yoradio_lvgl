#ifndef LVGL_UI_H
#define LVGL_UI_H

#include <stdint.h>
#include "../core/common.h"

#ifdef __cplusplus
extern "C" {
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
}
#endif

// LVGL UI subsystem stub (Stage 0)
// Заглушка UI подсистемы LVGL (Stage 0)

namespace lvgl_ui {

// Stage 0: compile-time check that LVGL is linked
// Stage 0: проверка на этапе компиляции, что LVGL подключён
bool isCompiled();

// Stage 2: init LVGL runtime (lv_init()). No display driver / screens.
// Stage 2: инициализация рантайма LVGL без драйвера дисплея и экранов
void initRuntime();

// Stage 2: init LVGL tick source (esp_timer + lv_tick_inc.).
// Stage 2: инициализация источника тиков LVGL (esp_timer + lv_tick_inc())
void initTick();

// Stage 2: register LVGL display driver that renders into Arduino_Canvas.
// Stage 2: регистрация LVGL-дисплея, рисующего в Arduino_Canvas
void initDisplayDriver(uint16_t hor_res, uint16_t ver_res);

// Stage 5.2: register pointer indev after display driver (read_cb on DspTask only).
// Этап 5.2: pointer indev после дисплея (read_cb только на DspTask).
void initTouchIndev();

// Stage 2: run LVGL timers inside Display::loop() / DspTask context.
// Stage 2: запуск таймеров LVGL внутри Display::loop() / контекста DspTask
void taskHandler();

// Stage 2: minimal visible test overlay (one label) to validate rendering.
// Stage 2: минимальный видимый тестовый оверлей для проверки рендеринга
void createTestOverlay();

// INFO v1: refresh value labels from runtime data. Call only when INFO is active; throttle to ~1 Hz.
void refreshInfoScreen();

// Stage 5.5: refresh Main screen labels. Call only when PLAYER is active + Lvgl backend; throttle ~1 Hz.
// Stage 5.5: обновление label'ов Main. Только при PLAYER + Lvgl backend; ≤1 Гц.
void refreshMainScreen();

// Stage 6.1F-d: WebUI committed /bg/main_*.bin — reload Main PSRAM bg if slot matches active preset (DspTask queue only).
// После upload_bg: перечитать фон в PSRAM только для активного слота темы; только из обработчика displayQueue.
void onMainBackgroundSlotCommitted(uint8_t slot);

// Station Art MVP: WebUI committed /logo/<key>.bin (upload or remove) — force art reload on Main (DspTask queue only).
// Station Art MVP: после upload_art / remove_art — принудительно перезагрузить арт на Main (только DspTask).
void onStationArtCommitted();

// Stage 3.2: backend selection stub (no behavior change yet).
// Stage 3.2: заглушка выбора backend'а (без изменения поведения).
enum class UiBackend {
    LegacyCanvas,
    Lvgl
};

UiBackend getPreferredBackend(displayMode_e mode);
// prev_mode: mode before transition (Display::_swichMode); used to preserve PageChain when leaving saver/blank.
// prev_mode — режим до перехода; нужен чтобы не сбрасывать карусель при выходе из saver/blank.
void onModeChanged(displayMode_e mode, UiBackend backend, displayMode_e prev_mode);

// Stage 5.4: LVGL Boot (DspTask only). / Boot LVGL только из DspTask.
bool isLvglBootActive();
// First DspTask loop: show Boot if LVGL display registered; else caller uses legacy boot.
// Первый цикл DspTask: Boot при зарегистрированном дисплее LVGL; иначе legacy boot.
bool tryPresentLvglBootOnFirstDspLoop();
// Drop Boot special mode before Main (PageChain goTo) or AP legacy handoff.
// Снять Boot перед Main или перед legacy AP.
void dismissBootForMainHandoff();
// Wi‑Fi 5B: Boot fail → Wi‑Fi Recovery — tear down Boot into RebootRequired without Main create/enter (no BG preload).
// Wi‑Fi 5B: снять Boot сразу в Wi‑Fi shell, без Main/preload.
void dismissBootForWifiRecoveryHandoff();
// Main path: dismiss only after min time on screen (non-blocking; see Display::_tryCompleteLvglPlayerHandoff).
// Main: снять Boot не раньше min времени на экране (без блокировки DspTask).
bool dismissBootForMainHandoffWhenDue();
// Wi‑Fi 5A: min Boot dwell elapsed (same threshold as dismissBootWhenDue).
bool isLvglBootMinDwellElapsed();
void dismissBootForApLegacyHandoff();
void bootScreenSetStatusUtf8(const char* text);
void bootScreenNotifyBootSignal();

// Stage 5.3: horizontal carousel — LV_EVENT_GESTURE on page root (not Boot). DspTask only.
// Этап 5.3: карусель — жест на корне страницы (не Boot), только DspTask.
void installCarouselGesturesOnPageRoot(lv_obj_t* screen_root);
// Stage 5.3: screensaver prep — hook from tap/gesture; body stays empty until 5.6.
// Этап 5.3: задел под screensaver; вызов из тапа/жеста, реализация позже.
void notifyPageChainActivity();

// True when LVGL carousel shows Info slot (index 0), even if display.mode() is still PLAYER (swipe path).
// Карусель на слоте Info, хотя mode может оставаться PLAYER — путь свайпом.
bool isLvglCarouselOnInfoSlot();

// Wi-Fi 3A: cancel ops, dismiss RebootRequired shell, return display mode to PLAYER (DspTask only).
// Wi-Fi 3A: cancel ops, снять RebootRequired, режим PLAYER (только DspTask).
void dismissWifiFlowReturnToPlayer();

// Wi‑Fi 4C: LVGL Wi‑Fi Setup shell is on screen (RebootRequired + wifi flow screen).
// Wi‑Fi 4C: активен LVGL shell настройки Wi‑Fi (RebootRequired + экран потока).
bool isWifiSetupFlowActive();

// Wi‑Fi 5A: next Wi‑Fi shell enter() is from boot failure (hide Home Back, subtitle).
void notifyWifiRecoveryEnteredFromBootFailure();
bool consumeWifiRecoveryEnteredFromBootFailure();

// S6V8A: runtime disconnect LOST → Recovery escalation (60 s timeout; Display-owned timer).
// S6V8A: эскалация runtime LOST → Recovery (60 с таймер; владелец — Display).
void notifyWifiRecoveryEnteredFromRuntimeDisconnect();
bool consumeWifiRecoveryEnteredFromRuntimeDisconnect();

// S6V8A: overlayLostSetStatusText exposed so Display::_tryCompleteLostEscalation can call it.
// S6V8A: overlayLostSetStatusText доступна Display для обновления milestone текста.
void overlayLostSetStatusText(const char* text);

}

#endif
