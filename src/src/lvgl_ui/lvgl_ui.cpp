#include "lvgl_ui.h"
#include "lv_overlay.h"
#include "lv_screensaver.h"
#include "lv_touch_indev.h"
#include "lv_ui_events.h"
#include "profiles/lv_profile_select.h"
#include "theme/lv_theme_yoradio.h"
#include "lv_fs_littlefs.h"

#include <cstdarg>

#include "lvgl.h"
#if LV_USE_PERF_MONITOR
#include <cstring>
#endif
#include "esp_timer.h"
#include "Arduino.h"
#include "lv_page_chain.h"
#include "screens/scr_info.h"
#include "screens/scr_main.h"
#include "screens/scr_station.h"
#include "screens/scr_stub.h"
#include "screens/scr_visual.h"
#include "screens/scr_weather.h"
#include "screens/scr_preset.h"
#include "screens/scr_boot.h"
#include "screens/scr_wifi_flow.h"
#include "../core/config.h"
#include "../core/display.h"
#include "../core/options.h"
#include "../core/spidog.h"
#if DSP_MODEL == DSP_ST7701
#include <Arduino_GFX.h>  // flush() on Arduino_GFX; getOutputDisplay() returns Arduino_G*
#endif
#include "../core/network.h"
#include "../core/wifi_ops_adapter.h"

using namespace lvgl_ui;

static PageChain s_page_chain;
static LvglInfoPage s_info_page;
static LvglMainScreen s_main_screen;
static LvglVisualPage s_visual_page;
static LvglStationPage s_station_page;
static LvglWeatherPage s_weather_page;  // Weather W2: real page replaces the stub / реальная страница вместо заглушки
static LvglStubPage s_stub_settings("Settings");
static LvglPresetScreen s_preset_screen;
static LvglBootScreen s_boot_screen;
static LvglWifiFlowScreen s_wifi_flow_screen;

namespace {

// Stage 6.4A: Preset Temporary timeout — explicit caller value; PageChain default stays 20s.
// Этап 6.4A: таймаут Preset — явная константа; дефолт PageChain не меняем.
static constexpr uint32_t kPresetTimeoutMs = 15000;

// Top-edge zone: swipe starting with start_y <= this value can open Preset.
// Верхняя зона: свайп, начатый при start_y <= этого значения, может открыть Preset.
static constexpr int16_t kPresetTopEdgeZonePx = 64;

// Minimum downward delta (px) to confirm swipe intent. Rejects short taps.
// Минимальная вертикальная дельта для подтверждения намерения; отсекает короткие тапы.
static constexpr int16_t kPresetMinVerticalDelta = 22;

// Polling state machine for top-edge swipe (DspTask, updated from taskHandler).
// Unlike page-root event callbacks, this polls the indev directly — unaffected by child
// widgets that stop PRESSING/GESTURE event bubbling (e.g. scr_weather, scr_station list).
// State machine работает на уровне indev — child-виджеты не могут заблокировать обнаружение.
static bool s_swipe_tracking  = false;
static int16_t s_swipe_start_y = -1;
static int16_t s_swipe_start_x = -1;
static bool s_swipe_triggered  = false;

// Stage 6.4A: explicit allowlist — Preset only from Info / Main / Visual / Weather.
// Этап 6.4A: явный allowlist — Preset только с Info / Main / Visual / Weather.
static bool preset_carousel_slot_allowed(int index) {
    switch (index) {
        case PageChain::INFO_INDEX:
        case PageChain::MAIN_INDEX:
        case PageChain::VISUAL_INDEX:
        case PageChain::WEATHER_INDEX:
            return true;
        default:
            return false;
    }
}

static bool preset_open_gesture_allowed() {
    if (s_page_chain.isTemporaryActive()) return false;
    if (lvgl_ui::isLvglBootActive()) return false;
    if (lvgl_ui::isWifiSetupFlowActive()) return false;
    const displayMode_e m = display.mode();
    if (m == SCREENBLANK || m == SCREENSAVER || m == WIFI || m == LOST || m == UPDATING) return false;
    if (m == STATIONS) return false;
    return preset_carousel_slot_allowed(s_page_chain.currentIndex());
}

// Horizontal carousel: direct mapping from LVGL gesture dir.
// X normalization is now in lv_touch_read_cb — no per-board swap needed here.
// Горизонтальная карусель: прямой маппинг из gesture dir (нормализация X теперь в lv_touch_read_cb).
static void map_horizontal_gesture_to_carousel(lv_dir_t dir) {
    if (dir == LV_DIR_LEFT) {
        s_page_chain.swipeLeft();
        return;
    }
    if (dir == LV_DIR_RIGHT) {
        s_page_chain.swipeRight();
    }
}

// Horizontal carousel only — vertical swipe (Preset) handled by poll_top_edge_swipe().
// Только горизонтальная карусель; вертикальный свайп (Preset) — в poll_top_edge_swipe().
static void carousel_gesture_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;

