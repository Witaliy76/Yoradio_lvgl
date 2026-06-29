#ifndef LVGL_UI_H
#define LVGL_UI_H

#include <stddef.h>
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

// Weather W2: refresh Weather page from WeatherState. Call only when Weather slot active; ~1 Hz.
// Weather W2: обновление страницы погоды из WeatherState; только когда активен слот Weather; ≤1 Гц.
void refreshWeatherScreen();

// E33: refresh Station status line (clock/RSSI/weather). Call only when Station slot active; ~1 Hz.
// E33: обновление status line на Station (часы/RSSI/погода); только при активном слоте Station; ≤1 Гц.
void refreshStationScreen();

// 8.1H-I-B: forced Main redraw for WebUI settings/reset (replaces the old CLEAR;PLAYER guard-buster).
// Goes to Main page + refreshes, without a mode transition. Use only from REFRESH_MAIN handler (DspTask).
// force_full_redraw: invalidate whole screen + flush now — needed after panel orientation flip.
// 8.1H-I-B: принудительная перерисовка Main для настроек/сброса WebUI (замена идиомы CLEAR;PLAYER).
// Переходит на Main + refresh, без смены режима. Только из обработчика REFRESH_MAIN (DspTask).
// force_full_redraw: инвалидация всего экрана + немедленный flush — нужно после смены ориентации.
void refreshMainScreenFromSettings(bool force_full_redraw = false);

// Stage 6.1F-d: WebUI committed /bg/main_*.bin — reload Main PSRAM bg if slot matches active preset (DspTask queue only).
// После upload_bg: перечитать фон в PSRAM только для активного слота темы; только из обработчика displayQueue.
void onMainBackgroundSlotCommitted(uint8_t slot);

// Station Art MVP: WebUI committed /logo/<key>.bin (upload or remove) — force art reload on Main (DspTask queue only).
// Station Art MVP: после upload_art / remove_art — принудительно перезагрузить арт на Main (только DspTask).
void onStationArtCommitted();

// Stage 6.6R-B: apply runtime theme preset change (DspTask queue handler only).
// preset_id: 0=Dark 1=Light 2=Custom (matches ThemePreset enum value).
// Never call LVGL APIs from NetServer/WebUI — enqueue SET_THEME_PRESET instead.
// Этап 6.6R-B: применить смену пресета темы. Только из обработчика очереди DspTask.
void onThemePresetChanged(uint8_t preset_id);

// Stage 6.6R-F1: reload /data/theme_custom.txt into runtime Custom palette (DspTask only).
// Live LVGL reinit only when active preset is Custom; does not change theme.dat.
// Этап 6.6R-F1: перезагрузка custom palette file; live UI только при активном Custom.
void onCustomThemeFileUpdated();

// prev_mode: mode before transition — used to preserve PageChain when leaving saver/blank.
// prev_mode — режим до перехода; нужен чтобы не сбрасывать карусель при выходе из saver/blank.
void onModeChanged(displayMode_e mode, displayMode_e prev_mode);

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

// Block 8 / 8-E1: append LVGL mem + flush model lines to diag buffer (DspTask/telnet only).
// Block 8 / 8-E1: дописать в буфер diag строки LVGL (только по запросу, не из hot path).
size_t appendDisplayDiag(char* out, size_t len, size_t offset, bool* truncated_out = nullptr);
// Block 8-E3: ST7701 direct panel flush stats (called from lvgl_flush_cb; DspTask only).
// Block 8-E3: учёт panel flush при прямом выводе LVGL (только DspTask).
void recordLvglDirectPanelFlush();

// Block 8-E5C: carousel PageChain transition animation (goTo swipe); not persisted / не в NVS.
// Future Settings page may expose this runtime flag. Default: YORADIO_LVGL_PAGE_TRANSITION_ANIM_DEFAULT.
// Будущая страница Settings может включить slide-анимацию (ESP32-P4 и др.).
void setPageTransitionAnimationEnabled(bool enabled);
bool isPageTransitionAnimationEnabled();
// Block 8-E10: DSP_START Wi‑Fi fail when LVGL Boot already gone — show Recovery shell without legacy AP.
void showWifiRecoveryFlowFromDisplayStart();
void bootScreenSetStatusUtf8(const char* text);
void bootScreenNotifyBootSignal();

// Stage 5.3: horizontal carousel — LV_EVENT_GESTURE on page root (not Boot). DspTask only.
// Этап 5.3: карусель — жест на корне страницы (не Boot), только DspTask.
void installCarouselGesturesOnPageRoot(lv_obj_t* screen_root);

// Stage 6.4A: dismiss active Preset Temporary (tap / future Back parity). DspTask only.
// Этап 6.4A: закрыть Preset Temporary (тап / позже Back). Только DspTask.
void dismissActiveTemporary();
// Stage 6.4B: extend Preset Temporary timeout while row is pressed / saving. DspTask only.
// Этап 6.4B: продлить таймаут Preset при удержании строки / save. Только DspTask.
void refreshActiveTemporaryTimeout();
// Stage 6.4C: remaining ms until Preset Temporary auto-dismiss (0 if inactive/expired).
// Этап 6.4C: оставшееся время (мс) до auto-dismiss Preset Temporary.
uint32_t temporaryRemainingMs();
// Stage 5.3: screensaver prep — hook from tap/gesture; body stays empty until 5.6.
// Этап 5.3: задел под screensaver; вызов из тапа/жеста, реализация позже.
void notifyPageChainActivity();

// Navigate carousel by slot index (PageChain::STATION_INDEX, SETTINGS_INDEX, …). DspTask / LVGL events only.
// Переход по индексу карусели — только из DspTask / LVGL callbacks.
void goToCarouselPage(int page_index);

// Block 8-E12: product inputs that “open station list” → PageChain Station slot.
// Block 8-E12: открыть список станций — LvglStationPage в карусели.
void openStationPageFromProductInput();
void toggleStationListUiFromProductInput();

// Block 8-E13: NEWMODE SETTINGS → Lvgl Settings carousel stub (Main button may use goToCarouselPage directly).
// Block 8-E13: открыть Settings — слот карусели, без legacy const_DlgNextion.
void openSettingsPageFromProductInput();

// True when LVGL carousel shows Info slot (index 0), even if display.mode() is still PLAYER (swipe path).
// Карусель на слоте Info, хотя mode может оставаться PLAYER — путь свайпом.
bool isLvglCarouselOnInfoSlot();
// Block 8-E12: carousel on Station slot (List button / NEWMODE STATIONS / swipe).
bool isLvglCarouselOnStationSlot();

// Weather W2: carousel on Weather slot (index 4, swipe) — gates ~1 Hz refresh in Display::loop.
// Weather W2: карусель на слоте Weather (индекс 4) — основание refresh ~1 Гц в Display::loop.
bool isLvglCarouselOnWeatherSlot();

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
