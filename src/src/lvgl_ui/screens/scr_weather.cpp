/*
 * LvglWeatherPage — Weather W2 LVGL page (read-only consumer of core WeatherState).
 * LvglWeatherPage — страница погоды W2 (только чтение core WeatherState).
 *
 * - Implements ILvglScreen: create → enter → update → exit → destroy (+ liveReapplyTheme).
 * - DspTask-only lv_* via Display::loop → lvgl_ui::taskHandler / refreshWeatherScreen().
 * - Data: weatherGetStateSnapshot() ONLY (W1 double-buffer seqlock). No network from UI.
 * - Layout: LV_ACTIVE_PROFILE + yoradio_palette() only; reuse OWM glyph map + weather icon fonts.
 *
 * Accepted W2 narrowing (no WeatherState change):
 *   - WeatherState.current is filled from the forecast (W1), so "current valid" == "forecast valid".
 *     → states collapse to: full / waiting (enabled, no forecast) / unavailable (disabled or no key).
 *   - Hourly/daily time labels are RELATIVE (Now/+3h…, Today/Tomorrow/+2d) because the snapshot does
 *     not carry the city timezone. Wall-clock labels are a follow-up (needs tz in WeatherState).
 * W2C: bottom status footer — Station _hint_area pill (panel_background 30%, divider border,
 * radius 14) pinned via _body_area flex-grow + _cont_footer sibling. Footer text carries state.
 * W2C: нижний футер — pill как _hint_area на Station; прижат через flex-grow body + footer sibling.
 *
 * W2A flat UI: strip inherited LVGL theme gradients on create (wx_flat_base right after
 * lv_obj_create, before flex/size/pad). liveReapplyTheme only retints colors + LV_GRAD_DIR_NONE.
 * W2A: плоский UI — сброс темы только в create(); liveReapplyTheme не трогает layout.
 */

#include "scr_weather.h"

#include "lvgl.h"
#include "Arduino.h"
#include <cstdio>
#include <cstring>

#include "../fonts/lv_fonts.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../weather_owm_glyph.h"
#include "lvgl_ui.h"
#include "../../core/config.h"   // pulls options.h → myoptions.h (YORADIO_WEATHER_UI_DIAG)
#include "../../core/weather_state.h"

// W2D: gated LVGL/heap diagnostics for the gradient-OOM investigation. Default OFF — set
// YORADIO_WEATHER_UI_DIAG=1 in myoptions.h (local, not committed) to capture transition logs.
// W2D: gated диагностика LVGL/кучи под расследование OOM градиента. По умолчанию выкл.
#ifndef YORADIO_WEATHER_UI_DIAG
#define YORADIO_WEATHER_UI_DIAG 0
#endif

#if YORADIO_WEATHER_UI_DIAG
#include <esp_heap_caps.h>
#endif

namespace lvgl_ui {

namespace {

#if YORADIO_WEATHER_UI_DIAG
// Recursive LVGL object counter under a root (root included). Diagnostics only.
// Рекурсивный счётчик объектов LVGL под корнем (с корнем). Только диагностика.
static uint32_t wx_diag_count_objs(lv_obj_t* root) {
    if (!root) return 0;
    uint32_t n = 1;
    const uint32_t c = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < c; ++i) {
        n += wx_diag_count_objs(lv_obj_get_child(root, i));
    }
    return n;
}

// One-line snapshot: LVGL pool monitor + internal/PSRAM heap + object count under root.
// LVGL pool (LV_MEM_SIZE 48K, LV_GRAD_CACHE_DEF_SIZE 0) is where rect gradients allocate.
// Срез в одну строку: монитор пула LVGL + internal/PSRAM + число объектов под корнем.
static void wx_diag_dump(const char* tag, lv_obj_t* root) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const uint32_t objs = wx_diag_count_objs(root);
    Serial.printf("[WX_UI_DIAG] %s objs=%lu | lvpool free=%lu biggest=%lu used=%u%% frag=%u%% "
                  "| int_free=%u int_big=%u psram_free=%u psram_big=%u\n",
                  tag, (unsigned long)objs,
                  (unsigned long)mon.free_size, (unsigned long)mon.free_biggest_size,
                  (unsigned)mon.used_pct, (unsigned)mon.frag_pct,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}
#endif // YORADIO_WEATHER_UI_DIAG

// Font ladder for this page (reuse existing generated fonts only; no new assets in W2).
// Лестница шрифтов страницы — только существующие сгенерированные шрифты.
static const void* k_font_hero_icon  = reinterpret_cast<const void*>(&lv_font_yora_weather_icons_28);
static const void* k_font_strip_icon = reinterpret_cast<const void*>(&lv_font_yora_weather_icons_20);
static const void* k_font_hero_temp  = reinterpret_cast<const void*>(&lv_font_yora_montserrat_40_cyr);
static const void* k_font_cond       = reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
static const void* k_font_small      = reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr);
static const void* k_font_caption    = reinterpret_cast<const void*>(&lv_font_yora_montserrat_12_cyr);