    // Screensaver / blank wake is handled in lv_touch_read_cb — skip here.
    // Пробуждение saver/blank — в lv_touch_read_cb, здесь пропускаем.
    if (display.mode() == SCREENBLANK || display.mode() == SCREENSAVER) return;
    if (display.mode() == WIFI) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    s_page_chain.onActivity();
    map_horizontal_gesture_to_carousel(dir);
    // Consume so child widgets don't receive click/SHORT_CLICKED for the same stroke.
    // Поглощаем, чтобы дочерние виджеты не получали click за тот же жест.
    lv_indev_wait_release(indev);
}

// Poll touch indev state directly — bypasses LVGL object tree / event bubbling.
// Called from taskHandler() every frame. Reliably detects top-edge downward swipe
// even when the swipe starts over a child widget that stops PRESSING/GESTURE bubbling
// (e.g. scr_weather metric rows, scr_station list area).
// Polling indev напрямую: не зависит от bubbling. Работает при старте над любым дочерним виджетом.
static void poll_top_edge_swipe() {
    const bool    down = touchIndevIsDown();
    const int16_t cx   = touchIndevX();
    const int16_t cy   = touchIndevY();

    if (down) {
        if (!s_swipe_tracking) {
            s_swipe_tracking  = true;
            s_swipe_start_x   = cx;
            s_swipe_start_y   = cy;
            s_swipe_triggered = false;
        } else if (!s_swipe_triggered &&
                   s_swipe_start_y >= 0 &&
                   s_swipe_start_y <= kPresetTopEdgeZonePx) {
            const int16_t dy = static_cast<int16_t>(cy - s_swipe_start_y);
            if (dy < kPresetMinVerticalDelta) return;
            const int16_t dx = (cx > s_swipe_start_x)
                ? static_cast<int16_t>(cx - s_swipe_start_x)
                : static_cast<int16_t>(s_swipe_start_x - cx);
            if (dx >= dy) return;   // more horizontal than vertical → carousel swipe, not Preset
            if (!preset_open_gesture_allowed()) return;
            s_swipe_triggered = true;
            s_page_chain.onActivity();
            s_page_chain.showTemporary(&s_preset_screen, kPresetTimeoutMs);
        }
    } else {
        s_swipe_tracking  = false;
        s_swipe_start_y   = -1;
        s_swipe_start_x   = -1;
        s_swipe_triggered = false;
    }
}

} // namespace

// True while Boot special mode is shown (Stage 5.4). / Пока виден Boot (этап 5.4).
static bool s_lvgl_boot_active = false;
// millis() when LVGL Boot was shown — min dwell before Main handoff (legacy boot had ~3s logo dwell).
// Время показа Boot — минимум на экране до перехода на Main (как ~3s лого в legacy).
static uint32_t s_lvgl_boot_shown_ms = 0;
static constexpr uint32_t kLvglBootMinVisibleMs = 3000;

static void ensurePageChainRegistered() {
    static bool s_registered = false;
    if (s_registered) return;
    s_page_chain.registerPage(PageChain::INFO_INDEX, &s_info_page);
    s_page_chain.registerPage(PageChain::MAIN_INDEX, &s_main_screen);
    s_page_chain.registerPage(PageChain::VISUAL_INDEX, &s_visual_page);
    s_page_chain.registerPage(PageChain::STATION_INDEX, &s_station_page);
    s_page_chain.registerPage(PageChain::WEATHER_INDEX, &s_weather_page);
    s_page_chain.registerPage(PageChain::SETTINGS_INDEX, &s_stub_settings);
    s_registered = true;
}

// Lightweight LVGL tick callback: only lv_tick_inc()
// Лёгкий callback тиков LVGL: только lv_tick_inc()
static void lvgl_tick_cb(void *arg) {
    (void)arg;
    lv_tick_inc(1);
}

static esp_timer_handle_t s_lv_tick_timer = nullptr;

// LVGL display driver state / Состояние драйвера дисплея LVGL
static lv_disp_draw_buf_t s_disp_draw_buf;
static lv_color_t*        s_disp_buf1 = nullptr;
static lv_disp_drv_t      s_disp_drv;
static lv_disp_t*         s_disp = nullptr;

#if DSP_MODEL == DSP_ST7701
// Block 8-E5: partial PSRAM stripe height for diag (0 until initDisplayDriver).
// Block 8-E5: высота полосы partial-буфера для diag (0 до init).
static uint16_t s_disp_buf_partial_h = 0;
#endif

// Block 8-E1: flush stats for one-shot diag only (no Serial in hot path).
// Block 8-E1: счётчики flush только для diag по запросу.
static uint32_t s_lvgl_flush_count = 0;
static uint32_t s_lvgl_last_flush_ms = 0;

