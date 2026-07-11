/*
 * LvglSettingsPage — Settings carousel page (Stage 6.7S).
 * LvglSettingsPage — страница Settings в карусели.
 *
 * Main view: category rows + footer. No page title (status line is top chrome).
 * Row unit: fixed-height data row + 1 px bottom divider; sleep block uses kSleepLineGap.
 * Главный вид: строки категорий + footer; заголовок страницы не рисуется.
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

#include "scr_settings.h"

#include "lvgl.h"

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
static constexpr char kStrValOn[]           = "ON";
static constexpr char kStrValOff[]          = "OFF";
static constexpr char kStrValStopRadio[]    = "STOP RADIO";
static constexpr char kStrValNotConnected[] = "NOT CONNECTED";
static constexpr char kStrFooterReturn[]    = "RETURN TO MAIN";

static const void* const kFontSettingsIcon =
    reinterpret_cast<const void*>(&lv_font_yora_settings_icons_28);

static const void* const kFontChevron =
    reinterpret_cast<const void*>(&lv_font_yora_control_icons_28);

static constexpr lv_coord_t kRootRowGap        = 6;
static constexpr lv_coord_t kDividerHeight     = 1;
static constexpr lv_coord_t kIconColWidth      = 42;
static constexpr lv_coord_t kRowHeight         = 60;
static constexpr lv_coord_t kSleepBlockHeight  = 88;
static constexpr lv_coord_t kContentTopInset   = 8;
static constexpr lv_coord_t kContentBottomGap  = 4;
static constexpr lv_coord_t kFooterPadH        = 16;
static constexpr lv_coord_t kFooterPadV        = 10;
// Inter-line gap inside sleep text column; pad_row between line_main and line_sub.
// Зазор между SLEEP TIMER и WHEN TIMER ENDS — flex pad_row, не magic y-offset.
static constexpr lv_coord_t kSleepLineGap      = 6;

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

// Row unit: [fixed-height data row] + [1 px bottom divider]. Divider is not a sibling between rows.
// Row unit: [строка фикс. высоты] + [divider снизу]; не отдельный flex-child между строками.
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
    // Main-axis CENTER: two-line group centered in kSleepBlockHeight; pad_row = inter-line gap.
    // CENTER по main axis — группа по центру блока; pad_row — зазор между строками.
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
    lv_obj_t*     screen,
    lv_obj_t*&    out_footer,
    lv_obj_t*&    out_lbl_footer,
    LvglSettingsPage* owner,
    const YoRadioPalette& pal) {
    if (!screen || !owner) return;

    out_footer = lv_obj_create(screen);
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

    _cont_content = lv_obj_create(_screen);
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

    _row_display = add_category_row_unit(
        _cont_content, YORA_SETTINGS_GLYPH_DISPLAY, kStrDisplay, nullptr, true, pal);

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

    create_footer(_screen, _footer_area, _lbl_footer, this, pal);

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglSettingsPage::enter() {
    update();
}

void LvglSettingsPage::update() {
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
}

void LvglSettingsPage::exit() {}

void LvglSettingsPage::footerClickedEvt(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglSettingsPage*>(lv_event_get_user_data(e));
    if (!self || !self->_footer_area) return;
    if (lv_event_get_target(e) != self->_footer_area) return;
    goToCarouselPage(PageChain::MAIN_INDEX);
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
    _screen = nullptr;
    _status_line = {};
    _cont_content = nullptr;
    _footer_area = nullptr;
    _lbl_footer = nullptr;
    _row_display = {};
    _row_music = {};
    _row_ai = {};
    _row_sleep = {};
    _row_sleep_sub = {};
    _row_wifi = {};
}

lv_obj_t* LvglSettingsPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
