/*
 * LvglSettingsPage — Settings carousel page (Stage 6.7S).
 * LvglSettingsPage — страница Settings в карусели.
 *
 * Main view: category rows + footer. In-page detail views: Display (MEM1 lazy),
 * Music Rail (lazy, destroy-on-Back). Row unit: fixed-height data row + 1 px divider.
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 * Object tree: scr_settings_layout_tree.md
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_settings.h"

#include <stdio.h>
#include <time.h>

#include <WiFi.h>
#include "lvgl.h"

#include "../../core/config.h"
#include "../../core/autodim.h"
#include "../../core/display.h"
#include "../../core/player.h"
#include "../../core/sleep_timer.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../font_provider.h"
#include "../fonts/settings_glyph_utf8.h"
#include "../lv_page_chain.h"
#include "../lv_text_scroll.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../widgets/wgt_footer_pill.h"
#include "lvgl_ui.h"

namespace lvgl_ui {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// UI string constants (l10n readiness) / Строки UI
// Gathered here for future localization; do NOT scatter raw literals through code.
// Собраны здесь для будущей локализации; не разбрасывать строки по коду.
// ─────────────────────────────────────────────────────────────────────────────

// Main category row labels / Подписи категорий главного вида
static constexpr char kStrDisplay[]           = "DISPLAY";
static constexpr char kStrMusicRail[]         = "MUSIC RAIL";
static constexpr char kStrResumeOnStartup[]   = "RESUME ON STARTUP";
// TIMERS replaces the interim Deep Sleep detail without adding a PageChain slot.
// TIMERS заменяет промежуточную страницу Deep Sleep без нового PageChain slot.
static constexpr char kStrSleepTimer[]        = "TIMERS";
static constexpr char kStrRadio[]             = "RADIO";
static constexpr char kStrDeepSleep[]         = "DEEP SLEEP";
static constexpr char kStrStopRadioAfter[]    = "STOP RADIO AFTER";
static constexpr char kStrStartRadioAfter[]   = "START RADIO AFTER";
static constexpr char kStrDeepSleepAfter[]    = "DEEP SLEEP AFTER";
static constexpr char kStrWakeAfterSleep[]    = "WAKE AFTER SLEEP";
static constexpr char kStrHours[]             = "HOURS";
static constexpr char kStrMinutes[]           = "MINUTES";
static constexpr char kStrStartRadioTimer[]   = "START RADIO TIMER";
static constexpr char kStrCancelRadioTimer[]  = "CANCEL RADIO TIMER";
static constexpr char kStrStartDeepSleepTimer[] = "START DEEP SLEEP TIMER";
static constexpr char kStrCancelDeepSleepTimer[] = "CANCEL DEEP SLEEP TIMER";
static constexpr char kStrEnterDeepSleepNow[] = "ENTER DEEP SLEEP NOW";
static constexpr char kStrStopStartConflict[] = "STOP AND START TIMES MUST DIFFER";
static constexpr char kStrClockNotSynced[]    = "CLOCK NOT SYNCED";
static constexpr char kStrCancelRadioFirst[]  = "CANCEL RADIO TIMER FIRST";
static constexpr char kStrCancelDeepFirst[]   = "CANCEL DEEP SLEEP TIMER FIRST";
static constexpr char kStrZeroDisabled[]      = "0 H 0 MIN = DISABLED";
static constexpr char kStrShutdownActive[]    = "DEEP SLEEP IN PROGRESS";
static constexpr char kStrWifi[]              = "WI-FI";

// Display detail labels / Подписи Display detail
static constexpr char kStrBrightness[]        = "BRIGHTNESS";
static constexpr char kStrTheme[]             = "THEME";
static constexpr char kStrAutoDim[]           = "AUTO DIM";
static constexpr char kStrDimAfter[]          = "DIM AFTER";
static constexpr char kStrDimLevel[]          = "DIM LEVEL";
static constexpr char kStrPerformanceMonitor[] = "PERFORMANCE MONITOR";
// FU6-A Text scrolling / Скролл текста
static constexpr char kStrScrolling[]         = "SCROLLING";
static constexpr char kStrScrollingPreview[]  = "SCROLLING PREVIEW";
// Fixed preview string, deliberately long enough to overflow 480 px at font_normal_px.
// Not persisted, not localized (Settings uses file-local constants).
// Фиксированная строка preview — заведомо длиннее 480 px; не сохраняется.
static constexpr char kStrScrollingPreviewText[] =
    "Testing text scrolling speed, mode and delay - this line is intentionally long "
    "enough to overflow the screen width";
static constexpr char kStrScrollSpeed[]       = "SCROLL SPEED";
static constexpr char kStrScrollType[]        = "SCROLL TYPE";
static constexpr char kStrScrollDelay[]       = "SCROLL DELAY";

// Music Rail detail labels / Подписи Music Rail detail
static constexpr char kStrPresenceRail[]      = "PRESENCE RAIL";
static constexpr char kStrRailProfile[]       = "RAIL PROFILE";

// Value labels — toggles and presets / Значения ON/OFF и пресеты
static constexpr char kStrValOn[]               = "ON";
static constexpr char kStrValOff[]              = "OFF";
static constexpr char kStrValRadio[]            = "RADIO";
static constexpr char kStrValDeepSleep[]        = "DEEP SLEEP";
static constexpr char kStrValNotConnected[]   = "NOT CONNECTED";
static constexpr char kStrValDark[]            = "DARK";
static constexpr char kStrValLight[]          = "LIGHT";
static constexpr char kStrValCustom[]           = "CUSTOM";
static constexpr char kStrValFence[]            = "FENCE";
static constexpr char kStrValOscilloscope[]    = "OSCILLOSCOPE";
// FU6-A scroll type values; OFF reuses the existing kStrValOff constant.
// Значения типа скролла; для OFF переиспользуется существующая kStrValOff.
static constexpr char kStrValCircular[]         = "CIRCULAR";
static constexpr char kStrValBackAndForth[]     = "BACK AND FORTH";

// Autodim timeout cycle labels / Метки таймаута autodim
static constexpr char kStrTimeout30Sec[]        = "30 SEC";
static constexpr char kStrTimeout60Sec[]        = "60 SEC";
static constexpr char kStrTimeout2Min[]         = "2 MIN";
static constexpr char kStrTimeout5Min[]         = "5 MIN";
static constexpr char kStrTimeout10Min[]        = "10 MIN";

// Footer / Футер
static constexpr char kStrFooterReturn[]        = "RETURN TO MAIN";

// ─────────────────────────────────────────────────────────────────────────────
// Font resources / Шрифты экрана
// ─────────────────────────────────────────────────────────────────────────────

static const void* font_settings_icon() { return FontProvider::icon(28); }

static const void* font_chevron() { return FontProvider::icon(28); }

static const void* font_display_header() { return FontProvider::text(20); }

// ─────────────────────────────────────────────────────────────────────────────
// Visual and layout constants / Визуальные и геометрические константы
// ─────────────────────────────────────────────────────────────────────────────

static constexpr lv_coord_t kRootRowGap           = 6;
static constexpr lv_coord_t kDividerHeight        = 1;
static constexpr lv_coord_t kIconColWidth         = 42;
static constexpr lv_coord_t kRowHeight            = 60;
static constexpr lv_coord_t kContentTopInset      = 8;
static constexpr lv_coord_t kContentBottomGap     = 4;
static constexpr lv_coord_t kFooterPadH           = 16;
static constexpr lv_coord_t kFooterPadV           = 10;
static constexpr lv_coord_t kDisplayHeaderHeight  = 52;
static constexpr lv_coord_t kBackColWidth         = 40;
// Track groove matches Main volume bar (scr_main.cpp _bar_volume) — knobless fill only.
// Канавка как у Main volume bar — только заливка, без видимого knob.
static constexpr lv_coord_t kSliderTrackH         = 16;
static constexpr lv_coord_t kSliderRowH           = 32;
static constexpr lv_coord_t kSliderValueGap       = 10;
static constexpr lv_coord_t kValueColWidth        = 52;
// FU6-A: "120 PX/S" / "BACK AND FORTH" need more room than the percent column.
// FU6-A: «120 PX/S» шире процентной колонки.
static constexpr lv_coord_t kScrollValueColWidth  = 86;
// FU6 UX: Display page is a fixed viewport (no scrolling). These tighten the Auto Dim group and
// the standalone Display rows so BRIGHTNESS + Auto Dim group + THEME + PERF + SCROLLING all fit.
// FU6 UX: страница Display — фиксированный экран без прокрутки; константы ужимают группу
// Auto Dim и одиночные строки, чтобы всё поместилось.
static constexpr lv_coord_t kCompactRowHeight     = 44;  // Auto Dim group rows
static constexpr lv_coord_t kDisplayRowHeight     = 52;  // THEME / PERF / SCROLLING rows
static constexpr lv_coord_t kGroupRowGap          = 2;   // inside the Auto Dim group
static constexpr lv_coord_t kSliderBlockRowGap    = 6;   // title -> slider inside a block
// FU6 UX: left touch margin so the slider knob at minimum is not against the panel edge.
// Full min..max range and full track travel are preserved — only the track start moves inboard.
// FU6 UX: левый отступ, чтобы ручка слайдера в минимуме не упиралась в край панели.
static constexpr lv_coord_t kScrollSliderLeftInset = 20;
// FU6 UX .10: breathing room between a divider and the section title that follows it, on the
// Scrolling sub-view. Subtle by design - the sub-view must stay fixed and non-scrollable.
// FU6 UX .10: небольшой отступ между divider и заголовком следующей секции на подстранице
// Scrolling. Намеренно малый - подстраница остаётся фиксированной, без прокрутки.
static constexpr lv_coord_t kSubSectionTopGap      = 8;
// FU6 UX .12: extra gap between the SCROLLING PREVIEW caption and the long testing label.
// Caption stays put; only the preview text moves down. Section pad_top (8 px) is unchanged.
// FU6 UX .12: доп. зазор между caption SCROLLING PREVIEW и длинным тестовым лейблом.
// Caption на месте; вниз сдвигается только preview. pad_top секции (8 px) не меняется.
static constexpr lv_coord_t kPreviewCaptionToTextGap = 14;  // prior pad_row 6 + 8 px additional
static constexpr lv_coord_t kBrightnessBlockGap   = 10;
// (kDisplayThemeSectionGap removed: the Auto Dim group's closing divider now provides the
//  section separation, so no spacer object is needed before THEME.)
// (kDisplayThemeSectionGap удалён: разделение секции даёт divider в конце группы Auto Dim.)
static constexpr lv_coord_t kBrightnessMinUi      = 1;
static constexpr uint8_t    kBrightnessMaxUi      = 100;
static constexpr uint8_t    kDimLevelMinUi        = 1;

static constexpr uint16_t kAutodimTimeoutsSec[] = {30, 60, 120, 300, 600};
static constexpr lv_coord_t kTimerTabsHeight      = 38;
static constexpr lv_coord_t kTimerEventHeight     = 92;
static constexpr lv_coord_t kTimerButtonHeight    = 42;
static constexpr lv_coord_t kTimerColumnGap       = 12;

// ─────────────────────────────────────────────────────────────────────────────
// Layout helpers / Вспомогательные функции разметки
// ─────────────────────────────────────────────────────────────────────────────

static void set_font_slot(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void style_transparent_flex(lv_obj_t* o) {
    if (!o) return;
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void format_brightness_pct(char* buf, size_t cap, uint8_t pct) {
    if (!buf || cap == 0) return;
    snprintf(buf, cap, "%u%%", static_cast<unsigned>(pct));
}

static const char* autodim_enabled_label() {
    return config.store.autodim_enabled ? kStrValOn : kStrValOff;
}

// FU6-A: value formatting follows the local convention (see format_brightness_pct).
// FU6-A: форматирование значений — по местной конвенции.
static void format_scroll_speed(char* buf, size_t cap, uint8_t px_per_sec) {
    if (!buf || cap == 0) return;
    snprintf(buf, cap, "%u PX/S", static_cast<unsigned>(px_per_sec));
}

static void format_scroll_delay(char* buf, size_t cap, uint8_t sec) {
    if (!buf || cap == 0) return;
    snprintf(buf, cap, "%u S", static_cast<unsigned>(sec));
}

static const char* scroll_type_label(text_scroll::Mode m) {
    switch (m) {
        case text_scroll::Mode::Circular:     return kStrValCircular;
        case text_scroll::Mode::BackAndForth: return kStrValBackAndForth;
        case text_scroll::Mode::Off:
        default:                              return kStrValOff;
    }
}

// Tap order: OFF -> CIRCULAR -> BACK AND FORTH -> OFF.
// Порядок по тапу: OFF -> CIRCULAR -> BACK AND FORTH -> OFF.
static void cycle_scroll_type() {
    const uint8_t cur = static_cast<uint8_t>(text_scroll::mode());
    const uint8_t next =
        (cur >= static_cast<uint8_t>(text_scroll::Mode::BackAndForth)) ? 0u : static_cast<uint8_t>(cur + 1u);
    config.saveValue(&config.store.text_scroll_type, next);
    text_scroll::reapplyAll();
}

static const char* performance_monitor_enabled_label() {
    return config.store.performance_monitor ? kStrValOn : kStrValOff;
}

static const char* vumeter_enabled_label() {
    return config.store.vumeter ? kStrValOn : kStrValOff;
}

static const char* rail_profile_label() {
    return config.store.usespectrum ? kStrValOscilloscope : kStrValFence;
}

static bool resume_on_startup_enabled() {
    return config.store.smartstart != 2;
}

static const char* resume_on_startup_value_label() {
    return resume_on_startup_enabled() ? kStrValOn : kStrValOff;
}

static const char* autodim_timeout_label(uint16_t sec) {
    switch (sec) {
        case 30:  return kStrTimeout30Sec;
        case 60:  return kStrTimeout60Sec;
        case 120: return kStrTimeout2Min;
        case 300: return kStrTimeout5Min;
        case 600: return kStrTimeout10Min;
        default:  return kStrTimeout60Sec;
    }
}

static const char* sleep_timer_settings_value_label() {
    switch (timer_active_plan()) {
        case TimerPlanKind::Radio:     return kStrValRadio;
        case TimerPlanKind::DeepSleep: return kStrValDeepSleep;
        case TimerPlanKind::None:
        default:                       return kStrValOff;
    }
}

static void format_timer_hours(char* buf, size_t cap, uint8_t hours) {
    if (!buf || cap == 0) return;
    snprintf(buf, cap, "%u H", static_cast<unsigned>(hours));
}

static void format_timer_minutes(char* buf, size_t cap, uint8_t minutes) {
    if (!buf || cap == 0) return;
    snprintf(buf, cap, "%u MIN", static_cast<unsigned>(minutes));
}

static int local_day_delta(time_t now, time_t event_at) {
    struct tm now_tm {};
    struct tm event_tm {};
    if (localtime_r(&now, &now_tm) == nullptr || localtime_r(&event_at, &event_tm) == nullptr) {
        return 0;
    }
    now_tm.tm_hour = now_tm.tm_min = now_tm.tm_sec = 0;
    event_tm.tm_hour = event_tm.tm_min = event_tm.tm_sec = 0;
    const double seconds = difftime(mktime(&event_tm), mktime(&now_tm));
    return seconds > 0 ? static_cast<int>((seconds + 43200.0) / 86400.0) : 0;
}

static void format_timer_at(char* buf, size_t cap, const char* verb, time_t event_at,
                            time_t now) {
    if (!buf || cap == 0 || !verb) return;
    if (event_at <= 0 || now <= 0) {
        snprintf(buf, cap, "%s AT --:--", verb);
        return;
    }
    struct tm event_tm {};
    if (localtime_r(&event_at, &event_tm) == nullptr) {
        snprintf(buf, cap, "%s AT --:--", verb);
        return;
    }
    const int day_delta = local_day_delta(now, event_at);
    if (day_delta <= 0) {
        snprintf(buf, cap, "%s AT %02d:%02d", verb, event_tm.tm_hour, event_tm.tm_min);
    } else if (day_delta == 1) {
        snprintf(buf, cap, "%s AT TOMORROW %02d:%02d",
                 verb, event_tm.tm_hour, event_tm.tm_min);
    } else {
        snprintf(buf, cap, "%s AT +%d DAYS %02d:%02d",
                 verb, day_delta, event_tm.tm_hour, event_tm.tm_min);
    }
}

static void format_timer_remaining(char* buf, size_t cap, const char* name,
                                   uint32_t remaining_seconds) {
    if (!buf || cap == 0 || !name) return;
    const uint32_t minutes = (remaining_seconds + 59u) / 60u;
    if (minutes >= 60u) {
        snprintf(buf, cap, "%s %luh %02lum", name,
                 static_cast<unsigned long>(minutes / 60u),
                 static_cast<unsigned long>(minutes % 60u));
    } else {
        snprintf(buf, cap, "%s %lum", name, static_cast<unsigned long>(minutes));
    }
}

static void cycle_autodim_timeout_sec() {
    constexpr size_t n = sizeof(kAutodimTimeoutsSec) / sizeof(kAutodimTimeoutsSec[0]);
    size_t idx = 0;
    for (; idx < n; ++idx) {
        if (config.store.autodim_timeout_sec == kAutodimTimeoutsSec[idx]) {
            break;
        }
    }
    if (idx >= n) {
        idx = 1;
    } else {
        idx = (idx + 1) % n;
    }
    // saveValue assigns store — pass new value only, do not pre-write the field.
    // saveValue сам пишет store — передавать новое значение, не менять field заранее.
    config.saveValue(&config.store.autodim_timeout_sec, kAutodimTimeoutsSec[idx]);
}

static const char* theme_preset_label(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Light:
            return kStrValLight;
        case ThemePreset::Custom:
            return kStrValCustom;
        case ThemePreset::Dark:
        default:
            return kStrValDark;
    }
}

static ThemePreset cycle_theme_preset(ThemePreset current) {
    switch (current) {
        case ThemePreset::Dark:
            return ThemePreset::Light;
        case ThemePreset::Light:
            return ThemePreset::Custom;
        case ThemePreset::Custom:
        default:
            return ThemePreset::Dark;
    }
}

// Row unit: [fixed-height data row] + [1 px bottom divider].
static void style_row_unit(lv_obj_t* unit) {
    if (!unit) return;
    lv_obj_set_width(unit, LV_PCT(100));
    lv_obj_set_height(unit, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(unit, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(unit, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    style_transparent_flex(unit);
    lv_obj_set_style_pad_row(unit, 0, LV_PART_MAIN);
    lv_obj_set_flex_grow(unit, 0);
}

static void style_data_row(lv_obj_t* row, lv_coord_t height) {
    if (!row) return;
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, height);
    lv_obj_set_style_min_height(row, height, LV_PART_MAIN);
    lv_obj_set_style_max_height(row, height, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_transparent_flex(row);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    lv_obj_set_flex_grow(row, 0);
}

static lv_obj_t* add_bottom_divider(lv_obj_t* unit, const YoRadioPalette& pal) {
    if (!unit) return nullptr;
    lv_obj_t* d = lv_obj_create(unit);
    if (!d) return nullptr;
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_height(d, kDividerHeight);
    lv_obj_set_style_min_height(d, kDividerHeight, LV_PART_MAIN);
    lv_obj_set_style_max_height(d, kDividerHeight, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d, 0, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(d, 0);
    return d;
}

static lv_obj_t* add_icon_column(lv_obj_t* row, const char* glyph, const YoRadioPalette& pal) {
    lv_obj_t* col = lv_obj_create(row);
    if (!col) return nullptr;
    lv_obj_set_width(col, kIconColWidth);
    lv_obj_set_height(col, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_transparent_flex(col);

    lv_obj_t* icon = lv_label_create(col);
    if (icon) {
        lv_label_set_text(icon, glyph);
        lv_label_set_long_mode(icon, LV_LABEL_LONG_CLIP);
        set_font_slot(icon, font_settings_icon());
        lv_obj_set_style_text_color(icon, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    return icon;
}

static lv_obj_t* add_row_label(
    lv_obj_t* row, const char* text, const YoRadioPalette& pal, bool secondary, bool flex_grow) {
    lv_obj_t* lbl = lv_label_create(row);
    if (!lbl) return nullptr;
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    set_font_slot(lbl, FontProvider::text(LV_ACTIVE_PROFILE.font_normal_px));
    lv_obj_set_style_text_color(lbl, secondary ? pal.text_meta : pal.text_primary, LV_PART_MAIN);
    if (flex_grow) {
        lv_obj_set_flex_grow(lbl, 1);
    }
    return lbl;
}

static lv_obj_t* add_row_value(lv_obj_t* row, const char* text, const YoRadioPalette& pal) {
    lv_obj_t* val = lv_label_create(row);
    if (!val) return nullptr;
    lv_label_set_text(val, text);
    lv_label_set_long_mode(val, LV_LABEL_LONG_CLIP);
    set_font_slot(val, FontProvider::text(LV_ACTIVE_PROFILE.font_header_px));
    lv_obj_set_style_text_color(val, pal.text_meta, LV_PART_MAIN);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    return val;
}

static lv_obj_t* add_row_chevron(lv_obj_t* row, const YoRadioPalette& pal) {
    lv_obj_t* chev = lv_label_create(row);
    if (!chev) return nullptr;
    lv_label_set_text(chev, control_glyph_utf8_chevron_right());
    lv_label_set_long_mode(chev, LV_LABEL_LONG_CLIP);
    set_font_slot(chev, font_chevron());
    lv_obj_set_style_text_color(chev, pal.text_meta, LV_PART_MAIN);
    return chev;
}

// with_divider=false builds a row that visually merges with the next one — used to fuse
// AUTO DIM / DIM AFTER / DIM LEVEL into a single Auto Dim block.
// with_divider=false — строка без разделителя, чтобы слить группу Auto Dim в один блок.
static lv_obj_t* create_row_unit(lv_obj_t* parent, const YoRadioPalette& pal, lv_coord_t row_height,
                                 bool with_divider = true) {
    lv_obj_t* unit = lv_obj_create(parent);
    if (!unit) return nullptr;
    style_row_unit(unit);

    lv_obj_t* row = lv_obj_create(unit);
    if (!row) return nullptr;
    style_data_row(row, row_height);

    if (with_divider) add_bottom_divider(unit, pal);
    return row;
}

static LvglSettingsPage::RowChrome fill_category_row(
    lv_obj_t*          row,
    const char*        icon_glyph,
    const char*        label_text,
    const char*        value_text,
    bool               show_chevron,
    const YoRadioPalette& pal) {
    LvglSettingsPage::RowChrome out{};
    if (!row) return out;

    out.hit = row;
    out.icon = add_icon_column(row, icon_glyph, pal);
    out.label = add_row_label(row, label_text, pal, false, true);
    if (value_text != nullptr && value_text[0] != '\0') {
        out.value = add_row_value(row, value_text, pal);
    }
    if (show_chevron) {
        out.chevron = add_row_chevron(row, pal);
    }
    return out;
}

static LvglSettingsPage::RowChrome add_category_row_unit(
    lv_obj_t*          parent,
    const char*        icon_glyph,
    const char*        label_text,
    const char*        value_text,
    bool               show_chevron,
    const YoRadioPalette& pal) {
    lv_obj_t* row = create_row_unit(parent, pal, kRowHeight);
    return fill_category_row(row, icon_glyph, label_text, value_text, show_chevron, pal);
}

static void reapply_dividers_in(lv_obj_t* parent, const YoRadioPalette& pal) {
    if (!parent) return;
    const uint32_t n = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* ch = lv_obj_get_child(parent, i);
        if (!ch) continue;
        if (lv_obj_get_height(ch) == kDividerHeight &&
            lv_obj_get_style_bg_opa(ch, LV_PART_MAIN) == LV_OPA_COVER) {
            lv_obj_set_style_bg_color(ch, pal.divider, LV_PART_MAIN);
        }
        reapply_dividers_in(ch, pal);
    }
}

static void create_footer(
    lv_obj_t*     parent,
    lv_obj_t*&    out_footer,
    lv_obj_t*&    out_lbl_footer,
    LvglSettingsPage* owner,
    lv_event_cb_t footer_cb,
    const YoRadioPalette& pal) {
    if (!parent || !owner || !footer_cb) return;

    out_footer = lv_obj_create(parent);
    if (!out_footer) return;

    lv_obj_set_width(out_footer, LV_PCT(100));
    lv_obj_set_height(out_footer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(out_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        out_footer,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(out_footer, kFooterPadH, LV_PART_MAIN);
    lv_obj_set_style_pad_right(out_footer, kFooterPadH, LV_PART_MAIN);
    lv_obj_set_style_pad_top(out_footer, kFooterPadV, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(out_footer, kFooterPadV, LV_PART_MAIN);
    wgt_footer_pill::prepare_surface(out_footer);
    wgt_footer_pill::apply_palette(out_footer, pal);
    lv_obj_add_flag(out_footer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(out_footer, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(out_footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(out_footer, footer_cb, LV_EVENT_CLICKED, owner);

    out_lbl_footer = lv_label_create(out_footer);
    if (out_lbl_footer) {
        lv_label_set_text(out_lbl_footer, kStrFooterReturn);
        set_font_slot(out_lbl_footer, FontProvider::text(LV_ACTIVE_PROFILE.font_normal_px));
        lv_obj_set_style_text_color(out_lbl_footer, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(out_lbl_footer, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        wgt_footer_pill::make_child_passive(out_lbl_footer);
    }
}

static void make_row_tappable(lv_obj_t* row, lv_event_cb_t cb, LvglSettingsPage* owner) {
    if (!row || !cb || !owner) return;
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, owner);
}

static void create_detail_header(
    lv_obj_t*          parent,
    const char*        title_text,
    const char*        icon_glyph,
    lv_event_cb_t      back_cb,
    lv_obj_t*&         out_back_hit,
    lv_obj_t*&         out_icon,
    lv_obj_t*&         out_title,
    LvglSettingsPage*  owner,
    const YoRadioPalette& pal) {
    if (!parent || !owner || !title_text || !icon_glyph || !back_cb) return;

    lv_obj_t* header = lv_obj_create(parent);
    if (!header) return;
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, kDisplayHeaderHeight);
    lv_obj_set_style_min_height(header, kDisplayHeaderHeight, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_transparent_flex(header);
    lv_obj_set_style_pad_column(header, 4, LV_PART_MAIN);

    out_back_hit = lv_obj_create(header);
    if (out_back_hit) {
        lv_obj_set_width(out_back_hit, kBackColWidth);
        lv_obj_set_height(out_back_hit, LV_PCT(100));
        lv_obj_set_flex_flow(out_back_hit, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(out_back_hit, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        style_transparent_flex(out_back_hit);
        lv_obj_add_flag(out_back_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(out_back_hit, back_cb, LV_EVENT_CLICKED, owner);

        lv_obj_t* back_glyph = lv_label_create(out_back_hit);
        if (back_glyph) {
            lv_label_set_text(back_glyph, control_glyph_utf8_chevron_left());
            set_font_slot(back_glyph, font_chevron());
            lv_obj_set_style_text_color(back_glyph, pal.text_meta, LV_PART_MAIN);
            lv_obj_add_flag(back_glyph, LV_OBJ_FLAG_EVENT_BUBBLE);
        }
    }

    lv_obj_t* icon_col = lv_obj_create(header);
    if (icon_col) {
        lv_obj_set_width(icon_col, kIconColWidth);
        lv_obj_set_height(icon_col, LV_PCT(100));
        lv_obj_set_flex_flow(icon_col, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(icon_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        style_transparent_flex(icon_col);

        out_icon = lv_label_create(icon_col);
        if (out_icon) {
            lv_label_set_text(out_icon, icon_glyph);
            set_font_slot(out_icon, font_settings_icon());
            lv_obj_set_style_text_color(out_icon, pal.text_secondary, LV_PART_MAIN);
        }
    }

    out_title = lv_label_create(header);
    if (out_title) {
        lv_label_set_text(out_title, title_text);
        set_font_slot(out_title, font_display_header());
        lv_obj_set_style_text_color(out_title, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_flex_grow(out_title, 1);
    }

    add_bottom_divider(parent, pal);
}

static void block_gesture_bubble_deep(lv_obj_t* root) {
    if (!root) return;
    lv_obj_clear_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    const uint32_t n = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < n; ++i) {
        block_gesture_bubble_deep(lv_obj_get_child(root, i));
    }
}

static void style_settings_bar_slider(lv_obj_t* slider, const YoRadioPalette& pal) {
    if (!slider) return;
    lv_obj_set_height(slider, kSliderTrackH);
    lv_obj_set_style_min_height(slider, kSliderTrackH, LV_PART_MAIN);
    lv_obj_set_style_max_height(slider, kSliderTrackH, LV_PART_MAIN);

    // Groove / track — same tokens as Main _bar_volume (LV_PART_MAIN).
    // Канавка — те же токены, что у Main volume bar.
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, pal.volume_bar_track, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(slider, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(slider, pal.panel_border, LV_PART_MAIN);
    lv_obj_set_style_border_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(slider, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(slider, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(slider, -1, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(slider, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_top(slider, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(slider, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_left(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(slider, 0, LV_PART_MAIN);

    // Fill / indicator — HOR gradient clip like Main volume bar.
    // Заливка — горизонтальный градиент как у Main volume bar.
    lv_obj_set_style_radius(slider, 5, LV_PART_INDICATOR);
    {
        const lv_color_t g0 = lv_color_mix(pal.volume_bar_fill, pal.volume_bar_track, LV_OPA_50);
        lv_obj_set_style_bg_color(slider, g0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(slider, pal.volume_bar_fill, LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    }

    // Knob exists for LVGL hit-testing only — fully invisible (no dot/outline).
    // Knob только для hit-test LVGL — полностью прозрачный.
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_outline_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
}

} // namespace

ScreenType LvglSettingsPage::screenType() const {
    return ScreenType::Page;
}

// ─────────────────────────────────────────────────────────────────────────────
// SETTINGSREF-A: Layout builders / Билдеры разметки
// ─────────────────────────────────────────────────────────────────────────────

bool LvglSettingsPage::create_main_structure(LvglSettingsPage& self, const YoRadioPalette& pal) {
    self._view_main = lv_obj_create(self._screen);
    if (!self._view_main) {
        return false;
    }
    lv_obj_set_width(self._view_main, LV_PCT(100));
    lv_obj_set_flex_grow(self._view_main, 1);
    lv_obj_set_flex_flow(self._view_main, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(self._view_main);
    lv_obj_set_style_pad_row(self._view_main, 0, LV_PART_MAIN);

    self._cont_content = lv_obj_create(self._view_main);
    if (!self._cont_content) {
        return false;
    }
    lv_obj_set_width(self._cont_content, LV_PCT(100));
    lv_obj_set_flex_grow(self._cont_content, 1);
    lv_obj_set_flex_flow(self._cont_content, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(self._cont_content);
    lv_obj_set_style_pad_top(self._cont_content, kContentTopInset, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(self._cont_content, kContentBottomGap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._cont_content, 0, LV_PART_MAIN);
    (void)pal;
    return true;
}

void LvglSettingsPage::populate_main_rows(LvglSettingsPage& self, const YoRadioPalette& pal) {
    {
        lv_obj_t* display_row = create_row_unit(self._cont_content, pal, kRowHeight);
        char brightness_buf[8] = "100%";
#ifdef ENABLE_BRIGHTNESS_CONTROL
        format_brightness_pct(brightness_buf, sizeof(brightness_buf), config.store.brightness);
#endif
        self._row_display = fill_category_row(
            display_row, YORA_SETTINGS_GLYPH_DISPLAY, kStrDisplay, brightness_buf, true, pal);
        make_row_tappable(display_row, displayRowClickedEvt, &self);
    }

    {
        lv_obj_t* music_row = create_row_unit(self._cont_content, pal, kRowHeight);
        self._row_music = fill_category_row(
            music_row,
            YORA_SETTINGS_GLYPH_MUSIC_RAIL,
            kStrMusicRail,
            vumeter_enabled_label(),
            true,
            pal);
        make_row_tappable(music_row, musicRowClickedEvt, &self);
    }

    {
        lv_obj_t* resume_row = create_row_unit(self._cont_content, pal, kRowHeight);
        self._row_resume_startup = fill_category_row(
            resume_row,
            YORA_SETTINGS_GLYPH_RESUME_ON_STARTUP,
            kStrResumeOnStartup,
            resume_on_startup_value_label(),
            false,
            pal);
        make_row_tappable(resume_row, resumeOnStartupRowClickedEvt, &self);
    }

    {
        lv_obj_t* sleep_timer_row = create_row_unit(self._cont_content, pal, kRowHeight);
        self._row_sleep_timer = fill_category_row(
            sleep_timer_row,
            YORA_SETTINGS_GLYPH_SLEEP_TIMER,
            kStrSleepTimer,
            sleep_timer_settings_value_label(),
            true,
            pal);
        make_row_tappable(sleep_timer_row, sleepTimerRowClickedEvt, &self);
    }

    self._row_wifi = add_category_row_unit(
        self._cont_content,
        YORA_SETTINGS_GLYPH_WIFI,
        kStrWifi,
        kStrValNotConnected,
        true,
        pal);
    make_row_tappable(self._row_wifi.hit, wifiRowClickedEvt, &self);
}

void LvglSettingsPage::build_display_detail(LvglSettingsPage& self, const YoRadioPalette& pal) {
    self._cont_display_content = lv_obj_create(self._view_display);
    if (self._cont_display_content) {
        lv_obj_set_width(self._cont_display_content, LV_PCT(100));
        lv_obj_set_flex_grow(self._cont_display_content, 1);
        lv_obj_set_flex_flow(self._cont_display_content, LV_FLEX_FLOW_COLUMN);
        style_transparent_flex(self._cont_display_content);
        lv_obj_set_style_pad_top(self._cont_display_content, kContentTopInset, LV_PART_MAIN);
        lv_obj_set_style_pad_row(self._cont_display_content, 0, LV_PART_MAIN);

#ifdef ENABLE_BRIGHTNESS_CONTROL
        // -- BRIGHTNESS ---------------------------------------------------------------------
        lv_obj_t* brightness_unit = lv_obj_create(self._cont_display_content);
        if (brightness_unit) {
            style_row_unit(brightness_unit);

            lv_obj_t* brightness_block = lv_obj_create(brightness_unit);
            if (brightness_block) {
                lv_obj_set_width(brightness_block, LV_PCT(100));
                lv_obj_set_height(brightness_block, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(brightness_block, LV_FLEX_FLOW_COLUMN);
                style_transparent_flex(brightness_block);
                lv_obj_set_style_pad_row(brightness_block, kSliderBlockRowGap, LV_PART_MAIN);
                lv_obj_clear_flag(brightness_block, LV_OBJ_FLAG_GESTURE_BUBBLE);

                self._lbl_brightness_title = add_row_label(brightness_block, kStrBrightness, pal, false, false);

                lv_obj_t* slider_row = lv_obj_create(brightness_block);
                if (slider_row) {
                    lv_obj_set_width(slider_row, LV_PCT(100));
                    lv_obj_set_height(slider_row, kSliderRowH);
                    lv_obj_set_style_min_height(slider_row, kSliderRowH, LV_PART_MAIN);
                    lv_obj_set_flex_flow(slider_row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(slider_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    style_transparent_flex(slider_row);
                    lv_obj_add_flag(slider_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                    lv_obj_clear_flag(slider_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

                    lv_obj_t* slider_wrapper = lv_obj_create(slider_row);
                    if (slider_wrapper) {
                        lv_obj_set_height(slider_wrapper, kSliderRowH);
                        lv_obj_set_flex_grow(slider_wrapper, 1);
                        lv_obj_set_flex_flow(slider_wrapper, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(
                            slider_wrapper,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
                        style_transparent_flex(slider_wrapper);
                        lv_obj_add_flag(slider_wrapper, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                        lv_obj_clear_flag(slider_wrapper, LV_OBJ_FLAG_GESTURE_BUBBLE);

                        self._brightness_slider = lv_slider_create(slider_wrapper);
                        if (self._brightness_slider) {
                            lv_obj_set_width(self._brightness_slider, LV_PCT(100));
                            lv_slider_set_range(self._brightness_slider, kBrightnessMinUi, kBrightnessMaxUi);
                            lv_slider_set_value(self._brightness_slider, config.store.brightness, LV_ANIM_OFF);
                            style_settings_bar_slider(self._brightness_slider, pal);
                            lv_obj_add_flag(self._brightness_slider, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                            lv_obj_clear_flag(self._brightness_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
                            lv_obj_add_event_cb(self._brightness_slider, brightnessSliderEvt, LV_EVENT_ALL, &self);
                        }
                    }

                    lv_obj_t* gap = lv_obj_create(slider_row);
                    if (gap) {
                        lv_obj_set_width(gap, kSliderValueGap);
                        lv_obj_set_height(gap, 1);
                        style_transparent_flex(gap);
                    }

                    lv_obj_t* value_col = lv_obj_create(slider_row);
                    if (value_col) {
                        lv_obj_set_width(value_col, kValueColWidth);
                        lv_obj_set_style_min_width(value_col, kValueColWidth, LV_PART_MAIN);
                        lv_obj_set_style_max_width(value_col, kValueColWidth, LV_PART_MAIN);
                        lv_obj_set_flex_grow(value_col, 0);
                        lv_obj_set_flex_flow(value_col, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(
                            value_col,
                            LV_FLEX_ALIGN_END,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
                        style_transparent_flex(value_col);
                        lv_obj_clear_flag(value_col, LV_OBJ_FLAG_GESTURE_BUBBLE);

                        char b_buf[8];
                        format_brightness_pct(b_buf, sizeof(b_buf), config.store.brightness);
                        self._lbl_brightness_value = add_row_value(value_col, b_buf, pal);
                    }
                }
            }
            add_bottom_divider(brightness_unit, pal);
        }
#endif

        // -- AUTO DIM group: AUTO DIM + DIM AFTER + DIM LEVEL as ONE visual block ------------
        // No separators between the three, tight row gap, one divider closing the group.
        // Tri kontrola kak odin blok: bez razdelitelej vnutri, plotnyj shag, odin divider v konce.
        lv_obj_t* autodim_group = lv_obj_create(self._cont_display_content);
        if (autodim_group) {
            style_row_unit(autodim_group);
            lv_obj_set_style_pad_row(autodim_group, kGroupRowGap, LV_PART_MAIN);

            lv_obj_t* autodim_row =
                create_row_unit(autodim_group, pal, kCompactRowHeight, /*with_divider=*/false);
            if (autodim_row) {
                self._row_autodim.hit = autodim_row;
                self._row_autodim.label = add_row_label(autodim_row, kStrAutoDim, pal, false, true);
                self._row_autodim.value = add_row_value(autodim_row, autodim_enabled_label(), pal);
                make_row_tappable(autodim_row, autodimRowClickedEvt, &self);
            }

            lv_obj_t* dim_after_row =
                create_row_unit(autodim_group, pal, kCompactRowHeight, /*with_divider=*/false);
            if (dim_after_row) {
                self._row_dim_after.hit = dim_after_row;
                self._row_dim_after.label = add_row_label(dim_after_row, kStrDimAfter, pal, false, true);
                self._row_dim_after.value = add_row_value(
                    dim_after_row, autodim_timeout_label(config.store.autodim_timeout_sec), pal);
                make_row_tappable(dim_after_row, dimAfterRowClickedEvt, &self);
            }

            lv_obj_t* dim_level_block = lv_obj_create(autodim_group);
            if (dim_level_block) {
                lv_obj_set_width(dim_level_block, LV_PCT(100));
                lv_obj_set_height(dim_level_block, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(dim_level_block, LV_FLEX_FLOW_COLUMN);
                style_transparent_flex(dim_level_block);
                lv_obj_set_style_pad_row(dim_level_block, kSliderBlockRowGap, LV_PART_MAIN);
                lv_obj_clear_flag(dim_level_block, LV_OBJ_FLAG_GESTURE_BUBBLE);

                self._lbl_dim_level_title = add_row_label(dim_level_block, kStrDimLevel, pal, false, false);

                lv_obj_t* slider_row = lv_obj_create(dim_level_block);
                if (slider_row) {
                    lv_obj_set_width(slider_row, LV_PCT(100));
                    lv_obj_set_height(slider_row, kSliderRowH);
                    lv_obj_set_style_min_height(slider_row, kSliderRowH, LV_PART_MAIN);
                    lv_obj_set_flex_flow(slider_row, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(slider_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    style_transparent_flex(slider_row);
                    lv_obj_add_flag(slider_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                    lv_obj_clear_flag(slider_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

                    lv_obj_t* slider_wrapper = lv_obj_create(slider_row);
                    if (slider_wrapper) {
                        lv_obj_set_height(slider_wrapper, kSliderRowH);
                        lv_obj_set_flex_grow(slider_wrapper, 1);
                        lv_obj_set_flex_flow(slider_wrapper, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(
                            slider_wrapper,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
                        style_transparent_flex(slider_wrapper);
                        lv_obj_add_flag(slider_wrapper, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                        lv_obj_clear_flag(slider_wrapper, LV_OBJ_FLAG_GESTURE_BUBBLE);

                        self._dim_level_slider = lv_slider_create(slider_wrapper);
                        if (self._dim_level_slider) {
                            lv_obj_set_width(self._dim_level_slider, LV_PCT(100));
                            lv_slider_set_range(
                                self._dim_level_slider,
                                kDimLevelMinUi,
                                autodim_level_max_for_brightness(config.store.brightness));
                            lv_slider_set_value(self._dim_level_slider, config.store.autodim_level, LV_ANIM_OFF);
                            style_settings_bar_slider(self._dim_level_slider, pal);
                            lv_obj_add_flag(self._dim_level_slider, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                            lv_obj_clear_flag(self._dim_level_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
                            lv_obj_add_event_cb(self._dim_level_slider, dimLevelSliderEvt, LV_EVENT_ALL, &self);
                        }
                    }

                    lv_obj_t* gap = lv_obj_create(slider_row);
                    if (gap) {
                        lv_obj_set_width(gap, kSliderValueGap);
                        lv_obj_set_height(gap, 1);
                        style_transparent_flex(gap);
                    }

                    lv_obj_t* value_col = lv_obj_create(slider_row);
                    if (value_col) {
                        lv_obj_set_width(value_col, kValueColWidth);
                        lv_obj_set_style_min_width(value_col, kValueColWidth, LV_PART_MAIN);
                        lv_obj_set_style_max_width(value_col, kValueColWidth, LV_PART_MAIN);
                        lv_obj_set_flex_grow(value_col, 0);
                        lv_obj_set_flex_flow(value_col, LV_FLEX_FLOW_ROW);
                        lv_obj_set_flex_align(
                            value_col,
                            LV_FLEX_ALIGN_END,
                            LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
                        style_transparent_flex(value_col);
                        lv_obj_clear_flag(value_col, LV_OBJ_FLAG_GESTURE_BUBBLE);

                        char dim_buf[8];
                        format_brightness_pct(dim_buf, sizeof(dim_buf), config.store.autodim_level);
                        self._lbl_dim_level_value = add_row_value(value_col, dim_buf, pal);
                    }
                }
            }

            // Section separation after the Auto Dim group.
            add_bottom_divider(autodim_group, pal);
        }

        // -- THEME -------------------------------------------------------------------------
        lv_obj_t* theme_row = create_row_unit(self._cont_display_content, pal, kDisplayRowHeight);
        if (theme_row) {
            self._row_theme.hit = theme_row;
            self._row_theme.label = add_row_label(theme_row, kStrTheme, pal, false, true);
            self._row_theme.value = add_row_value(
                theme_row, theme_preset_label(themeRequestedPreset()), pal);
            self._row_theme.chevron = add_row_chevron(theme_row, pal);
            make_row_tappable(theme_row, themeRowClickedEvt, &self);
        }

        // -- PERFORMANCE MONITOR -----------------------------------------------------------
        lv_obj_t* perf_row = create_row_unit(self._cont_display_content, pal, kDisplayRowHeight);
        if (perf_row) {
            self._row_perf_monitor.hit = perf_row;
            self._row_perf_monitor.label = add_row_label(perf_row, kStrPerformanceMonitor, pal, false, true);
            self._row_perf_monitor.value = add_row_value(
                perf_row, performance_monitor_enabled_label(), pal);
            make_row_tappable(perf_row, performanceMonitorRowClickedEvt, &self);
        }

        // -- SCROLLING > (opens the Scrolling sub-view) -------------------------------------
        lv_obj_t* scrolling_row = create_row_unit(self._cont_display_content, pal, kDisplayRowHeight);
        if (scrolling_row) {
            self._row_scrolling.hit = scrolling_row;
            self._row_scrolling.label = add_row_label(scrolling_row, kStrScrolling, pal, false, true);
            self._row_scrolling.chevron = add_row_chevron(scrolling_row, pal);
            make_row_tappable(scrolling_row, scrollingRowClickedEvt, &self);
        }
    }
}

// FU6 UX: one slider row with a left touch inset.
// The inset shifts the whole track inboard so the knob at MINIMUM is not against the panel edge.
// Range and travel are untouched: LVGL maps min..max across whatever track width results, so
// 10..120 (and 0..10) stay fully reachable; only the geometry moves.
// FU6 UX: ряд слайдера с левым отступом — ручка в минимуме не упирается в край панели.
// Диапазон и ход не меняются: LVGL раскладывает min..max по любой ширине трека.
static lv_obj_t* add_scroll_slider_row(
    lv_obj_t*             parent,
    lv_event_cb_t         slider_cb,
    LvglSettingsPage*     owner,
    int32_t               range_min,
    int32_t               range_max,
    int32_t               initial,
    const char*           initial_value_text,
    lv_obj_t**            out_slider,
    lv_obj_t**            out_value,
    const YoRadioPalette& pal) {
    if (!parent || !owner) return nullptr;

    lv_obj_t* slider_row = lv_obj_create(parent);
    if (!slider_row) return nullptr;
    lv_obj_set_width(slider_row, LV_PCT(100));
    lv_obj_set_height(slider_row, kSliderRowH);
    lv_obj_set_style_min_height(slider_row, kSliderRowH, LV_PART_MAIN);
    lv_obj_set_flex_flow(slider_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slider_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_transparent_flex(slider_row);
    lv_obj_add_flag(slider_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(slider_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Left touch margin (fixed-width spacer, not slider padding: padding would shrink the
    // knob's usable travel, a spacer only moves the track).
    // Левый отступ — спейсер, а не padding слайдера: padding урезал бы ход ручки.
    lv_obj_t* left_inset = lv_obj_create(slider_row);
    if (left_inset) {
        lv_obj_set_width(left_inset, kScrollSliderLeftInset);
        lv_obj_set_height(left_inset, 1);
        lv_obj_set_flex_grow(left_inset, 0);
        style_transparent_flex(left_inset);
    }

    lv_obj_t* slider_wrapper = lv_obj_create(slider_row);
    if (slider_wrapper) {
        lv_obj_set_height(slider_wrapper, kSliderRowH);
        lv_obj_set_flex_grow(slider_wrapper, 1);
        lv_obj_set_flex_flow(slider_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            slider_wrapper,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        style_transparent_flex(slider_wrapper);
        lv_obj_add_flag(slider_wrapper, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_clear_flag(slider_wrapper, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t* sl = lv_slider_create(slider_wrapper);
        if (sl) {
            lv_obj_set_width(sl, LV_PCT(100));
            // Full-height hit area is preserved (kSliderRowH); only the track start moved right.
            // Высота зоны нажатия сохранена — сдвинулось только начало трека.
            lv_slider_set_range(sl, range_min, range_max);
            lv_slider_set_value(sl, initial, LV_ANIM_OFF);
            style_settings_bar_slider(sl, pal);
            lv_obj_add_flag(sl, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
            lv_obj_clear_flag(sl, LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_ALL, owner);
        }
        if (out_slider) *out_slider = sl;
    }

    lv_obj_t* gap = lv_obj_create(slider_row);
    if (gap) {
        lv_obj_set_width(gap, kSliderValueGap);
        lv_obj_set_height(gap, 1);
        style_transparent_flex(gap);
    }

    lv_obj_t* value_col = lv_obj_create(slider_row);
    if (value_col) {
        lv_obj_set_width(value_col, kScrollValueColWidth);
        lv_obj_set_style_min_width(value_col, kScrollValueColWidth, LV_PART_MAIN);
        lv_obj_set_style_max_width(value_col, kScrollValueColWidth, LV_PART_MAIN);
        lv_obj_set_flex_grow(value_col, 0);
        lv_obj_set_flex_flow(value_col, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            value_col,
            LV_FLEX_ALIGN_END,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        style_transparent_flex(value_col);
        lv_obj_clear_flag(value_col, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t* v = add_row_value(value_col, initial_value_text, pal);
        if (out_value) *out_value = v;
    }
    return slider_row;
}

// FU6 UX: Settings -> Display -> Scrolling. Holds the three FU6 controls (behaviour unchanged)
// plus a live preview label driven by the SAME lv_text_scroll helper as production labels.
// FU6 UX: подстраница Scrolling — три контрола FU6 без изменения поведения плюс живой preview,
// использующий тот же хелпер lv_text_scroll, что и продакшн-лейблы.
void LvglSettingsPage::build_scrolling_detail(LvglSettingsPage& self, const YoRadioPalette& pal) {
    self._cont_scrolling_content = lv_obj_create(self._view_scrolling);
    if (!self._cont_scrolling_content) return;

    lv_obj_set_width(self._cont_scrolling_content, LV_PCT(100));
    lv_obj_set_flex_grow(self._cont_scrolling_content, 1);
    lv_obj_set_flex_flow(self._cont_scrolling_content, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(self._cont_scrolling_content);
    lv_obj_set_style_pad_top(self._cont_scrolling_content, kContentTopInset, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._cont_scrolling_content, 0, LV_PART_MAIN);

    // -- SCROLL SPEED -------------------------------------------------------------------------
    lv_obj_t* speed_unit = lv_obj_create(self._cont_scrolling_content);
    if (speed_unit) {
        style_row_unit(speed_unit);

        lv_obj_t* speed_block = lv_obj_create(speed_unit);
        if (speed_block) {
            lv_obj_set_width(speed_block, LV_PCT(100));
            lv_obj_set_height(speed_block, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(speed_block, LV_FLEX_FLOW_COLUMN);
            style_transparent_flex(speed_block);
            lv_obj_set_style_pad_row(speed_block, kSliderBlockRowGap, LV_PART_MAIN);
            lv_obj_clear_flag(speed_block, LV_OBJ_FLAG_GESTURE_BUBBLE);

            self._lbl_scroll_speed_title = add_row_label(speed_block, kStrScrollSpeed, pal, false, false);

            char speed_buf[16];
            format_scroll_speed(speed_buf, sizeof(speed_buf), text_scroll::speedPxPerSec());
            add_scroll_slider_row(
                speed_block,
                scrollSpeedSliderEvt,
                &self,
                text_scroll::kSpeedMin,
                text_scroll::kSpeedMax,
                text_scroll::speedPxPerSec(),
                speed_buf,
                &self._scroll_speed_slider,
                &self._lbl_scroll_speed_value,
                pal);
        }
        add_bottom_divider(speed_unit, pal);
    }

    // -- SCROLL TYPE (tap to cycle OFF -> CIRCULAR -> BACK AND FORTH) --------------------------
    lv_obj_t* type_row = create_row_unit(self._cont_scrolling_content, pal, kDisplayRowHeight);
    if (type_row) {
        self._row_scroll_type.hit = type_row;
        self._row_scroll_type.label = add_row_label(type_row, kStrScrollType, pal, false, true);
        self._row_scroll_type.value = add_row_value(
            type_row, scroll_type_label(text_scroll::mode()), pal);
        make_row_tappable(type_row, scrollTypeRowClickedEvt, &self);
    }

    // -- SCROLL DELAY -------------------------------------------------------------------------
    lv_obj_t* delay_unit = lv_obj_create(self._cont_scrolling_content);
    if (delay_unit) {
        style_row_unit(delay_unit);
        // .10: small top gap so SCROLL DELAY is not flush against the divider above.
        // .10: небольшой верхний отступ, чтобы секция не липла к divider сверху.
        lv_obj_set_style_pad_top(delay_unit, kSubSectionTopGap, LV_PART_MAIN);

        lv_obj_t* delay_block = lv_obj_create(delay_unit);
        if (delay_block) {
            lv_obj_set_width(delay_block, LV_PCT(100));
            lv_obj_set_height(delay_block, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(delay_block, LV_FLEX_FLOW_COLUMN);
            style_transparent_flex(delay_block);
            lv_obj_set_style_pad_row(delay_block, kSliderBlockRowGap, LV_PART_MAIN);
            lv_obj_clear_flag(delay_block, LV_OBJ_FLAG_GESTURE_BUBBLE);

            self._lbl_scroll_delay_title = add_row_label(delay_block, kStrScrollDelay, pal, false, false);

            char delay_buf[16];
            format_scroll_delay(delay_buf, sizeof(delay_buf), text_scroll::delaySec());
            add_scroll_slider_row(
                delay_block,
                scrollDelaySliderEvt,
                &self,
                text_scroll::kDelayMinSec,
                text_scroll::kDelayMaxSec,
                text_scroll::delaySec(),
                delay_buf,
                &self._scroll_delay_slider,
                &self._lbl_scroll_delay_value,
                pal);
        }
        add_bottom_divider(delay_unit, pal);
    }

    // -- SCROLLING PREVIEW --------------------------------------------------------------------
    // The preview is an ordinary registered FU6 label: no separate animation logic, no persistence.
    // text_scroll::registerLabel() gives it the same speed/type/delay as every production label,
    // and reapplyAll() from the sliders reaches it live. OFF stops it exactly like the others.
    // Preview — обычный зарегистрированный лейбл FU6: без отдельной анимации и без сохранения.
    lv_obj_t* preview_unit = lv_obj_create(self._cont_scrolling_content);
    if (preview_unit) {
        style_row_unit(preview_unit);
        // .10: same small top gap above the whole Preview section (unchanged).
        // .12: caption-to-text gap is kPreviewCaptionToTextGap, not the slider pad_row.
        // .10: тот же отступ над всей секцией Preview (без изменений).
        // .12: зазор caption→текст — kPreviewCaptionToTextGap, не slider pad_row.
        lv_obj_set_style_pad_top(preview_unit, kSubSectionTopGap, LV_PART_MAIN);

        lv_obj_t* preview_block = lv_obj_create(preview_unit);
        if (preview_block) {
            lv_obj_set_width(preview_block, LV_PCT(100));
            lv_obj_set_height(preview_block, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(preview_block, LV_FLEX_FLOW_COLUMN);
            style_transparent_flex(preview_block);
            lv_obj_set_style_pad_row(preview_block, kPreviewCaptionToTextGap, LV_PART_MAIN);
            lv_obj_clear_flag(preview_block, LV_OBJ_FLAG_GESTURE_BUBBLE);

            self._lbl_preview_caption =
                add_row_label(preview_block, kStrScrollingPreview, pal, true, false);

            self._lbl_scroll_preview = lv_label_create(preview_block);
            if (self._lbl_scroll_preview) {
                lv_label_set_text(self._lbl_scroll_preview, kStrScrollingPreviewText);
                lv_obj_set_width(self._lbl_scroll_preview, LV_PCT(100));
                set_font_slot(self._lbl_scroll_preview, FontProvider::text(LV_ACTIVE_PROFILE.font_normal_px));
                lv_obj_set_style_text_color(self._lbl_scroll_preview, pal.text_primary, LV_PART_MAIN);
                lv_obj_set_style_text_align(self._lbl_scroll_preview, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                lv_obj_clear_flag(self._lbl_scroll_preview, LV_OBJ_FLAG_GESTURE_BUBBLE);
                // Register LAST, once width/font/alignment are final (FU6 helper contract).
                // Slot cleanup is automatic: lv_obj_null_on_delete() clears it when the sub-view
                // tree is destroyed on Back, so no stale pointer survives.
                // Регистрируем последним; слот очищается сам при удалении дерева на Back.
                text_scroll::registerLabel(self._lbl_scroll_preview);
            }
        }
    }
}

static void style_timers_slider(lv_obj_t* slider, const YoRadioPalette& pal) {
    if (!slider) return;
    lv_obj_set_height(slider, 28);
    lv_obj_set_style_bg_color(slider, pal.volume_bar_track, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(slider, pal.panel_border, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, pal.volume_bar_fill, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, pal.accent, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, pal.panel_border, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
}

static void style_timer_action_button(lv_obj_t* button, const YoRadioPalette& pal,
                                      bool primary, bool enabled) {
    if (!button) return;
    lv_obj_set_style_bg_color(button, primary ? pal.accent : pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, enabled ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, primary ? pal.accent : pal.panel_border, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    if (enabled) {
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
    }
}

// TIMERS detail reuses one fixed 480x480 control tree for both tabs. The active runtime plan
// shows its immutable snapshot in disabled controls; cancelling restores persisted presets.
// TIMERS использует одно фиксированное дерево для вкладок. Active plan показывает snapshot,
// а после Cancel возвращаются сохранённые presets.
void LvglSettingsPage::build_sleep_timer_detail(LvglSettingsPage& self, const YoRadioPalette& pal) {
    self._cont_sleep_timer_content = lv_obj_create(self._view_sleep_timer);
    if (!self._cont_sleep_timer_content) return;

    lv_obj_set_width(self._cont_sleep_timer_content, LV_PCT(100));
    lv_obj_set_flex_grow(self._cont_sleep_timer_content, 1);
    lv_obj_set_flex_flow(self._cont_sleep_timer_content, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(self._cont_sleep_timer_content);
    lv_obj_set_style_pad_top(self._cont_sleep_timer_content, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._cont_sleep_timer_content, 5, LV_PART_MAIN);

    lv_obj_t* tabs = lv_obj_create(self._cont_sleep_timer_content);
    if (tabs) {
        lv_obj_set_width(tabs, LV_PCT(100));
        lv_obj_set_height(tabs, kTimerTabsHeight);
        lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(tabs, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(tabs, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(tabs, 0, LV_PART_MAIN);
        lv_obj_clear_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);

        const char* tab_text[2] = {kStrRadio, kStrDeepSleep};
        lv_event_cb_t tab_cb[2] = {timersRadioTabClickedEvt, timersDeepSleepTabClickedEvt};
        for (uint8_t i = 0; i < 2; ++i) {
            self._timer_tab_buttons[i] = lv_obj_create(tabs);
            lv_obj_t* button = self._timer_tab_buttons[i];
            if (!button) continue;
            lv_obj_set_height(button, kTimerTabsHeight);
            lv_obj_set_flex_grow(button, 1);
            lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
            lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(button, tab_cb[i], LV_EVENT_CLICKED, &self);
            self._timer_tab_labels[i] = lv_label_create(button);
            if (self._timer_tab_labels[i]) {
                lv_label_set_text(self._timer_tab_labels[i], tab_text[i]);
                set_font_slot(self._timer_tab_labels[i], FontProvider::text(LV_ACTIVE_PROFILE.font_normal_px));
                lv_obj_center(self._timer_tab_labels[i]);
                lv_obj_clear_flag(self._timer_tab_labels[i], LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }

    lv_event_cb_t slider_callbacks[4] = {
        timerEvent1HoursSliderEvt,
        timerEvent1MinutesSliderEvt,
        timerEvent2HoursSliderEvt,
        timerEvent2MinutesSliderEvt,
    };
    for (uint8_t event_index = 0; event_index < 2; ++event_index) {
        lv_obj_t* event_card = lv_obj_create(self._cont_sleep_timer_content);
        if (!event_card) continue;
        self._timer_event_cards[event_index] = event_card;
        lv_obj_set_width(event_card, LV_PCT(100));
        lv_obj_set_height(event_card, kTimerEventHeight);
        lv_obj_set_flex_flow(event_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(event_card, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_row(event_card, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_color(event_card, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(event_card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(event_card, pal.divider, LV_PART_MAIN);
        lv_obj_set_style_border_width(event_card, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(event_card, 8, LV_PART_MAIN);
        lv_obj_clear_flag(event_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* event_header = lv_obj_create(event_card);
        if (event_header) {
            lv_obj_set_width(event_header, LV_PCT(100));
            lv_obj_set_height(event_header, 20);
            lv_obj_set_flex_flow(event_header, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(event_header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            style_transparent_flex(event_header);

            self._timer_event_titles[event_index] = lv_label_create(event_header);
            if (self._timer_event_titles[event_index]) {
                set_font_slot(self._timer_event_titles[event_index],
                              FontProvider::text(LV_ACTIVE_PROFILE.font_small_px));
                lv_obj_set_style_text_color(self._timer_event_titles[event_index],
                                            pal.text_primary, LV_PART_MAIN);
                lv_obj_set_flex_grow(self._timer_event_titles[event_index], 1);
                lv_label_set_long_mode(self._timer_event_titles[event_index], LV_LABEL_LONG_CLIP);
            }
            self._timer_at_labels[event_index] = lv_label_create(event_header);
            if (self._timer_at_labels[event_index]) {
                set_font_slot(self._timer_at_labels[event_index],
                              FontProvider::text(LV_ACTIVE_PROFILE.font_small_px));
                lv_obj_set_style_text_color(self._timer_at_labels[event_index],
                                            pal.text_meta, LV_PART_MAIN);
                lv_obj_set_style_text_align(self._timer_at_labels[event_index],
                                            LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
                lv_obj_set_width(self._timer_at_labels[event_index], LV_PCT(58));
                lv_label_set_long_mode(self._timer_at_labels[event_index], LV_LABEL_LONG_CLIP);
            }
        }

        lv_obj_t* controls = lv_obj_create(event_card);
        if (!controls) continue;
        lv_obj_set_width(controls, LV_PCT(100));
        lv_obj_set_flex_grow(controls, 1);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(controls, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(controls, kTimerColumnGap, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(controls, 0, LV_PART_MAIN);
        lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

        for (uint8_t part = 0; part < 2; ++part) {
            const uint8_t slider_index = event_index * 2 + part;
            lv_obj_t* column = lv_obj_create(controls);
            if (!column) continue;
            lv_obj_set_height(column, LV_PCT(100));
            lv_obj_set_flex_grow(column, 1);
            lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_all(column, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_row(column, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(column, 0, LV_PART_MAIN);
            lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* caption_row = lv_obj_create(column);
            if (caption_row) {
                lv_obj_set_width(caption_row, LV_PCT(100));
                lv_obj_set_height(caption_row, 18);
                lv_obj_set_flex_flow(caption_row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(caption_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                style_transparent_flex(caption_row);
                self._timer_captions[slider_index] = lv_label_create(caption_row);
                if (self._timer_captions[slider_index]) {
                    lv_label_set_text(self._timer_captions[slider_index],
                                      part == 0 ? kStrHours : kStrMinutes);
                    set_font_slot(self._timer_captions[slider_index],
                                  FontProvider::text(LV_ACTIVE_PROFILE.font_small_px));
                    lv_obj_set_style_text_color(self._timer_captions[slider_index],
                                                pal.text_secondary, LV_PART_MAIN);
                }
                self._timer_value_labels[slider_index] = lv_label_create(caption_row);
                if (self._timer_value_labels[slider_index]) {
                    set_font_slot(self._timer_value_labels[slider_index],
                                  FontProvider::text(LV_ACTIVE_PROFILE.font_small_px));
                    lv_obj_set_style_text_color(self._timer_value_labels[slider_index],
                                                pal.text_meta, LV_PART_MAIN);
                }
            }

            self._timer_sliders[slider_index] = lv_slider_create(column);
            if (self._timer_sliders[slider_index]) {
                lv_obj_set_width(self._timer_sliders[slider_index], LV_PCT(100));
                lv_slider_set_range(self._timer_sliders[slider_index], 0, part == 0 ? 24 : 59);
                style_timers_slider(self._timer_sliders[slider_index], pal);
                lv_obj_clear_flag(self._timer_sliders[slider_index], LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_event_cb(self._timer_sliders[slider_index],
                                    slider_callbacks[slider_index], LV_EVENT_ALL, &self);
            }
        }
    }

    self._lbl_timer_state = lv_label_create(self._cont_sleep_timer_content);
    if (self._lbl_timer_state) {
        lv_obj_set_width(self._lbl_timer_state, LV_PCT(100));
        lv_obj_set_height(self._lbl_timer_state, 20);
        lv_label_set_long_mode(self._lbl_timer_state, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(self._lbl_timer_state, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        set_font_slot(self._lbl_timer_state, FontProvider::text(LV_ACTIVE_PROFILE.font_small_px));
    }

    lv_obj_t* buttons = lv_obj_create(self._cont_sleep_timer_content);
    if (buttons) {
        lv_obj_set_width(buttons, LV_PCT(100));
        lv_obj_set_height(buttons, kTimerButtonHeight);
        lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(buttons, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(buttons, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(buttons, 0, LV_PART_MAIN);
        lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

        self._btn_timer_primary = lv_obj_create(buttons);
        if (self._btn_timer_primary) {
            lv_obj_set_height(self._btn_timer_primary, kTimerButtonHeight);
            lv_obj_set_flex_grow(self._btn_timer_primary, 1);
            lv_obj_set_style_pad_all(self._btn_timer_primary, 0, LV_PART_MAIN);
            lv_obj_clear_flag(self._btn_timer_primary, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(self._btn_timer_primary, timerPrimaryClickedEvt,
                                LV_EVENT_CLICKED, &self);
            self._lbl_timer_primary = lv_label_create(self._btn_timer_primary);
            if (self._lbl_timer_primary) {
                set_font_slot(self._lbl_timer_primary,
                              FontProvider::text(LV_ACTIVE_PROFILE.font_small_px));
                lv_obj_center(self._lbl_timer_primary);
                lv_obj_clear_flag(self._lbl_timer_primary, LV_OBJ_FLAG_CLICKABLE);
            }
        }

        self._btn_deep_sleep_now = lv_obj_create(buttons);
        if (self._btn_deep_sleep_now) {
            lv_obj_set_height(self._btn_deep_sleep_now, kTimerButtonHeight);
            lv_obj_set_flex_grow(self._btn_deep_sleep_now, 1);
            lv_obj_set_style_pad_all(self._btn_deep_sleep_now, 0, LV_PART_MAIN);
            lv_obj_clear_flag(self._btn_deep_sleep_now, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(self._btn_deep_sleep_now, enterDeepSleepNowClickedEvt,
                                LV_EVENT_CLICKED, &self);
            self._lbl_deep_sleep_now = lv_label_create(self._btn_deep_sleep_now);
            if (self._lbl_deep_sleep_now) {
                lv_label_set_text(self._lbl_deep_sleep_now, kStrEnterDeepSleepNow);
                set_font_slot(self._lbl_deep_sleep_now,
                              FontProvider::text(LV_ACTIVE_PROFILE.font_small_px));
                lv_obj_center(self._lbl_deep_sleep_now);
                lv_obj_clear_flag(self._lbl_deep_sleep_now, LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }

    self._loadTimerTabValues(true);
    self._syncSleepTimerValues();
    self._applyTimersTheme(pal);
}

bool LvglSettingsPage::_ensureDisplayView() {
    if (_view_display) {
        return true;
    }
    if (!_screen) {
        return false;
    }

    const YoRadioPalette& pal = yoradio_palette();

    _view_display = lv_obj_create(_screen);
    if (!_view_display) {
        return false;
    }
    lv_obj_set_width(_view_display, LV_PCT(100));
    lv_obj_set_flex_grow(_view_display, 1);
    lv_obj_set_flex_flow(_view_display, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(_view_display);
    lv_obj_set_style_pad_row(_view_display, 0, LV_PART_MAIN);
    lv_obj_add_flag(_view_display, LV_OBJ_FLAG_HIDDEN);

    create_detail_header(
        _view_display,
        kStrDisplay,
        YORA_SETTINGS_GLYPH_DISPLAY,
        LvglSettingsPage::displayBackClickedEvt,
        _display_back_hit,
        _display_header_icon,
        _lbl_display_header,
        this,
        pal);

    build_display_detail(*this, pal);
    block_gesture_bubble_deep(_view_display);
    return true;
}

void LvglSettingsPage::build_music_rail_detail(LvglSettingsPage& self, const YoRadioPalette& pal) {
    self._cont_music_content = lv_obj_create(self._view_music);
    if (!self._cont_music_content) {
        return;
    }
    lv_obj_set_width(self._cont_music_content, LV_PCT(100));
    lv_obj_set_flex_grow(self._cont_music_content, 1);
    lv_obj_set_flex_flow(self._cont_music_content, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(self._cont_music_content);
    lv_obj_set_style_pad_top(self._cont_music_content, kContentTopInset, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._cont_music_content, 0, LV_PART_MAIN);

    lv_obj_t* enabled_row = create_row_unit(self._cont_music_content, pal, kRowHeight);
    if (enabled_row) {
        self._row_rail_enabled.hit = enabled_row;
        self._row_rail_enabled.label = add_row_label(enabled_row, kStrPresenceRail, pal, false, true);
        self._row_rail_enabled.value = add_row_value(enabled_row, vumeter_enabled_label(), pal);
        make_row_tappable(enabled_row, musicPresenceRailClickedEvt, &self);
    }

    lv_obj_t* profile_row = create_row_unit(self._cont_music_content, pal, kRowHeight);
    if (profile_row) {
        self._row_rail_profile.hit = profile_row;
        self._row_rail_profile.label = add_row_label(profile_row, kStrRailProfile, pal, false, true);
        self._row_rail_profile.value = add_row_value(profile_row, rail_profile_label(), pal);
        make_row_tappable(profile_row, musicProfileRowClickedEvt, &self);
    }
}

bool LvglSettingsPage::_ensureMusicRailView() {
    if (_view_music) {
        return true;
    }
    if (!_screen) {
        return false;
    }

    const YoRadioPalette& pal = yoradio_palette();

    _view_music = lv_obj_create(_screen);
    if (!_view_music) {
        return false;
    }
    lv_obj_set_width(_view_music, LV_PCT(100));
    lv_obj_set_flex_grow(_view_music, 1);
    lv_obj_set_flex_flow(_view_music, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(_view_music);
    lv_obj_set_style_pad_row(_view_music, 0, LV_PART_MAIN);
    lv_obj_add_flag(_view_music, LV_OBJ_FLAG_HIDDEN);

    create_detail_header(
        _view_music,
        kStrMusicRail,
        YORA_SETTINGS_GLYPH_MUSIC_RAIL,
        LvglSettingsPage::musicBackClickedEvt,
        _music_back_hit,
        _music_header_icon,
        _lbl_music_header,
        this,
        pal);

    build_music_rail_detail(*this, pal);
    block_gesture_bubble_deep(_view_music);
    _applyMusicRailProfileRowTreatment(pal);
    return true;
}

void LvglSettingsPage::_destroyMusicRailView() {
    if (_view_music) {
        lv_obj_del(_view_music);
    }
    _view_music = nullptr;
    _music_back_hit = nullptr;
    _music_header_icon = nullptr;
    _lbl_music_header = nullptr;
    _cont_music_content = nullptr;
    _row_rail_enabled = {};
    _row_rail_profile = {};
}

bool LvglSettingsPage::_ensureScrollingView() {
    if (_view_scrolling) {
        return true;
    }
    if (!_screen) {
        return false;
    }

    const YoRadioPalette& pal = yoradio_palette();

    _view_scrolling = lv_obj_create(_screen);
    if (!_view_scrolling) {
        return false;
    }
    lv_obj_set_width(_view_scrolling, LV_PCT(100));
    lv_obj_set_flex_grow(_view_scrolling, 1);
    lv_obj_set_flex_flow(_view_scrolling, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(_view_scrolling);
    lv_obj_set_style_pad_row(_view_scrolling, 0, LV_PART_MAIN);
    lv_obj_add_flag(_view_scrolling, LV_OBJ_FLAG_HIDDEN);

    // Reuses the Display glyph: Scrolling is a Display sub-page and the icon set is a
    // generated font subset, so this repair introduces no new glyph.
    // Pereispolzuem glif Display: Scrolling - podstranica Display, novyj glif ne vvodim.
    create_detail_header(
        _view_scrolling,
        kStrScrolling,
        YORA_SETTINGS_GLYPH_DISPLAY,
        LvglSettingsPage::scrollingBackClickedEvt,
        _scrolling_back_hit,
        _scrolling_header_icon,
        _lbl_scrolling_header,
        this,
        pal);

    build_scrolling_detail(*this, pal);
    block_gesture_bubble_deep(_view_scrolling);
    return true;
}

void LvglSettingsPage::_destroyScrollingView() {
    // Deleting the tree also deletes the registered preview label; lv_obj_null_on_delete()
    // inside lv_text_scroll clears its registry slot, so no stale LVGL pointer survives.
    // Udalenie dereva ubivaet i preview-lejbl; slot v reestre ochishchaetsya avtomaticheski.
    if (_view_scrolling) {
        lv_obj_del(_view_scrolling);
    }
    _view_scrolling = nullptr;
    _scrolling_back_hit = nullptr;
    _scrolling_header_icon = nullptr;
    _lbl_scrolling_header = nullptr;
    _cont_scrolling_content = nullptr;
    _lbl_scroll_speed_title = nullptr;
    _scroll_speed_slider = nullptr;
    _lbl_scroll_speed_value = nullptr;
    _lbl_scroll_delay_title = nullptr;
    _scroll_delay_slider = nullptr;
    _lbl_scroll_delay_value = nullptr;
    _lbl_preview_caption = nullptr;
    _lbl_scroll_preview = nullptr;
    _row_scroll_type = {};
    _scroll_speed_drag_active = false;
    _scroll_delay_drag_active = false;
}

bool LvglSettingsPage::_ensureSleepTimerView() {
    if (_view_sleep_timer) {
        return true;
    }
    if (!_screen) {
        return false;
    }

    const YoRadioPalette& pal = yoradio_palette();

    _view_sleep_timer = lv_obj_create(_screen);
    if (!_view_sleep_timer) {
        return false;
    }
    lv_obj_set_width(_view_sleep_timer, LV_PCT(100));
    lv_obj_set_flex_grow(_view_sleep_timer, 1);
    lv_obj_set_flex_flow(_view_sleep_timer, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(_view_sleep_timer);
    lv_obj_set_style_pad_row(_view_sleep_timer, 0, LV_PART_MAIN);
    lv_obj_add_flag(_view_sleep_timer, LV_OBJ_FLAG_HIDDEN);

    create_detail_header(
        _view_sleep_timer,
        kStrSleepTimer,
        YORA_SETTINGS_GLYPH_SLEEP_TIMER,
        LvglSettingsPage::sleepTimerBackClickedEvt,
        _sleep_timer_back_hit,
        _sleep_timer_header_icon,
        _lbl_sleep_timer_header,
        this,
        pal);

    build_sleep_timer_detail(*this, pal);
    block_gesture_bubble_deep(_view_sleep_timer);
    return true;
}

void LvglSettingsPage::_destroySleepTimerView() {
    if (_view_sleep_timer) {
        lv_obj_del(_view_sleep_timer);
    }
    _view_sleep_timer = nullptr;
    _sleep_timer_back_hit = nullptr;
    _sleep_timer_header_icon = nullptr;
    _lbl_sleep_timer_header = nullptr;
    _cont_sleep_timer_content = nullptr;
    for (uint8_t i = 0; i < 2; ++i) {
        _timer_tab_buttons[i] = nullptr;
        _timer_tab_labels[i] = nullptr;
        _timer_event_cards[i] = nullptr;
        _timer_event_titles[i] = nullptr;
        _timer_at_labels[i] = nullptr;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        _timer_sliders[i] = nullptr;
        _timer_captions[i] = nullptr;
        _timer_value_labels[i] = nullptr;
        _timer_drag_active[i] = false;
    }
    _lbl_timer_state = nullptr;
    _btn_timer_primary = nullptr;
    _lbl_timer_primary = nullptr;
    _btn_deep_sleep_now = nullptr;
    _lbl_deep_sleep_now = nullptr;
    _timer_syncing_controls = false;
    _timer_event1_draft = 0;
    _timer_event2_draft = 0;
}

void LvglSettingsPage::create() {
    if (_screen) return;

    const YoRadioPalette& pal = yoradio_palette();

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(_screen, LV_ACTIVE_PROFILE.frame_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, kRootRowGap, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    wgt_status_line::create(_screen, _status_line);
    if (!_status_line.root) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }
    add_bottom_divider(_screen, pal);

    if (!create_main_structure(*this, pal)) {
        lv_obj_del(_screen);
        _screen = nullptr;
        _nullHandles();
        return;
    }

    populate_main_rows(*this, pal);
    create_footer(_view_main, _footer_area, _lbl_footer, this, footerClickedEvt, pal);

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglSettingsPage::enter() {
    _view = SettingsView::Main;
    _brightness_drag_active = false;
    _dim_level_drag_active = false;
    _showView(SettingsView::Main);
    update();
}

void LvglSettingsPage::update() {
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
    if (_view == SettingsView::Main) {
        _syncMainRowValues();
    } else if (_view == SettingsView::MusicRail && _view_music) {
        _syncMusicRailValues();
    } else if (_view == SettingsView::SleepTimer && _view_sleep_timer) {
        _syncSleepTimerValues();
    }
}

void LvglSettingsPage::exit() {
    _brightness_drag_active = false;
    _dim_level_drag_active = false;
    _destroyMusicRailView();
    // FU6 UX: the Scrolling sub-view (and its registered preview label) must not survive
    // leaving the Settings slot. / Podstranica Scrolling ne dolzhna perezhit uhod so slota.
    _destroyScrollingView();
    _destroySleepTimerView();
    _view = SettingsView::Main;
}

void LvglSettingsPage::_showView(SettingsView view) {
    _view = view;
    if (!_view_main) return;

    if (view == SettingsView::Main) {
        lv_obj_clear_flag(_view_main, LV_OBJ_FLAG_HIDDEN);
        if (_view_display) {
            lv_obj_add_flag(_view_display, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_scrolling) {
            lv_obj_add_flag(_view_scrolling, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_sleep_timer) {
            lv_obj_add_flag(_view_sleep_timer, LV_OBJ_FLAG_HIDDEN);
        }
        _syncMainRowValues();
    } else if (view == SettingsView::Display) {
        if (!_view_display) return;
        lv_obj_add_flag(_view_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_view_display, LV_OBJ_FLAG_HIDDEN);
        if (_view_music) {
            lv_obj_add_flag(_view_music, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_scrolling) {
            lv_obj_add_flag(_view_scrolling, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_sleep_timer) {
            lv_obj_add_flag(_view_sleep_timer, LV_OBJ_FLAG_HIDDEN);
        }
        _syncDisplayValues();
    } else if (view == SettingsView::MusicRail) {
        if (!_view_music) return;
        lv_obj_add_flag(_view_main, LV_OBJ_FLAG_HIDDEN);
        if (_view_display) {
            lv_obj_add_flag(_view_display, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_scrolling) {
            lv_obj_add_flag(_view_scrolling, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_sleep_timer) {
            lv_obj_add_flag(_view_sleep_timer, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(_view_music, LV_OBJ_FLAG_HIDDEN);
        _syncMusicRailValues();
    } else if (view == SettingsView::Scrolling) {
        if (!_view_scrolling) return;
        lv_obj_add_flag(_view_main, LV_OBJ_FLAG_HIDDEN);
        if (_view_display) {
            lv_obj_add_flag(_view_display, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_music) {
            lv_obj_add_flag(_view_music, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_sleep_timer) {
            lv_obj_add_flag(_view_sleep_timer, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(_view_scrolling, LV_OBJ_FLAG_HIDDEN);
        _syncScrollingValues();
    } else if (view == SettingsView::SleepTimer) {
        if (!_view_sleep_timer) return;
        lv_obj_add_flag(_view_main, LV_OBJ_FLAG_HIDDEN);
        if (_view_display) {
            lv_obj_add_flag(_view_display, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_music) {
            lv_obj_add_flag(_view_music, LV_OBJ_FLAG_HIDDEN);
        }
        if (_view_scrolling) {
            lv_obj_add_flag(_view_scrolling, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(_view_sleep_timer, LV_OBJ_FLAG_HIDDEN);
        _syncSleepTimerValues();
    }
}

void LvglSettingsPage::_syncMainRowValues() {
#ifdef ENABLE_BRIGHTNESS_CONTROL
    if (_row_display.value) {
        char buf[8];
        format_brightness_pct(buf, sizeof(buf), config.store.brightness);
        lv_label_set_text(_row_display.value, buf);
    }
#endif
    if (_row_music.value) {
        lv_label_set_text(_row_music.value, vumeter_enabled_label());
    }
    if (_row_resume_startup.value) {
        lv_label_set_text(_row_resume_startup.value, resume_on_startup_value_label());
    }
    if (_row_sleep_timer.value) {
        lv_label_set_text(_row_sleep_timer.value, sleep_timer_settings_value_label());
    }
    if (_row_wifi.value) {
        if (WiFi.status() == WL_CONNECTED) {
            const String ssid = WiFi.SSID();
            if (ssid.length() > 0) {
                lv_label_set_text(_row_wifi.value, ssid.c_str());
            } else {
                lv_label_set_text(_row_wifi.value, kStrValNotConnected);
            }
        } else {
            lv_label_set_text(_row_wifi.value, kStrValNotConnected);
        }
    }
}

void LvglSettingsPage::_syncDisplayValues() {
#ifdef ENABLE_BRIGHTNESS_CONTROL
    if (_brightness_slider) {
        const int32_t val = lv_slider_get_value(_brightness_slider);
        if (val != static_cast<int32_t>(config.store.brightness)) {
            lv_slider_set_value(_brightness_slider, config.store.brightness, LV_ANIM_OFF);
        }
        _updateBrightnessLabels(config.store.brightness);
    }
#endif
    if (_row_theme.value) {
        lv_label_set_text(_row_theme.value, theme_preset_label(themeRequestedPreset()));
    }
    if (_row_perf_monitor.value) {
        lv_label_set_text(_row_perf_monitor.value, performance_monitor_enabled_label());
    }
    if (_row_autodim.value) {
        lv_label_set_text(_row_autodim.value, autodim_enabled_label());
    }
    if (_row_dim_after.value) {
        lv_label_set_text(
            _row_dim_after.value, autodim_timeout_label(config.store.autodim_timeout_sec));
    }
    if (_dim_level_slider) {
        _syncDimLevelSliderRange(true);
    }
    _applyAutodimRowTreatment(yoradio_palette());
}

// FU6 UX: Scrolling sub-view value sync. Never fights an active drag.
// FU6 UX: sinhronizaciya podstranicy Scrolling; ne meshaet aktivnomu peretaskivaniyu.
void LvglSettingsPage::_syncScrollingValues() {
    if (_row_scroll_type.value) {
        lv_label_set_text(_row_scroll_type.value, scroll_type_label(text_scroll::mode()));
    }
    if (_scroll_speed_slider && !_scroll_speed_drag_active) {
        const uint8_t sp = text_scroll::speedPxPerSec();
        if (lv_slider_get_value(_scroll_speed_slider) != static_cast<int32_t>(sp)) {
            lv_slider_set_value(_scroll_speed_slider, sp, LV_ANIM_OFF);
        }
        _updateScrollSpeedLabel(sp);
    }
    if (_scroll_delay_slider && !_scroll_delay_drag_active) {
        const uint8_t dl = text_scroll::delaySec();
        if (lv_slider_get_value(_scroll_delay_slider) != static_cast<int32_t>(dl)) {
            lv_slider_set_value(_scroll_delay_slider, dl, LV_ANIM_OFF);
        }
        _updateScrollDelayLabel(dl);
    }
}

void LvglSettingsPage::_updateTimerDraftFromSliders() {
    if (!_timer_sliders[0] || !_timer_sliders[1] ||
        !_timer_sliders[2] || !_timer_sliders[3]) return;
    _timer_event1_draft = timer_sanitize_minutes(
        static_cast<uint16_t>(lv_slider_get_value(_timer_sliders[0])) * 60u +
        static_cast<uint16_t>(lv_slider_get_value(_timer_sliders[1])));
    _timer_event2_draft = timer_sanitize_minutes(
        static_cast<uint16_t>(lv_slider_get_value(_timer_sliders[2])) * 60u +
        static_cast<uint16_t>(lv_slider_get_value(_timer_sliders[3])));
}

void LvglSettingsPage::_loadTimerTabValues(bool force) {
    const TimerRuntimeSnapshot snapshot = timer_runtime_snapshot();
    const bool active_tab =
        (!_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::Radio) ||
        (_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::DeepSleep);

    uint16_t event1 = 0;
    uint16_t event2 = 0;
    if (active_tab) {
        event1 = _timers_deep_sleep_tab ? snapshot.deep_sleep_after_minutes
                                        : snapshot.radio_stop_minutes;
        event2 = _timers_deep_sleep_tab ? snapshot.deep_sleep_wake_after_minutes
                                        : snapshot.radio_start_minutes;
    } else {
        event1 = timer_preset_minutes(_timers_deep_sleep_tab
                                          ? TimerPreset::DeepSleepAfter
                                          : TimerPreset::RadioStop);
        event2 = timer_preset_minutes(_timers_deep_sleep_tab
                                          ? TimerPreset::DeepSleepWakeAfter
                                          : TimerPreset::RadioStart);
    }

    const bool dragging = _timer_drag_active[0] || _timer_drag_active[1] ||
                          _timer_drag_active[2] || _timer_drag_active[3];
    if (!force && dragging) return;
    _timer_event1_draft = event1;
    _timer_event2_draft = event2;
    const uint16_t values[4] = {
        static_cast<uint16_t>(event1 / 60u),
        static_cast<uint16_t>(event1 % 60u),
        static_cast<uint16_t>(event2 / 60u),
        static_cast<uint16_t>(event2 % 60u),
    };
    _timer_syncing_controls = true;
    for (uint8_t i = 0; i < 4; ++i) {
        if (_timer_sliders[i] && lv_slider_get_value(_timer_sliders[i]) != values[i]) {
            lv_slider_set_value(_timer_sliders[i], values[i], LV_ANIM_OFF);
        }
    }
    _timer_syncing_controls = false;
}

void LvglSettingsPage::_updateTimerLabels() {
    if (_timer_event_titles[0]) {
        lv_label_set_text(_timer_event_titles[0],
                          _timers_deep_sleep_tab ? kStrDeepSleepAfter : kStrStopRadioAfter);
    }
    if (_timer_event_titles[1]) {
        lv_label_set_text(_timer_event_titles[1],
                          _timers_deep_sleep_tab ? kStrWakeAfterSleep : kStrStartRadioAfter);
    }

    const uint16_t values[4] = {
        static_cast<uint16_t>(_timer_event1_draft / 60u),
        static_cast<uint16_t>(_timer_event1_draft % 60u),
        static_cast<uint16_t>(_timer_event2_draft / 60u),
        static_cast<uint16_t>(_timer_event2_draft % 60u),
    };
    for (uint8_t i = 0; i < 4; ++i) {
        if (!_timer_value_labels[i]) continue;
        char value[16];
        if ((i & 1u) == 0) {
            format_timer_hours(value, sizeof(value), static_cast<uint8_t>(values[i]));
        } else {
            format_timer_minutes(value, sizeof(value), static_cast<uint8_t>(values[i]));
        }
        lv_label_set_text(_timer_value_labels[i], value);
    }

    const TimerRuntimeSnapshot snapshot = timer_runtime_snapshot();
    const bool active_tab =
        (!_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::Radio) ||
        (_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::DeepSleep);
    time_t now = 0;
    const bool clock_synced = timer_local_clock_now(&now);
    time_t event1_at = 0;
    time_t event2_at = 0;
    if (active_tab) {
        event1_at = _timers_deep_sleep_tab ? snapshot.deep_sleep_at : snapshot.radio_stop_at;
        event2_at = _timers_deep_sleep_tab ? snapshot.deep_sleep_wake_at : snapshot.radio_start_at;
    } else if (clock_synced) {
        if (_timer_event1_draft > 0) {
            event1_at = now + static_cast<time_t>(_timer_event1_draft) * 60;
        }
        if (_timer_event2_draft > 0) {
            const uint16_t base_minutes = _timers_deep_sleep_tab ? _timer_event1_draft : 0;
            event2_at = now + static_cast<time_t>(base_minutes + _timer_event2_draft) * 60;
        }
    }

    char at_text[48];
    format_timer_at(at_text, sizeof(at_text), _timers_deep_sleep_tab ? "SLEEP" : "STOPS",
                    event1_at, now);
    if (_timer_at_labels[0]) lv_label_set_text(_timer_at_labels[0], at_text);
    format_timer_at(at_text, sizeof(at_text), _timers_deep_sleep_tab ? "WAKE" : "STARTS",
                    event2_at, now);
    if (_timer_at_labels[1]) lv_label_set_text(_timer_at_labels[1], at_text);

    char state[96] = "";
    if (snapshot.plan != TimerPlanKind::None && !active_tab) {
        snprintf(state, sizeof(state), "%s",
                 snapshot.plan == TimerPlanKind::Radio ? kStrCancelRadioFirst : kStrCancelDeepFirst);
    } else if (active_tab && snapshot.shutdown_active) {
        snprintf(state, sizeof(state), "%s", kStrShutdownActive);
    } else if (active_tab && !_timers_deep_sleep_tab) {
        char stop[32] = "";
        char start[32] = "";
        if (snapshot.radio_stop_active) {
            format_timer_remaining(stop, sizeof(stop), "STOP IN",
                                   snapshot.radio_stop_remaining_seconds);
        }
        if (snapshot.radio_start_active) {
            format_timer_remaining(start, sizeof(start), "START IN",
                                   snapshot.radio_start_remaining_seconds);
        }
        snprintf(state, sizeof(state), "%s%s%s", stop,
                 (stop[0] && start[0]) ? "  |  " : "", start);
        const bool active_clock_missing =
            (snapshot.radio_stop_active && snapshot.radio_stop_at <= 0) ||
            (snapshot.radio_start_active && snapshot.radio_start_at <= 0);
        if (active_clock_missing) {
            strlcat(state, " | CLOCK NOT SYNCED", sizeof(state));
        }
    } else if (active_tab) {
        format_timer_remaining(state, sizeof(state), "DEEP SLEEP IN",
                               snapshot.deep_sleep_remaining_seconds);
        if (snapshot.deep_sleep_at <= 0 ||
            (snapshot.deep_sleep_wake_after_minutes > 0 && snapshot.deep_sleep_wake_at <= 0)) {
            strlcat(state, " | CLOCK NOT SYNCED", sizeof(state));
        }
    } else if (!_timers_deep_sleep_tab && _timer_event1_draft > 0 &&
               _timer_event1_draft == _timer_event2_draft) {
        snprintf(state, sizeof(state), "%s", kStrStopStartConflict);
    } else if (!clock_synced && (_timer_event1_draft > 0 || _timer_event2_draft > 0)) {
        snprintf(state, sizeof(state), "%s", kStrClockNotSynced);
    } else {
        snprintf(state, sizeof(state), "%s", kStrZeroDisabled);
    }
    if (_lbl_timer_state) lv_label_set_text(_lbl_timer_state, state);
}

// TIMERS value sync runs on the existing Settings cadence (DspTask). Preview follows current
// local time before Start; active ...AT values come from frozen runtime epochs and never drift.
// Sync идёт в существующем cadence Settings на DspTask. Active ...AT берётся из snapshot.
void LvglSettingsPage::_syncSleepTimerValues() {
    _loadTimerTabValues(false);
    _updateTimerLabels();
    _applyTimerInteractionState();
    _applyTimersTheme(yoradio_palette());
}

void LvglSettingsPage::_syncMusicRailValues() {
    if (_row_rail_enabled.value) {
        lv_label_set_text(_row_rail_enabled.value, vumeter_enabled_label());
    }
    if (_row_rail_profile.value) {
        lv_label_set_text(_row_rail_profile.value, rail_profile_label());
    }
    _applyMusicRailProfileRowTreatment(yoradio_palette());
}

void LvglSettingsPage::_applyMusicRailProfileRowTreatment(const YoRadioPalette& pal) {
    if (!_row_rail_profile.hit) return;

    const bool disabled = !config.store.vumeter;
    if (_row_rail_profile.label) {
        lv_obj_set_style_text_color(
            _row_rail_profile.label, disabled ? pal.text_meta : pal.text_primary, LV_PART_MAIN);
    }
    if (_row_rail_profile.value) {
        lv_obj_set_style_text_color(_row_rail_profile.value, pal.text_meta, LV_PART_MAIN);
    }
    if (disabled) {
        lv_obj_clear_flag(_row_rail_profile.hit, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_flag(_row_rail_profile.hit, LV_OBJ_FLAG_CLICKABLE);
    }
}

void LvglSettingsPage::_applyTimerInteractionState() {
    const TimerRuntimeSnapshot snapshot = timer_runtime_snapshot();
    const bool active_tab =
        (!_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::Radio) ||
        (_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::DeepSleep);
    const bool other_active = snapshot.plan != TimerPlanKind::None && !active_tab;
    const bool sliders_enabled = snapshot.plan == TimerPlanKind::None;
    for (lv_obj_t* slider : _timer_sliders) {
        if (!slider) continue;
        lv_obj_set_style_opa(slider, sliders_enabled ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
        if (sliders_enabled) {
            lv_obj_clear_state(slider, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(slider, LV_STATE_DISABLED);
        }
    }

    bool primary_enabled = false;
    if (active_tab) {
        primary_enabled = !snapshot.shutdown_active;
    } else if (!other_active) {
        primary_enabled = _timers_deep_sleep_tab
                              ? (_timer_event1_draft > 0)
                              : ((_timer_event1_draft > 0 || _timer_event2_draft > 0) &&
                                 !(_timer_event1_draft > 0 &&
                                   _timer_event1_draft == _timer_event2_draft));
    }
    if (_lbl_timer_primary) {
        lv_label_set_text(_lbl_timer_primary,
                          active_tab
                              ? (_timers_deep_sleep_tab ? kStrCancelDeepSleepTimer
                                                        : kStrCancelRadioTimer)
                              : (_timers_deep_sleep_tab ? kStrStartDeepSleepTimer
                                                        : kStrStartRadioTimer));
    }

    const bool now_enabled = _timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::None;
    if (_btn_deep_sleep_now) {
        if (_timers_deep_sleep_tab) {
            lv_obj_clear_flag(_btn_deep_sleep_now, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_btn_deep_sleep_now, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const YoRadioPalette& pal = yoradio_palette();
    style_timer_action_button(_btn_timer_primary, pal, true, primary_enabled);
    style_timer_action_button(_btn_deep_sleep_now, pal, false, now_enabled);
}

void LvglSettingsPage::_updateBrightnessLabels(uint8_t pct) {
    char buf[8];
    format_brightness_pct(buf, sizeof(buf), pct);
    if (_lbl_brightness_value) {
        lv_label_set_text(_lbl_brightness_value, buf);
    }
    if (_row_display.value) {
        lv_label_set_text(_row_display.value, buf);
    }
}

void LvglSettingsPage::_updateDimLevelLabels(uint8_t pct) {
    char buf[8];
    format_brightness_pct(buf, sizeof(buf), pct);
    if (_lbl_dim_level_value) {
        lv_label_set_text(_lbl_dim_level_value, buf);
    }
}

uint8_t LvglSettingsPage::_normalBrightnessForDimUi() const {
    if (_brightness_slider && _brightness_drag_active) {
        return static_cast<uint8_t>(lv_slider_get_value(_brightness_slider));
    }
    return config.store.brightness;
}

void LvglSettingsPage::_syncDimLevelSliderRange(bool persist_clamp) {
    if (!_dim_level_slider) {
        return;
    }
    const uint8_t normal = _normalBrightnessForDimUi();
    const uint8_t max_lv = autodim_level_max_for_brightness(normal);
    lv_slider_set_range(_dim_level_slider, kDimLevelMinUi, max_lv);

    uint8_t level = config.store.autodim_level;
    if (persist_clamp && level > max_lv) {
        level = max_lv;
        config.store.autodim_level = level;
    }
    const uint8_t show = (level > max_lv) ? max_lv : level;
    lv_slider_set_value(_dim_level_slider, show, LV_ANIM_OFF);
    _updateDimLevelLabels(show);
}

void LvglSettingsPage::_applyAutodimRowTreatment(const YoRadioPalette& pal) {
    const bool secondary = !config.store.autodim_enabled;
    auto apply_secondary = [&](const RowChrome& r) {
        if (r.label) {
            lv_obj_set_style_text_color(
                r.label, secondary ? pal.text_meta : pal.text_primary, LV_PART_MAIN);
        }
        if (r.value) {
            lv_obj_set_style_text_color(r.value, pal.text_meta, LV_PART_MAIN);
        }
    };
    apply_secondary(_row_dim_after);
    if (_lbl_dim_level_title) {
        lv_obj_set_style_text_color(
            _lbl_dim_level_title, secondary ? pal.text_meta : pal.text_primary, LV_PART_MAIN);
    }
    if (_lbl_dim_level_value) {
        lv_obj_set_style_text_color(_lbl_dim_level_value, pal.text_meta, LV_PART_MAIN);
    }
}

void LvglSettingsPage::_applyTimersTheme(const YoRadioPalette& pal) {
    const TimerRuntimeSnapshot snapshot = timer_runtime_snapshot();
    for (uint8_t i = 0; i < 2; ++i) {
        const bool selected = (i == (_timers_deep_sleep_tab ? 1u : 0u));
        if (_timer_tab_buttons[i]) {
            lv_obj_set_style_bg_color(_timer_tab_buttons[i],
                                      selected ? pal.accent_soft : pal.panel_background,
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_timer_tab_buttons[i], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(_timer_tab_buttons[i],
                                          selected ? pal.accent : pal.divider, LV_PART_MAIN);
            lv_obj_set_style_border_width(_timer_tab_buttons[i], 1, LV_PART_MAIN);
        }
        if (_timer_tab_labels[i]) {
            lv_obj_set_style_text_color(_timer_tab_labels[i],
                                        selected ? pal.accent : pal.text_secondary, LV_PART_MAIN);
        }
        if (_timer_event_cards[i]) {
            lv_obj_set_style_bg_color(_timer_event_cards[i], pal.panel_background, LV_PART_MAIN);
            lv_obj_set_style_border_color(_timer_event_cards[i], pal.divider, LV_PART_MAIN);
        }
        if (_timer_event_titles[i]) {
            lv_obj_set_style_text_color(_timer_event_titles[i], pal.text_primary, LV_PART_MAIN);
        }
        if (_timer_at_labels[i]) {
            lv_obj_set_style_text_color(_timer_at_labels[i], pal.text_meta, LV_PART_MAIN);
        }
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (_timer_captions[i]) {
            lv_obj_set_style_text_color(_timer_captions[i], pal.text_secondary, LV_PART_MAIN);
        }
        if (_timer_value_labels[i]) {
            lv_obj_set_style_text_color(_timer_value_labels[i], pal.text_meta, LV_PART_MAIN);
        }
        style_timers_slider(_timer_sliders[i], pal);
    }

    const bool active_tab =
        (!_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::Radio) ||
        (_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::DeepSleep);
    const bool warning =
        (snapshot.plan != TimerPlanKind::None && !active_tab) || snapshot.shutdown_active ||
        (!_timers_deep_sleep_tab && _timer_event1_draft > 0 &&
         _timer_event1_draft == _timer_event2_draft);
    if (_lbl_timer_state) {
        lv_obj_set_style_text_color(_lbl_timer_state,
                                    warning ? pal.accent : pal.text_meta, LV_PART_MAIN);
    }

    const bool primary_enabled =
        _btn_timer_primary && lv_obj_has_flag(_btn_timer_primary, LV_OBJ_FLAG_CLICKABLE);
    const bool now_enabled =
        _btn_deep_sleep_now && lv_obj_has_flag(_btn_deep_sleep_now, LV_OBJ_FLAG_CLICKABLE);
    style_timer_action_button(_btn_timer_primary, pal, true, primary_enabled);
    style_timer_action_button(_btn_deep_sleep_now, pal, false, now_enabled);
    if (_lbl_timer_primary) {
        lv_obj_set_style_text_color(_lbl_timer_primary,
                                    primary_enabled ? pal.device_background : pal.text_meta,
                                    LV_PART_MAIN);
    }
    if (_lbl_deep_sleep_now) {
        lv_obj_set_style_text_color(_lbl_deep_sleep_now,
                                    now_enabled ? pal.text_secondary : pal.text_meta,
                                    LV_PART_MAIN);
    }
}

void LvglSettingsPage::_applySliderTheme(const YoRadioPalette& pal) {
#ifdef ENABLE_BRIGHTNESS_CONTROL
    style_settings_bar_slider(_brightness_slider, pal);
#endif
    style_settings_bar_slider(_dim_level_slider, pal);
    style_settings_bar_slider(_scroll_speed_slider, pal);
    style_settings_bar_slider(_scroll_delay_slider, pal);
}

void LvglSettingsPage::footerClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || !self->_footer_area) return;
    if (lv_event_get_target(e) != self->_footer_area) return;
    goToCarouselPage(PageChain::MAIN_INDEX);
}

void LvglSettingsPage::wifiRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    notifyPageChainActivity("settings-wifi");
    // Phase A: request only — deferred via lv_async_call; do NOT touch origin tree here.
    // Фаза A: только запрос — deferred через lv_async_call; origin tree здесь не трогать.
    Serial.println("[SETTINGS_WIFI] request");
    requestSettingsWifiServiceEntry();
}

void LvglSettingsPage::displayRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    notifyPageChainActivity("settings-display");
    if (!self->_ensureDisplayView()) {
        return;
    }
    self->_showView(SettingsView::Display);
}

void LvglSettingsPage::displayBackClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_brightness_drag_active = false;
    self->_dim_level_drag_active = false;
    self->_showView(SettingsView::Main);
}

void LvglSettingsPage::musicRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    notifyPageChainActivity("settings-music-rail");
    if (!self->_ensureMusicRailView()) {
        return;
    }
    self->_showView(SettingsView::MusicRail);
}

void LvglSettingsPage::scrollingRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    notifyPageChainActivity("settings-scrolling");
    if (!self->_ensureScrollingView()) {
        return;
    }
    self->_showView(SettingsView::Scrolling);
}

void LvglSettingsPage::scrollingBackClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    // Back returns to Display (the parent page), not to the Settings root.
    // Destroy first: the preview label must not outlive the sub-view.
    // Back vozvrashchaet na Display; snachala destroy - preview ne dolzhen perezhit podstranicu.
    self->_destroyScrollingView();
    self->_showView(SettingsView::Display);
}

void LvglSettingsPage::musicBackClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    // 6.7S6: destroy Music detail on Back — do not accumulate lazy trees in LVGL pool.
    // 6.7S6: уничтожаем Music detail на Back — не копим lazy-деревья в LVGL pool.
    self->_destroyMusicRailView();
    self->_showView(SettingsView::Main);
}

void LvglSettingsPage::musicPresenceRailClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    const bool new_value = !config.store.vumeter;
    config.saveValue(&config.store.vumeter, new_value);
    notifyPageChainActivity("settings-rail-enabled");
    self->_syncMusicRailValues();
    self->_syncMainRowValues();
}

void LvglSettingsPage::musicProfileRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    if (!config.store.vumeter) return;

    const bool new_value = !config.store.usespectrum;
    config.saveValue(&config.store.usespectrum, new_value);
    notifyPageChainActivity("settings-rail-profile");
    self->_syncMusicRailValues();
}

void LvglSettingsPage::resumeOnStartupRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    // Match WebUI smartstart path (netserver.cpp) — boot reads store.smartstart on next boot.
    // Совпадает с WebUI smartstart (netserver.cpp) — boot читает store.smartstart при следующем старте.
    if (config.store.smartstart == 2) {
        uint8_t ss = 1;
        if (!player.isRunning()) {
            ss = 0;
        }
        config.setSmartStart(ss);
    } else {
        config.setSmartStart(2);
    }
    notifyPageChainActivity("settings-resume-startup");
    self->_syncMainRowValues();
}

void LvglSettingsPage::timersRadioTabClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_timers_deep_sleep_tab = false;
    for (bool& dragging : self->_timer_drag_active) dragging = false;
    self->_loadTimerTabValues(true);
    self->_syncSleepTimerValues();
    notifyPageChainActivity("settings-timers-radio-tab");
}

void LvglSettingsPage::timersDeepSleepTabClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_timers_deep_sleep_tab = true;
    for (bool& dragging : self->_timer_drag_active) dragging = false;
    self->_loadTimerTabValues(true);
    self->_syncSleepTimerValues();
    notifyPageChainActivity("settings-timers-deep-tab");
}

void LvglSettingsPage::_handleTimerSliderEvent(lv_event_t* e, uint8_t slider_index) {
    if (!e || _timer_syncing_controls || slider_index >= 4 || !_timer_sliders[slider_index] ||
        lv_event_get_target(e) != _timer_sliders[slider_index]) return;
    if (timer_active_plan() != TimerPlanKind::None) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        _timer_drag_active[slider_index] = true;
        notifyPageChainActivity("settings-timer-slider");
        return;
    }
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Preview-only draft: no config/NVS mutation during drag. Runtime plans use their own
        // immutable snapshot. / Во время drag меняется только preview, без config/NVS.
        _timer_drag_active[slider_index] = true;
        _updateTimerDraftFromSliders();
        _updateTimerLabels();
        _applyTimerInteractionState();
        _applyTimersTheme(yoradio_palette());
        notifyPageChainActivity("settings-timer-slider");
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        _updateTimerDraftFromSliders();
        const bool first_event = slider_index < 2;
        const TimerPreset preset = _timers_deep_sleep_tab
                                       ? (first_event ? TimerPreset::DeepSleepAfter
                                                      : TimerPreset::DeepSleepWakeAfter)
                                       : (first_event ? TimerPreset::RadioStop
                                                      : TimerPreset::RadioStart);
        timer_preset_set_minutes(preset,
                                 first_event ? _timer_event1_draft : _timer_event2_draft);
        _timer_drag_active[slider_index] = false;
        _syncSleepTimerValues();
    }
}

void LvglSettingsPage::timerEvent1HoursSliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (self) self->_handleTimerSliderEvent(e, 0);
}

void LvglSettingsPage::timerEvent1MinutesSliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (self) self->_handleTimerSliderEvent(e, 1);
}

void LvglSettingsPage::timerEvent2HoursSliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (self) self->_handleTimerSliderEvent(e, 2);
}

void LvglSettingsPage::timerEvent2MinutesSliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (self) self->_handleTimerSliderEvent(e, 3);
}

void LvglSettingsPage::timerPrimaryClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    const TimerRuntimeSnapshot snapshot = timer_runtime_snapshot();
    const bool active_tab =
        (!self->_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::Radio) ||
        (self->_timers_deep_sleep_tab && snapshot.plan == TimerPlanKind::DeepSleep);
    if (active_tab) {
        if (self->_timers_deep_sleep_tab) {
            timer_cancel_deep_sleep();
        } else {
            timer_cancel_radio_plan();
        }
    } else if (snapshot.plan == TimerPlanKind::None) {
        if (self->_timers_deep_sleep_tab) {
            timer_schedule_deep_sleep(
                self->_timer_event1_draft,
                DeepSleepWakeRequest::explicitMinutes(self->_timer_event2_draft));
        } else {
            timer_schedule_radio_plan(self->_timer_event1_draft, self->_timer_event2_draft);
        }
    }
    for (bool& dragging : self->_timer_drag_active) dragging = false;
    self->_loadTimerTabValues(true);
    self->_syncSleepTimerValues();
    wgt_status_line::update(self->_status_line);
    notifyPageChainActivity("settings-timer-primary");
}

void LvglSettingsPage::sleepTimerRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    notifyPageChainActivity("settings-sleep-timer-open");
    if (!self->_ensureSleepTimerView()) {
        return;
    }
    self->_showView(SettingsView::SleepTimer);
}

void LvglSettingsPage::enterDeepSleepNowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || timer_active_plan() != TimerPlanKind::None) return;
    notifyPageChainActivity("settings-enter-deep-sleep-now");
    // Same queued path as bare deepsleep; visible WAKE AFTER SLEEP is an explicit snapshot.
    // Тот же queued path, что bare deepsleep; видимый WAKE AFTER SLEEP фиксируется snapshot-ом.
    timer_request_deep_sleep_now(
        DeepSleepWakeRequest::explicitMinutes(self->_timer_event2_draft));
}

void LvglSettingsPage::sleepTimerBackClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    // Destroy on Back — same lifecycle contract as Music Rail: no lazy tree accumulates in
    // the LVGL pool across repeated visits. / Уничтожаем на Back — контракт как у Music Rail.
    self->_destroySleepTimerView();
    self->_showView(SettingsView::Main);
}

void LvglSettingsPage::themeRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    // S6-THEME-01: cycle from the *requested* preset, not the applied one. During the
    // coalescing window the apply has not happened yet, so cycling from applied would make
    // every rapid tap toggle between the same two values.
    // S6-THEME-01: цикл от *запрошенного* пресета, не от применённого. В окне коалесинга
    // применение ещё не произошло, и цикл от применённого заставил бы каждое быстрое
    // нажатие переключаться между одними и теми же двумя значениями.
    const ThemePreset next = cycle_theme_preset(themeRequestedPreset());
    // S6-THEME-01: this only records the request; the heavy LVGL theme transaction and its
    // Repair-E recovery run later from taskHandler(). Do not restart/lv_refr_now here —
    // this callback still runs inside lv_timer_handler.
    // S6-THEME-01: здесь только регистрируется запрос; тяжёлая транзакция темы и её
    // Repair-E recovery выполняются позже из taskHandler(). Не restart/lv_refr_now здесь —
    // callback ещё внутри lv_timer_handler.
    onThemePresetChanged(static_cast<uint8_t>(next));
    self->_syncDisplayValues();
    self->_syncMainRowValues();
}

void LvglSettingsPage::brightnessSliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || !self->_brightness_slider) return;
    if (lv_event_get_target(e) != self->_brightness_slider) return;

    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        self->_brightness_drag_active = true;
        notifyPageChainActivity("settings-brightness");
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        self->_brightness_drag_active = true;
        uint8_t val = static_cast<uint8_t>(lv_slider_get_value(self->_brightness_slider));
        if (val < kBrightnessMinUi) {
            val = kBrightnessMinUi;
            lv_slider_set_value(self->_brightness_slider, val, LV_ANIM_OFF);
        }
        config.store.brightness = val;
        config.setBrightness(false);
        self->_updateBrightnessLabels(val);
        self->_syncDimLevelSliderRange(false);
        notifyPageChainActivity("settings-brightness");
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        uint8_t val = static_cast<uint8_t>(lv_slider_get_value(self->_brightness_slider));
        if (val < kBrightnessMinUi) {
            val = static_cast<uint8_t>(kBrightnessMinUi);
            lv_slider_set_value(self->_brightness_slider, val, LV_ANIM_OFF);
        }
        config.store.brightness = val;
        config.setBrightness(true);
        self->_updateBrightnessLabels(val);
        self->_syncDimLevelSliderRange(true);
        self->_brightness_drag_active = false;
    }
}

void LvglSettingsPage::performanceMonitorRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    const bool new_value = !config.store.performance_monitor;
    config.saveValue(&config.store.performance_monitor, new_value);
    applyPerformanceMonitorState(new_value);
    notifyPageChainActivity("settings-perf-monitor");
    self->_syncDisplayValues();
}

void LvglSettingsPage::autodimRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    config.saveValue(&config.store.autodim_enabled, !config.store.autodim_enabled);
    if (!config.store.autodim_enabled) {
        autodim_on_disabled();
    } else {
        notifyPageChainActivity("settings-autodim");
    }
    self->_syncDisplayValues();
}

void LvglSettingsPage::dimAfterRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    cycle_autodim_timeout_sec();
    notifyPageChainActivity("settings-dim-after");
    self->_syncDisplayValues();
}

void LvglSettingsPage::dimLevelSliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || !self->_dim_level_slider) return;
    if (lv_event_get_target(e) != self->_dim_level_slider) return;

    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        self->_dim_level_drag_active = true;
        notifyPageChainActivity("settings-dim-level");
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        self->_dim_level_drag_active = true;
        const uint8_t max_lv = autodim_level_max_for_brightness(self->_normalBrightnessForDimUi());
        uint8_t val = static_cast<uint8_t>(lv_slider_get_value(self->_dim_level_slider));
        if (val < kDimLevelMinUi) {
            val = kDimLevelMinUi;
            lv_slider_set_value(self->_dim_level_slider, val, LV_ANIM_OFF);
        }
        if (val > max_lv) {
            val = max_lv;
            lv_slider_set_value(self->_dim_level_slider, val, LV_ANIM_OFF);
        }
        config.store.autodim_level = val;
        autodim_on_dim_level_changed();
        self->_updateDimLevelLabels(val);
        notifyPageChainActivity("settings-dim-level");
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        const uint8_t max_lv = autodim_level_max_for_brightness(self->_normalBrightnessForDimUi());
        uint8_t val = static_cast<uint8_t>(lv_slider_get_value(self->_dim_level_slider));
        if (val < kDimLevelMinUi) {
            val = kDimLevelMinUi;
            lv_slider_set_value(self->_dim_level_slider, val, LV_ANIM_OFF);
        }
        if (val > max_lv) {
            val = max_lv;
            lv_slider_set_value(self->_dim_level_slider, val, LV_ANIM_OFF);
        }
        config.store.autodim_level = val;
        // force=true after live drag (store already equals val) — mirrors config.saveVolume().
        // force=true после live drag — как config.saveVolume().
        config.saveValue(&config.store.autodim_level, val, true, true);
        autodim_on_dim_level_changed();
        self->_updateDimLevelLabels(val);
        self->_dim_level_drag_active = false;
    }
}

// ── FU6-A handlers ────────────────────────────────────────────────────────────────────────────
// Slider pattern mirrors dimLevelSliderEvt(): VALUE_CHANGED applies live without touching NVS,
// RELEASED persists once with force=true (the store already equals the dragged value).
// Слайдеры повторяют dimLevelSliderEvt(): VALUE_CHANGED — живое применение без записи в NVS,
// RELEASED — одна запись с force=true.

void LvglSettingsPage::_updateScrollSpeedLabel(uint8_t px_per_sec) {
    if (!_lbl_scroll_speed_value) return;
    char buf[16];
    format_scroll_speed(buf, sizeof(buf), px_per_sec);
    lv_label_set_text(_lbl_scroll_speed_value, buf);
}

void LvglSettingsPage::_updateScrollDelayLabel(uint8_t sec) {
    if (!_lbl_scroll_delay_value) return;
    char buf[16];
    format_scroll_delay(buf, sizeof(buf), sec);
    lv_label_set_text(_lbl_scroll_delay_value, buf);
}

void LvglSettingsPage::scrollSpeedSliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || !self->_scroll_speed_slider) return;
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        self->_scroll_speed_drag_active = true;
        notifyPageChainActivity("settings-scroll-speed");
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        self->_scroll_speed_drag_active = true;
        // Snap to the product step so UI, runtime and NVS stay on one grid.
        // Привязка к шагу: UI, runtime и NVS — на одной сетке.
        const uint8_t raw = static_cast<uint8_t>(lv_slider_get_value(self->_scroll_speed_slider));
        const uint8_t val = text_scroll::sanitizeSpeed(raw);
        if (val != raw) {
            lv_slider_set_value(self->_scroll_speed_slider, val, LV_ANIM_OFF);
        }
        // Only re-apply when the SNAPPED value really moved. Touch samples arrive far faster than
        // the 5 px/s grid changes, and each re-apply walks every registered label.
        // Пересчитываем только при реальной смене значения на сетке: тач-сэмплы приходят намного
        // чаще, чем меняется шаг 5 px/s, а каждый пересчёт обходит все зарегистрированные лейблы.
        if (val != config.store.text_scroll_speed) {
            config.store.text_scroll_speed = val;  // live only — no NVS write while dragging
            text_scroll::reapplyAll();
            self->_updateScrollSpeedLabel(val);
        }
        notifyPageChainActivity("settings-scroll-speed");
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        const uint8_t val =
            text_scroll::sanitizeSpeed(static_cast<uint8_t>(lv_slider_get_value(self->_scroll_speed_slider)));
        lv_slider_set_value(self->_scroll_speed_slider, val, LV_ANIM_OFF);
        config.store.text_scroll_speed = val;
        // force=true after live drag (store already equals val) — mirrors dimLevelSliderEvt().
        config.saveValue(&config.store.text_scroll_speed, val, true, true);
        text_scroll::reapplyAll();
        self->_updateScrollSpeedLabel(val);
        self->_scroll_speed_drag_active = false;
    }
}

void LvglSettingsPage::scrollDelaySliderEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || !self->_scroll_delay_slider) return;
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        self->_scroll_delay_drag_active = true;
        notifyPageChainActivity("settings-scroll-delay");
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        self->_scroll_delay_drag_active = true;
        const uint8_t val =
            text_scroll::sanitizeDelaySec(static_cast<uint8_t>(lv_slider_get_value(self->_scroll_delay_slider)));
        // Same rate guard as the speed slider (11 distinct values across the whole drag).
        // Тот же ограничитель частоты, что и у слайдера скорости.
        if (val != config.store.text_scroll_delay_s) {
            config.store.text_scroll_delay_s = val;  // live only — no NVS write while dragging
            text_scroll::reapplyAll();
            self->_updateScrollDelayLabel(val);
        }
        notifyPageChainActivity("settings-scroll-delay");
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        const uint8_t val =
            text_scroll::sanitizeDelaySec(static_cast<uint8_t>(lv_slider_get_value(self->_scroll_delay_slider)));
        lv_slider_set_value(self->_scroll_delay_slider, val, LV_ANIM_OFF);
        config.store.text_scroll_delay_s = val;
        config.saveValue(&config.store.text_scroll_delay_s, val, true, true);
        text_scroll::reapplyAll();
        self->_updateScrollDelayLabel(val);
        self->_scroll_delay_drag_active = false;
    }
}

void LvglSettingsPage::scrollTypeRowClickedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    cycle_scroll_type();  // persists + text_scroll::reapplyAll()
    if (self->_row_scroll_type.value) {
        lv_label_set_text(self->_row_scroll_type.value, scroll_type_label(text_scroll::mode()));
    }
    notifyPageChainActivity("settings-scroll-type");
}

void LvglSettingsPage::liveReapplyTheme() {
    _applyThemeColors();
}

void LvglSettingsPage::_applyThemeColors() {
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    wgt_status_line::reapplyTheme(_status_line);

    auto apply_row = [&](const RowChrome& r, bool sub_row) {
        if (r.icon) {
            lv_obj_set_style_text_color(r.icon, pal.text_secondary, LV_PART_MAIN);
        }
        if (r.label) {
            lv_obj_set_style_text_color(
                r.label, sub_row ? pal.text_meta : pal.text_primary, LV_PART_MAIN);
        }
        if (r.value) {
            lv_obj_set_style_text_color(r.value, pal.text_meta, LV_PART_MAIN);
        }
        if (r.chevron) {
            lv_obj_set_style_text_color(r.chevron, pal.text_meta, LV_PART_MAIN);
        }
    };

    apply_row(_row_display, false);
    apply_row(_row_music, false);
    apply_row(_row_resume_startup, false);
    apply_row(_row_sleep_timer, false);
    apply_row(_row_wifi, false);
    apply_row(_row_theme, false);
    apply_row(_row_perf_monitor, false);
    apply_row(_row_autodim, false);
    apply_row(_row_dim_after, false);
    apply_row(_row_scrolling, false);
    _applyAutodimRowTreatment(pal);

    if (_display_header_icon) {
        lv_obj_set_style_text_color(_display_header_icon, pal.text_secondary, LV_PART_MAIN);
    }
    if (_lbl_display_header) {
        lv_obj_set_style_text_color(_lbl_display_header, pal.text_primary, LV_PART_MAIN);
    }
    if (_display_back_hit) {
        lv_obj_t* back_glyph = lv_obj_get_child(_display_back_hit, 0);
        if (back_glyph) {
            lv_obj_set_style_text_color(back_glyph, pal.text_meta, LV_PART_MAIN);
        }
    }
    if (_view_music) {
        apply_row(_row_rail_enabled, false);
        apply_row(_row_rail_profile, false);
        _applyMusicRailProfileRowTreatment(pal);
        if (_music_header_icon) {
            lv_obj_set_style_text_color(_music_header_icon, pal.text_secondary, LV_PART_MAIN);
        }
        if (_lbl_music_header) {
            lv_obj_set_style_text_color(_lbl_music_header, pal.text_primary, LV_PART_MAIN);
        }
        if (_music_back_hit) {
            lv_obj_t* back_glyph = lv_obj_get_child(_music_back_hit, 0);
            if (back_glyph) {
                lv_obj_set_style_text_color(back_glyph, pal.text_meta, LV_PART_MAIN);
            }
        }
        if (_cont_music_content) {
            reapply_dividers_in(_cont_music_content, pal);
        }
    }
    if (_view_scrolling) {
        apply_row(_row_scroll_type, false);
        if (_scrolling_header_icon) {
            lv_obj_set_style_text_color(_scrolling_header_icon, pal.text_secondary, LV_PART_MAIN);
        }
        if (_lbl_scrolling_header) {
            lv_obj_set_style_text_color(_lbl_scrolling_header, pal.text_primary, LV_PART_MAIN);
        }
        if (_scrolling_back_hit) {
            lv_obj_t* back_glyph = lv_obj_get_child(_scrolling_back_hit, 0);
            if (back_glyph) {
                lv_obj_set_style_text_color(back_glyph, pal.text_meta, LV_PART_MAIN);
            }
        }
        if (_lbl_scroll_speed_title) {
            lv_obj_set_style_text_color(_lbl_scroll_speed_title, pal.text_primary, LV_PART_MAIN);
        }
        if (_lbl_scroll_delay_title) {
            lv_obj_set_style_text_color(_lbl_scroll_delay_title, pal.text_primary, LV_PART_MAIN);
        }
        if (_lbl_preview_caption) {
            lv_obj_set_style_text_color(_lbl_preview_caption, pal.text_meta, LV_PART_MAIN);
        }
        if (_lbl_scroll_preview) {
            lv_obj_set_style_text_color(_lbl_scroll_preview, pal.text_primary, LV_PART_MAIN);
        }
        if (_cont_scrolling_content) {
            reapply_dividers_in(_cont_scrolling_content, pal);
        }
    }
    if (_view_sleep_timer) {
        if (_sleep_timer_header_icon) {
            lv_obj_set_style_text_color(_sleep_timer_header_icon, pal.text_secondary, LV_PART_MAIN);
        }
        if (_lbl_sleep_timer_header) {
            lv_obj_set_style_text_color(_lbl_sleep_timer_header, pal.text_primary, LV_PART_MAIN);
        }
        if (_sleep_timer_back_hit) {
            lv_obj_t* back_glyph = lv_obj_get_child(_sleep_timer_back_hit, 0);
            if (back_glyph) {
                lv_obj_set_style_text_color(back_glyph, pal.text_meta, LV_PART_MAIN);
            }
        }
        if (_cont_sleep_timer_content) {
            reapply_dividers_in(_cont_sleep_timer_content, pal);
        }
        _applyTimersTheme(pal);
    }
    if (_lbl_brightness_title) {
        lv_obj_set_style_text_color(_lbl_brightness_title, pal.text_primary, LV_PART_MAIN);
    }
    _applySliderTheme(pal);

    if (_footer_area) {
        wgt_footer_pill::apply_palette(_footer_area, pal);
    }
    if (_lbl_footer) {
        lv_obj_set_style_text_color(_lbl_footer, pal.text_secondary, LV_PART_MAIN);
    }

    reapply_dividers_in(_screen, pal);
    if (_cont_content) {
        reapply_dividers_in(_cont_content, pal);
    }
    if (_cont_display_content) {
        reapply_dividers_in(_cont_display_content, pal);
    }

    lv_obj_invalidate(_screen);
}

void LvglSettingsPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglSettingsPage::releaseAfterAutoDelete() {
    _nullHandles();
}

void LvglSettingsPage::_nullHandles() {
    _view = SettingsView::Main;
    _brightness_drag_active = false;
    _dim_level_drag_active = false;
    _scroll_speed_drag_active = false;
    _scroll_delay_drag_active = false;
    _timers_deep_sleep_tab = false;
    _timer_syncing_controls = false;
    for (bool& dragging : _timer_drag_active) dragging = false;
    _timer_event1_draft = 0;
    _timer_event2_draft = 0;
    _screen = nullptr;
    _status_line = {};
    _view_main = nullptr;
    _cont_content = nullptr;
    _footer_area = nullptr;
    _lbl_footer = nullptr;
    _view_music = nullptr;
    _music_back_hit = nullptr;
    _music_header_icon = nullptr;
    _lbl_music_header = nullptr;
    _cont_music_content = nullptr;
    _row_rail_enabled = {};
    _row_rail_profile = {};
    _view_display = nullptr;
    _display_back_hit = nullptr;
    _display_header_icon = nullptr;
    _lbl_display_header = nullptr;
    _cont_display_content = nullptr;
    _lbl_brightness_title = nullptr;
    _brightness_slider = nullptr;
    _lbl_brightness_value = nullptr;
    _lbl_dim_level_title = nullptr;
    _dim_level_slider = nullptr;
    _lbl_dim_level_value = nullptr;
    _view_scrolling = nullptr;
    _scrolling_back_hit = nullptr;
    _scrolling_header_icon = nullptr;
    _lbl_scrolling_header = nullptr;
    _cont_scrolling_content = nullptr;
    _lbl_scroll_speed_title = nullptr;
    _scroll_speed_slider = nullptr;
    _lbl_scroll_speed_value = nullptr;
    _lbl_scroll_delay_title = nullptr;
    _scroll_delay_slider = nullptr;
    _lbl_scroll_delay_value = nullptr;
    _lbl_preview_caption = nullptr;
    _lbl_scroll_preview = nullptr;
    _view_sleep_timer = nullptr;
    _sleep_timer_back_hit = nullptr;
    _sleep_timer_header_icon = nullptr;
    _lbl_sleep_timer_header = nullptr;
    _cont_sleep_timer_content = nullptr;
    for (uint8_t i = 0; i < 2; ++i) {
        _timer_tab_buttons[i] = nullptr;
        _timer_tab_labels[i] = nullptr;
        _timer_event_cards[i] = nullptr;
        _timer_event_titles[i] = nullptr;
        _timer_at_labels[i] = nullptr;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        _timer_sliders[i] = nullptr;
        _timer_captions[i] = nullptr;
        _timer_value_labels[i] = nullptr;
    }
    _lbl_timer_state = nullptr;
    _btn_timer_primary = nullptr;
    _lbl_timer_primary = nullptr;
    _btn_deep_sleep_now = nullptr;
    _lbl_deep_sleep_now = nullptr;
    _row_scrolling = {};
    _row_scroll_type = {};
    _row_display = {};
    _row_music = {};
    _row_resume_startup = {};
    _row_sleep_timer = {};
    _row_wifi = {};
    _row_theme = {};
    _row_perf_monitor = {};
    _row_autodim = {};
    _row_dim_after = {};
}

lv_obj_t* LvglSettingsPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