#if DSP_MODEL == DSP_ST7701
// Block 8-E3/E17/E18C (4848S040): LVGL → output_display directly; no Arduino_Canvas on product path.
// Block 8-E3/E17/E18C: LVGL → output_display; Canvas снят (E17), legacy dirty flush отключён (E18C).
static void lvgl_flush_direct_panel(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    Arduino_G* panel_g = dsp.getOutputDisplay();
    if (!panel_g || !color_p || !area) {
        lv_disp_flush_ready(drv);
        return;
    }
    Arduino_GFX* panel = static_cast<Arduino_GFX*>(panel_g);

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    if (x2 < x1 || y2 < y1) {
        lv_disp_flush_ready(drv);
        return;
    }

    const int32_t hor = static_cast<int32_t>(drv->hor_res);
    const int32_t ver = static_cast<int32_t>(drv->ver_res);
    if (x1 >= hor || y1 >= ver) {
        lv_disp_flush_ready(drv);
        return;
    }
    if (x2 >= hor) x2 = hor - 1;
    if (y2 >= ver) y2 = ver - 1;

    const int32_t w = x2 - x1 + 1;
    const int32_t h = y2 - y1 + 1;
    const int32_t src_stride = area->x2 - area->x1 + 1;
    uint16_t* px = reinterpret_cast<uint16_t*>(color_p);
    if (x1 != area->x1 || y1 != area->y1) {
        px += static_cast<int32_t>(y1 - area->y1) * src_stride + (x1 - area->x1);
    }

    sdog.takeMutex();
    panel_g->draw16bitRGBBitmap(x1, y1, px, w, h);
    panel->flush();
    sdog.giveMutex();

    s_lvgl_flush_count++;
    s_lvgl_last_flush_ms = millis();
    lvgl_ui::recordLvglDirectPanelFlush();

    lv_disp_flush_ready(drv);
}
#endif

// Flush callback: LVGL product path → output_display direct only (E5C). Canvas fallback removed (E18D).
// Flush callback: только прямой вывод на panel (E5C); Canvas fallback удалён (E18D).
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
#if DSP_MODEL == DSP_ST7701
    lvgl_flush_direct_panel(drv, area, color_p);
#else
    // Non-ST7701 envs: legacy Canvas LVGL flush not supported on this fork (E18D).
    // Иные env: Canvas LVGL flush не поддерживается на этом форке (E18D).
    (void)area;
    (void)color_p;
    lv_disp_flush_ready(drv);
#endif
}

// Periodic page refresh policy: no Main/Info/stub updates under saver or blank (any carousel slot).
// Политика периодического refresh: не трогать Main/Info/стабы в saver или blank (любой слот карусели).
static bool lvgl_page_refresh_allowed() {
    const displayMode_e m = display.mode();
    return m != SCREENBLANK && m != SCREENSAVER && m != WIFI;
}

#if LV_USE_PERF_MONITOR
// Stock LVGL perf label: TOP_RIGHT + auto width → we pin left of a fixed box + right text (no digit jump).
// Встроенный perf label: фикс. ширина + текст справа — левый край блока не смещается при 4%↔10% CPU.
static constexpr lv_coord_t kLvglPerfMonitorLabelW = 76; // fits "50 FPS\n100% CPU" / под двузначные FPS/CPU

// Stage 6.6R-GB2: cached perf-label so theme switches can recolor it without rescanning sys layer.
// Этап 6.6R-GB2: кэш perf-label — перекраска при смене темы без повторного скана sys-слоя.
static lv_obj_t* s_perf_label = nullptr;

// Apply theme-aware text color to the debug perf overlay (transparent bg → text must read on any theme).
// Light → graphite text_primary; Dark/Custom-dark → their light text_primary. Always readable by palette design.
// Тема-зависимый цвет текста debug-оверлея: на Light графит, на Dark светлый — по палитре всегда читаемо.
static void applyPerfMonitorThemeTextColor() {
    if (!s_perf_label) return;
    lv_obj_set_style_text_color(s_perf_label, yoradio_palette().text_primary, LV_PART_MAIN);
}

static void repositionBuiltinLvglPerfMonitorOnce() {
    static bool s_done = false;
    if (s_done) return;
    lv_obj_t* sys = lv_layer_sys();
    if (!sys) return;
    const uint32_t n = lv_obj_get_child_cnt(sys);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* ch = lv_obj_get_child(sys, i);
        if (!ch || lv_obj_get_class(ch) != &lv_label_class) continue;
        const char* txt = lv_label_get_text(ch);
        if (!txt || std::strstr(txt, "FPS") == nullptr) continue;

        lv_disp_t* d = lv_disp_get_default();
        const lv_coord_t w =
            d ? static_cast<lv_coord_t>(lv_disp_get_hor_res(d)) : static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width);
        const lv_coord_t x_est = (w * 12) / 100;
        const lv_coord_t x0   = x_est > 40 ? x_est : 40;

        lv_obj_set_width(ch, kLvglPerfMonitorLabelW);
        lv_obj_set_style_text_align(ch, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_long_mode(ch, LV_LABEL_LONG_CLIP);
        lv_obj_align(ch, LV_ALIGN_TOP_LEFT, x0, 2);
        // Stage 6.6R-GB1: perf monitor is a debug overlay — drop its default grey pill so it does not
        // clash with the Light theme. Background stays transparent.
        // 6.6R-GB1: убираем серую подложку debug-оверлея — фон прозрачный.
        lv_obj_set_style_bg_opa(ch, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ch, 0, LV_PART_MAIN);
        // Stage 6.6R-GB2: theme-aware text color (white default washed out on Light ivory).
        // 6.6R-GB2: тема-зависимый цвет текста (белый сливался на Light).
        s_perf_label = ch;
        applyPerfMonitorThemeTextColor();
        s_done = true;
        break;
    }
}
#endif

