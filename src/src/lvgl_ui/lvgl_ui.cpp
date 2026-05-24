#include "lvgl_ui.h"
#include "lv_overlay.h"
#include "lv_screensaver.h"
#include "lv_touch_indev.h"
#include "lv_ui_events.h"
#include "profiles/lv_profile_select.h"
#include "theme/lv_theme_yoradio.h"
#include "lv_fs_littlefs.h"

#include <cstdarg>

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
#include "screens/scr_boot.h"
#include "screens/scr_wifi_flow.h"
#include "../displays/tools/GFX_Canvas_screen.h"
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

// External canvas instance from display subsystem / Внешний экземпляр canvas из подсистемы дисплея
extern Arduino_Canvas* gfx;

// Stage 4.3: empty default screen — restored when leaving LVGL-owned modes.
// Stage 4.3: пустой экран по умолчанию — восстанавливается при выходе из режимов LVGL.
static lv_obj_t* s_default_screen = nullptr;

static PageChain s_page_chain;
static LvglInfoPage s_info_page;
static LvglMainScreen s_main_screen;
static LvglStubPage s_stub_visual("Visual");
static LvglStationPage s_station_page;
static LvglStubPage s_stub_weather("Weather");
static LvglStubPage s_stub_settings("Settings");
static LvglBootScreen s_boot_screen;
static LvglWifiFlowScreen s_wifi_flow_screen;

namespace {

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

static void carousel_gesture_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    // SCREENSAVER / SCREENBLANK: horizontal wake + carousel suppressed — lv_touch_read_cb handles wake first.
    // Saver/blank: горизонтальный жест не ведёт в карусель; пробуждение — в lv_touch_read_cb.
    if (display.mode() == SCREENBLANK || display.mode() == SCREENSAVER) {
        return;
    }
    // Wi-Fi 3A: service shell is not a carousel page; ignore horizontal swipe on page roots during WIFI mode.
    // Wi‑Fi 3A: сервисный shell не в карусели; горизонтальный жест на корнях страниц в режиме WIFI игнорируем.
    if (display.mode() == WIFI) {
        return;
    }
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    s_page_chain.onActivity();
    map_horizontal_gesture_to_carousel(dir);
    // Consume indev gesture so child widgets do not emit click/SHORT_CLICKED for same stroke (Station focus).
    // Поглощаем последовательность — дочерние виджеты не получают click за тот же жест карусели.
    lv_indev_wait_release(indev);
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
    s_page_chain.registerPage(2, &s_stub_visual);
    s_page_chain.registerPage(PageChain::STATION_INDEX, &s_station_page);
    s_page_chain.registerPage(4, &s_stub_weather);
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
// Block 8-E3 (4848S040): LVGL → output_display directly; Canvas stays allocated for Phase 2 legacy.
// Block 8-E3: LVGL → output_display напрямую; Canvas не трогаем на LVGL-пути (удаление — Phase 2).
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

// Flush callback: M0 Canvas path (non-ST7701) or legacy markFrameDirty + Display::loop flush.
// Flush callback: M0 через Canvas; на ST7701 — см. lvgl_flush_direct_panel (8-E3).
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
#if DSP_MODEL == DSP_ST7701
    lvgl_flush_direct_panel(drv, area, color_p);
    return;
#endif

    if (!gfx || !color_p || !area) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    if (x2 < x1 || y2 < y1) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;

    gfxDrawBitmap(gfx, x1, y1, reinterpret_cast<const uint16_t*>(color_p), w, h);

    s_lvgl_flush_count++;
    s_lvgl_last_flush_ms = millis();

    lv_disp_flush_ready(drv);
}

// Periodic page refresh policy: no Main/Info/stub updates under saver or blank (any carousel slot).
// Политика периодического refresh: не трогать Main/Info/стабы в saver или blank (любой слот карусели).
static bool lvgl_page_refresh_allowed() {
    const displayMode_e m = display.mode();
    return m != SCREENBLANK && m != SCREENSAVER && m != WIFI;
}

#if LV_USE_PERF_MONITOR
// Stock LVGL perf label uses lv_obj_align(..., LV_USE_PERF_MONITOR_POS, 0, 0) — no x/y ofs in lv_conf.
// Move overlay right of Wi‑Fi (left status column) so it does not cover weather (right column). / Встроенный
// perf label без смещения в lv_conf — сдвигаем вправо от Wi‑Fi, чтобы не перекрывать погоду справа.
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
        lv_obj_align(ch, LV_ALIGN_TOP_LEFT, x0, 2);
        s_done = true;
        break;
    }
}
#endif

