/*
 * LvglSettingsPage — Settings carousel page (Stage 6.7S).
 * LvglSettingsPage — страница Settings в карусели.
 *
 * Main view: category rows + footer. Display detail: brightness + theme (6.7S2).
 * Row unit: fixed-height data row + 1 px bottom divider; sleep block uses kSleepLineGap.
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

#include "scr_settings.h"

#include <stdio.h>

#include "lvgl.h"

#include "../../core/config.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../fonts/settings_glyph_utf8.h"
#include "../lv_page_chain.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../widgets/wgt_footer_pill.h"
#include "lvgl_ui.h"

namespace lvgl_ui {

namespace {

static constexpr char kStrDisplay[]         = "DISPLAY";
static constexpr char kStrMusicRail[]       = "MUSIC RAIL";
static constexpr char kStrAiLayer[]         = "AI LAYER";
static constexpr char kStrSleepTimer[]      = "SLEEP TIMER";
static constexpr char kStrWhenTimerEnds[]   = "WHEN TIMER ENDS";
static constexpr char kStrWifi[]            = "WI-FI";
static constexpr char kStrBrightness[]      = "BRIGHTNESS";
static constexpr char kStrTheme[]           = "THEME";
static constexpr char kStrValOn[]           = "ON";
static constexpr char kStrValOff[]          = "OFF";
static constexpr char kStrValStopRadio[]    = "STOP RADIO";
static constexpr char kStrValNotConnected[] = "NOT CONNECTED";
static constexpr char kStrValDark[]         = "DARK";
static constexpr char kStrValLight[]        = "LIGHT";
static constexpr char kStrValCustom[]       = "CUSTOM";
static constexpr char kStrFooterReturn[]    = "RETURN TO MAIN";

static const void* const kFontSettingsIcon =
    reinterpret_cast<const void*>(&lv_font_yora_settings_icons_28);

static const void* const kFontChevron =
    reinterpret_cast<const void*>(&lv_font_yora_control_icons_28);

static const void* const kFontDisplayHeader =
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_20_cyr);

static constexpr lv_coord_t kRootRowGap           = 6;
static constexpr lv_coord_t kDividerHeight        = 1;
static constexpr lv_coord_t kIconColWidth         = 42;
static constexpr lv_coord_t kRowHeight            = 60;
static constexpr lv_coord_t kSleepBlockHeight     = 88;
static constexpr lv_coord_t kContentTopInset      = 8;
static constexpr lv_coord_t kContentBottomGap     = 4;
static constexpr lv_coord_t kFooterPadH           = 16;
static constexpr lv_coord_t kFooterPadV           = 10;
static constexpr lv_coord_t kSleepLineGap         = 6;
static constexpr lv_coord_t kDisplayHeaderHeight  = 52;
static constexpr lv_coord_t kBackColWidth         = 40;
// Track groove matches Main volume bar (scr_main.cpp _bar_volume) — knobless fill only.
// Канавка как у Main volume bar — только заливка, без видимого knob.
static constexpr lv_coord_t kSliderTrackH         = 16;
static constexpr lv_coord_t kSliderRowH           = 32;
static constexpr lv_coord_t kSliderValueGap       = 10;
static constexpr lv_coord_t kValueColWidth        = 52;
static constexpr lv_coord_t kBrightnessBlockGap   = 10;
static constexpr lv_coord_t kBrightnessMinUi      = 5;
static constexpr uint8_t    kBrightnessMaxUi      = 100;

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
        set_font_slot(icon, kFontSettingsIcon);
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
    set_font_slot(lbl, LV_ACTIVE_PROFILE.font_normal);
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
    set_font_slot(val, LV_ACTIVE_PROFILE.font_header);
    lv_obj_set_style_text_color(val, pal.text_meta, LV_PART_MAIN);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    return val;
}

static lv_obj_t* add_row_chevron(lv_obj_t* row, const YoRadioPalette& pal) {
    lv_obj_t* chev = lv_label_create(row);
    if (!chev) return nullptr;
    lv_label_set_text(chev, control_glyph_utf8_chevron_right());
    lv_label_set_long_mode(chev, LV_LABEL_LONG_CLIP);
    set_font_slot(chev, kFontChevron);
    lv_obj_set_style_text_color(chev, pal.text_meta, LV_PART_MAIN);
    return chev;
}

static lv_obj_t* create_row_unit(lv_obj_t* parent, const YoRadioPalette& pal, lv_coord_t row_height) {
    lv_obj_t* unit = lv_obj_create(parent);
    if (!unit) return nullptr;
    style_row_unit(unit);

    lv_obj_t* row = lv_obj_create(unit);
    if (!row) return nullptr;
    style_data_row(row, row_height);

    add_bottom_divider(unit, pal);
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

static void add_sleep_block_unit(
    lv_obj_t*          parent,
    LvglSettingsPage::RowChrome& out_main,
    LvglSettingsPage::RowChrome& out_sub,
    const YoRadioPalette& pal) {
    out_main = {};
    out_sub = {};

    lv_obj_t* unit = lv_obj_create(parent);
    if (!unit) return;
    style_row_unit(unit);

    lv_obj_t* block = lv_obj_create(unit);
    if (!block) return;
    style_data_row(block, kSleepBlockHeight);

    out_main.icon = add_icon_column(block, YORA_SETTINGS_GLYPH_SLEEP_TIMER, pal);

    lv_obj_t* text_col = lv_obj_create(block);
    if (!text_col) return;
    lv_obj_set_height(text_col, LV_PCT(100));
    lv_obj_set_flex_grow(text_col, 1);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    style_transparent_flex(text_col);
    lv_obj_set_style_pad_row(text_col, kSleepLineGap, LV_PART_MAIN);

    lv_obj_t* line_main = lv_obj_create(text_col);
    if (line_main) {
        lv_obj_set_width(line_main, LV_PCT(100));
        lv_obj_set_height(line_main, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(line_main, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line_main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        style_transparent_flex(line_main);
        lv_obj_set_style_pad_column(line_main, 8, LV_PART_MAIN);

        out_main.label = add_row_label(line_main, kStrSleepTimer, pal, false, true);
        out_main.value = add_row_value(line_main, kStrValOff, pal);
    }

    lv_obj_t* line_sub = lv_obj_create(text_col);
    if (line_sub) {
        lv_obj_set_width(line_sub, LV_PCT(100));
        lv_obj_set_height(line_sub, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(line_sub, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line_sub, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        style_transparent_flex(line_sub);
        lv_obj_set_style_pad_column(line_sub, 8, LV_PART_MAIN);

        out_sub.label = add_row_label(line_sub, kStrWhenTimerEnds, pal, true, true);
        out_sub.value = add_row_value(line_sub, kStrValStopRadio, pal);
    }

    add_bottom_divider(unit, pal);
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
    const YoRadioPalette& pal) {
    if (!parent || !owner) return;

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
    lv_obj_add_event_cb(out_footer, LvglSettingsPage::footerClickedEvt, LV_EVENT_CLICKED, owner);

    out_lbl_footer = lv_label_create(out_footer);
    if (out_lbl_footer) {
        lv_label_set_text(out_lbl_footer, kStrFooterReturn);
        set_font_slot(out_lbl_footer, LV_ACTIVE_PROFILE.font_normal);
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

static void create_display_header(
    lv_obj_t*          parent,
    lv_obj_t*&         out_back_hit,
    lv_obj_t*&         out_icon,
    lv_obj_t*&         out_title,
    LvglSettingsPage*  owner,
    const YoRadioPalette& pal) {
    if (!parent || !owner) return;

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
        lv_obj_add_event_cb(out_back_hit, LvglSettingsPage::displayBackClickedEvt, LV_EVENT_CLICKED, owner);

        lv_obj_t* back_glyph = lv_label_create(out_back_hit);
        if (back_glyph) {
            lv_label_set_text(back_glyph, control_glyph_utf8_chevron_left());
            set_font_slot(back_glyph, kFontChevron);
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
            lv_label_set_text(out_icon, YORA_SETTINGS_GLYPH_DISPLAY);
            set_font_slot(out_icon, kFontSettingsIcon);
            lv_obj_set_style_text_color(out_icon, pal.text_secondary, LV_PART_MAIN);
        }
    }

    out_title = lv_label_create(header);
    if (out_title) {
        lv_label_set_text(out_title, kStrDisplay);
        set_font_slot(out_title, kFontDisplayHeader);
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

static void style_brightness_slider(lv_obj_t* slider, const YoRadioPalette& pal) {
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

    _view_main = lv_obj_create(_screen);
    if (!_view_main) {
        lv_obj_del(_screen);
        _screen = nullptr;
        _nullHandles();
        return;
    }
    lv_obj_set_width(_view_main, LV_PCT(100));
    lv_obj_set_flex_grow(_view_main, 1);
    lv_obj_set_flex_flow(_view_main, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(_view_main);
    lv_obj_set_style_pad_row(_view_main, 0, LV_PART_MAIN);

    _cont_content = lv_obj_create(_view_main);
    if (!_cont_content) {
        lv_obj_del(_screen);
        _screen = nullptr;
        _nullHandles();
        return;
    }
    lv_obj_set_width(_cont_content, LV_PCT(100));
    lv_obj_set_flex_grow(_cont_content, 1);
    lv_obj_set_flex_flow(_cont_content, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(_cont_content);
    lv_obj_set_style_pad_top(_cont_content, kContentTopInset, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_cont_content, kContentBottomGap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_cont_content, 0, LV_PART_MAIN);

    {
        lv_obj_t* display_row = create_row_unit(_cont_content, pal, kRowHeight);
        char brightness_buf[8] = "100%";
#ifdef ENABLE_BRIGHTNESS_CONTROL
        format_brightness_pct(brightness_buf, sizeof(brightness_buf), config.store.brightness);
#endif
        _row_display = fill_category_row(
            display_row, YORA_SETTINGS_GLYPH_DISPLAY, kStrDisplay, brightness_buf, true, pal);
        make_row_tappable(display_row, displayRowClickedEvt, this);
    }

    _row_music = add_category_row_unit(
        _cont_content, YORA_SETTINGS_GLYPH_MUSIC_RAIL, kStrMusicRail, kStrValOn, false, pal);

    _row_ai = add_category_row_unit(
        _cont_content, YORA_SETTINGS_GLYPH_AI_LAYER, kStrAiLayer, kStrValOn, false, pal);

    add_sleep_block_unit(_cont_content, _row_sleep, _row_sleep_sub, pal);

    _row_wifi = add_category_row_unit(
        _cont_content,
        YORA_SETTINGS_GLYPH_WIFI,
        kStrWifi,
        kStrValNotConnected,
        true,
        pal);

    create_footer(_view_main, _footer_area, _lbl_footer, this, pal);

    _view_display = lv_obj_create(_screen);
    if (!_view_display) {
        lv_obj_del(_screen);
        _screen = nullptr;
        _nullHandles();
        return;
    }
    lv_obj_set_width(_view_display, LV_PCT(100));
    lv_obj_set_flex_grow(_view_display, 1);
    lv_obj_set_flex_flow(_view_display, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(_view_display);
    lv_obj_set_style_pad_row(_view_display, 0, LV_PART_MAIN);
    lv_obj_add_flag(_view_display, LV_OBJ_FLAG_HIDDEN);

    create_display_header(
        _view_display, _display_back_hit, _display_header_icon, _lbl_display_header, this, pal);

    _cont_display_content = lv_obj_create(_view_display);
    if (_cont_display_content) {
        lv_obj_set_width(_cont_display_content, LV_PCT(100));
        lv_obj_set_flex_grow(_cont_display_content, 1);
        lv_obj_set_flex_flow(_cont_display_content, LV_FLEX_FLOW_COLUMN);
        style_transparent_flex(_cont_display_content);
        lv_obj_set_style_pad_top(_cont_display_content, kContentTopInset, LV_PART_MAIN);
        lv_obj_set_style_pad_row(_cont_display_content, kBrightnessBlockGap, LV_PART_MAIN);

#ifdef ENABLE_BRIGHTNESS_CONTROL
        lv_obj_t* brightness_block = lv_obj_create(_cont_display_content);
        if (brightness_block) {
            lv_obj_set_width(brightness_block, LV_PCT(100));
            lv_obj_set_height(brightness_block, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(brightness_block, LV_FLEX_FLOW_COLUMN);
            style_transparent_flex(brightness_block);
            lv_obj_set_style_pad_row(brightness_block, 8, LV_PART_MAIN);
            lv_obj_clear_flag(brightness_block, LV_OBJ_FLAG_GESTURE_BUBBLE);

            _lbl_brightness_title = add_row_label(brightness_block, kStrBrightness, pal, false, false);

            lv_obj_t* slider_row = lv_obj_create(brightness_block);
            if (slider_row) {
                lv_obj_set_width(slider_row, LV_PCT(100));
                lv_obj_set_height(slider_row, kSliderRowH);
                lv_obj_set_style_min_height(slider_row, kSliderRowH, LV_PART_MAIN);
                lv_obj_set_flex_flow(slider_row, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(slider_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                style_transparent_flex(slider_row);
                lv_obj_set_style_pad_column(slider_row, 0, LV_PART_MAIN);
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

                    _brightness_slider = lv_slider_create(slider_wrapper);
                    if (_brightness_slider) {
                        lv_obj_set_width(_brightness_slider, LV_PCT(100));
                        lv_slider_set_range(_brightness_slider, kBrightnessMinUi, kBrightnessMaxUi);
                        lv_slider_set_value(_brightness_slider, config.store.brightness, LV_ANIM_OFF);
                        style_brightness_slider(_brightness_slider, pal);
                        lv_obj_add_flag(_brightness_slider, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                        lv_obj_clear_flag(_brightness_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
                        lv_obj_add_event_cb(_brightness_slider, brightnessSliderEvt, LV_EVENT_ALL, this);
                    }
                }

                lv_obj_t* gap = lv_obj_create(slider_row);
                if (gap) {
                    lv_obj_set_width(gap, kSliderValueGap);
                    lv_obj_set_height(gap, 1);
                    style_transparent_flex(gap);
                    lv_obj_set_flex_grow(gap, 0);
                }

                lv_obj_t* value_col = lv_obj_create(slider_row);
                if (value_col) {
                    lv_obj_set_width(value_col, kValueColWidth);
                    lv_obj_set_style_min_width(value_col, kValueColWidth, LV_PART_MAIN);
                    lv_obj_set_style_max_width(value_col, kValueColWidth, LV_PART_MAIN);
                    lv_obj_set_height(value_col, LV_SIZE_CONTENT);
                    lv_obj_set_flex_grow(value_col, 0);
                    lv_obj_set_flex_flow(value_col, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(
                        value_col,
                        LV_FLEX_ALIGN_END,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                    style_transparent_flex(value_col);
                    lv_obj_clear_flag(value_col, LV_OBJ_FLAG_GESTURE_BUBBLE);

                    char brightness_buf[8] = "100%";
                    format_brightness_pct(brightness_buf, sizeof(brightness_buf), config.store.brightness);
                    _lbl_brightness_value = add_row_value(value_col, brightness_buf, pal);
                }
            }
        }
#endif

        lv_obj_t* theme_row = create_row_unit(_cont_display_content, pal, kRowHeight);
        if (theme_row) {
            _row_theme.hit = theme_row;
            _row_theme.label = add_row_label(theme_row, kStrTheme, pal, false, true);
            _row_theme.value = add_row_value(
                theme_row, theme_preset_label(yoradio_theme_active_preset()), pal);
            _row_theme.chevron = add_row_chevron(theme_row, pal);
            make_row_tappable(theme_row, themeRowClickedEvt, this);
        }
    }

    installCarouselGesturesOnPageRoot(_screen);
    block_gesture_bubble_deep(_view_display);
}

void LvglSettingsPage::enter() {
    _view = SettingsView::Main;
    _brightness_drag_active = false;
    _showView(SettingsView::Main);
    update();
}

void LvglSettingsPage::update() {
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
}

void LvglSettingsPage::exit() {
    _brightness_drag_active = false;
    _view = SettingsView::Main;
}

void LvglSettingsPage::_showView(SettingsView view) {
    _view = view;
    if (!_view_main || !_view_display) return;

    if (view == SettingsView::Main) {
        lv_obj_clear_flag(_view_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_view_display, LV_OBJ_FLAG_HIDDEN);
        _syncMainRowValues();
    } else {
        lv_obj_add_flag(_view_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_view_display, LV_OBJ_FLAG_HIDDEN);
        _syncDisplayValues();
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
        lv_label_set_text(_row_theme.value, theme_preset_label(yoradio_theme_active_preset()));
    }
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

void LvglSettingsPage::_applySliderTheme(const YoRadioPalette& pal) {
#ifdef ENABLE_BRIGHTNESS_CONTROL
    style_brightness_slider(_brightness_slider, pal);
#endif
}

void LvglSettingsPage::footerClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || !self->_footer_area) return;
    if (lv_event_get_target(e) != self->_footer_area) return;
    goToCarouselPage(PageChain::MAIN_INDEX);
}

void LvglSettingsPage::displayRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_showView(SettingsView::Display);
}

void LvglSettingsPage::displayBackClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_brightness_drag_active = false;
    self->_showView(SettingsView::Main);
}

void LvglSettingsPage::themeRowClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self) return;

    const ThemePreset next = cycle_theme_preset(yoradio_theme_active_preset());
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
        notifyPageChainActivity();
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
        notifyPageChainActivity();
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        uint8_t val = static_cast<uint8_t>(lv_slider_get_value(self->_brightness_slider));
        if (val < kBrightnessMinUi) {
            val = kBrightnessMinUi;
            lv_slider_set_value(self->_brightness_slider, val, LV_ANIM_OFF);
        }
        config.store.brightness = val;
        config.setBrightness(true);
        self->_updateBrightnessLabels(val);
        self->_brightness_drag_active = false;
    }
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
    apply_row(_row_ai, false);
    apply_row(_row_sleep, false);
    apply_row(_row_sleep_sub, true);
    apply_row(_row_wifi, false);
    apply_row(_row_theme, false);

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
    _screen = nullptr;
    _status_line = {};
    _view_main = nullptr;
    _cont_content = nullptr;
    _footer_area = nullptr;
    _lbl_footer = nullptr;
    _view_display = nullptr;
    _display_back_hit = nullptr;
    _display_header_icon = nullptr;
    _lbl_display_header = nullptr;
    _cont_display_content = nullptr;
    _lbl_brightness_title = nullptr;
    _brightness_slider = nullptr;
    _lbl_brightness_value = nullptr;
    _row_display = {};
    _row_music = {};
    _row_ai = {};
    _row_sleep = {};
    _row_sleep_sub = {};
    _row_wifi = {};
    _row_theme = {};
}

lv_obj_t* LvglSettingsPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