void lvgl_ui::refreshInfoScreen() {
    if (!lvgl_page_refresh_allowed()) return;
    s_info_page.update();
}

void lvgl_ui::refreshMainScreen() {
    if (!lvgl_page_refresh_allowed()) return;
    // E1R2: Main update only while Main is the active carousel slot (same policy as Weather/Station).
    // E1R2: обновление Main только на активном слоте карусели (как Weather/Station).
    if (s_page_chain.currentIndex() != PageChain::MAIN_INDEX) return;
    s_main_screen.update();
}

// Weather W2: refresh the Weather page labels from WeatherState. Call only while the Weather
// carousel slot is active; throttle ~1 Hz (same policy as Info). No network from UI.
// Weather W2: обновление страницы погоды из WeatherState; только когда активен слот Weather, ≤1 Гц.
void lvgl_ui::refreshWeatherScreen() {
    if (!lvgl_page_refresh_allowed()) return;
    s_weather_page.update();
}

// E33: refresh Station status line (clock/RSSI/weather glance) while Station slot is active.
// LvglStationPage::update() contains only null guards + wgt_status_line::update() — safe at 1 Hz.
// E33: обновление status line Station (часы/RSSI/погода); только когда активен слот Station, ≤1 Гц.
// LvglStationPage::update() содержит только guard + wgt_status_line::update() — безопасно.
void lvgl_ui::refreshStationScreen() {
    if (!lvgl_page_refresh_allowed()) return;
    s_station_page.update();
}

// E6C1: refresh Visual status row + static metadata while Visual slot is active.
// E6C1: обновление status row и статических метаданных на слоте Visual.
void lvgl_ui::refreshVisualScreen() {
    if (!lvgl_page_refresh_allowed()) return;
    if (s_page_chain.currentIndex() != PageChain::VISUAL_INDEX) return;
    s_visual_page.update();
}

void lvgl_ui::onMainBackgroundSlotCommitted(uint8_t slot) {
    if (slot > 2u) {
        return;
    }
    const uint8_t active = static_cast<uint8_t>(yoradio_theme_active_preset());
    if (slot != active) {
        return;
    }
    s_main_screen.reloadFileBackgroundFromLittlefs();
}

void lvgl_ui::onStationArtCommitted() {
    // Force art reload on Main after WebUI upload_art / remove_art (DspTask queue handler only).
    // Принудительная перезагрузка арта после upload_art / remove_art из обработчика очереди DspTask.
    s_main_screen.reloadStationArtFromLittlefs();
}

void lvgl_ui::onCustomThemeFileUpdated() {
    (void)yoradio_theme_load_custom_palette_file(nullptr);
    if (yoradio_theme_active_preset() != ThemePreset::Custom) {
        return;
    }
    yoradio_theme_reinit(s_disp);
    ensurePageChainRegistered();
    s_page_chain.reapplyThemeToCreatedPages();
    if (screensaverIsVisible()) {
        screensaverHide();
        screensaverShow();
    }
#if LV_USE_PERF_MONITOR
    // 6.6R-GB2: Custom (file) may change text_primary → refresh perf overlay color.
    // 6.6R-GB2: Custom-файл мог изменить text_primary → обновить цвет perf-оверлея.
    applyPerfMonitorThemeTextColor();
#endif
}

void lvgl_ui::onThemePresetChanged(uint8_t preset_id) {
    // Clamp to valid range; unknown preset falls back to Dark / Некорректный ID → Dark.
    if (preset_id > 2u) preset_id = 0u;

    const ThemePreset next = static_cast<ThemePreset>(preset_id);

    // 1. Switch palette state — all subsequent yoradio_palette() calls return new preset.
    // 1. Переключить палитру — все вызовы yoradio_palette() вернут новый пресет.
    yoradio_theme_set_preset(next);

    // Stage 6.6R-E: persist preset name to /data/theme.dat (LittleFS, not NVS/config_t).
    // Этап 6.6R-E: сохранить пресет в /data/theme.dat.
    (void)yoradio_theme_save_persisted_preset(next);

    // 2. Reinit LVGL default theme with new accent/dark flag.
    //    lv_theme_default_init() reuses existing allocation, resets styles, auto-propagates.
    // 2. Пересоздать базовую тему LVGL с новым акцентом/dark-флагом.
    //    lv_theme_default_init() переиспользует блок, сбрасывает стили, автопропагирует.
    yoradio_theme_reinit(s_disp);

    // 3. Live reapply palette-bound overrides on all created carousel pages (Main/Info/Station/…).
    // 3. Live reapply на всех созданных страницах карусели (Main/Info/Station/…).
    ensurePageChainRegistered();
    s_page_chain.reapplyThemeToCreatedPages();

    // 4. Screensaver: palette-safe by design (destroy+show = auto new palette); just refresh if visible.
    // 4. Screensaver: всегда palette-safe (destroy+show = новая палитра); обновить если открыт.
    if (screensaverIsVisible()) {
        screensaverHide();
        screensaverShow();
    }

#if LV_USE_PERF_MONITOR
    // 6.6R-GB2: keep debug perf overlay text readable after theme switch.
    // 6.6R-GB2: сохранить читаемость текста debug-оверлея после смены темы.
    applyPerfMonitorThemeTextColor();
#endif
}