#endif

void lvgl_ui::refreshInfoScreen() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!lvgl_page_refresh_allowed()) return;
    s_info_page.update();
#endif
}

void lvgl_ui::refreshMainScreen() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!lvgl_page_refresh_allowed()) return;
    s_main_screen.update();
#endif
}

void lvgl_ui::onMainBackgroundSlotCommitted(uint8_t slot) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (slot > 2u) {
        return;
    }
    const uint8_t active = static_cast<uint8_t>(yoradio_theme_active_preset());
    if (slot != active) {
        return;
    }
    s_main_screen.reloadFileBackgroundFromLittlefs();
#else
    (void)slot;
#endif
}

void lvgl_ui::onStationArtCommitted() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    // Force art reload on Main after WebUI upload_art / remove_art (DspTask queue handler only).
    // Принудительная перезагрузка арта после upload_art / remove_art из обработчика очереди DspTask.
    s_main_screen.reloadStationArtFromLittlefs();
#endif
}

void lvgl_ui::onCustomThemeFileUpdated() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
#else
    (void)0;
#endif
}

void lvgl_ui::onThemePresetChanged(uint8_t preset_id) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
#else
    (void)preset_id;
#endif
}

// Stage 0: stub — confirms LVGL library is compiled into the build
// Stage 0: заглушка — подтверждает, что библиотека LVGL скомпилирована в сборку

bool lvgl_ui::isCompiled() {
    return true;
}

void lvgl_ui::initRuntime() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    static bool s_inited = false;
    if (s_inited) return;
    lv_init();
    // Stage 6.1F-b: LVGL file API → same LittleFS mount as legacy (drive L:).
    // Этап 6.1F-b: файловый API LVGL → тот же LittleFS (диск L:).
    lv_fs_littlefs_register();
    s_inited = true;
#endif
}

void lvgl_ui::initTick() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
#endif
}

void lvgl_ui::initDisplayDriver(uint16_t hor_res, uint16_t ver_res) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
#endif
}

void lvgl_ui::taskHandler() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    s_page_chain.tick();
    lv_timer_handler();
#if LV_USE_PERF_MONITOR
    repositionBuiltinLvglPerfMonitorOnce();
#endif
#endif
}

// Сохраняем дефолтный (пустой) экран LVGL для переключения при выходе из INFO.
// Пустой — без виджетов, чтобы не ломать boot/legacy и не рисовать лишнее поверх плейера.
// Позже сюда можно перенести LVGL boot/overlay при миграции.
void lvgl_ui::createTestOverlay() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    static bool s_created = false;
    if (s_created) return;

    lv_obj_t* scr = lv_scr_act();
    if (!scr) return;

    if (!s_default_screen) s_default_screen = scr;

    ensurePageChainRegistered();

    s_created = true;
#endif
}

// Stage 3.1 + 6.3D-b1: forward displayQueue events for LVGL (DspTask only; see Display::loop).
// 6.3D-b1: station change = cheap marker/header refresh; playlist change = optional full rebuild.
void lvgl_ui::onDisplayEvent(const DisplayEvent& evt) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    ensurePageChainRegistered();
    if (evt.type == NEWSTATION) {
        if (s_page_chain.currentIndex() == PageChain::STATION_INDEX) {
            s_station_page.refreshCurrentStationVisuals();
        }
        return;
    }
    if (evt.type == DRAWPLAYLIST) {
        if (s_page_chain.currentIndex() == PageChain::STATION_INDEX) {
            s_station_page.onPlaylistDataMaybeChanged();
        }
        return;
    }
#else
    (void)evt;
#endif
}

// Stage 5.5 + 5.7 + 5.6: LVGL owns INFO, PLAYER, LOST, UPDATING, VOL, SCREENSAVER overlay, SCREENBLANK coord.
// Stage 5.5 + 5.7 + 5.6: LVGL — INFO, PLAYER, LOST, UPDATING, VOL, оверлей SCREENSAVER, коорд. SCREENBLANK.
lvgl_ui::UiBackend lvgl_ui::getPreferredBackend(displayMode_e mode) {
    if (mode == INFO || mode == PLAYER || mode == LOST || mode == UPDATING || mode == VOL || mode == WIFI ||
        mode == SCREENSAVER || mode == SCREENBLANK
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
        || mode == STATIONS || mode == SETTINGS
#endif
        ) {
        return UiBackend::Lvgl;
    }
    return UiBackend::LegacyCanvas;
}

