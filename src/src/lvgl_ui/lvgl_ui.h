#ifndef LVGL_UI_H
#define LVGL_UI_H

#include <stdint.h>
#include "../core/common.h"

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

// Stage 3.2: backend selection stub (no behavior change yet).
// Stage 3.2: заглушка выбора backend'а (без изменения поведения).
enum class UiBackend {
    LegacyCanvas,
    Lvgl
};

UiBackend getPreferredBackend(displayMode_e mode);
void onModeChanged(displayMode_e mode, UiBackend backend);

// Stage 5.4: LVGL Boot (DspTask only). / Boot LVGL только из DspTask.
bool isLvglBootActive();
// First DspTask loop: show Boot if LVGL display registered; else caller uses legacy boot.
// Первый цикл DspTask: Boot при зарегистрированном дисплее LVGL; иначе legacy boot.
bool tryPresentLvglBootOnFirstDspLoop();
// Drop Boot special mode before Main (PageChain goTo) or AP legacy handoff.
// Снять Boot перед Main или перед legacy AP.
void dismissBootForMainHandoff();
// Main path: dismiss only after min time on screen (non-blocking; see Display::_tryCompleteLvglPlayerHandoff).
// Main: снять Boot не раньше min времени на экране (без блокировки DspTask).
bool dismissBootForMainHandoffWhenDue();
void dismissBootForApLegacyHandoff();
void bootScreenSetStatusUtf8(const char* text);
void bootScreenNotifyBootSignal();

}

#endif
