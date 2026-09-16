// wgt_status_line — top status strip for Main (Wi‑Fi + centered clock + weather on the right).
// Три равные колонки: Wi‑Fi слева, часы по центру экрана, погода (иконка+°C) справа.
// Author: Witaliy76 - https://github.com/Witaliy76

#include "wgt_status_line.h"


#include <cstdio>
#include <cstring>
#include <ctime>

#include "Arduino.h"
#include "WiFi.h"

#include "../fonts/lv_fonts.h"
#include "../font_provider.h"
#include "../lv_page_chain.h"
#include "../lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../weather_owm_glyph.h"
#include "../wifi_signal_map.h"
#include "../../core/config.h"
#include "../../core/network.h"
#include "../../core/sleep_timer.h"
#include "../../core/weather_state.h"

namespace lvgl_ui {
namespace wgt_status_line {

// Wi-Fi icon: Tabler via FontProvider at 22 px (current 480 request, not a TTF limit).
// Иконка Wi‑Fi: Tabler через FontProvider, 22 px (запрос профиля 480, не лимит TTF).
static const lv_font_t* wifi_icon_font() { return FontProvider::icon(22); }
// Weather mini: one step smaller than the 22 px Wi-Fi glyph on the status row.
// Мини-погода: на ступень меньше 22 px глифа Wi‑Fi на status row.
static const lv_font_t* weather_icon_font() { return FontProvider::icon(20); }

// 480x480 geometry: 112 px stays inside the right status column; 42 px uses the top frame,
// the 30 px status row and the smallest 4 px divider gap without touching that divider.
// Геометрия 480x480: 112 px остаются справа, а 42 px используют верхнюю рамку, status row
// и минимальный зазор до divider, не сдвигая видимую строку.
static constexpr lv_coord_t kWeatherHitWidth = 112;
static constexpr lv_coord_t kWeatherHitHeight = 42;
static constexpr lv_coord_t kWeatherHitPadHorizontal = 8;
static constexpr lv_coord_t kWeatherHitPadTop = 4;
static constexpr lv_coord_t kStatusRootPadVertical = 4;
static constexpr int32_t kWeatherHitRootOverflow = 8;

static bool s_weather_navigation_pending = false;

static void status_root_ext_draw_size_cb(lv_event_t* e) {
    if (!e || lv_event_get_code(e) != LV_EVENT_REFR_EXT_DRAW_SIZE) return;
    lv_event_set_ext_draw_size(e, kWeatherHitRootOverflow);
}

static void open_weather_async_cb(void* /*data*/) {
    s_weather_navigation_pending = false;
    if (isLvglCarouselOnWeatherSlot()) return;
    goToCarouselPage(PageChain::WEATHER_INDEX);
}

static void weather_clicked_cb(lv_event_t* e) {
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    notifyPageChainActivity("status-weather");
    if (isLvglCarouselOnWeatherSlot() || s_weather_navigation_pending) return;

    // PageChain owns Weather lifecycle. Defer until LVGL finishes this click dispatch because
    // goTo() may synchronously delete the active page tree. / Жизненным циклом владеет PageChain;
    // переход откладываем, чтобы goTo() не удалял active tree внутри обработки клика.
    if (lv_async_call(open_weather_async_cb, nullptr) == LV_RES_OK) {
        s_weather_navigation_pending = true;
    }
}

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
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
}

static void format_sleep_timer_text(char* out, size_t cap, bool compact) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    const TimerRuntimeSnapshot snapshot = timer_runtime_snapshot();
    const bool radio_stop = snapshot.radio_stop_active;
    const bool deep_sleep = snapshot.deep_sleep_active;
    // Radio Stop keeps the established SLEEP marker; delayed device sleep gets an explicit
    // DEEP SLEEP marker. Radio Start remains intentionally hidden. / Radio Start не выводим.
    if (!radio_stop && !deep_sleep) return;

    const uint32_t remaining_seconds =
        radio_stop ? snapshot.radio_stop_remaining_seconds
                   : snapshot.deep_sleep_remaining_seconds;
    if (remaining_seconds == 0) return;
    const char* full_name = radio_stop ? "SLEEP" : "DEEP SLEEP";
    const char* compact_name = radio_stop ? "" : "DEEP ";
    const char* shown_name = compact ? compact_name : full_name;
    const char* separator = compact ? "" : " ";
    if (remaining_seconds < 60) {
        snprintf(out, cap, "%s%s<1m", shown_name, separator);
        return;
    }