// Stage 5.5 / 5.7 / 5.6: PageChain + overlays; SCREENSAVER/BLANK without killing PageChain on wake.
// Stage 5.5 / 5.7 / 5.6: PageChain + оверлеи; SCREENSAVER/BLANK без сброса карусели при пробуждении.
void lvgl_ui::onModeChanged(displayMode_e mode, UiBackend backend, displayMode_e prev_mode) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (backend == UiBackend::Lvgl) {
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
            overlayHideAll();
            s_page_chain.goTo(PageChain::MAIN_INDEX);
            refreshMainScreen();
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
            // Block 8-E12: legacy STATIONS → existing LvglStationPage (no Canvas PG_PLAYLIST).
            // Block 8-E12: режим STATIONS → карусель Station, без legacy playlist.
            overlayHideAll();
            s_page_chain.goTo(PageChain::STATION_INDEX);
        } else if (mode == SETTINGS) {
            // Block 8-E13: legacy SETTINGS → existing LvglStubPage Settings slot (no const_DlgNextion).
            // Block 8-E13: режим SETTINGS → карусель Settings, без legacy PG_DIALOG.
            overlayHideAll();
            s_page_chain.goTo(PageChain::SETTINGS_INDEX);
        }
    } else {
        overlayHideAll();
        if (s_default_screen) lv_scr_load(s_default_screen);
    }
#else
    (void)mode;
    (void)backend;
    (void)prev_mode;
#endif
}

bool lvgl_ui::isLvglBootActive() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    return s_lvgl_boot_active;
#else
    return false;
#endif
}

bool lvgl_ui::tryPresentLvglBootOnFirstDspLoop() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!lv_disp_get_default()) return false;
    ensurePageChainRegistered();
    s_page_chain.showBoot(&s_boot_screen);
    s_lvgl_boot_active = true;
    s_lvgl_boot_shown_ms = millis();
    return true;
#else
    return false;
#endif
}

void lvgl_ui::dismissBootForMainHandoff() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!s_lvgl_boot_active) return;
    overlayHideAll();
    s_page_chain.dismissBoot();
    s_lvgl_boot_active = false;
#endif
}

void lvgl_ui::dismissBootForWifiRecoveryHandoff() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!s_lvgl_boot_active) return;
    ensurePageChainRegistered();
    overlayHideAll();
    s_page_chain.dismissBootThenShowRebootRequired(&s_wifi_flow_screen);
    s_lvgl_boot_active = false;
#endif
}

bool lvgl_ui::dismissBootForMainHandoffWhenDue() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!s_lvgl_boot_active) return true;
    if ((uint32_t)(millis() - s_lvgl_boot_shown_ms) < kLvglBootMinVisibleMs) return false;
    overlayHideAll();
    s_page_chain.dismissBoot();
    s_lvgl_boot_active = false;
    return true;
#else
    return true;
#endif
}

bool lvgl_ui::isLvglBootMinDwellElapsed() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!s_lvgl_boot_active) return true;
    return (uint32_t)(millis() - s_lvgl_boot_shown_ms) >= kLvglBootMinVisibleMs;
#else
    return true;
#endif
}

namespace {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
static bool s_wifi_recovery_enter_from_boot_failure     = false;
// S6V8A: runtime disconnect escalation context flag (one-shot, consumed in enter()).
// S6V8A: флаг runtime-контекста эскалации (одноразовый, consumable в enter()).
static bool s_wifi_recovery_enter_from_runtime_disconnect = false;
#endif
} // namespace

void lvgl_ui::notifyWifiRecoveryEnteredFromBootFailure() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    s_wifi_recovery_enter_from_boot_failure = true;
#else
#endif
}

bool lvgl_ui::consumeWifiRecoveryEnteredFromBootFailure() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!s_wifi_recovery_enter_from_boot_failure) return false;
    s_wifi_recovery_enter_from_boot_failure = false;
    return true;
#else
    return false;
#endif
}