// Stage 0: stub — confirms LVGL library is compiled into the build
// Stage 0: заглушка — подтверждает, что библиотека LVGL скомпилирована в сборку

bool lvgl_ui::isCompiled() {
    return true;
}

void lvgl_ui::initRuntime() {
    static bool s_inited = false;
    if (s_inited) return;
    lv_init();
    // Stage 6.1F-b: LVGL file API → same LittleFS mount as legacy (drive L:).
    // Этап 6.1F-b: файловый API LVGL → тот же LittleFS (диск L:).
    lv_fs_littlefs_register();
    s_inited = true;
}

void lvgl_ui::initTick() {
    if (s_lv_tick_timer) return;

    esp_timer_create_args_t args = {};
    args.callback = lvgl_tick_cb;
    args.name = "lv_tick";

    if (esp_timer_create(&args, &s_lv_tick_timer) != ESP_OK || !s_lv_tick_timer) {
        // Timer create failed: keep LVGL tick disabled to avoid undefined behavior.
        // Ошибка создания таймера: оставляем tick LVGL выключенным, чтобы избежать некорректного поведения.
        s_lv_tick_timer = nullptr;
        return;
    }

    if (esp_timer_start_periodic(s_lv_tick_timer, 1000) != ESP_OK) {
        // Start failed: clean up timer and keep LVGL tick disabled.
        // Ошибка запуска: очищаем таймер и оставляем tick LVGL выключенным.
        esp_timer_delete(s_lv_tick_timer);
        s_lv_tick_timer = nullptr;
        return;
    }
}

void lvgl_ui::initDisplayDriver(uint16_t hor_res, uint16_t ver_res) {
    if (s_disp) return;
    if (hor_res == 0 || ver_res == 0) return;

#if DSP_MODEL == DSP_ST7701
    // Block 8-E5B (4848S040): partial stripe 480×160 (was 80 in 8-E5A); 3 full-screen chunks.
    // Block 8-E5B: полоса 160 строк — тест chunk count для переходов/Station (без UI redesign).
    constexpr uint32_t kPartialLines = 160;
    const uint32_t px_count = static_cast<uint32_t>(hor_res) * kPartialLines;
    s_disp_buf_partial_h = static_cast<uint16_t>(kPartialLines);
    s_disp_buf1 = static_cast<lv_color_t*>(ps_malloc(px_count * sizeof(lv_color_t)));
    if (!s_disp_buf1) {
        s_disp_buf_partial_h = 0;
        Serial.println("[LVGL] partial draw buffer PSRAM alloc failed, LVGL display disabled");
        return;
    }
    lv_disp_draw_buf_init(&s_disp_draw_buf, s_disp_buf1, nullptr, px_count);
#else
    // Full-frame draw buffer + full_refresh: partial stripes on a shared Arduino_Canvas caused
    // visible “black rectangles” / tearing during Boot→Main (M0 Canvas path; not ST7701 post-8-E3).
    // Полный кадр + full_refresh: на старом Canvas-пути полосы давали артефакты (не ST7701 после 8-E3).
    const uint32_t lines = ver_res;
    const uint32_t px_count = static_cast<uint32_t>(hor_res) * lines;
    s_disp_buf1 = static_cast<lv_color_t*>(ps_malloc(px_count * sizeof(lv_color_t)));
    if (!s_disp_buf1) {
        Serial.println("[LVGL] draw buffer PSRAM alloc failed, LVGL display disabled");
        return;
    }
    lv_disp_draw_buf_init(&s_disp_draw_buf, s_disp_buf1, nullptr, px_count);
#endif

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = hor_res;
    s_disp_drv.ver_res = ver_res;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_disp_draw_buf;
#if DSP_MODEL == DSP_ST7701
    s_disp_drv.full_refresh = 0;
#else
    s_disp_drv.full_refresh = 1;
#endif
    s_disp_drv.direct_mode = 0;

    s_disp = lv_disp_drv_register(&s_disp_drv);
    if (!s_disp) {
        Serial.println("[LVGL] lv_disp_drv_register failed, LVGL display disabled");
        return;
    }
    // Stage 6.6A: LVGL base theme + YoRadio palette — single init point after valid display.
    // Этап 6.6A: базовая тема LVGL + палитра YoRadio — одна точка после валидного дисплея.
    yoradio_theme_init(s_disp);
    initTouchIndev();
}

void lvgl_ui::taskHandler() {
    s_page_chain.tick();
    lv_timer_handler();
    // Poll after lv_timer_handler so we read the just-processed indev state.
    // После lv_timer_handler — читаем свежеобработанное состояние indev.
    poll_top_edge_swipe();
#if LV_USE_PERF_MONITOR
    repositionBuiltinLvglPerfMonitorOnce();
#endif
}