static void wx_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void wx_set_text_if_changed(lv_obj_t* lbl, const char* s) {
    if (!lbl || !s) return;
    const char* cur = lv_label_get_text(lbl);
    if (cur != nullptr && strcmp(cur, s) == 0) return;
    lv_label_set_text(lbl, s);
}

static void wx_show(lv_obj_t* obj, bool show) {
    if (!obj) return;
    if (show) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// W2A: strip theme immediately after lv_obj_create(), BEFORE any flex/size/pad setup.
// W2A: сброс темы сразу после create(), ДО flex/size/pad — иначе layout затирается.
static void wx_flat_base(lv_obj_t* o, bool transparent = true) {
    if (!o) return;
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(o, 0, LV_PART_MAIN);
    if (transparent) {
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

// Solid screen root on create only (remove_style_all once, then caller sets flex/pad).
// Сплошной корень экрана — только в create(); flex/pad задаёт вызывающий код.
static void wx_flat_screen_root_on_create(lv_obj_t* o, const YoRadioPalette& pal) {
    wx_flat_base(o, false);
    lv_obj_set_style_bg_color(o, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
}

// 1 px flat divider (solid pal.divider, no gradient). / Плоский разделитель 1 px.
static lv_obj_t* add_thin_divider(lv_obj_t* parent, const YoRadioPalette& pal) {
    lv_obj_t* d = lv_obj_create(parent);
    if (!d) return nullptr;
    wx_flat_base(d, false);
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_height(d, 1);
    lv_obj_set_style_min_height(d, 1, LV_PART_MAIN);
    lv_obj_set_style_max_height(d, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_grow(d, 0);
    return d;
}

// Metric mini-cell: [value][caption]. Returns value label via out_val. / Мини-ячейка метрики.
static void add_metric_cell(lv_obj_t* row, const char* caption, lv_obj_t** out_val,
                            const YoRadioPalette& pal) {
    if (!row) return;
    lv_obj_t* cell = lv_obj_create(row);
    if (!cell) return;
    wx_flat_base(cell);
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 1, LV_PART_MAIN);

    lv_obj_t* v = lv_label_create(cell);
    if (v) {
        lv_label_set_text(v, "--");
        wx_set_font(v, k_font_cond);
        lv_obj_set_style_text_color(v, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    lv_obj_t* c = lv_label_create(cell);
    if (c) {
        lv_label_set_text(c, caption);
        wx_set_font(c, k_font_caption);
        lv_obj_set_style_text_color(c, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    if (out_val) *out_val = v;
}

// Generic vertical strip cell with 4 stacked labels. / Универсальная ячейка ленты из 4 строк.
static lv_obj_t* add_strip_cell(lv_obj_t* row, const YoRadioPalette& pal,
                                lv_obj_t** l0, lv_obj_t** l1, lv_obj_t** l2, lv_obj_t** l3) {
    lv_obj_t* cell = lv_obj_create(row);
    if (!cell) return nullptr;
    wx_flat_base(cell);
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 2, LV_PART_MAIN);

    auto make = [&](const void* font, lv_color_t col) -> lv_obj_t* {
        lv_obj_t* l = lv_label_create(cell);
        if (l) {
            lv_label_set_text(l, "--");
            wx_set_font(l, font);
            lv_obj_set_style_text_color(l, col, LV_PART_MAIN);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        }
        return l;
    };
    lv_obj_t* a = make(k_font_caption, pal.text_secondary);    // time / day
    lv_obj_t* b = make(k_font_strip_icon, pal.status_weather_icon); // icon
    lv_obj_t* c = make(k_font_small, pal.text_primary);        // temp / range
    lv_obj_t* d = make(k_font_caption, pal.text_secondary);    // pop
    if (l0) *l0 = a;
    if (l1) *l1 = b;
    if (l2) *l2 = c;
    if (l3) *l3 = d;
    return cell;
}

// Station _hint_area pill tokens — reused for Weather bottom status footer (W2C).
// Токены pill _hint_area Station — для нижнего status-футера Weather (W2C).
static constexpr lv_coord_t k_footer_pill_radius   = 14;
static constexpr lv_coord_t k_footer_pill_pad_h    = 16;
static constexpr lv_coord_t k_footer_pill_pad_v    = 10;

// Station-like quiet band: flat panel fill + divider rim, no gradient/shadow.
// Тихая полоса как на Station: panel_background + рамка divider, без градиента/тени.
static void wx_style_footer_pill(lv_obj_t* o, const YoRadioPalette& pal) {
    wx_flat_base(o, false);
    lv_obj_set_style_bg_color(o, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(o, k_footer_pill_radius, LV_PART_MAIN);
    lv_obj_set_style_pad_left(o, k_footer_pill_pad_h, LV_PART_MAIN);
    lv_obj_set_style_pad_right(o, k_footer_pill_pad_h, LV_PART_MAIN);
    lv_obj_set_style_pad_top(o, k_footer_pill_pad_v, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(o, k_footer_pill_pad_v, LV_PART_MAIN);
}

// Relative age footer string (no tz/wall-clock dependency). / Относительный возраст для футера.
static void wx_format_age(char* buf, size_t cap, uint32_t updated_at_ms, bool stale) {
    if (!buf || cap == 0) return;
    if (stale) {
        snprintf(buf, cap, "Update deferred");
        return;
    }
    const uint32_t age_min = (millis() - updated_at_ms) / 60000u;
    if (age_min == 0u)        snprintf(buf, cap, "Updated just now");
    else if (age_min < 60u)   snprintf(buf, cap, "Updated %um ago", (unsigned)age_min);
    else                      snprintf(buf, cap, "Updated %uh ago", (unsigned)(age_min / 60u));
}

} // namespace

ScreenType LvglWeatherPage::screenType() const {
    return ScreenType::Page;
}

void LvglWeatherPage::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();

    wx_flat_screen_root_on_create(_screen, pal);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(_screen, LV_ACTIVE_PROFILE.frame_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, 6, LV_PART_MAIN);

    if (!wgt_status_line::create(_screen, _status_line)) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    add_thin_divider(_screen, pal);

    _content = lv_obj_create(_screen);
    if (!_content) return;
    wx_flat_base(_content);
    lv_obj_set_width(_content, LV_PCT(100));
    lv_obj_set_flex_grow(_content, 1);
    lv_obj_set_flex_flow(_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_content, 0, LV_PART_MAIN);

    // ── Body (flex-grow) — forecast content or centered empty state ───────────
    _body_area = lv_obj_create(_content);
    if (!_body_area) return;
    wx_flat_base(_body_area);
    lv_obj_set_width(_body_area, LV_PCT(100));
    lv_obj_set_flex_grow(_body_area, 1);
    lv_obj_set_flex_flow(_body_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_body_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_body_area, 8, LV_PART_MAIN);

    // ── Data block (hidden until forecast_valid) ─────────────────────────────
    _cont_data = lv_obj_create(_body_area);
    if (_cont_data) {
        wx_flat_base(_cont_data);
        lv_obj_set_width(_cont_data, LV_PCT(100));
        lv_obj_set_height(_cont_data, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(_cont_data, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_cont_data, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(_cont_data, 10, LV_PART_MAIN);

        _cont_hero = lv_obj_create(_cont_data);
        if (_cont_hero) {
            wx_flat_base(_cont_hero);
            lv_obj_set_width(_cont_hero, LV_PCT(100));
            lv_obj_set_height(_cont_hero, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_cont_hero, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_cont_hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(_cont_hero, 14, LV_PART_MAIN);

            _lbl_hero_icon = lv_label_create(_cont_hero);
            if (_lbl_hero_icon) {
                lv_label_set_text(_lbl_hero_icon, "");
                wx_set_font(_lbl_hero_icon, k_font_hero_icon);
                lv_obj_set_style_text_color(_lbl_hero_icon, pal.status_weather_icon, LV_PART_MAIN);
            }

            lv_obj_t* hero_text = lv_obj_create(_cont_hero);
            if (hero_text) {
                wx_flat_base(hero_text);
                lv_obj_set_height(hero_text, LV_SIZE_CONTENT);
                lv_obj_set_flex_grow(hero_text, 1);
                lv_obj_set_flex_flow(hero_text, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(hero_text, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_row(hero_text, 2, LV_PART_MAIN);

                _lbl_hero_temp = lv_label_create(hero_text);
                if (_lbl_hero_temp) {
                    lv_label_set_text(_lbl_hero_temp, "--");
                    wx_set_font(_lbl_hero_temp, k_font_hero_temp);
                    lv_obj_set_style_text_color(_lbl_hero_temp, pal.text_primary, LV_PART_MAIN);
                }
                _lbl_hero_cond = lv_label_create(hero_text);
                if (_lbl_hero_cond) {
                    lv_label_set_text(_lbl_hero_cond, "");
                    lv_label_set_long_mode(_lbl_hero_cond, LV_LABEL_LONG_DOT);
                    lv_obj_set_width(_lbl_hero_cond, LV_PCT(100));
                    wx_set_font(_lbl_hero_cond, k_font_cond);
                    lv_obj_set_style_text_color(_lbl_hero_cond, pal.text_secondary, LV_PART_MAIN);
                }
                _lbl_hero_feels = lv_label_create(hero_text);
                if (_lbl_hero_feels) {
                    lv_label_set_text(_lbl_hero_feels, "");
                    wx_set_font(_lbl_hero_feels, k_font_small);
                    lv_obj_set_style_text_color(_lbl_hero_feels, pal.text_meta, LV_PART_MAIN);
                }
            }
        }

        _cont_metrics = lv_obj_create(_cont_data);
        if (_cont_metrics) {
            wx_flat_base(_cont_metrics);
            lv_obj_set_width(_cont_metrics, LV_PCT(100));
            lv_obj_set_height(_cont_metrics, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_cont_metrics, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_cont_metrics, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            add_metric_cell(_cont_metrics, "Wind", &_val_wind, pal);
            add_metric_cell(_cont_metrics, "Humidity", &_val_humidity, pal);
            add_metric_cell(_cont_metrics, "Pressure", &_val_pressure, pal);
            add_metric_cell(_cont_metrics, "Rain", &_val_rain, pal);
        }

        _div_mid = add_thin_divider(_cont_data, pal);

        _cont_hourly = lv_obj_create(_cont_data);
        if (_cont_hourly) {
            wx_flat_base(_cont_hourly);
            lv_obj_set_width(_cont_hourly, LV_PCT(100));
            lv_obj_set_height(_cont_hourly, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_cont_hourly, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_cont_hourly, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            for (int i = 0; i < kHourlyCells; ++i) {
                _hourly[i].cont = add_strip_cell(_cont_hourly, pal,
                    &_hourly[i].time, &_hourly[i].icon, &_hourly[i].temp, &_hourly[i].pop);
            }
        }

        _cont_daily = lv_obj_create(_cont_data);
        if (_cont_daily) {
            wx_flat_base(_cont_daily);
            lv_obj_set_width(_cont_daily, LV_PCT(100));
            lv_obj_set_height(_cont_daily, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_cont_daily, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_cont_daily, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            for (int i = 0; i < kDailyCells; ++i) {
                _daily[i].cont = add_strip_cell(_cont_daily, pal,
                    &_daily[i].day, &_daily[i].icon, &_daily[i].range, &_daily[i].pop);
            }
        }

        // Start hidden until first valid snapshot (enter/update will toggle).
        // Изначально скрыт до первого валидного снапшота.
        lv_obj_add_flag(_cont_data, LV_OBJ_FLAG_HIDDEN);
    }

    // ── Empty / waiting center (flex-grow inside body; footer stays below) ───
    _cont_empty_center = lv_obj_create(_body_area);
    if (_cont_empty_center) {
        wx_flat_base(_cont_empty_center);
        lv_obj_set_width(_cont_empty_center, LV_PCT(100));
        lv_obj_set_flex_grow(_cont_empty_center, 1);
        lv_obj_set_flex_flow(_cont_empty_center, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_cont_empty_center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        _lbl_message = lv_label_create(_cont_empty_center);
        if (_lbl_message) {
            lv_label_set_text(_lbl_message, "Forecast waiting");
            lv_label_set_long_mode(_lbl_message, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(_lbl_message, LV_PCT(85));
            wx_set_font(_lbl_message, k_font_cond);
            lv_obj_set_style_text_color(_lbl_message, pal.text_secondary, LV_PART_MAIN);
            lv_obj_set_style_text_align(_lbl_message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        }
    }

    // ── Bottom status footer (pinned sibling of _body_area) ───────────────────
    _cont_footer = lv_obj_create(_content);
    if (_cont_footer) {
        wx_flat_base(_cont_footer);
        lv_obj_set_width(_cont_footer, LV_PCT(100));
        lv_obj_set_height(_cont_footer, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(_cont_footer, 0);
        lv_obj_set_style_pad_top(_cont_footer, 4, LV_PART_MAIN);

        _footer_box = lv_obj_create(_cont_footer);
        if (_footer_box) {
            wx_style_footer_pill(_footer_box, pal);
            lv_obj_set_width(_footer_box, LV_PCT(100));
            lv_obj_set_height(_footer_box, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_footer_box, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_footer_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            _lbl_footer = lv_label_create(_footer_box);
            if (_lbl_footer) {
                lv_label_set_text(_lbl_footer, "Forecast waiting");
                wx_set_font(_lbl_footer, k_font_cond);
                lv_obj_set_style_text_color(_lbl_footer, pal.text_secondary, LV_PART_MAIN);
                lv_obj_set_style_text_align(_lbl_footer, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                lv_label_set_long_mode(_lbl_footer, LV_LABEL_LONG_CLIP);
                lv_obj_set_width(_lbl_footer, LV_PCT(100));
            }
        }
    }

    installCarouselGesturesOnPageRoot(_screen);

#if YORADIO_WEATHER_UI_DIAG
    wx_diag_dump("after_create", _screen);
#endif
}

void LvglWeatherPage::enter() {
    // Render immediately on navigation; periodic refresh continues via refreshWeatherScreen().
    // Немедленный рендер при входе; периодика — через refreshWeatherScreen().
#if YORADIO_WEATHER_UI_DIAG
    wx_diag_dump("enter_before_update", _screen);
#endif
    update();
}

void LvglWeatherPage::exit() {
#if YORADIO_WEATHER_UI_DIAG
    // W2F: snapshot before leaving. PageChain auto-deletes this screen on the next switch
    // (lv_scr_load_anim auto_del) then calls releaseAfterAutoDelete() — pool recovers.
    // W2F: снимок перед уходом. PageChain удалит экран при переходе (auto_del) и вызовет release.
    wx_diag_dump("before_leave", _screen);
#endif
}

void LvglWeatherPage::update() {
    if (!_screen || !_status_line.root || !_content) return;

    wgt_status_line::update(_status_line);

    // Local static snapshot — keeps the (~330 B) WeatherState off the DspTask stack. Single reader.
    // Статический снапшот — держим ~330 B вне стека DspTask; единственный читатель.
    static WeatherState s_snap;
    if (!weatherGetStateSnapshot(&s_snap)) {
        return; // writer busy → keep last rendered state / писатель занят — оставляем кадр
    }

    const bool wxEnabled  = config.store.showweather && (strlen(config.store.weatherkey) > 0);
    const bool haveData   = s_snap.forecast_valid && s_snap.current.valid;

#if YORADIO_WEATHER_UI_DIAG
    // Log once per published version when data is present — not every 1 Hz frame (avoid UART churn).
    // Лог раз на версию публикации при наличии данных — не каждый кадр (без флуда UART).
    static uint32_t s_diag_last_ver = 0;
    if (haveData && s_snap.version != s_diag_last_ver) {
        s_diag_last_ver = s_snap.version;
        wx_diag_dump("update_with_data", _screen);
    }
#endif

    char buf[40];

    if (!haveData) {
        // W2C: body shows centered message; footer pill carries state text at bottom.
        // W2C: body — центрированное сообщение; футер-pill внизу с текстом состояния.
        wx_show(_cont_data, false);
        wx_show(_cont_empty_center, true);
        wx_show(_cont_footer, true);
        if (wxEnabled) {
            wx_set_text_if_changed(_lbl_message, "Forecast waiting");
            wx_set_text_if_changed(_lbl_footer, "Forecast waiting");
        } else {
            wx_set_text_if_changed(_lbl_message, "Weather unavailable");
            wx_set_text_if_changed(_lbl_footer, "Check weather settings");
        }
        return;
    }

    // ── State A: full forecast ──────────────────────────────────────────────
    wx_show(_cont_empty_center, false);
    wx_show(_cont_data, true);
    wx_show(_cont_footer, true);

    const WeatherCurrent& cur = s_snap.current;

    wx_set_text_if_changed(_lbl_hero_icon, weather_owm_icon_to_glyph_utf8(cur.owm_icon));
    snprintf(buf, sizeof(buf), "%+.0f\xC2\xB0\x43", static_cast<double>(cur.temp_c)); // "+NN°C"
    wx_set_text_if_changed(_lbl_hero_temp, buf);
    wx_set_text_if_changed(_lbl_hero_cond, (cur.condition[0] != '\0') ? cur.condition : "--");
    snprintf(buf, sizeof(buf), "Feels %+.0f\xC2\xB0\x43", static_cast<double>(cur.feels_like_c));
    wx_set_text_if_changed(_lbl_hero_feels, buf);

    snprintf(buf, sizeof(buf), "%.0f m/s", static_cast<double>(cur.wind_speed));
    wx_set_text_if_changed(_val_wind, buf);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)cur.humidity);
    wx_set_text_if_changed(_val_humidity, buf);
    snprintf(buf, sizeof(buf), "%u hPa", (unsigned)cur.pressure_hpa);
    wx_set_text_if_changed(_val_pressure, buf);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)cur.rain_probability);
    wx_set_text_if_changed(_val_rain, buf);

    // Hourly: relative offsets from the first slot (Now / +3h / …).
    const uint32_t base_ts = s_snap.hourly[0].valid ? s_snap.hourly[0].ts : 0u;
    for (int i = 0; i < kHourlyCells; ++i) {
        const WeatherHourly& h = s_snap.hourly[i];
        if (!h.valid) {
            wx_set_text_if_changed(_hourly[i].time, "--");
            wx_set_text_if_changed(_hourly[i].icon, weather_owm_icon_to_glyph_utf8(nullptr));
            wx_set_text_if_changed(_hourly[i].temp, "--");
            wx_set_text_if_changed(_hourly[i].pop, "");
            continue;
        }
        if (i == 0 || base_ts == 0u) {
            wx_set_text_if_changed(_hourly[i].time, "Now");
        } else {
            const uint32_t dh = (h.ts > base_ts) ? (h.ts - base_ts) / 3600u : 0u;
            snprintf(buf, sizeof(buf), "+%uh", (unsigned)dh);
            wx_set_text_if_changed(_hourly[i].time, buf);
        }
        wx_set_text_if_changed(_hourly[i].icon, weather_owm_icon_to_glyph_utf8(h.owm_icon));
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", static_cast<double>(h.temp_c));
        wx_set_text_if_changed(_hourly[i].temp, buf);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)h.rain_probability);
        wx_set_text_if_changed(_hourly[i].pop, buf);
    }

    // Daily: relative day labels by index (Today / Tomorrow / +2d).
    static const char* const k_day_label[kDailyCells] = {"Today", "Tomorrow", "+2d"};
    for (int i = 0; i < kDailyCells; ++i) {
        const WeatherDaily& d = s_snap.daily[i];
        if (!d.valid) {
            wx_set_text_if_changed(_daily[i].day, "--");
            wx_set_text_if_changed(_daily[i].icon, weather_owm_icon_to_glyph_utf8(nullptr));
            wx_set_text_if_changed(_daily[i].range, "--");
            wx_set_text_if_changed(_daily[i].pop, "");
            continue;
        }
        wx_set_text_if_changed(_daily[i].day, k_day_label[i]);
        wx_set_text_if_changed(_daily[i].icon, weather_owm_icon_to_glyph_utf8(d.owm_icon));
        snprintf(buf, sizeof(buf), "%.0f/%.0f\xC2\xB0",
                 static_cast<double>(d.temp_min_c), static_cast<double>(d.temp_max_c));
        wx_set_text_if_changed(_daily[i].range, buf);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)d.rain_probability_max);
        wx_set_text_if_changed(_daily[i].pop, buf);
    }

    wx_format_age(buf, sizeof(buf), s_snap.forecast_updated_at, s_snap.stale);
    wx_set_text_if_changed(_lbl_footer, buf);
}

void LvglWeatherPage::liveReapplyTheme() {
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    // W2A: colors/grad only — never remove_style_all here (would erase flex/layout).
    // W2A: только цвета и grad off — без remove_style_all (сотрёт flex/layout).
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    wgt_status_line::reapplyTheme(_status_line);

    auto paint = [&](lv_obj_t* o, lv_color_t c) {
        if (o) lv_obj_set_style_text_color(o, c, LV_PART_MAIN);
    };

    paint(_lbl_hero_icon, pal.status_weather_icon);
    paint(_lbl_hero_temp, pal.text_primary);
    paint(_lbl_hero_cond, pal.text_secondary);
    paint(_lbl_hero_feels, pal.text_meta);
    paint(_val_wind, pal.text_primary);
    paint(_val_humidity, pal.text_primary);
    paint(_val_pressure, pal.text_primary);
    paint(_val_rain, pal.text_primary);
    paint(_lbl_footer, pal.text_secondary);
    paint(_lbl_message, pal.text_secondary);

    if (_footer_box) {
        lv_obj_set_style_bg_color(_footer_box, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(_footer_box, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_color(_footer_box, pal.divider, LV_PART_MAIN);
    }

    for (int i = 0; i < kHourlyCells; ++i) {
        paint(_hourly[i].time, pal.text_secondary);
        paint(_hourly[i].icon, pal.status_weather_icon);
        paint(_hourly[i].temp, pal.text_primary);
        paint(_hourly[i].pop,  pal.text_secondary);
    }
    for (int i = 0; i < kDailyCells; ++i) {
        paint(_daily[i].day,   pal.text_secondary);
        paint(_daily[i].icon,  pal.status_weather_icon);
        paint(_daily[i].range, pal.text_primary);
        paint(_daily[i].pop,   pal.text_secondary);
    }

    // Flat dividers + metric captions; force grad off on any lv_obj in the tree.
    // Плоские разделители и подписи; принудительно grad off на lv_obj в дереве.
    auto retint_tree = [&](lv_obj_t* root, auto&& self) -> void {
        if (!root) return;
        if (root != _status_line.root && lv_obj_check_type(root, &lv_obj_class)) {
            lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        }
        const uint32_t n = lv_obj_get_child_cnt(root);
        for (uint32_t i = 0; i < n; ++i) {
            lv_obj_t* ch = lv_obj_get_child(root, i);
            if (!ch || ch == _status_line.root) continue;
            if (lv_obj_check_type(ch, &lv_label_class)) {
                const lv_font_t* f = lv_obj_get_style_text_font(ch, LV_PART_MAIN);
                if (f == static_cast<const lv_font_t*>(k_font_caption)) {
                    lv_obj_set_style_text_color(ch, pal.text_secondary, LV_PART_MAIN);
                }
            } else if (lv_obj_get_height(ch) == 1 &&
                       lv_obj_get_style_bg_opa(ch, LV_PART_MAIN) == LV_OPA_COVER) {
                lv_obj_set_style_bg_color(ch, pal.divider, LV_PART_MAIN);
                lv_obj_set_style_bg_grad_dir(ch, LV_GRAD_DIR_NONE, LV_PART_MAIN);
            }
            self(ch, self);
        }
    };
    retint_tree(_screen, retint_tree);

    lv_obj_invalidate(_screen);
}

void LvglWeatherPage::destroy() {
    // Manual delete path: drop the LVGL root tree, then null handles.
    // Ручное удаление: удаляем дерево LVGL, затем обнуляем указатели.
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglWeatherPage::releaseAfterAutoDelete() {
    // W2F: LVGL already deleted the screen tree (auto_del). Data lives in core WeatherState,
    // so nothing to persist here — just null handles. Never lv_obj_del here.
    // W2F: дерево уже удалено LVGL; данные в core WeatherState — только обнуляем указатели.
    _nullHandles();
}

void LvglWeatherPage::_nullHandles() {
    _screen = nullptr;  // dangling after auto_del; already nulled on the manual destroy() path
    _status_line = {};
    _content = _body_area = nullptr;
    _cont_data = _div_mid = _cont_empty_center = _lbl_message = nullptr;
    _cont_footer = _footer_box = _lbl_footer = nullptr;
    _cont_hero = _lbl_hero_icon = _lbl_hero_temp = _lbl_hero_cond = _lbl_hero_feels = nullptr;
    _cont_metrics = _val_wind = _val_humidity = _val_pressure = _val_rain = nullptr;
    _cont_hourly = _cont_daily = nullptr;
    for (int i = 0; i < kHourlyCells; ++i) _hourly[i] = HourlyCell{};
    for (int i = 0; i < kDailyCells; ++i)  _daily[i] = DailyCell{};
}

lv_obj_t* LvglWeatherPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
