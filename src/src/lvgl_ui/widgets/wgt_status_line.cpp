// wgt_status_line — top status strip for Main (Wi‑Fi + centered clock + weather on the right).
// Три равные колонки: Wi‑Fi слева, часы по центру экрана, погода (иконка+°C) справа.

#include "wgt_status_line.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include <cstdio>
#include <cstring>
#include <ctime>

#include "Arduino.h"
#include "WiFi.h"

#include "../fonts/lv_fonts.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../weather_owm_glyph.h"
#include "../wifi_signal_map.h"
#include "../../core/config.h"
#include "../../core/network.h"

namespace lvgl_ui {
namespace wgt_status_line {

// Wi-Fi icon: Tabler subset 22 px (on-device OK vs Montserrat 18 clock). Alternatives: _18 / _20 in lv_fonts.h.
// Иконка Wi‑Fi: Tabler 22 px (проверено на устройстве). Замена: lv_font_yora_status_icons_18 / _20.
static const lv_font_t* k_wifi_icon_font = &lv_font_yora_status_icons_22;
// Weather icons: one step smaller than 22 for dense status row; ladder 18/20/22/… in lv_fonts.h.
// Иконки погоды — на ступень меньше 22 для верхней полосы.
static const lv_font_t* k_weather_icon_font = &lv_font_yora_weather_icons_20;

static void set_font_slot(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

// Style a column shell: equal third of status row, row flow, transparent.
// Оболочка колонки: треть ширины полосы, прозрачный фон.
static void style_status_column(lv_obj_t* col) {
    if (!col) return;
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
}

bool create(lv_obj_t* parent, Instance& out) {
    out = Instance{};
    if (!parent) return false;

    out.root = lv_obj_create(parent);
    if (!out.root) return false;

    const YoRadioPalette& pal = yoradio_palette();

    lv_obj_set_width(out.root, LV_PCT(100));
    lv_obj_set_height(out.root, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(out.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        out.root,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(out.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(out.root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(out.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(out.root, 0, LV_PART_MAIN);
    const void* clock_f = reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr);
    const void* wx_temp_f = reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr);
    lv_obj_set_style_pad_ver(out.root, 4, LV_PART_MAIN);

    lv_obj_t* col_left = lv_obj_create(out.root);
    lv_obj_t* col_center = lv_obj_create(out.root);
    lv_obj_t* col_right = lv_obj_create(out.root);
    if (!col_left || !col_center || !col_right) {
        if (out.root) lv_obj_del(out.root);
        out = Instance{};
        return false;
    }
    style_status_column(col_left);
    style_status_column(col_center);
    style_status_column(col_right);
    // Left: Wi‑Fi at column start; center: clock centered (= screen center); right: weather at column end.
    // Слева START, центр — часы CENTER, справа погода END у правого края.
    lv_obj_set_flex_align(col_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_align(col_center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_align(col_right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    out.lbl_wifi = lv_label_create(col_left);
    if (out.lbl_wifi) {
        lv_label_set_text(out.lbl_wifi, reinterpret_cast<const char*>(u8"\uEBA3"));
        lv_label_set_long_mode(out.lbl_wifi, LV_LABEL_LONG_CLIP);
        set_font_slot(out.lbl_wifi, reinterpret_cast<const void*>(k_wifi_icon_font));
        lv_obj_set_style_text_color(out.lbl_wifi, pal.status_line_text, LV_PART_MAIN);
    }

    out.lbl_clock = lv_label_create(col_center);
    if (out.lbl_clock) {
        lv_label_set_text(out.lbl_clock, "--:--");
        lv_label_set_long_mode(out.lbl_clock, LV_LABEL_LONG_CLIP);
        set_font_slot(out.lbl_clock, clock_f);
        lv_obj_set_style_text_color(out.lbl_clock, pal.clock_text, LV_PART_MAIN);
    }

    out.cont_weather = lv_obj_create(col_right);
    if (out.cont_weather) {
        lv_obj_set_width(out.cont_weather, LV_SIZE_CONTENT);
        lv_obj_set_height(out.cont_weather, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(out.cont_weather, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            out.cont_weather,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(out.cont_weather, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(out.cont_weather, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(out.cont_weather, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(out.cont_weather, 0, LV_PART_MAIN);
        lv_obj_clear_flag(out.cont_weather, LV_OBJ_FLAG_SCROLLABLE);

        out.lbl_weather_glyph = lv_label_create(out.cont_weather);
        if (out.lbl_weather_glyph) {
            lv_label_set_text(out.lbl_weather_glyph, "");
            lv_label_set_long_mode(out.lbl_weather_glyph, LV_LABEL_LONG_CLIP);
            set_font_slot(out.lbl_weather_glyph, reinterpret_cast<const void*>(k_weather_icon_font));
            // Theme: status_weather_icon / status_weather_temp (glance row, not bottom_weather).
            // Тема: отдельные токены глифа и °C для верхней полосы.
            lv_obj_set_style_text_color(out.lbl_weather_glyph, pal.status_weather_icon, LV_PART_MAIN);
        }
        out.lbl_weather_temp = lv_label_create(out.cont_weather);
        if (out.lbl_weather_temp) {
            lv_label_set_text(out.lbl_weather_temp, "");
            lv_label_set_long_mode(out.lbl_weather_temp, LV_LABEL_LONG_CLIP);
            set_font_slot(out.lbl_weather_temp, wx_temp_f);
            lv_obj_set_style_text_color(out.lbl_weather_temp, pal.status_weather_temp, LV_PART_MAIN);
        }
        lv_obj_add_flag(out.cont_weather, LV_OBJ_FLAG_HIDDEN);
    }

    const bool ok = col_left && col_center && col_right && out.lbl_wifi && out.cont_weather && out.lbl_weather_glyph
                    && out.lbl_weather_temp && out.lbl_clock;
    if (!ok && out.root) {
        lv_obj_del(out.root);
        out = Instance{};
        return false;
    }
    return true;
}

// Avoid lv_label_set_text when unchanged — fewer layout passes (same as scr_main 5.5a pattern).
// Не дергать set_text без изменения текста — меньше лишнего layout.
static void status_line_set_text_if_changed(lv_obj_t* lbl, const char* s) {
    if (!lbl || !s) return;
    const char* cur = lv_label_get_text(lbl);
    if (cur != nullptr && strcmp(cur, s) == 0) return;
    lv_label_set_text(lbl, s);
}

void update(const Instance& inst) {
    // Wi-Fi icon: Tabler subset glyphs; RSSI sampled at most every 3s when connected.
    // Иконка Wi‑Fi; RSSI не чаще 3 с при подключении.
    static uint32_t s_last_rssi_ms = 0;
    static int      s_rssi_cached_dbm = -100;
    static bool     s_wifi_was_connected = false;
    const bool      wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected != s_wifi_was_connected) {
        s_wifi_was_connected = wifi_connected;
        s_last_rssi_ms = 0;
    }
    const uint32_t now = millis();
    if (!wifi_connected) {
        status_line_set_text_if_changed(inst.lbl_wifi, wifi_status_glyph_utf8_disconnected());
    } else {
        if (s_last_rssi_ms == 0u || (now - s_last_rssi_ms >= 3000u)) {
            s_last_rssi_ms = now;
            s_rssi_cached_dbm = WiFi.RSSI();
        }
        const int lvl = wifi_rssi_to_level(s_rssi_cached_dbm);
        status_line_set_text_if_changed(inst.lbl_wifi, wifi_status_glyph_utf8_for_level(lvl));
    }

    // Clock from network.timeinfo (filled by existing stack — no new bridge in 6.1).
    // Часы из network.timeinfo — тот же источник, что и screensaver.
    static char s_clock_line[16];
    if (network.timeinfo.tm_year > 100) {
        if (strftime(s_clock_line, sizeof(s_clock_line), "%H:%M", &network.timeinfo) == 0) {
            strncpy(s_clock_line, "--:--", sizeof(s_clock_line) - 1);
            s_clock_line[sizeof(s_clock_line) - 1] = '\0';
        }
    } else {
        strncpy(s_clock_line, "--:--", sizeof(s_clock_line) - 1);
        s_clock_line[sizeof(s_clock_line) - 1] = '\0';
    }
    status_line_set_text_if_changed(inst.lbl_clock, s_clock_line);

    // Compact weather in status line (glyph + °C); same data as 6.1C glance fields.
    // Компактная погода в status line — те же network.weather* поля.
    static char weather_temp[16];
    if (inst.cont_weather && inst.lbl_weather_glyph && inst.lbl_weather_temp) {
        // Compact status weather: key + showweather only; do not gate on legacy full-string weatherBuf.
        const bool wantWx = config.store.showweather && (strlen(config.store.weatherkey) > 0);
        if (wantWx && network.weatherGlanceValid) {
            status_line_set_text_if_changed(
                inst.lbl_weather_glyph,
                weather_owm_icon_to_glyph_utf8(network.weatherOwmIcon));
            snprintf(
                weather_temp,
                sizeof(weather_temp),
                "%+.0f°C",
                static_cast<double>(network.weatherLastTempC));
            status_line_set_text_if_changed(inst.lbl_weather_temp, weather_temp);
            lv_obj_clear_flag(inst.lbl_weather_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(inst.cont_weather, LV_OBJ_FLAG_HIDDEN);
        } else if (wantWx) {
            lv_obj_add_flag(inst.lbl_weather_glyph, LV_OBJ_FLAG_HIDDEN);
            status_line_set_text_if_changed(inst.lbl_weather_temp, "…");
            lv_obj_clear_flag(inst.cont_weather, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(inst.cont_weather, LV_OBJ_FLAG_HIDDEN);
            status_line_set_text_if_changed(inst.lbl_weather_glyph, "");
            status_line_set_text_if_changed(inst.lbl_weather_temp, "");
        }
    }
}

} // namespace wgt_status_line
} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
