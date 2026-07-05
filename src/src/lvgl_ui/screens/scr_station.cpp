/*
 * LvglStationPage — Station page chrome + legacy continuous-scroll list renderer delegation.
 * Страница Station: chrome + делегирование continuous-scroll renderer'у.
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

#include "scr_station.h"

#include <cstdio>

#include "Arduino.h"
#include <cstdint>

#include "lvgl.h"
#include "../adapters/station_list_adapter.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../lv_page_chain.h"
#include "../lvgl_ui.h"
#include "../widgets/wgt_footer_pill.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

namespace {

static constexpr char kStrStationsTitle[]        = "STATIONS";
static constexpr char kStrCountPlaceholder[]      = "-- / --";
static constexpr char kStrHintSwipe[]             = "Swipe up/down to scroll";
static constexpr char kStrHintReturnMain[]        = "Tap to return to Main";

static constexpr char kFmtCountCurrentTotal[]  = "%u / %u";
static constexpr char kFmtCountUnknownTotal[]  = "-- / %u";
static constexpr char kFmtHintBand[]           = "%s%s%s";

static constexpr char kMetaFieldSepUtf8[] = " \xE2\x80\xA2 ";

static const char* const kIconHintClick = station_glyph_utf8_hand_click();

static const void* const kFontTitle    = reinterpret_cast<const void*>(&lv_font_yora_montserrat_20_cyr);
static const void* const kFontCount    = reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr);
static const void* const kFontHintText = reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
static const void* const kFontHintIcon = reinterpret_cast<const void*>(&lv_font_yora_station_icons_20);

constexpr size_t kCountBufferSize = 24;
constexpr size_t kHintBufferSize  = 80;

static constexpr lv_coord_t kRootRowGap = 8;

static constexpr lv_coord_t kHintPadHorizontal = 16;
static constexpr lv_coord_t kHintPadVertical   = 10;
static constexpr lv_coord_t kHintRowGap        = 10;
static constexpr lv_coord_t kHintMinTextWidth  = 80;

static void station_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void style_transparent(lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void add_thin_divider(lv_obj_t* parent, const YoRadioPalette& pal) {
    lv_obj_t* div = lv_obj_create(parent);
    if (!div) return;
    lv_obj_set_width(div, LV_PCT(100));
    lv_obj_set_height(div, 1);
    lv_obj_set_style_bg_color(div, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
}

static void station_reapply_dividers(lv_obj_t* obj, const YoRadioPalette& pal) {
    if (!obj) return;
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* ch = lv_obj_get_child(obj, i);
        if (!ch) continue;
        if (lv_obj_get_height(ch) == 1 && lv_obj_get_style_bg_opa(ch, LV_PART_MAIN) == LV_OPA_COVER) {
            lv_obj_set_style_bg_color(ch, pal.divider, LV_PART_MAIN);
        }
        station_reapply_dividers(ch, pal);
    }
}

} // namespace

ScreenType LvglStationPage::screenType() const {
    return ScreenType::Page;
}

void LvglStationPage::create_status_chrome(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    wgt_status_line::create(self._screen, self._status_line);
    if (!self._status_line.root) return;
    add_thin_divider(self._screen, pal);
}

void LvglStationPage::create_header(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    lv_obj_t* header = lv_obj_create(self._screen);
    if (!header) return;
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_transparent(header);

    self._lbl_title = lv_label_create(header);
    if (self._lbl_title) {
        lv_label_set_text(self._lbl_title, kStrStationsTitle);
        station_set_font(self._lbl_title, kFontTitle);
        lv_obj_set_style_text_color(self._lbl_title, pal.text_primary, LV_PART_MAIN);
    }

    self._lbl_count = lv_label_create(header);
    if (self._lbl_count) {
        lv_label_set_text(self._lbl_count, kStrCountPlaceholder);
        station_set_font(self._lbl_count, kFontCount);
        lv_obj_set_style_text_color(self._lbl_count, pal.text_secondary, LV_PART_MAIN);
    }
}

void LvglStationPage::create_hint_band(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    self._hint_area = lv_obj_create(self._screen);
    if (!self._hint_area) return;
    lv_obj_set_width(self._hint_area, LV_PCT(100));
    lv_obj_set_height(self._hint_area, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(self._hint_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(self._hint_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(self._hint_area, kHintPadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_right(self._hint_area, kHintPadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_top(self._hint_area, kHintPadVertical, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(self._hint_area, kHintPadVertical, LV_PART_MAIN);
    wgt_footer_pill::prepare_surface(self._hint_area);
    wgt_footer_pill::apply_palette(self._hint_area, pal);
    lv_obj_add_flag(self._hint_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(self._hint_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(self._hint_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(self._hint_area, _hintAreaClickedEvt, LV_EVENT_CLICKED, &self);

    lv_obj_t* hint_row = lv_obj_create(self._hint_area);
    if (!hint_row) return;
    lv_obj_set_width(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_height(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hint_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hint_row, kHintRowGap, LV_PART_MAIN);
    style_transparent(hint_row);
    wgt_footer_pill::make_child_passive(hint_row);

    self._lbl_hint_icon = lv_label_create(hint_row);
    if (self._lbl_hint_icon) {
        lv_label_set_text(self._lbl_hint_icon, kIconHintClick);
        station_set_font(self._lbl_hint_icon, kFontHintIcon);
        lv_obj_set_style_text_color(self._lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        wgt_footer_pill::make_child_passive(self._lbl_hint_icon);
    }

    self._lbl_hint_text = lv_label_create(hint_row);
    if (self._lbl_hint_text) {
        char hint_buf[kHintBufferSize];
        snprintf(hint_buf, sizeof(hint_buf), kFmtHintBand,
                 kStrHintSwipe, kMetaFieldSepUtf8, kStrHintReturnMain);
        lv_label_set_text(self._lbl_hint_text, hint_buf);
        station_set_font(self._lbl_hint_text, kFontHintText);
        lv_obj_set_style_text_color(self._lbl_hint_text, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_text, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_label_set_long_mode(self._lbl_hint_text, LV_LABEL_LONG_CLIP);
        const lv_coord_t max_w = static_cast<lv_coord_t>(
            LV_ACTIVE_PROFILE.width
            - 2u * static_cast<uint32_t>(LV_ACTIVE_PROFILE.frame_padding)
            - 32u - 20u - 30u);
        if (max_w > kHintMinTextWidth) {
            lv_obj_set_width(self._lbl_hint_text, max_w);
        }
        wgt_footer_pill::make_child_passive(self._lbl_hint_text);
    }
}

void LvglStationPage::create() {
    if (_screen) return;

    const YoRadioPalette& pal = yoradio_palette();

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(_screen, LV_ACTIVE_PROFILE.frame_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, kRootRowGap, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_chrome(*this, pal);

    if (!_status_line.root) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    create_header(*this, pal);

    if (station_list_legacy_scroll::create(_list, _screen, pal)) {
        station_list_legacy_scroll::populate(_list);
        _updateCountLabel(station_list_adapter::current_station_num(),
                          station_list_adapter::station_count());
    }

    create_hint_band(*this, pal);

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglStationPage::_refreshOnPageActivate() {
    if (!_list.list_area) return;

    const auto result = station_list_legacy_scroll::refreshOnActivate(_list);
    if (result == station_list_legacy_scroll::RefreshOnActivateResult::Rebuilt) {
        _updateCountLabel(station_list_adapter::current_station_num(),
                          station_list_adapter::station_count());
        return;
    }
    refreshCurrentStationVisuals();
}

void LvglStationPage::enter() {
    _refreshOnPageActivate();
    station_list_legacy_scroll::onEnter(_list);
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
}

void LvglStationPage::update() {
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
}

void LvglStationPage::refreshCurrentStationVisuals() {
    if (!_screen) return;
    const uint16_t total = station_list_adapter::station_count();
    const uint16_t current = station_list_adapter::current_station_num();
    _updateCountLabel(current, total);
    station_list_legacy_scroll::refreshCurrentStationVisuals(_list);
}

void LvglStationPage::_updateCountLabel(uint16_t current, uint16_t total) {
    if (!_lbl_count) return;
    _station_total = total;
    char count_buf[kCountBufferSize];
    if (total == 0u) {
        lv_label_set_text(_lbl_count, kStrCountPlaceholder);
        return;
    }
    if (station_list_adapter::is_valid_station_num(current)) {
        snprintf(count_buf, sizeof(count_buf), kFmtCountCurrentTotal,
                 static_cast<unsigned>(current), static_cast<unsigned>(total));
    } else {
        snprintf(count_buf, sizeof(count_buf), kFmtCountUnknownTotal,
                 static_cast<unsigned>(total));
    }
    lv_label_set_text(_lbl_count, count_buf);
}

void LvglStationPage::_hintAreaClickedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (self) self->_onHintAreaClicked(e);
}

void LvglStationPage::_onHintAreaClicked(lv_event_t* e) {
    if (!_hint_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_event_get_target(e) != _hint_area) return;
    lvgl_ui::goToCarouselPage(PageChain::MAIN_INDEX);
}

void LvglStationPage::exit() {}

void LvglStationPage::liveReapplyTheme() {
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    wgt_status_line::reapplyTheme(_status_line);

    if (_lbl_title) lv_obj_set_style_text_color(_lbl_title, pal.text_primary, LV_PART_MAIN);
    if (_lbl_count) lv_obj_set_style_text_color(_lbl_count, pal.text_secondary, LV_PART_MAIN);

    if (_hint_area) {
        wgt_footer_pill::apply_palette(_hint_area, pal);
    }
    if (_lbl_hint_icon) lv_obj_set_style_text_color(_lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
    if (_lbl_hint_text) lv_obj_set_style_text_color(_lbl_hint_text, pal.text_secondary, LV_PART_MAIN);

    station_reapply_dividers(_screen, pal);
    station_list_legacy_scroll::liveReapplyTheme(_list, pal);

    lv_obj_invalidate(_screen);
}

void LvglStationPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglStationPage::releaseAfterAutoDelete() {
    _nullHandles();
}

void LvglStationPage::_nullHandles() {
    station_list_legacy_scroll::releaseAfterTreeDelete(_list);
    _screen = nullptr;
    _status_line = {};
    _lbl_title = nullptr;
    _lbl_count = nullptr;
    _lbl_hint_icon = nullptr;
    _lbl_hint_text = nullptr;
    _hint_area = nullptr;
    _station_total = 0;
}

lv_obj_t* LvglStationPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
