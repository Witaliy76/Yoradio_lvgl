/*
 * LvglWeatherPage — Weather W2 LVGL page (read-only consumer of core WeatherState).
 * LvglWeatherPage — страница погоды W2 (только чтение core WeatherState).
 *
 * - Implements ILvglScreen: create → enter → update → exit → destroy (+ liveReapplyTheme).
 * - DspTask-only lv_* via Display::loop → lvgl_ui::taskHandler / refreshWeatherScreen().
 * - Data: weatherGetStateSnapshot() ONLY (W1 double-buffer seqlock). No network from UI.
 *   A2b: footer tap calls weatherRequestManualRefresh() (async flag only; HTTP in doSync).
 * - Layout: LV_ACTIVE_PROFILE + yoradio_palette() only; reuse OWM glyph map + weather icon fonts.
 *
 * Accepted W2 narrowing (no WeatherState change):
 *   - WeatherState.current is filled from the forecast (W1), so "current valid" == "forecast valid".
 *     → states collapse to: full / waiting (enabled, no forecast) / unavailable (disabled or no key).
 *   - Hourly fallback offsets (+3 ч…) when ts missing; daily cards show weekday+DD.MM (A3.1h).
 *     Condition text from WeatherState/API (locale via weatherLang).
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
#include <time.h>

#include "../fonts/lv_fonts.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../weather_owm_glyph.h"
#include "lvgl_ui.h"
#include "../../core/config.h"   // pulls options.h → myoptions.h (YORADIO_WEATHER_UI_DIAG)
#include "../../core/network.h"  // A3.1: network.timeinfo (NTP-synced local date, read-only) / дата из NTP
#include "../../core/weather_fetch.h"  // A2b: weatherRequestManualRefresh() / async refresh flag
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

// Weather Page UI strings: Russian baseline until a real i18n layer is added.
// Keep user-visible copy centralized; do not scatter literals through widget code.
// Строки Weather Page: русская база до i18n; не разбрасывать литералы по виджетам.
static const char* const kStrFeelsLike      = "Ощущается ";
static const char* const kStrMetricWind     = "Ветер";
static const char* const kStrMetricHumidity = "Влажность";
static const char* const kStrMetricPressure = "Давление";
static const char* const kStrMetricRain     = "Осадки";
static const char* const kStrTomorrow       = "Завтра"; // Завтра (hourly header only)
static const char* const kStrPlus3h         = "+3 ч";
static const char* const kStrPlus6h         = "+6 ч";
static const char* const kStrPlus9h         = "+9 ч";
static const char* const kStrTodayOnly      = "Сегодня";
// A3.1f: right hourly block day header strings / заголовок дня в правом hourly-блоке.
static const char* const kStrHourlyNearest       = "Ближайшие часы";
static const char* const kStrTodaySlashTomorrow  = "Сегодня / завтра";
static const char* const kStrTomorrowSlashLater  = "Завтра / позже";
static const char* const kStrForecastWaiting = "Ожидание данных о погоде";
static const char* const kStrForecastNotLoaded = "Прогноз ещё не загружен";
static const char* const kStrWeatherUnavail  = "Погода недоступна";
static const char* const kStrPleaseWait      = "Пожалуйста, подождите";
static const char* const kStrTemporarilyUnavailable = "Погода временно недоступна";
static const char* const kStrDataMayBeOutdated = "Данные могут быть устаревшими";
static const char* const kStrCheckSettings   = "Проверьте настройки погоды";
// A2b: footer status + tap hint strings / строки футера: статус и подсказка тапа.
// A2c: separator matches Main/Station k_meta_field_sep — U+2022 • in montserrat_16_cyr (not U+00B7).
// A2c: разделитель как на Main/Station — U+2022, не U+00B7.
static constexpr const char* kStrFooterSep          = " \xE2\x80\xA2 ";
static const char* const kStrFooterRefreshing       = "Обновление погоды...";
static const char* const kStrFooterTapRefresh       = "Нажать для обновления";
static const char* const kStrFooterTapRetry         = "Нажмите, чтобы повторить";
// A4.1: footer format buffers — city[64] + country + longest Russian status/age/action + seps.
// A4.1: буферы футера — city[64] + country + самые длинные русские фразы + разделители.
static constexpr size_t kFooterAgeCap  = 48;
static constexpr size_t kFooterTextCap = 192;

// A3.1: Russian genitive months + weekdays for hero date line (local to Weather page).
// A3.1: месяцы (род. п.) и дни недели для строки даты в hero (только эта страница).
static const char* const kRuMonthsGenitive[12] = {
    "января",
    "февраля",
    "марта",
    "апреля",
    "мая",
    "июня",
    "июля",
    "августа",
    "сентября",
    "октября",
    "ноября",
    "декабря",
};
// tm_wday: 0 = Sunday … 6 = Saturday / 0 = воскресенье
static const char* const kRuWeekdayLower[7] = {
    "воскресенье",
    "понедельник",
    "вторник",
    "среда",
    "четверг",
    "пятница",
    "суббота",
};
static const char* const kRuWeekdayTitle[7] = {
    "Воскресенье",
    "Понедельник",
    "Вторник",
    "Среда",
    "Четверг",
    "Пятница",
    "Суббота",
};
// A3.1h: short weekday for daily card date line (Вс…Сб). / Краткий день недели для daily.
static const char* const kRuWeekdayShort[7] = {
    "Вс",
    "Пн",
    "Вт",
    "Ср",
    "Чт",
    "Пт",
    "Сб",
};

// Same validity gate as status line / screensaver (tm_year > 100 ≈ year > 2000).
// Тот же gate, что у status line / screensaver (tm_year > 100).
static bool wx_system_date_valid(const struct tm* tm) {
    return tm && tm->tm_year > 100;
}

// A3: font ladder — hero 64 px; strip forecast 36 px; metric icons 26 px (discrete lv_font_conv sizes).
// A3: лестница шрифтов — hero 64 пкс; прогноз 36 пкс; метрики 26 пкс (дискретные размеры).
static const void* k_font_hero_icon    = reinterpret_cast<const void*>(&lv_font_yora_weather_icons_64);
static const void* k_font_daily_icon   = reinterpret_cast<const void*>(&lv_font_yora_weather_icons_36); // daily wx / посуточная погода
static const void* k_font_daily_pop    = reinterpret_cast<const void*>(&lv_font_yora_weather_metric_icons_22); // umbrella / зонт
static const void* k_font_hourly_icon  = reinterpret_cast<const void*>(&lv_font_yora_weather_icons_28);
static const void* k_font_hourly_pop   = reinterpret_cast<const void*>(&lv_font_yora_weather_metric_icons_22); // umbrella / зонт
static const void* k_font_daily_range  = reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr); // tmin° / tmax°
static const void* k_font_metric_icon  = reinterpret_cast<const void*>(&lv_font_yora_weather_metric_icons_26);
static const void* k_font_metric_value = reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr); // narrow cells / узкие ячейки
static const void* k_font_hero_temp  = reinterpret_cast<const void*>(&lv_font_yora_montserrat_40_cyr);
static const void* k_font_cond       = reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
static const void* k_font_small      = reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr);
static const void* k_font_caption    = reinterpret_cast<const void*>(&lv_font_yora_montserrat_12_cyr);

// Hero date: full line preferred; compact if caption font exceeds hero inner width.
// Дата hero: полная строка; компактная, если не влезает по ширине.
static void wx_format_hero_date(char* buf, size_t cap, const struct tm* tm, lv_coord_t max_text_w) {
    if (!buf || cap == 0) return;
    if (!wx_system_date_valid(tm)) {
        snprintf(buf, cap, "%s", kStrTodayOnly);
        return;
    }
    const int mon  = tm->tm_mon;
    const int wday = tm->tm_wday;
    if (mon < 0 || mon > 11 || wday < 0 || wday > 6) {
        snprintf(buf, cap, "%s", kStrTodayOnly);
        return;
    }

    char full[96];
    snprintf(full, sizeof(full),
             "Сегодня, %d %s %d, %s",
             tm->tm_mday, kRuMonthsGenitive[mon], tm->tm_year + 1900, kRuWeekdayLower[wday]);

    const lv_font_t* cap_font = static_cast<const lv_font_t*>(k_font_caption);
    if (max_text_w > 0) {
        const lv_coord_t fw = lv_txt_get_width(full, strlen(full), cap_font, 0, LV_TEXT_FLAG_NONE);
        if (fw > max_text_w) {
            snprintf(buf, cap, "%s, %d %s",
                     kRuWeekdayTitle[wday], tm->tm_mday, kRuMonthsGenitive[mon]);
            return;
        }
    }
    snprintf(buf, cap, "%s", full);
}

// A3.1f: local calendar Y-M-D from unix ts (localtime_r); not from HH:MM label text.
// A3.1f: локальная дата из unix ts (localtime_r); не из текста метки HH:MM.
static bool wx_ymd_from_unix(uint32_t unix_ts, int* y, int* m, int* d) {
    if (!y || !m || !d || unix_ts == 0u) return false;
    const time_t t = static_cast<time_t>(unix_ts);
    struct tm loc;
    if (localtime_r(&t, &loc) == nullptr) return false;
    *y = loc.tm_year;
    *m = loc.tm_mon;
    *d = loc.tm_mday;
    return true;
}

static bool wx_ymd_from_tm(const struct tm* tm, int* y, int* m, int* d) {
    if (!wx_system_date_valid(tm) || !y || !m || !d) return false;
    *y = tm->tm_year;
    *m = tm->tm_mon;
    *d = tm->tm_mday;
    return true;
}

static bool wx_ymd_equal(int y1, int m1, int d1, int y2, int m2, int d2) {
    return y1 == y2 && m1 == m2 && d1 == d2;
}

static int wx_ymd_cmp(int y1, int m1, int d1, int y2, int m2, int d2) {
    if (y1 != y2) return y1 - y2;
    if (m1 != m2) return m1 - m2;
    return d1 - d2;
}

// Tomorrow calendar date from today's tm (mktime + 86400, local TZ).
// Завтрашняя дата из today_tm (mktime + 86400, локальный TZ).
static bool wx_tomorrow_ymd(const struct tm* today_tm, int* y, int* m, int* d) {
    if (!wx_system_date_valid(today_tm) || !y || !m || !d) return false;
    struct tm t = *today_tm;
    time_t tt = mktime(&t);
    if (tt == static_cast<time_t>(-1)) return false;
    tt += 86400;
    struct tm tom;
    if (localtime_r(&tt, &tom) == nullptr) return false;
    *y = tom.tm_year;
    *m = tom.tm_mon;
    *d = tom.tm_mday;
    return true;
}

// A3.1f: day header for visible hourly[1..3] vs network.timeinfo local today.
// A3.1f: заголовок дня для видимых hourly[1..3] относительно network.timeinfo.
static void wx_format_hourly_day_header(char* buf, size_t cap, const struct tm* today_tm,
                                        const WeatherHourly* visible_slots, int slot_count) {
    if (!buf || cap == 0) return;
    if (!wx_system_date_valid(today_tm) || !visible_slots || slot_count <= 0) {
        snprintf(buf, cap, "%s", kStrHourlyNearest);
        return;
    }
    int ty = 0, tmon = 0, td = 0;
    int tmy = 0, tmm = 0, tmd = 0;
    if (!wx_ymd_from_tm(today_tm, &ty, &tmon, &td) || !wx_tomorrow_ymd(today_tm, &tmy, &tmm, &tmd)) {
        snprintf(buf, cap, "%s", kStrHourlyNearest);
        return;
    }

    bool has_today = false;
    bool has_tomorrow = false;
    bool has_later = false;
    int valid_count = 0;

    for (int i = 0; i < slot_count; ++i) {
        const WeatherHourly& h = visible_slots[i];
        if (!h.valid || h.ts == 0u) continue;
        int y = 0, m = 0, d = 0;
        if (!wx_ymd_from_unix(h.ts, &y, &m, &d)) continue;
        ++valid_count;
        if (wx_ymd_equal(y, m, d, ty, tmon, td)) {
            has_today = true;
        } else if (wx_ymd_equal(y, m, d, tmy, tmm, tmd)) {
            has_tomorrow = true;
        } else if (wx_ymd_cmp(y, m, d, tmy, tmm, tmd) > 0) {
            has_later = true;
        }
    }

    if (valid_count == 0) {
        snprintf(buf, cap, "%s", kStrHourlyNearest);
    } else if (has_today && !has_tomorrow && !has_later) {
        snprintf(buf, cap, "%s", kStrTodayOnly);
    } else if (has_tomorrow && !has_today && !has_later) {
        snprintf(buf, cap, "%s", kStrTomorrow);
    } else if (has_today && has_tomorrow) {
        snprintf(buf, cap, "%s", kStrTodaySlashTomorrow);
    } else if (has_tomorrow && has_later) {
        snprintf(buf, cap, "%s", kStrTomorrowSlashLater);
    } else {
        snprintf(buf, cap, "%s", kStrHourlyNearest);
    }
}

// A3.1g/1h: local DD.MM fragment. / Локальный фрагмент DD.MM.
static void wx_format_dd_mm_local(char* buf, size_t cap, const struct tm* loc) {
    if (!buf || cap == 0 || !loc) return;
    snprintf(buf, cap, "%02d.%02d", loc->tm_mday, loc->tm_mon + 1);
}

// A3.1h: daily card date — short weekday + DD.MM (e.g. Чт 18.06).
// A3.1h: дата карточки — краткий день недели + DD.MM.
static void wx_format_daily_weekday_date(char* buf, size_t cap, const struct tm* loc) {
    if (!buf || cap == 0 || !loc) return;
    const int wday = loc->tm_wday;
    if (wday < 0 || wday > 6) {
        wx_format_dd_mm_local(buf, cap, loc);
        return;
    }
    snprintf(buf, cap, "%s %02d.%02d",
             kRuWeekdayShort[wday], loc->tm_mday, loc->tm_mon + 1);
}

// W-R2: forecast-location calendar from day_ts + OWM timezone (not device localtime_r).
// W-R2: календарь локации прогноза из day_ts + OWM timezone (не device localtime_r).
static bool wx_forecast_loc_tm_from_day_ts(uint32_t day_ts, int32_t forecast_tz_sec, struct tm* out) {
    if (!out || day_ts == 0u) return false;
    const int64_t shifted = static_cast<int64_t>(day_ts) + static_cast<int64_t>(forecast_tz_sec);
    if (shifted < 0) return false;
    const time_t t = static_cast<time_t>(shifted);
    return gmtime_r(&t, out) != nullptr;
}

static void wx_format_daily_date_label(char* buf, size_t cap, uint32_t day_ts,
                                       int32_t forecast_tz_sec) {
    if (!buf || cap == 0) return;
    struct tm loc;
    if (wx_forecast_loc_tm_from_day_ts(day_ts, forecast_tz_sec, &loc)) {
        wx_format_daily_weekday_date(buf, cap, &loc);
        return;
    }
    buf[0] = '\0';
}

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

// A3: stronger flat panel — readable on real RGB panel; still theme tokens, no gradient/shadow.
// A3: более заметная плоская панель на реальном дисплее; токены темы, без градиента/тени.
static void wx_style_panel(lv_obj_t* o, const YoRadioPalette& pal) {
    lv_obj_set_style_bg_color(o, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_border_opa(o, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
}

// A3.1B: calm leading inset — full-width hero row without icon flush to panel edge.
// A3.1B: спокойный левый отступ — полная ширина hero без иконки у края панели.
static constexpr lv_coord_t kHeroInnerPadLeft = 16;

// A3.1B: internal daily column separator — right border only on day 0/1 cells.
// A3.1B: внутренний разделитель колонок — только правый border на ячейках 0/1.
static void wx_style_daily_internal_separator(lv_obj_t* cell, const YoRadioPalette& pal) {
    if (!cell) return;
    lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_border_color(cell, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_border_opa(cell, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
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

// Fixed slot heights — keep icon / value / label rows aligned across all 4 metric cells.
// Фиксированные высоты слотов — иконки и подписи на одной линии во всех ячейках.
static constexpr lv_coord_t k_metric_icon_slot_h  = 26;
static constexpr lv_coord_t k_metric_value_slot_h = 18;
static constexpr lv_coord_t k_metric_label_slot_h = 17;

// Fixed-height centered slot inside a metric cell. / Слот фиксированной высоты по центру.
static lv_obj_t* add_metric_slot(lv_obj_t* cell, lv_coord_t slot_h, const void* font,
                                 lv_color_t col, lv_label_long_mode_t long_mode,
                                 bool label_fit_content, lv_obj_t** out_lbl) {
    if (!cell) return nullptr;
    lv_obj_t* slot = lv_obj_create(cell);
    if (!slot) return nullptr;
    wx_flat_base(slot);
    lv_obj_set_width(slot, LV_PCT(100));
    lv_obj_set_height(slot, slot_h);
    lv_obj_set_style_min_height(slot, slot_h, LV_PART_MAIN);
    lv_obj_set_style_max_height(slot, slot_h, LV_PART_MAIN);
    lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* l = lv_label_create(slot);
    if (l) {
        lv_label_set_text(l, "--");
        wx_set_font(l, font);
        lv_obj_set_style_text_color(l, col, LV_PART_MAIN);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(l, label_fit_content ? LV_SIZE_CONTENT : LV_PCT(100));
        lv_label_set_long_mode(l, long_mode);
    }
    if (out_lbl) *out_lbl = l;
    return slot;
}

// A3: equal-width column (flex_grow 1) — four cells span full hero metrics row.
// A3: колонка равной ширины (flex_grow 1) — 4 ячейки на всю ширину hero.
static void add_metric_cell(lv_obj_t* row, const char* icon_glyph, const char* label_text,
                            lv_obj_t** out_val, lv_obj_t** out_lbl, const YoRadioPalette& pal) {
    if (!row) return;
    lv_obj_t* cell = lv_obj_create(row);
    if (!cell) return;
    wx_flat_base(cell);
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_style_min_width(cell, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(cell, 0, LV_PART_MAIN);

    lv_obj_t* icon_lbl = nullptr;
    add_metric_slot(cell, k_metric_icon_slot_h, k_font_metric_icon, pal.text_secondary,
                    LV_LABEL_LONG_CLIP, false, &icon_lbl);
    if (icon_lbl && icon_glyph) {
        lv_label_set_text(icon_lbl, icon_glyph);
    }
    add_metric_slot(cell, k_metric_value_slot_h, k_font_metric_value, pal.text_primary,
                    LV_LABEL_LONG_CLIP, false, out_val);
    lv_obj_t* cap_lbl = nullptr;
    // A3.1a: caption uses content width — avoids rounding clip on «Влажность» (ь).
    // A3.1a: подпись по ширине текста — без обрезки «ь» у «Влажность».
    add_metric_slot(cell, k_metric_label_slot_h, k_font_caption, pal.text_secondary,
                    LV_LABEL_LONG_CLIP, true, &cap_lbl);
    if (cap_lbl && label_text) {
        lv_label_set_text(cap_lbl, label_text);
        // Single-line captions only — no wrap inside narrow flex columns.
        // Подписи только в одну строку — без переноса внутри колонки.
        lv_obj_set_style_text_line_space(cap_lbl, 0, LV_PART_MAIN);
    }
    if (out_lbl) *out_lbl = cap_lbl;
}

// Hourly slot label: wall-clock HH:00 from OWM unix ts (local TZ); fallback to +N ч.
// Подпись слота: локальные часы HH:00 из unix ts; иначе относительный fallback (+3 ч).
static void wx_format_hour_slot_label(char* buf, size_t cap, uint32_t forecast_unix_ts,
                                      const char* relative_fallback) {
    if (!buf || cap == 0) return;
    if (forecast_unix_ts == 0u) {
        snprintf(buf, cap, "%s", relative_fallback ? relative_fallback : "--");
        return;
    }
    const time_t t = static_cast<time_t>(forecast_unix_ts);
    struct tm tm_loc;
    if (localtime_r(&t, &tm_loc) != nullptr) {
        snprintf(buf, cap, "%02u:00", static_cast<unsigned>(tm_loc.tm_hour));
        return;
    }
    snprintf(buf, cap, "%s", relative_fallback ? relative_fallback : "--");
}

// Equalize hero + hourly panel heights after natural layout (LVGL 8 has no cross-axis STRETCH).
// Выравниваем высоту hero и почасовой панели после естественного layout.
static void wx_sync_top_row_heights(lv_obj_t* hero, lv_obj_t* hourly) {
    if (!hero || !hourly) return;
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_height(hourly, LV_SIZE_CONTENT);
    lv_obj_update_layout(hero);
    lv_obj_update_layout(hourly);
    const lv_coord_t hh = lv_obj_get_height(hero);
    const lv_coord_t oh = lv_obj_get_height(hourly);
    const lv_coord_t h  = (hh > oh) ? hh : oh;
    if (h <= 0) return;
    lv_obj_set_height(hero, h);
    lv_obj_set_height(hourly, h);
    lv_obj_update_layout(hero);
    lv_obj_update_layout(hourly);
}

// A3.1j: synced top row + small bump over pre-A3.1i baseline (hero/hourly readable).
// A3.1j: выравнивание top row + небольшой прирост над baseline до A3.1i.
static constexpr lv_coord_t kWeatherTopRowExtraH = 10;

static void wx_finalize_top_row_heights(lv_obj_t* hero, lv_obj_t* hourly) {
    wx_sync_top_row_heights(hero, hourly);
    if (kWeatherTopRowExtraH <= 0) return;
    if (hero) {
        const lv_coord_t h = lv_obj_get_height(hero);
        if (h > 0) lv_obj_set_height(hero, h + kWeatherTopRowExtraH);
    }
    if (hourly) {
        const lv_coord_t h = lv_obj_get_height(hourly);
        if (h > 0) lv_obj_set_height(hourly, h + kWeatherTopRowExtraH);
    }
}

// A3b: hourly row — time (left, wide gap) | compact group: wx icon | temp | umbrella | pop%.
// A3b: строка — время слева с отступом | группа: погода | t° | зонт | %.
static lv_obj_t* add_hourly_row(lv_obj_t* col, const YoRadioPalette& pal,
                                lv_obj_t** out_time, lv_obj_t** out_icon,
                                lv_obj_t** out_temp, lv_obj_t** out_pop_icon,
                                lv_obj_t** out_pop) {
    lv_obj_t* row = lv_obj_create(col);
    if (!row) return nullptr;
    wx_flat_base(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 0, LV_PART_MAIN);

    lv_obj_t* time = lv_label_create(row);
    if (time) {
        lv_label_set_text(time, "--");
        wx_set_font(time, k_font_caption);
        lv_obj_set_style_text_color(time, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_min_width(time, 40, LV_PART_MAIN);
        lv_obj_set_style_pad_right(time, 3, LV_PART_MAIN); // gap after time / отступ после времени (was 7)
        lv_label_set_long_mode(time, LV_LABEL_LONG_CLIP);
    }

    // Tight cluster: weather icon + temp + umbrella + % (small internal gaps).
    // Плотная группа: иконка погоды + t° + зонт + %.
    lv_obj_t* group = lv_obj_create(row);
    if (group) {
        wx_flat_base(group);
        lv_obj_set_height(group, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(group, 1);
        lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(group, 2, LV_PART_MAIN);
    }

    auto make_in_group = [&](const void* font, lv_color_t col) -> lv_obj_t* {
        if (!group) return nullptr;
        lv_obj_t* l = lv_label_create(group);
        if (!l) return nullptr;
        lv_label_set_text(l, "--");
        wx_set_font(l, font);
        lv_obj_set_style_text_color(l, col, LV_PART_MAIN);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        return l;
    };

    lv_obj_t* icon     = make_in_group(k_font_hourly_icon, pal.status_weather_icon);
    lv_obj_t* temp     = make_in_group(k_font_small, pal.text_primary);
    lv_obj_t* pop_icon = make_in_group(k_font_hourly_pop, pal.text_secondary);
    if (pop_icon) {
        lv_label_set_text(pop_icon, YORA_WEATHER_METRIC_GLYPH_UMBRELLA);
    }
    lv_obj_t* pop = make_in_group(k_font_caption, pal.text_secondary);

    if (out_time)     *out_time     = time;
    if (out_icon)     *out_icon     = icon;
    if (out_temp)     *out_temp     = temp;
    if (out_pop_icon) *out_pop_icon = pop_icon;
    if (out_pop)      *out_pop      = pop;
    return row;
}

// A3.1h: daily forecast card — date | wx icon | tmin° / tmax° | umbrella + pop%.
// A3.1h: карточка прогноза — дата | иконка | tmin° / tmax° | зонт + %.
static lv_obj_t* add_daily_cell(lv_obj_t* row, const YoRadioPalette& pal,
                                lv_obj_t** out_day, lv_obj_t** out_icon,
                                lv_obj_t** out_range, lv_obj_t** out_pop_icon,
                                lv_obj_t** out_pop) {
    lv_obj_t* cell = lv_obj_create(row);
    if (!cell) return nullptr;
    wx_flat_base(cell);
    lv_obj_set_flex_grow(cell, 1);
    lv_obj_set_height(cell, LV_PCT(100)); // A3.1i: stretch with daily panel height / растягиваем с панелью
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 5, LV_PART_MAIN); // A3.1i: 3→5 — calmer vertical rhythm / ритм по высоте
    lv_obj_set_style_pad_top(cell, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(cell, 6, LV_PART_MAIN);

    auto make_label = [&](const void* font, lv_color_t col) -> lv_obj_t* {
        lv_obj_t* l = lv_label_create(cell);
        if (!l) return nullptr;
        lv_label_set_text(l, "");
        wx_set_font(l, font);
        lv_obj_set_style_text_color(l, col, LV_PART_MAIN);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        return l;
    };

    lv_obj_t* day   = make_label(k_font_caption, pal.text_secondary);
    lv_obj_t* icon  = make_label(k_font_daily_icon, pal.status_weather_icon);
    lv_obj_t* range = make_label(k_font_daily_range, pal.text_primary);

    // Precipitation row: umbrella glyph + percent (centered cluster).
    // Строка осадков: зонт + процент (центрированная группа).
    lv_obj_t* pop_row = lv_obj_create(cell);
    if (pop_row) {
        wx_flat_base(pop_row);
        lv_obj_set_height(pop_row, LV_SIZE_CONTENT);
        lv_obj_set_width(pop_row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(pop_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pop_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pop_row, 2, LV_PART_MAIN);
    }

    lv_obj_t* pop_icon = nullptr;
    lv_obj_t* pop      = nullptr;
    if (pop_row) {
        pop_icon = lv_label_create(pop_row);
        if (pop_icon) {
            lv_label_set_text(pop_icon, YORA_WEATHER_METRIC_GLYPH_UMBRELLA);
            wx_set_font(pop_icon, k_font_daily_pop);
            lv_obj_set_style_text_color(pop_icon, pal.text_secondary, LV_PART_MAIN);
            lv_label_set_long_mode(pop_icon, LV_LABEL_LONG_CLIP);
        }
        pop = lv_label_create(pop_row);
        if (pop) {
            lv_label_set_text(pop, "");
            wx_set_font(pop, k_font_caption);
            lv_obj_set_style_text_color(pop, pal.text_secondary, LV_PART_MAIN);
            lv_label_set_long_mode(pop, LV_LABEL_LONG_CLIP);
        }
    }

    if (out_day)      *out_day      = day;
    if (out_icon)     *out_icon     = icon;
    if (out_range)    *out_range    = range;
    if (out_pop_icon) *out_pop_icon = pop_icon;
    if (out_pop)      *out_pop      = pop;
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

// A2b: clickable footer pill — slightly stronger fill/border; pressed state via theme tokens.
// A2b: кликабельный footer-pill — чуть сильнее заливка/рамка; pressed через токены темы.
static void wx_style_footer_pill_clickable(lv_obj_t* o, const YoRadioPalette& pal) {
    wx_style_footer_pill(o, pal);
    lv_obj_set_style_bg_opa(o, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(o, pal.text_meta, LV_STATE_PRESSED);
}

// W-R3: terminal fetch errors → Unavailable without payload / терминальные ошибки → Unavailable.
static bool wx_is_terminal_weather_error(WeatherLastError err) {
    return err == WeatherLastError::FetchFailed ||
           err == WeatherLastError::NotConfigured ||
           err == WeatherLastError::NotConnected;
}

// Relative age fragment — «мин. назад» / «ч. назад» (normal RU abbreviations).
// Фрагмент возраста — обычные сокращения «мин.» / «ч.» + «назад».
static void wx_format_age(char* buf, size_t cap, uint32_t updated_at_ms) {
    if (!buf || cap == 0) return;
    const uint32_t age_min = (millis() - updated_at_ms) / 60000u;
    if (age_min == 0u)        snprintf(buf, cap, "Обновлено только что");
    else if (age_min < 60u)   snprintf(buf, cap, "Обновлено %u мин. назад", (unsigned)age_min);
    else                      snprintf(buf, cap, "Обновлено %u ч. назад", (unsigned)(age_min / 60u));
}

// A2b/A3.2: footer segments joined via kStrFooterSep; trailing sep for circular scroll gap.
// A2b/A3.2: сегменты футера через kStrFooterSep; завершающий sep — зазор при круговой прокрутке.

// A4.1: location prefix — "" | "city" | "city, country" (UTF-8 byte-safe, bounded).
// A4.1: префикс локации — "" | "город" | "город, CC".
static void wx_format_location_prefix(char* buf, size_t cap, const WeatherLocation& loc) {
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    if (!loc.valid || loc.city[0] == '\0') return;
    if (loc.country[0] != '\0') {
        snprintf(buf, cap, "%s, %s", loc.city, loc.country);
    } else {
        strlcpy(buf, loc.city, cap);
    }
}

// A4.1: prepend location + separator when prefix is non-empty.
// A4.1: добавить локацию + разделитель, если префикс не пустой.
static void wx_footer_prepend_location(char* out, size_t out_cap,
                                       const char* location_prefix,
                                       const char* body) {
    if (!out || out_cap == 0) return;
    if (!location_prefix || location_prefix[0] == '\0') {
        strlcpy(out, body, out_cap);
    } else {
        snprintf(out, out_cap, "%s%s%s", location_prefix, kStrFooterSep, body);
    }
}

static void wx_format_footer_action(char* buf, size_t cap, const char* status, const char* action,
                                    const char* location_prefix) {
    if (!buf || cap == 0) return;
    char inner[kFooterTextCap];
    // E33: no trailing sep here — caller appends it only when text overflows.
    // E33: trailing sep убран — вызывающий добавит его только при переполнении.
    snprintf(inner, sizeof(inner), "%s%s%s", status, kStrFooterSep, action);
    wx_footer_prepend_location(buf, cap, location_prefix, inner);
}

static void wx_format_footer(char* buf, size_t cap, bool wx_enabled, bool have_data,
                             bool show_refreshing, bool effective_stale,
                             uint32_t forecast_updated_at_ms, bool unavailable_no_data,
                             const WeatherLocation* loc) {
    if (!buf || cap == 0) return;

    char location_prefix[72];
    location_prefix[0] = '\0';
    // A4.1: location only on payload-backed footer states (have_data).
    // A4.1: локация только при наличии payload (have_data).
    if (have_data && loc) {
        wx_format_location_prefix(location_prefix, sizeof(location_prefix), *loc);
    }
    const char* loc_p = (location_prefix[0] != '\0') ? location_prefix : nullptr;

    if (show_refreshing) {
        // E33: no trailing sep — refreshing text is often short and fits without scroll.
        // E33: trailing sep убран; "Обновление погоды..." короткое и обычно не скроллируется.
        wx_footer_prepend_location(buf, cap, loc_p, kStrFooterRefreshing);
        return;
    }
    if (!wx_enabled) {
        wx_format_footer_action(buf, cap, kStrWeatherUnavail, kStrFooterTapRetry, nullptr);
        return;
    }
    if (!have_data) {
        if (unavailable_no_data) {
            wx_format_footer_action(buf, cap, kStrTemporarilyUnavailable, kStrFooterTapRetry, nullptr);
        } else {
            wx_format_footer_action(buf, cap, kStrForecastNotLoaded, kStrFooterTapRefresh, nullptr);
        }
        return;
    }
    if (effective_stale) {
        wx_format_footer_action(buf, cap, kStrDataMayBeOutdated, kStrFooterTapRefresh, loc_p);
        return;
    }
    char age[kFooterAgeCap];
    wx_format_age(age, sizeof(age), forecast_updated_at_ms);
    char inner[kFooterTextCap];
    // E33: no trailing sep — appended by caller only if text overflows the label.
    // E33: trailing sep убран — вызывающий добавит только если текст переполняет label.
    snprintf(inner, sizeof(inner), "%s%s%s", age, kStrFooterSep, kStrFooterTapRefresh);
    wx_footer_prepend_location(buf, cap, loc_p, inner);
}

// E33: append trailing kStrFooterSep only when base text overflows the footer label width.
// Circular scroll needs the gap between repeated copies; static text does not.
// E33: trailing sep добавляется только при переполнении: circular scroll нужен зазор;
// статический текст, который влезает, не нуждается в trailing bullet.
static void wx_footer_maybe_add_trailing_sep(char* buf, size_t cap, lv_obj_t* label) {
    if (!buf || !label || cap < 2u) return;
    const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (!font) return;
    const lv_coord_t letter_space = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
    const lv_coord_t avail = lv_obj_get_content_width(label);
    if (avail <= 0) return;  // layout not yet valid — skip, no trailing sep
    const size_t len = strlen(buf);
    const lv_coord_t text_w = lv_txt_get_width(
        buf, static_cast<uint32_t>(len), font, letter_space, LV_TEXT_FLAG_NONE);
    if (text_w > avail) {
        strlcat(buf, kStrFooterSep, cap);
    }
}

} // namespace

void LvglWeatherPage::_onFooterRefreshClick(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWeatherPage*>(lv_event_get_user_data(e));
    if (!self || !self->_lbl_footer) return;

    const uint32_t now = millis();
    if (now - self->_last_refresh_tap_ms < LvglWeatherPage::kRefreshTapThrottleMs) {
        return; // throttle spam / антиспам тапов
    }
    self->_last_refresh_tap_ms = now;
    self->_manual_refresh_pending = true;
    self->_refresh_pending_since_ms = now;

    // Snapshot version at request time — update() clears pending when it changes.
    // Версия на момент запроса — update() сбросит pending при изменении.
    WeatherState snap{};
    if (weatherGetStateSnapshot(&snap)) {
        self->_refresh_watch_version = snap.version;
    } else {
        self->_refresh_watch_version = 0;
    }

    weatherRequestManualRefresh(); // flag only — no HTTP in LVGL / только флаг, без HTTP
    char fb[kFooterTextCap];
    const bool wxEnabled = config.store.showweather && (strlen(config.store.weatherkey) > 0);
    const bool haveData = snap.forecast_valid && snap.current.valid;
    wx_format_footer(fb, sizeof(fb), wxEnabled, haveData, true, false,
                     snap.forecast_updated_at, false, &snap.location);
    wx_footer_maybe_add_trailing_sep(fb, sizeof(fb), self->_lbl_footer);
    wx_set_text_if_changed(self->_lbl_footer, fb);
}

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
    lv_obj_set_flex_align(_body_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_body_area, 4, LV_PART_MAIN); // A3.1i: 8→4 — tighter to footer / ближе к футеру

    // ── A2: Data block — hidden until forecast_valid ──────────────────────────
    // Layout: [top ROW: fixed-height hero+hourly] / [divider] / [daily — flex-grow to footer]
    // A3.1j: top row content-sized (flex_grow=0); daily absorbs slack — не сжимает hero/hourly.
    // Раскладка: [hero+hourly фикс. высота] / [разделитель] / [daily flex-grow до футера]
    _cont_data = lv_obj_create(_body_area);
    if (_cont_data) {
        wx_flat_base(_cont_data);
        lv_obj_set_width(_cont_data, LV_PCT(100));
        lv_obj_set_height(_cont_data, LV_PCT(100)); // A3.1i: fill body, not content-only / на всю body_area
        lv_obj_set_flex_grow(_cont_data, 1);
        lv_obj_set_flex_flow(_cont_data, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_cont_data, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(_cont_data, 6, LV_PART_MAIN); // A3.1i: 8→6

        // ── Top row — hero (left) + hourly (right); bounded height, not flex-grown ──
        // Верхний ROW: hero + hourly; фиксированная высота, без flex_grow.
        _cont_top = lv_obj_create(_cont_data);
        if (_cont_top) {
            wx_flat_base(_cont_top);
            lv_obj_set_width(_cont_top, LV_PCT(100));
            lv_obj_set_height(_cont_top, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(_cont_top, 0); // A3.1j: no slack steal from daily / не забирать slack у daily
            lv_obj_set_flex_flow(_cont_top, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_cont_top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_column(_cont_top, 4, LV_PART_MAIN);

            // Left: current weather card (icon, temp, condition, feels, metrics).
            // Левая карточка: иконка, температура, условие, ощущается, метрики.
            _cont_hero = lv_obj_create(_cont_top);
            if (_cont_hero) {
                wx_flat_base(_cont_hero, false);
                wx_style_panel(_cont_hero, pal);
                lv_obj_set_height(_cont_hero, LV_SIZE_CONTENT);
                // A3.1d: hero:hourly 16:9 — middle between 17:8 (A3.1b) and 15:10 (A3.1c).
                // A3.1d: hero:hourly 16:9 — середина между 17:8 и 15:10.
                lv_obj_set_flex_grow(_cont_hero, 16);
                lv_obj_set_flex_flow(_cont_hero, LV_FLEX_FLOW_COLUMN);
                // A3.1a: cross-axis CENTER — date + hero_inner group centered in card.
                // A3.1a: cross-axis CENTER — дата и блок icon+temp по центру карточки.
                lv_obj_set_flex_align(_cont_hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_top(_cont_hero, 8, LV_PART_MAIN);
                lv_obj_set_style_pad_bottom(_cont_hero, 8, LV_PART_MAIN);
                lv_obj_set_style_pad_left(_cont_hero, 2, LV_PART_MAIN);   // A3.1b: 4→2 — metrics closer to edges
                lv_obj_set_style_pad_right(_cont_hero, 2, LV_PART_MAIN);  // A3.1b: 4→2 — не вплотную к border
                lv_obj_set_style_pad_row(_cont_hero, 4, LV_PART_MAIN);

                // A3.1: today date from network.timeinfo (NTP/local TZ); no weather API date.
                // A3.1: строка «Сегодня …» из network.timeinfo (NTP/локальный TZ); не из API погоды.
                _lbl_hero_date = lv_label_create(_cont_hero);
                if (_lbl_hero_date) {
                    lv_label_set_text(_lbl_hero_date, kStrTodayOnly);
                    wx_set_font(_lbl_hero_date, k_font_caption);
                    lv_obj_set_style_text_color(_lbl_hero_date, pal.text_meta, LV_PART_MAIN);
                    lv_obj_set_width(_lbl_hero_date, LV_PCT(100));
                    lv_label_set_long_mode(_lbl_hero_date, LV_LABEL_LONG_CLIP);
                    lv_obj_set_style_text_align(_lbl_hero_date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                }

                // A3.1B: full-width hero row — icon + flex-grown text column for wide condition line.
                // A3.1B: hero на всю ширину — иконка + текстовая колонка с flex_grow для условия.
                lv_obj_t* hero_inner = lv_obj_create(_cont_hero);
                if (hero_inner) {
                    wx_flat_base(hero_inner);
                    lv_obj_set_width(hero_inner, LV_PCT(100));
                    lv_obj_set_height(hero_inner, LV_SIZE_CONTENT);
                    lv_obj_set_flex_flow(hero_inner, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(hero_inner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_left(hero_inner, kHeroInnerPadLeft, LV_PART_MAIN);
                    lv_obj_set_style_pad_column(hero_inner, 10, LV_PART_MAIN);

                    _lbl_hero_icon = lv_label_create(hero_inner);
                    if (_lbl_hero_icon) {
                        lv_label_set_text(_lbl_hero_icon, "");
                        wx_set_font(_lbl_hero_icon, k_font_hero_icon);
                        lv_obj_set_style_text_color(_lbl_hero_icon, pal.status_weather_icon, LV_PART_MAIN);
                        lv_obj_set_flex_grow(_lbl_hero_icon, 0);
                    }

                    lv_obj_t* hero_text = lv_obj_create(hero_inner);
                    if (hero_text) {
                        wx_flat_base(hero_text);
                        lv_obj_set_width(hero_text, LV_SIZE_CONTENT);
                        lv_obj_set_height(hero_text, LV_SIZE_CONTENT);
                        lv_obj_set_flex_grow(hero_text, 1);
                        lv_obj_set_style_min_width(hero_text, 0, LV_PART_MAIN);
                        lv_obj_set_flex_flow(hero_text, LV_FLEX_FLOW_COLUMN);
                        lv_obj_set_flex_align(hero_text, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                        lv_obj_set_style_pad_row(hero_text, 2, LV_PART_MAIN);

                        _lbl_hero_temp = lv_label_create(hero_text);
                        if (_lbl_hero_temp) {
                            lv_label_set_text(_lbl_hero_temp, "--");
                            wx_set_font(_lbl_hero_temp, k_font_hero_temp);
                            lv_obj_set_style_text_color(_lbl_hero_temp, pal.text_primary, LV_PART_MAIN);
                            lv_obj_set_style_text_align(_lbl_hero_temp, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                        }
                        _lbl_hero_cond = lv_label_create(hero_text);
                        if (_lbl_hero_cond) {
                            lv_label_set_text(_lbl_hero_cond, "");
                            // A3.1B: bounded width + circular scroll on overflow only (Main/Info pattern).
                            // A3.1B: фиксированная ширина + круговой скролл только при переполнении.
                            lv_label_set_long_mode(_lbl_hero_cond, LV_LABEL_LONG_SCROLL_CIRCULAR);
                            lv_obj_set_width(_lbl_hero_cond, LV_PCT(100));
                            wx_set_font(_lbl_hero_cond, k_font_cond);
                            lv_obj_set_style_text_color(_lbl_hero_cond, pal.text_secondary, LV_PART_MAIN);
                            lv_obj_set_style_text_align(_lbl_hero_cond, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                        }
                        _lbl_hero_feels = lv_label_create(hero_text);
                        if (_lbl_hero_feels) {
                            lv_label_set_text(_lbl_hero_feels, "");
                            wx_set_font(_lbl_hero_feels, k_font_small);
                            lv_obj_set_style_text_color(_lbl_hero_feels, pal.text_meta, LV_PART_MAIN);
                            lv_obj_set_style_text_align(_lbl_hero_feels, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                        }
                    }
                }

                // Metrics row: 4 equal columns edge-to-edge; reduced pad for wider cells.
                // Строка метрик: 4 колонки на всю ширину; меньше pad — шире ячейки.
                _cont_metrics = lv_obj_create(_cont_hero);
                if (_cont_metrics) {
                    wx_flat_base(_cont_metrics);
                    lv_obj_set_width(_cont_metrics, LV_PCT(100));
                    lv_obj_set_height(_cont_metrics, LV_SIZE_CONTENT);
                    lv_obj_set_flex_flow(_cont_metrics, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(_cont_metrics, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                    lv_obj_set_style_pad_top(_cont_metrics, 4, LV_PART_MAIN);
                    lv_obj_set_style_pad_column(_cont_metrics, 2, LV_PART_MAIN); // A3.1c: 3→2 — hero narrower, protect labels
                    lv_obj_set_style_pad_left(_cont_metrics, 0, LV_PART_MAIN);
                    lv_obj_set_style_pad_right(_cont_metrics, 0, LV_PART_MAIN);
                    add_metric_cell(_cont_metrics, YORA_WEATHER_METRIC_GLYPH_WIND,     kStrMetricWind,
                                    &_val_wind,     &_lbl_wind,     pal);
                    add_metric_cell(_cont_metrics, YORA_WEATHER_METRIC_GLYPH_HUMIDITY, kStrMetricHumidity,
                                    &_val_humidity, &_lbl_humidity, pal);
                    add_metric_cell(_cont_metrics, YORA_WEATHER_METRIC_GLYPH_PRESSURE, kStrMetricPressure,
                                    &_val_pressure, &_lbl_pressure, pal);
                    add_metric_cell(_cont_metrics, YORA_WEATHER_METRIC_GLYPH_UMBRELLA, kStrMetricRain,
                                    &_val_rain,     &_lbl_rain,     pal);
                }
            }

            // Right: 3 horizontal hourly rows (time | icon | temp | pop%), height matches hero card.
            // Справа: 3 горизонтальные строки прогноза; высота панели = hero.
            _cont_hourly = lv_obj_create(_cont_top);
            if (_cont_hourly) {
                wx_flat_base(_cont_hourly, false);
                wx_style_panel(_cont_hourly, pal);
                lv_obj_set_height(_cont_hourly, LV_SIZE_CONTENT);
                lv_obj_set_flex_grow(_cont_hourly, 9); // A3.1d: 10→9 — pair with hero 16 (ratio 16:9)
                lv_obj_set_flex_flow(_cont_hourly, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(_cont_hourly, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_all(_cont_hourly, 4, LV_PART_MAIN); // A3.1c: 6→4 — more inner content width
                lv_obj_set_style_pad_row(_cont_hourly, 1, LV_PART_MAIN); // A3.1f: 2→1 — room for day header

                // A3.1f: day context header — same style as hero date line.
                // A3.1f: заголовок дня — тот же стиль, что строка даты в hero.
                _lbl_hourly_day = lv_label_create(_cont_hourly);
                if (_lbl_hourly_day) {
                    lv_label_set_text(_lbl_hourly_day, kStrHourlyNearest);
                    wx_set_font(_lbl_hourly_day, k_font_caption);
                    lv_obj_set_style_text_color(_lbl_hourly_day, pal.text_meta, LV_PART_MAIN);
                    lv_obj_set_width(_lbl_hourly_day, LV_PCT(100));
                    lv_label_set_long_mode(_lbl_hourly_day, LV_LABEL_LONG_CLIP);
                    lv_obj_set_style_text_align(_lbl_hourly_day, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                }

                for (int i = 0; i < kHourlyCells; ++i) {
                    _hourly[i].cont = add_hourly_row(_cont_hourly, pal,
                        &_hourly[i].time, &_hourly[i].icon, &_hourly[i].temp,
                        &_hourly[i].pop_icon, &_hourly[i].pop);
                }
            }

            // A3.1j: content-based equal heights + kWeatherTopRowExtraH (restores pre-A3.1i + bump).
            // A3.1j: равная высота по контенту + kWeatherTopRowExtraH (восстановление + прирост).
            wx_finalize_top_row_heights(_cont_hero, _cont_hourly);
        } // _cont_top

        _div_mid = add_thin_divider(_cont_data, pal);

        // Bottom: daily forecast panel (weekday+date cards; slot 0 = today skipped — in hero).
        // Нижний блок: карточки с датой; слот 0 «сегодня» — в hero.
        _cont_daily = lv_obj_create(_cont_data);
        if (_cont_daily) {
            wx_flat_base(_cont_daily, false);
            wx_style_panel(_cont_daily, pal);
            lv_obj_set_width(_cont_daily, LV_PCT(100));
            lv_obj_set_height(_cont_daily, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(_cont_daily, 1); // A3.1j: sole slack absorber toward footer / только daily тянется
            lv_obj_set_flex_flow(_cont_daily, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_cont_daily, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(_cont_daily, 8, LV_PART_MAIN);
            lv_obj_set_style_pad_column(_cont_daily, 4, LV_PART_MAIN);
            for (int i = 0; i < kDailyCells; ++i) {
                _daily[i].cont = add_daily_cell(_cont_daily, pal,
                    &_daily[i].day, &_daily[i].icon, &_daily[i].range,
                    &_daily[i].pop_icon, &_daily[i].pop);
            }
            // A3.1B: two internal column dividers — day 0/1 right border only; day 2 plain.
            // A3.1B: два внутренних разделителя — правый border только у day 0/1.
            if (_daily[0].cont) wx_style_daily_internal_separator(_daily[0].cont, pal);
            if (_daily[1].cont) wx_style_daily_internal_separator(_daily[1].cont, pal);
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
            lv_label_set_text(_lbl_message, kStrForecastWaiting);
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
            wx_style_footer_pill_clickable(_footer_box, pal);
            lv_obj_set_width(_footer_box, LV_PCT(100));
            lv_obj_set_height(_footer_box, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_footer_box, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(_footer_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_add_flag(_footer_box, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(_footer_box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(_footer_box, _onFooterRefreshClick, LV_EVENT_CLICKED, this);

            _lbl_footer = lv_label_create(_footer_box);
            if (_lbl_footer) {
                {
                    char fb[kFooterTextCap];
                    wx_format_footer_action(fb, sizeof(fb), kStrForecastNotLoaded, kStrFooterTapRefresh, nullptr);
                    lv_label_set_text(_lbl_footer, fb);
                }
                wx_set_font(_lbl_footer, k_font_cond);
                lv_obj_set_style_text_color(_lbl_footer, pal.text_secondary, LV_PART_MAIN);
                lv_obj_set_style_text_align(_lbl_footer, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                // A3.2: full-width circular scroll on overflow; tap stays on _footer_box.
                // A3.2: полная ширина + круговой скролл при переполнении; тап на _footer_box.
                lv_label_set_long_mode(_lbl_footer, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_obj_set_width(_lbl_footer, LV_PCT(100));
                lv_obj_set_flex_grow(_lbl_footer, 1);
                lv_obj_set_style_min_width(_lbl_footer, 0, LV_PART_MAIN);
                lv_obj_clear_flag(_lbl_footer, LV_OBJ_FLAG_CLICKABLE);
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
    // A3.2 perf: invalidate cache so the first update() after enter is always a full render.
    // A3.2 perf: сброс кэша — первый update() после входа всегда полный рендер.
    _render_cache_valid = false;
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
    // E34 corrective: body presentation requires both feature enabled and valid LKG payload.
    // E34 corrective: тело страницы — только при включённой погоде и валидном LKG.
    const bool showData   = wxEnabled && haveData;
    const bool terminalError = wx_is_terminal_weather_error(s_snap.last_error);
    const bool loadingWithoutData =
        !haveData &&
        (s_snap.fetch_in_progress || s_snap.last_error == WeatherLastError::InternalLow);
    const bool unavailableWithoutData = !haveData && terminalError;
    const uint32_t ageMs = haveData ? (millis() - s_snap.forecast_updated_at) : 0u;
    const bool staleByAge =
        haveData && s_snap.forecast_updated_at != 0u && ageMs > WEATHER_STALE_AFTER_MS;
    const bool effectiveStale = haveData && (s_snap.stale || staleByAge);
    const bool showRefreshingFooter =
        haveData && (_manual_refresh_pending || s_snap.fetch_in_progress);

#if YORADIO_WEATHER_UI_DIAG
    // Log once per published version when data is present — not every 1 Hz frame (avoid UART churn).
    // Лог раз на версию публикации при наличии данных — не каждый кадр (без флуда UART).
    static uint32_t s_diag_last_ver = 0;
    if (haveData && s_snap.version != s_diag_last_ver) {
        s_diag_last_ver = s_snap.version;
        wx_diag_dump("update_with_data", _screen);
    }
#endif

    // W-R3: сброс pending при завершении попытки, новой версии или таймауте.
    if (_manual_refresh_pending) {
        const bool fetch_done = !s_snap.fetch_in_progress;
        const int32_t attempt_after_request =
            (int32_t)(s_snap.last_attempt_at_ms - _refresh_pending_since_ms);
        if (fetch_done && s_snap.last_attempt_at_ms != 0u && attempt_after_request >= 0) {
            _manual_refresh_pending = false;
        } else if (s_snap.version != _refresh_watch_version) {
            _manual_refresh_pending = false;
        } else if (millis() - _refresh_pending_since_ms >= kRefreshPendingTimeoutMs) {
            _manual_refresh_pending = false;
        }
    }

    // A3.2 perf: compact visible-state signature — detects all transitions that require a body/footer rebuild.
    // A3.2 perf: компактная сигнатура видимого состояния; ловит все переходы, требующие rebuild.
    uint32_t view_sig = 0;
    view_sig |= wxEnabled              ? (1u << 0) : 0u;
    view_sig |= haveData               ? (1u << 1) : 0u;
    view_sig |= loadingWithoutData     ? (1u << 2) : 0u;
    view_sig |= unavailableWithoutData ? (1u << 3) : 0u;
    view_sig |= effectiveStale         ? (1u << 4) : 0u;
    view_sig |= showRefreshingFooter   ? (1u << 5) : 0u;

    // Footer age bucket: changes once per displayed minute; UINT32_MAX when age is not shown.
    // Минутный ключ возраста футера: меняется раз в минуту; UINT32_MAX при отсутствии данных.
    const uint32_t minute_bucket =
        (haveData && s_snap.forecast_updated_at != 0u)
        ? (millis() - s_snap.forecast_updated_at) / 60000u
        : UINT32_MAX;

    // Day key: triggers hero-date and hourly-label rebuild at midnight without a new payload.
    // Ключ дня: rebuild hero и hourly при смене суток, без новой публикации WeatherState.
    const uint32_t day_key =
        (network.timeinfo.tm_year > 100)
        ? static_cast<uint32_t>(network.timeinfo.tm_year) * 366u
          + static_cast<uint32_t>(network.timeinfo.tm_yday)
        : 0u;

    // Dirty-category derivation / Вычисление категорий изменений.
    const bool force_full      = !_render_cache_valid;
    const bool version_changed = force_full || s_snap.version != _rendered_version;
    const bool view_changed    = force_full || view_sig        != _rendered_view_sig;
    const bool day_changed     = force_full || day_key         != _rendered_day_key;
    const bool minute_changed  = force_full || minute_bucket   != _rendered_minute_bucket;

    const bool full_render_needed = force_full || version_changed || view_changed || day_changed;
    const bool footer_only_needed = !full_render_needed && minute_changed;

    if (!full_render_needed && !footer_only_needed) {
        return; // fast path: visible state unchanged / быстрый путь: видимое состояние не изменилось
    }

    char buf[64];
    char footer_buf[kFooterTextCap];

    if (footer_only_needed) {
        // A3.2 perf: only the footer age minute changed — skip the full body rebuild.
        // A3.2 perf: изменилась только минута возраста — пропускаем rebuild тела страницы.
        wx_format_footer(footer_buf, sizeof(footer_buf), wxEnabled, haveData, showRefreshingFooter,
                         effectiveStale, s_snap.forecast_updated_at, unavailableWithoutData,
                         &s_snap.location);
        wx_footer_maybe_add_trailing_sep(footer_buf, sizeof(footer_buf), _lbl_footer);
        wx_set_text_if_changed(_lbl_footer, footer_buf);
        _rendered_minute_bucket = minute_bucket;
        return;
    }

    // ── Full render path ─────────────────────────────────────────────────────
    if (!showData) {
        // W-R3 / E34: empty, loading, unavailable, or disabled — center message; footer at bottom.
        // W-R3 / E34: пусто, загрузка, недоступно или выключено — центр; футер внизу.
        wx_show(_cont_data, false);
        wx_show(_cont_empty_center, true);
        wx_show(_cont_footer, true);
        if (!wxEnabled) {
            wx_set_text_if_changed(_lbl_message, kStrWeatherUnavail);
        } else if (unavailableWithoutData) {
            wx_set_text_if_changed(_lbl_message, kStrTemporarilyUnavailable);
        } else if (loadingWithoutData) {
            wx_set_text_if_changed(_lbl_message, kStrPleaseWait);
        } else {
            wx_set_text_if_changed(_lbl_message, kStrForecastWaiting);
        }
        wx_format_footer(footer_buf, sizeof(footer_buf), wxEnabled, haveData, showRefreshingFooter,
                         effectiveStale, s_snap.forecast_updated_at, unavailableWithoutData,
                         &s_snap.location);
        wx_footer_maybe_add_trailing_sep(footer_buf, sizeof(footer_buf), _lbl_footer);
        wx_set_text_if_changed(_lbl_footer, footer_buf);
        // Commit render cache / Фиксируем кэш рендера.
        _rendered_version       = s_snap.version;
        _rendered_view_sig      = view_sig;
        _rendered_minute_bucket = minute_bucket;
        _rendered_day_key       = day_key;
        _render_cache_valid     = true;
        return;
    }

    // ── State A: full forecast ──────────────────────────────────────────────
    wx_show(_cont_empty_center, false);
    wx_show(_cont_data, true);
    wx_show(_cont_footer, true);

    const WeatherCurrent& cur = s_snap.current;

    lv_coord_t hero_inner_w = 0;
    if (_cont_hero) {
        lv_obj_update_layout(_cont_hero);
        hero_inner_w = lv_obj_get_content_width(_cont_hero);
    }
    wx_format_hero_date(buf, sizeof(buf), &network.timeinfo, hero_inner_w);
    wx_set_text_if_changed(_lbl_hero_date, buf);

    wx_set_text_if_changed(_lbl_hero_icon, weather_owm_icon_to_glyph_utf8(cur.owm_icon));
    snprintf(buf, sizeof(buf), "%+.0f\xC2\xB0\x43", static_cast<double>(cur.temp_c)); // "+NN°C"
    wx_set_text_if_changed(_lbl_hero_temp, buf);
    wx_set_text_if_changed(_lbl_hero_cond, (cur.condition[0] != '\0') ? cur.condition : "--");
    snprintf(buf, sizeof(buf), "%s%+.0f\xC2\xB0\x43", kStrFeelsLike, static_cast<double>(cur.feels_like_c));
    wx_set_text_if_changed(_lbl_hero_feels, buf);

    snprintf(buf, sizeof(buf), "%.0f m/s", static_cast<double>(cur.wind_speed));
    wx_set_text_if_changed(_val_wind, buf);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)cur.humidity);
    wx_set_text_if_changed(_val_humidity, buf);
    snprintf(buf, sizeof(buf), "%u hPa", (unsigned)cur.pressure_hpa);
    wx_set_text_if_changed(_val_pressure, buf);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)cur.rain_probability);
    wx_set_text_if_changed(_val_rain, buf);

    // Hourly right column: day header + horizontal rows; wall-clock from forecast ts (local TZ).
    // Правый блок: заголовок дня + строки; время из ts прогноза (локальный TZ).
    wx_format_hourly_day_header(buf, sizeof(buf), &network.timeinfo,
                                &s_snap.hourly[1], kHourlyCells);
    wx_set_text_if_changed(_lbl_hourly_day, buf);

    static const char* const k_hourly_fallback[kHourlyCells] = {kStrPlus3h, kStrPlus6h, kStrPlus9h};
    for (int i = 0; i < kHourlyCells; ++i) {
        const WeatherHourly& h = s_snap.hourly[i + 1]; // skip slot 0 (current / «сейчас»)
        if (!h.valid) {
            wx_format_hour_slot_label(buf, sizeof(buf), 0u, k_hourly_fallback[i]);
            wx_set_text_if_changed(_hourly[i].time, buf);
            wx_set_text_if_changed(_hourly[i].icon, weather_owm_icon_to_glyph_utf8(nullptr));
            wx_set_text_if_changed(_hourly[i].temp, "--");
            wx_set_text_if_changed(_hourly[i].pop, "");
            continue;
        }
        wx_format_hour_slot_label(buf, sizeof(buf), h.ts, k_hourly_fallback[i]);
        wx_set_text_if_changed(_hourly[i].time, buf);
        wx_set_text_if_changed(_hourly[i].icon, weather_owm_icon_to_glyph_utf8(h.owm_icon));
        snprintf(buf, sizeof(buf), "%+.0f\xC2\xB0", static_cast<double>(h.temp_c));
        wx_set_text_if_changed(_hourly[i].temp, buf);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)h.rain_probability);
        wx_set_text_if_changed(_hourly[i].pop, buf);
    }

    // Daily bottom block: weekday+date from day_ts; tmin° / tmax°; skip slot 0 (today in hero).
    // A3.1h: daily[1..3]; «Чт 18.06» из day_ts; daily[0] — «сегодня» — в hero.
    for (int i = 0; i < kDailyCells; ++i) {
        const WeatherDaily& d = s_snap.daily[i + 1]; // A2: skip slot 0 (today)
        if (!d.valid) {
            // Quiet empty placeholder — no lone umbrella / тихий placeholder, без зонта
            wx_set_text_if_changed(_daily[i].day, "");
            wx_set_text_if_changed(_daily[i].icon, "");
            wx_set_text_if_changed(_daily[i].range, "");
            wx_set_text_if_changed(_daily[i].pop, "");
            wx_set_text_if_changed(_daily[i].pop_icon, "");
            continue;
        }
        wx_format_daily_date_label(buf, sizeof(buf), d.day_ts, s_snap.forecast_tz_sec);
        wx_set_text_if_changed(_daily[i].day, buf);
        wx_set_text_if_changed(_daily[i].icon, weather_owm_icon_to_glyph_utf8(d.owm_icon));
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0 / %.0f\xC2\xB0",
                 static_cast<double>(d.temp_min_c), static_cast<double>(d.temp_max_c));
        wx_set_text_if_changed(_daily[i].range, buf);
        wx_set_text_if_changed(_daily[i].pop_icon, YORA_WEATHER_METRIC_GLYPH_UMBRELLA);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)d.rain_probability_max);
        wx_set_text_if_changed(_daily[i].pop, buf);
    }

    wx_format_footer(footer_buf, sizeof(footer_buf), wxEnabled, haveData, showRefreshingFooter,
                     effectiveStale, s_snap.forecast_updated_at, unavailableWithoutData,
                     &s_snap.location);
    wx_footer_maybe_add_trailing_sep(footer_buf, sizeof(footer_buf), _lbl_footer);
    wx_set_text_if_changed(_lbl_footer, footer_buf);

    // Commit render cache / Фиксируем кэш рендера.
    _rendered_version       = s_snap.version;
    _rendered_view_sig      = view_sig;
    _rendered_minute_bucket = minute_bucket;
    _rendered_day_key       = day_key;
    _render_cache_valid     = true;
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

    paint(_lbl_hero_date, pal.text_meta);
    paint(_lbl_hourly_day, pal.text_meta);
    paint(_lbl_hero_icon, pal.status_weather_icon);
    paint(_lbl_hero_temp, pal.text_primary);
    paint(_lbl_hero_cond, pal.text_secondary);
    paint(_lbl_hero_feels, pal.text_meta);
    paint(_val_wind, pal.text_primary);
    paint(_val_humidity, pal.text_primary);
    paint(_val_pressure, pal.text_primary);
    paint(_val_rain, pal.text_primary);
    paint(_lbl_wind, pal.text_secondary);
    paint(_lbl_humidity, pal.text_secondary);
    paint(_lbl_pressure, pal.text_secondary);
    paint(_lbl_rain, pal.text_secondary);
    paint(_lbl_footer, pal.text_secondary);
    paint(_lbl_message, pal.text_secondary);

    if (_footer_box) {
        lv_obj_set_style_bg_color(_footer_box, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(_footer_box, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_color(_footer_box, pal.divider, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_footer_box, LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_border_width(_footer_box, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_footer_box, LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(_footer_box, pal.text_meta, LV_STATE_PRESSED);
    }
    // A2: repaint hero card and daily panel backgrounds on theme switch.
    // A2: перекрашиваем hero-карточку и панель дней при смене темы.
    if (_cont_hero) {
        lv_obj_set_style_bg_color(_cont_hero, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(_cont_hero, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_color(_cont_hero, pal.divider, LV_PART_MAIN);
    }
    if (_cont_daily) {
        lv_obj_set_style_bg_color(_cont_daily, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(_cont_daily, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_color(_cont_daily, pal.divider, LV_PART_MAIN);
    }
    // A3.1B: retint internal daily separators after panel repaint.
    // A3.1B: перекраска внутренних разделителей daily после repaint панели.
    if (_daily[0].cont) wx_style_daily_internal_separator(_daily[0].cont, pal);
    if (_daily[1].cont) wx_style_daily_internal_separator(_daily[1].cont, pal);
    if (_daily[2].cont) lv_obj_set_style_border_width(_daily[2].cont, 0, LV_PART_MAIN);
    if (_cont_hourly) {
        lv_obj_set_style_bg_color(_cont_hourly, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(_cont_hourly, LV_GRAD_DIR_NONE, LV_PART_MAIN);
        lv_obj_set_style_border_color(_cont_hourly, pal.divider, LV_PART_MAIN);
    }

    // Hourly row icons use k_font_hourly_icon (28 px); daily wx icons use 36 px.
    for (int i = 0; i < kHourlyCells; ++i) {
        paint(_hourly[i].time, pal.text_secondary);
        paint(_hourly[i].icon, pal.status_weather_icon);
        paint(_hourly[i].temp, pal.text_primary);
        paint(_hourly[i].pop_icon, pal.text_secondary);
        paint(_hourly[i].pop,  pal.text_secondary);
    }
    for (int i = 0; i < kDailyCells; ++i) {
        paint(_daily[i].day,      pal.text_secondary);
        paint(_daily[i].icon,     pal.status_weather_icon);
        paint(_daily[i].range,    pal.text_primary);
        paint(_daily[i].pop_icon, pal.text_secondary);
        paint(_daily[i].pop,      pal.text_secondary);
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
                // Retint caption font (time/day/pop labels) and metric icon glyphs.
                // Перекраска шрифта caption и глифов метрических иконок.
                if (f == static_cast<const lv_font_t*>(k_font_caption) ||
                    f == static_cast<const lv_font_t*>(k_font_metric_icon)) {
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
    // A3.2 perf: reset render cache — next enter() will force a full render into fresh widgets.
    // A3.2 perf: сброс кэша — следующий enter() выполнит полный рендер в новые виджеты.
    _render_cache_valid     = false;
    _rendered_version       = 0;
    _rendered_view_sig      = 0;
    _rendered_minute_bucket = UINT32_MAX;
    _rendered_day_key       = 0;
    _screen = nullptr;  // dangling after auto_del; already nulled on the manual destroy() path
    _status_line = {};
    _content = _body_area = nullptr;
    _cont_data = _cont_top = _div_mid = _cont_empty_center = _lbl_message = nullptr;
    _cont_footer = _footer_box = _lbl_footer = nullptr;
    _cont_hero = _lbl_hero_date = _lbl_hero_icon = _lbl_hero_temp = _lbl_hero_cond = _lbl_hero_feels = nullptr;
    _cont_metrics = _val_wind = _val_humidity = _val_pressure = _val_rain = nullptr;
    _lbl_wind = _lbl_humidity = _lbl_pressure = _lbl_rain = nullptr;
    _cont_hourly = _lbl_hourly_day = _cont_daily = nullptr;
    for (int i = 0; i < kHourlyCells; ++i) _hourly[i] = HourlyCell{};
    for (int i = 0; i < kDailyCells; ++i)  _daily[i] = DailyCell{};
}

lv_obj_t* LvglWeatherPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