// S6V8A: notify that the next Wi-Fi shell enter() is from a runtime disconnect escalation.
// S6V8A: сообщить, что следующий enter() Wi-Fi shell — из runtime disconnect escalation.
void lvgl_ui::notifyWifiRecoveryEnteredFromRuntimeDisconnect() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    s_wifi_recovery_enter_from_runtime_disconnect = true;
#endif
}

bool lvgl_ui::consumeWifiRecoveryEnteredFromRuntimeDisconnect() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!s_wifi_recovery_enter_from_runtime_disconnect) return false;
    s_wifi_recovery_enter_from_runtime_disconnect = false;
    return true;
#else
    return false;
#endif
}


void lvgl_ui::dismissBootForApLegacyHandoff() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    // Block 8-E10: unused on LVGL Wi‑Fi fail path; kept for any external/legacy callers until 8-E16.
    overlayHideAll();
    if (s_lvgl_boot_active) {
        s_page_chain.dismissBoot();
        s_lvgl_boot_active = false;
    }
    if (s_default_screen) lv_scr_load(s_default_screen);
#endif
}

void lvgl_ui::showWifiRecoveryFlowFromDisplayStart() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    // Block 8-E10: same RebootRequired shell as 5B; no Hotspot policy change / без auto-Hotspot.
    overlayHideAll();
    ensurePageChainRegistered();
    if (!isWifiSetupFlowActive()) {
        s_page_chain.showRebootRequired(&s_wifi_flow_screen);
    }
#endif
}

void lvgl_ui::bootScreenSetStatusUtf8(const char* text) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (s_lvgl_boot_active) s_boot_screen.setStatusUtf8(text);
#else
    (void)text;
#endif
}

void lvgl_ui::bootScreenNotifyBootSignal() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (s_lvgl_boot_active) s_boot_screen.onBootSignal();
#endif
}

void lvgl_ui::installCarouselGesturesOnPageRoot(lv_obj_t* screen_root) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!screen_root) return;
    lv_obj_add_flag(screen_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_root, carousel_gesture_event_cb, LV_EVENT_GESTURE, nullptr);
#else
    (void)screen_root;
#endif
}

void lvgl_ui::notifyPageChainActivity() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    s_page_chain.onActivity();
#endif
}

void lvgl_ui::goToCarouselPage(int page_index) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    ensurePageChainRegistered();
    s_page_chain.goTo(page_index);
#else
    (void)page_index;
#endif
}

bool lvgl_ui::isLvglCarouselOnInfoSlot() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    return s_page_chain.currentIndex() == PageChain::INFO_INDEX;
#else
    return false;
#endif
}

void lvgl_ui::openStationPageFromProductInput() {
    display.putRequest(NEWMODE, STATIONS);
}

void lvgl_ui::openSettingsPageFromProductInput() {
    display.putRequest(NEWMODE, SETTINGS);
}

void lvgl_ui::toggleStationListUiFromProductInput() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    ensurePageChainRegistered();
    if (s_page_chain.currentIndex() == PageChain::STATION_INDEX || display.mode() == STATIONS) {
        display.putRequest(NEWMODE, PLAYER);
    } else {
        openStationPageFromProductInput();
    }
#else
    display.putRequest(NEWMODE, display.mode() == PLAYER ? STATIONS : PLAYER);
#endif
}

bool lvgl_ui::isLvglCarouselOnStationSlot() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    return s_page_chain.currentIndex() == PageChain::STATION_INDEX;
#else
    return false;
#endif
}

bool lvgl_ui::isWifiSetupFlowActive() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    ensurePageChainRegistered();
    return s_page_chain.isRebootRequiredActiveFor(&s_wifi_flow_screen);
#else
    return false;
#endif
}

void lvgl_ui::dismissWifiFlowReturnToPlayer() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    wifiOpsCancel();
    network.runtimeReconnectSuspendedForSetup = false;
    ensurePageChainRegistered();
    s_page_chain.dismissRebootRequired();
    display.putRequest(NEWMODE, PLAYER);
#endif
}

void lvgl_ui::setPageTransitionAnimationEnabled(bool enabled) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    PageChain::setCarouselTransitionAnimationEnabled(enabled);
#else
    (void)enabled;
#endif
}

bool lvgl_ui::isPageTransitionAnimationEnabled() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    return PageChain::isCarouselTransitionAnimationEnabled();
#else
    return false;
#endif
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

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
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
#else
    append_line("lvgl: disabled\n");
#endif
    return offset;
}