// 8-E19B: createTestOverlay triggers initial PageChain registration (DspTask / init only).
// 8-E19B: создаём TestOverlay → регистрация PageChain на старте (только DspTask / init).
void lvgl_ui::createTestOverlay() {
    static bool s_created = false;
    if (s_created) return;
    ensurePageChainRegistered();
    s_created = true;
}

// Stage 3.1 + 6.3D-b1: forward displayQueue events for LVGL (DspTask only; see Display::loop).
// 6.3D-b1: station change = cheap marker/header refresh; list rebuild on page entry (_refreshOnPageActivate).
void lvgl_ui::onDisplayEvent(const DisplayEvent& evt) {
    ensurePageChainRegistered();
    if (evt.type == NEWSTATION) {
        if (s_page_chain.currentIndex() == PageChain::STATION_INDEX) {
            s_station_page.refreshCurrentStationVisuals();
        }
    }
}

// 8.1H-I-B: shared Main entry — hide overlays, go to Main page, refresh. Used by the normal
// PLAYER transition and by refreshMainScreenFromSettings(); keeps the two paths from diverging.
// 8.1H-I-B: общий вход на Main — скрыть оверлеи, перейти на Main, refresh. Используется обычным
// переходом в PLAYER и refreshMainScreenFromSettings(); чтобы пути не разъезжались.
static void goToMainAndRefresh() {
    overlayHideAll();
    s_page_chain.goTo(PageChain::MAIN_INDEX);
    refreshMainScreen();
}

// 8.1H-I-B: explicit forced Main redraw for WebUI settings/reset (REFRESH_MAIN request).
// No mode change, no screensaver-state mutation — just rebuild Main like a forced PLAYER refresh.
// 8.1H-I-B: явная перерисовка Main для настроек/сброса WebUI (запрос REFRESH_MAIN).
// Без смены режима и без изменения screensaver-состояния — просто пересборка Main.
void lvgl_ui::refreshMainScreenFromSettings(bool force_full_redraw) {
    ensurePageChainRegistered();
    goToMainAndRefresh();
    if (force_full_redraw) {
        // 8.1H-I-B corrective: after panel orientation flip LVGL may keep stale pixels;
        // invalidate the whole active screen and flush immediately (DspTask context only).
        // 8.1H-I-B corrective: после flip LVGL может оставить старые пиксели;
        // инвалидируем весь активный экран и форсируем flush (только в контексте DspTask).
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);
    }
}

// 8-E19B: LVGL-only mode routing — direct PageChain/overlay dispatch, no backend selection.
// 8-E19B: только LVGL маршрутизация режимов — прямой PageChain/overlay, без выбора backend.
void lvgl_ui::onModeChanged(displayMode_e mode, displayMode_e prev_mode) {
    ensurePageChainRegistered();
    if (mode == SCREENSAVER) {
        overlayHideAll();
        screensaverShow();
        return;
    }
    if (mode == SCREENBLANK) {
        screensaverHide();
        overlayHideAll();
        return;
    }
    if (mode == WIFI) {
        overlayHideAll();
        (void)wifiOpsInit();
        // Wi‑Fi 5B: boot-fail handoff may already have loaded the shell via dismissBootThenShowRebootRequired().
        // Wi‑Fi 5B: после Boot→Wi‑Fi не вызывать showRebootRequired повторно.
        if (!isWifiSetupFlowActive()) {
            s_page_chain.showRebootRequired(&s_wifi_flow_screen);
        }
        return;
    }
    if (mode == INFO) {
        overlayHideAll();
        s_page_chain.goTo(PageChain::INFO_INDEX);
    } else if (mode == PLAYER) {
        if (prev_mode == SCREENSAVER || prev_mode == SCREENBLANK) {
            overlayHideAll();
            refreshMainScreen();
            return;
        }
        goToMainAndRefresh();
    } else if (mode == LOST) {
        // Wi‑Fi 4C: do not stack LOST over Wi‑Fi shell (STA may drop during manual connect).
        // Wi‑Fi 4C: не класть LOST поверх Wi‑Fi shell (STA может рваться при ручном connect).
        if (!isWifiSetupFlowActive()) {
            overlayShowLost();
        }
    } else if (mode == UPDATING) {
        overlayShowUpdating();
    } else if (mode == VOL) {
        overlayHideAll();
        s_page_chain.goTo(PageChain::MAIN_INDEX);
        refreshMainScreen();
    } else if (mode == STATIONS) {
        // Block 8-E12: STATIONS → LvglStationPage (PageChain).
        // Block 8-E12: режим STATIONS → карусель Station.
        overlayHideAll();
        s_page_chain.goTo(PageChain::STATION_INDEX);
    } else if (mode == SETTINGS) {
        // Block 8-E13: SETTINGS → LvglStubPage Settings slot.
        // Block 8-E13: режим SETTINGS → карусель Settings.
        overlayHideAll();
        s_page_chain.goTo(PageChain::SETTINGS_INDEX);
    }
}

bool lvgl_ui::isLvglBootActive() {
    return s_lvgl_boot_active;
}