    const uint32_t remaining_minutes = (remaining_seconds + 59u) / 60u;
    snprintf(out, cap, "%s%s%lum", shown_name, separator,
             static_cast<unsigned long>(remaining_minutes));
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
    const void* clock_f = FontProvider::text(18);
    const void* wx_temp_f = FontProvider::text(14);
    lv_obj_set_style_pad_ver(out.root, kStatusRootPadVertical, LV_PART_MAIN);
    // Passive shell: the floating weather wrapper below is the only status-line click owner.
    // Пассивная оболочка: единственный владелец клика — floating-контейнер погоды ниже.
    lv_obj_clear_flag(out.root, LV_OBJ_FLAG_CLICKABLE);
    // The 42 px child intentionally reaches into the screen frame/gap while root stays 30 px high.
    // Extend only clipping/search bounds; flex geometry remains unchanged. / Дочерняя зона 42 px
    // заходит в рамку/зазор, поэтому расширяем только clip/hit bounds, не flex-геометрию.
    lv_obj_add_flag(out.root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(
        out.root, status_root_ext_draw_size_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
    lv_obj_refresh_ext_draw_size(out.root);

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
        set_font_slot(out.lbl_wifi, reinterpret_cast<const void*>(wifi_icon_font()));
        lv_obj_set_style_text_color(out.lbl_wifi, pal.status_line_text, LV_PART_MAIN);
    }

    out.lbl_clock = lv_label_create(col_center);
    if (out.lbl_clock) {
        lv_label_set_text(out.lbl_clock, "--:--");
        lv_label_set_long_mode(out.lbl_clock, LV_LABEL_LONG_CLIP);
        set_font_slot(out.lbl_clock, clock_f);
        lv_obj_set_style_text_color(out.lbl_clock, pal.clock_text, LV_PART_MAIN);
    }

    out.cont_weather = lv_obj_create(out.root);
    if (out.cont_weather) {
        // Larger invisible hit owner, floating outside flex so glyph/text keep their old position
        // and the status/divider geometry does not move. / Увеличенная прозрачная зона вне flex:
        // видимые glyph/text и геометрия status/divider остаются на прежнем месте.
        lv_obj_add_flag(out.cont_weather, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(out.cont_weather, kWeatherHitWidth, kWeatherHitHeight);
        const lv_coord_t frame_pad = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);
        lv_obj_align(
            out.cont_weather,
            LV_ALIGN_TOP_RIGHT,
            frame_pad,
            -frame_pad);
        lv_obj_set_flex_flow(out.cont_weather, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            out.cont_weather,
            LV_FLEX_ALIGN_END,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(out.cont_weather, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(out.cont_weather, kWeatherHitPadHorizontal, LV_PART_MAIN);
        // Top-only padding keeps the visible glyph/text on the original status-row center while
        // the transparent hit owner spans y=0..41. / Верхний padding сохраняет прежний центр
        // glyph/text, пока прозрачная зона касания занимает y=0..41.
        lv_obj_set_style_pad_top(out.cont_weather, kWeatherHitPadTop, LV_PART_MAIN);
        lv_obj_set_style_pad_column(out.cont_weather, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(out.cont_weather, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(out.cont_weather, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(out.cont_weather, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(out.cont_weather, 0, LV_PART_MAIN);
        lv_obj_clear_flag(out.cont_weather, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(out.cont_weather, LV_OBJ_FLAG_CLICKABLE);
        // Preserve carousel ownership for a deliberate horizontal swipe begun on this zone.
        // Сохраняем владельца карусели для горизонтального свайпа, начатого на этой зоне.
        lv_obj_add_flag(out.cont_weather, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(out.cont_weather, weather_clicked_cb, LV_EVENT_CLICKED, nullptr);

        out.lbl_weather_glyph = lv_label_create(out.cont_weather);
        if (out.lbl_weather_glyph) {
            lv_label_set_text(out.lbl_weather_glyph, "");
            lv_label_set_long_mode(out.lbl_weather_glyph, LV_LABEL_LONG_CLIP);
            set_font_slot(out.lbl_weather_glyph, reinterpret_cast<const void*>(weather_icon_font()));
            // Theme: status_weather_icon / status_weather_temp (glance row, not bottom_weather).
            // Тема: отдельные токены глифа и °C для верхней полосы.
            lv_obj_set_style_text_color(out.lbl_weather_glyph, pal.status_weather_icon, LV_PART_MAIN);
            lv_obj_clear_flag(out.lbl_weather_glyph, LV_OBJ_FLAG_CLICKABLE);
        }
        out.lbl_weather_temp = lv_label_create(out.cont_weather);
        if (out.lbl_weather_temp) {
            lv_label_set_text(out.lbl_weather_temp, "");
            lv_label_set_long_mode(out.lbl_weather_temp, LV_LABEL_LONG_CLIP);
            set_font_slot(out.lbl_weather_temp, wx_temp_f);
            lv_obj_set_style_text_color(out.lbl_weather_temp, pal.status_weather_temp, LV_PART_MAIN);
            lv_obj_clear_flag(out.lbl_weather_temp, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_add_flag(out.cont_weather, LV_OBJ_FLAG_HIDDEN);
    }

    out.lbl_sleep_timer = lv_label_create(out.root);
    if (out.lbl_sleep_timer) {
        lv_obj_add_flag(out.lbl_sleep_timer, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_width(out.lbl_sleep_timer, 116);
        lv_label_set_text(out.lbl_sleep_timer, "");
        lv_label_set_long_mode(out.lbl_sleep_timer, LV_LABEL_LONG_CLIP);
        set_font_slot(out.lbl_sleep_timer, wx_temp_f);
        lv_obj_set_style_text_color(out.lbl_sleep_timer, pal.status_line_text, LV_PART_MAIN);
        // EXEC-01B-PERF: moved left (was RIGHT_MID -76) to the spot the sys-layer perf-monitor
        // overlay vacated when that moved to the right of the clock; text now grows rightward
        // away from the Wi‑Fi icon in col_left, so left-align replaces the old right-align.
        // Перенесено влево (было RIGHT_MID -76) — на место, освобождённое perf-монитором;
        // текст растёт вправо от иконки Wi‑Fi, поэтому выравнивание тоже сменено на левое.
        lv_obj_set_style_text_align(out.lbl_sleep_timer, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_align(out.lbl_sleep_timer, LV_ALIGN_LEFT_MID, 76, 0);
        lv_obj_add_flag(out.lbl_sleep_timer, LV_OBJ_FLAG_HIDDEN);
    }

    const bool ok = col_left && col_center && col_right && out.lbl_wifi && out.cont_weather && out.lbl_weather_glyph
                    && out.lbl_weather_temp && out.lbl_sleep_timer && out.lbl_clock;
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

    if (inst.root && inst.lbl_sleep_timer) {
        const lv_coord_t root_w = lv_obj_get_width(inst.root);
        const bool compact = root_w > 0 && root_w < 400;
        lv_obj_set_width(inst.lbl_sleep_timer, compact ? 84 : 116);
        char sleep_text[32];
        format_sleep_timer_text(sleep_text, sizeof(sleep_text), compact);
        status_line_set_text_if_changed(inst.lbl_sleep_timer, sleep_text);
        if (sleep_text[0] == '\0') {
            lv_obj_add_flag(inst.lbl_sleep_timer, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(inst.lbl_sleep_timer, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // A4.1: compact status weather from WeatherState.current (icon + °C).
    // A4.1: компактная погода из WeatherState.current (иконка + °C).
    static char weather_temp[16];
    if (inst.cont_weather && inst.lbl_weather_glyph && inst.lbl_weather_temp) {
        const bool wantWx = config.store.showweather && (strlen(config.store.weatherkey) > 0);
        WeatherState weather{};
        weatherGetStateSnapshot(&weather);
        if (wantWx && weather.current.valid) {
            status_line_set_text_if_changed(
                inst.lbl_weather_glyph,
                weather_owm_icon_to_glyph_utf8(weather.current.owm_icon));
            snprintf(
                weather_temp,
                sizeof(weather_temp),
                "%+.0f°C",
                static_cast<double>(weather.current.temp_c));
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

void reapplyTheme(Instance& inst) {
    // Stage 6.6R-B: update text colors on existing objects from current palette (DspTask only).
    // No layout changes; safe to call on any created Instance.
    // Этап 6.6R-B: обновить цвета из текущей палитры — без layout-изменений, только DspTask.
    if (!inst.root) return;
    const YoRadioPalette& pal = yoradio_palette();
    if (inst.lbl_wifi)          lv_obj_set_style_text_color(inst.lbl_wifi,          pal.status_line_text,    LV_PART_MAIN);
    if (inst.lbl_clock)         lv_obj_set_style_text_color(inst.lbl_clock,         pal.clock_text,          LV_PART_MAIN);
    if (inst.lbl_sleep_timer)   lv_obj_set_style_text_color(inst.lbl_sleep_timer,   pal.status_line_text,    LV_PART_MAIN);
    if (inst.lbl_weather_glyph) lv_obj_set_style_text_color(inst.lbl_weather_glyph, pal.status_weather_icon, LV_PART_MAIN);
    if (inst.lbl_weather_temp)  lv_obj_set_style_text_color(inst.lbl_weather_temp,  pal.status_weather_temp, LV_PART_MAIN);
}

} // namespace wgt_status_line
} // namespace lvgl_ui