bool lvgl_ui::tryPresentLvglBootOnFirstDspLoop() {
    if (!lv_disp_get_default()) return false;
    ensurePageChainRegistered();
    s_page_chain.showBoot(&s_boot_screen);
    s_lvgl_boot_active = true;
    s_lvgl_boot_shown_ms = millis();
    return true;
}

void lvgl_ui::dismissBootForMainHandoff() {
    if (!s_lvgl_boot_active) return;
    overlayHideAll();
    s_page_chain.dismissBoot();
    s_lvgl_boot_active = false;
}

void lvgl_ui::dismissBootForWifiRecoveryHandoff() {
    if (!s_lvgl_boot_active) return;
    ensurePageChainRegistered();
    overlayHideAll();
    s_page_chain.dismissBootThenShowRebootRequired(&s_wifi_flow_screen);
    s_lvgl_boot_active = false;
}

bool lvgl_ui::dismissBootForMainHandoffWhenDue() {
    if (!s_lvgl_boot_active) return true;
    if ((uint32_t)(millis() - s_lvgl_boot_shown_ms) < kLvglBootMinVisibleMs) return false;
    overlayHideAll();
    s_page_chain.dismissBoot();
    s_lvgl_boot_active = false;
    return true;
}

bool lvgl_ui::isLvglBootMinDwellElapsed() {
    if (!s_lvgl_boot_active) return true;
    return (uint32_t)(millis() - s_lvgl_boot_shown_ms) >= kLvglBootMinVisibleMs;
}

namespace {
static bool s_wifi_recovery_enter_from_boot_failure       = false;
// S6V8A: runtime disconnect escalation context flag (one-shot, consumed in enter()).
// S6V8A: флаг runtime-контекста эскалации (одноразовый, consumable в enter()).
static bool s_wifi_recovery_enter_from_runtime_disconnect = false;
} // namespace

void lvgl_ui::notifyWifiRecoveryEnteredFromBootFailure() {
    s_wifi_recovery_enter_from_boot_failure = true;
}

bool lvgl_ui::consumeWifiRecoveryEnteredFromBootFailure() {
    if (!s_wifi_recovery_enter_from_boot_failure) return false;
    s_wifi_recovery_enter_from_boot_failure = false;
    return true;
}

// S6V8A: notify that the next Wi-Fi shell enter() is from a runtime disconnect escalation.
// S6V8A: сообщить, что следующий enter() Wi-Fi shell — из runtime disconnect escalation.
void lvgl_ui::notifyWifiRecoveryEnteredFromRuntimeDisconnect() {
    s_wifi_recovery_enter_from_runtime_disconnect = true;
}

bool lvgl_ui::consumeWifiRecoveryEnteredFromRuntimeDisconnect() {
    if (!s_wifi_recovery_enter_from_runtime_disconnect) return false;
    s_wifi_recovery_enter_from_runtime_disconnect = false;
    return true;
}

void lvgl_ui::showWifiRecoveryFlowFromDisplayStart() {
    // Block 8-E10: same RebootRequired shell as 5B; no Hotspot policy change / без auto-Hotspot.
    overlayHideAll();
    ensurePageChainRegistered();
    if (!isWifiSetupFlowActive()) {
        s_page_chain.showRebootRequired(&s_wifi_flow_screen);
    }
}

void lvgl_ui::bootScreenSetStatusUtf8(const char* text) {
    if (s_lvgl_boot_active) s_boot_screen.setStatusUtf8(text);
}

void lvgl_ui::bootScreenNotifyBootSignal() {
    if (s_lvgl_boot_active) s_boot_screen.onBootSignal();
}

void lvgl_ui::installCarouselGesturesOnPageRoot(lv_obj_t* screen_root) {
    if (!screen_root) return;
    lv_obj_add_flag(screen_root, LV_OBJ_FLAG_CLICKABLE);
    // 6.4C: only GESTURE for horizontal carousel; top-edge vertical swipe moved to poll_top_edge_swipe().
    // 6.4C: только GESTURE для карусели; вертикальный свайп перенесён в poll_top_edge_swipe().
    lv_obj_add_event_cb(screen_root, carousel_gesture_event_cb, LV_EVENT_GESTURE, nullptr);
}

void lvgl_ui::dismissActiveTemporary() {
    ensurePageChainRegistered();
    s_page_chain.dismissTemporary();
}

void lvgl_ui::refreshActiveTemporaryTimeout() {
    ensurePageChainRegistered();
    s_page_chain.refreshTemporaryTimeout();
}

uint32_t lvgl_ui::temporaryRemainingMs() {
    ensurePageChainRegistered();
    return s_page_chain.temporaryRemainingMs();
}

void lvgl_ui::notifyPageChainActivity() {
    s_page_chain.onActivity();
}

void lvgl_ui::goToCarouselPage(int page_index) {
    ensurePageChainRegistered();
    s_page_chain.goTo(page_index);
}

bool lvgl_ui::isLvglCarouselOnInfoSlot() {
    return s_page_chain.currentIndex() == PageChain::INFO_INDEX;
}

void lvgl_ui::openStationPageFromProductInput() {
    display.putRequest(NEWMODE, STATIONS);
}

void lvgl_ui::openSettingsPageFromProductInput() {
    display.putRequest(NEWMODE, SETTINGS);
}

void lvgl_ui::toggleStationListUiFromProductInput() {
    ensurePageChainRegistered();
    if (s_page_chain.currentIndex() == PageChain::STATION_INDEX || display.mode() == STATIONS) {
        display.putRequest(NEWMODE, PLAYER);
    } else {
        openStationPageFromProductInput();
    }
}

bool lvgl_ui::isLvglCarouselOnStationSlot() {
    return s_page_chain.currentIndex() == PageChain::STATION_INDEX;
}

// Weather W2: carousel currently on the Weather slot (index 4) — drives ~1 Hz refresh in Display::loop.
// Weather W2: карусель на слоте Weather (индекс 4) — основание для refresh ~1 Гц в Display::loop.
bool lvgl_ui::isLvglCarouselOnWeatherSlot() {
    return s_page_chain.currentIndex() == PageChain::WEATHER_INDEX;
}

// E6C1: carousel currently on the Visual slot (index 2).
// E6C1: карусель на слоте Visual (индекс 2).
bool lvgl_ui::isLvglCarouselOnVisualSlot() {
    return s_page_chain.currentIndex() == PageChain::VISUAL_INDEX;
}

bool lvgl_ui::isWifiSetupFlowActive() {
    ensurePageChainRegistered();
    return s_page_chain.isRebootRequiredActiveFor(&s_wifi_flow_screen);
}

void lvgl_ui::dismissWifiFlowReturnToPlayer() {
    wifiOpsCancel();
    network.runtimeReconnectSuspendedForSetup = false;
    ensurePageChainRegistered();
    s_page_chain.dismissRebootRequired();
    display.putRequest(NEWMODE, PLAYER);
}

void lvgl_ui::setPageTransitionAnimationEnabled(bool enabled) {
    PageChain::setCarouselTransitionAnimationEnabled(enabled);
}

bool lvgl_ui::isPageTransitionAnimationEnabled() {
    return PageChain::isCarouselTransitionAnimationEnabled();
}

size_t lvgl_ui::appendDisplayDiag(char* out, size_t len, size_t offset, bool* truncated_out) {
    if (!out || len == 0 || offset >= len) return offset;

    auto append_line = [&](const char* fmt, ...) -> bool {
        if (offset >= len) {
            if (truncated_out) *truncated_out = true;
            return false;
        }
        va_list ap;
        va_start(ap, fmt);
        const int n = vsnprintf(out + offset, len - offset, fmt, ap);
        va_end(ap);
        if (n < 0) return false;
        if ((size_t)n >= len - offset) {
            if (truncated_out) *truncated_out = true;
            offset = len - 1;
            out[offset] = '\0';
            return false;
        }
        offset += (size_t)n;
        return true;
    };

    if (!s_disp) {
        append_line("lvgl.display: not_registered\n");
        return offset;
    }

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    append_line("lvgl.mem.total_size: %u\n", (unsigned)mon.total_size);
    append_line("lvgl.mem.free_size: %u\n", (unsigned)mon.free_size);
    append_line("lvgl.mem.free_biggest_size: %u\n", (unsigned)mon.free_biggest_size);
    append_line("lvgl.mem.used_pct: %u\n", (unsigned)mon.used_pct);
    append_line("lvgl.mem.frag_pct: %u\n", (unsigned)mon.frag_pct);

    append_line("lvgl.flush.full_refresh: %d\n", s_disp_drv.full_refresh ? 1 : 0);
    append_line("lvgl.flush.direct_mode: %d\n", s_disp_drv.direct_mode ? 1 : 0);
#if DSP_MODEL == DSP_ST7701
    append_line("lvgl.flush_target: output_display_direct\n");
#else
    append_line("lvgl.flush_target: canvas_mark_dirty\n");
#endif
    if (s_disp_buf1 && s_disp_drv.hor_res > 0) {
#if DSP_MODEL == DSP_ST7701
        if (s_disp_buf_partial_h > 0) {
            append_line("lvgl.draw_buf: partial_psram_single (%ux%u)\n",
                        (unsigned)s_disp_drv.hor_res, (unsigned)s_disp_buf_partial_h);
        } else {
            append_line("lvgl.draw_buf: partial_psram_single (uninitialized)\n");
        }
#else
        append_line("lvgl.draw_buf: full_frame_psram_single (%ux%u)\n",
                    (unsigned)s_disp_drv.hor_res, (unsigned)s_disp_drv.ver_res);
#endif
    } else {
        append_line("lvgl.draw_buf: unknown\n");
    }

    append_line("lvgl.page_transition_anim: %d\n",
                PageChain::isCarouselTransitionAnimationEnabled() ? 1 : 0);

    const uint32_t now = millis();
    append_line("lvgl.flush_count: %lu\n", (unsigned long)s_lvgl_flush_count);
    if (s_lvgl_last_flush_ms != 0) {
        append_line("lvgl.last_flush_ms_ago: %lu\n",
                    (unsigned long)(now - s_lvgl_last_flush_ms));
    } else {
        append_line("lvgl.last_flush_ms_ago: never\n");
    }
    return offset;
}
