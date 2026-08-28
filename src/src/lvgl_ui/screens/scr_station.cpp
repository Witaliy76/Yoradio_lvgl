/*
 * LvglStationPage — Station page chrome + compile-time selected list renderer (STATIONPAGED-2).
 * Страница Station: chrome + renderer списка, выбранный на этапе компиляции (STATIONPAGED-2).
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_station.h"

#include <cstdio>
#include <cstring>

#include "Arduino.h"
#include <cstdint>

#include "lvgl.h"
#include "../adapters/station_list_adapter.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../font_provider.h"
#include "../../i18n/i18n.h"
#include "../lv_page_chain.h"
#include "../lvgl_ui.h"
#include "../widgets/wgt_footer_pill.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

namespace {

static constexpr char kStrCountPlaceholder[]      = "-- / --";

static const char* const kIconHintClick = station_glyph_utf8_hand_click();

static const void* font_title() { return FontProvider::text(20); }
static const void* font_count() { return FontProvider::text(18); }
static const void* font_hint_text() { return FontProvider::text(16); }
static const void* font_hint_icon() { return FontProvider::icon(20); }

constexpr size_t kCountBufferSize = 24;

static constexpr lv_coord_t kRootRowGap = 8;

static constexpr lv_coord_t kHintPadHorizontal = 16;
static constexpr lv_coord_t kHintPadVertical   = 10;
static constexpr lv_coord_t kHintRowGap        = 10;
static constexpr lv_coord_t kHintMinTextWidth  = 80;

static void station_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

// Localized count formats must either fit completely or use one complete fallback.
// Локализованный счётчик выводится только целиком, иначе используется полный fallback.
template <typename... Args>
static bool station_format_checked(char* out, size_t cap, const char* fallback,
                                   const char* format, Args... args) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (format) {
        const int written = snprintf(out, cap, format, args...);
        if (written >= 0 && static_cast<size_t>(written) < cap) return true;
    }
    const char* safe = fallback ? fallback : "";
    const size_t bytes = strlen(safe) + 1u;
    if (bytes <= cap) memcpy(out, safe, bytes);
    return false;
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
        lv_label_set_text(self._lbl_title, i18n::text(i18n::TextId::StationTitle));
        station_set_font(self._lbl_title, font_title());
        lv_obj_set_style_text_color(self._lbl_title, pal.text_primary, LV_PART_MAIN);
    }

    self._lbl_count = lv_label_create(header);
    if (self._lbl_count) {
        lv_label_set_text(self._lbl_count, kStrCountPlaceholder);
        station_set_font(self._lbl_count, font_count());
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
        station_set_font(self._lbl_hint_icon, font_hint_icon());
        lv_obj_set_style_text_color(self._lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        wgt_footer_pill::make_child_passive(self._lbl_hint_icon);
    }

    self._lbl_hint_text = lv_label_create(hint_row);
    if (self._lbl_hint_text) {
        lv_label_set_text(self._lbl_hint_text, i18n::text(i18n::TextId::StationFooter));
        station_set_font(self._lbl_hint_text, font_hint_text());
        lv_obj_set_style_text_color(self._lbl_hint_text, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
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

    if (station_list_active::create(_list, _screen, pal)) {
        create_hint_band(*this, pal);
        installCarouselGesturesOnPageRoot(_screen);
        // Footer affects flex height — finalize layout before rows_per_page / first page fill.
        // Footer влияет на flex-высоту — layout до расчёта rows_per_page и первой страницы.
        lv_obj_update_layout(_screen);
        station_list_active::populate(_list);
        _updateCountLabel(station_list_adapter::current_station_num(),
                          station_list_adapter::station_count());
    } else {
        create_hint_band(*this, pal);
        installCarouselGesturesOnPageRoot(_screen);
    }
}

void LvglStationPage::_refreshOnPageActivate() {
    if (!_list.list_area) return;

    const auto result = station_list_active::refreshOnActivate(_list);
    if (result == station_list_active::RefreshOnActivateResult::Rebuilt) {
        _updateCountLabel(station_list_adapter::current_station_num(),
                          station_list_adapter::station_count());
        return;
    }
    refreshCurrentStationVisuals();
}

void LvglStationPage::enter() {
    _refreshOnPageActivate();
    station_list_active::onEnter(_list);
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
    station_list_active::refreshCurrentStationVisuals(_list);
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
        station_format_checked(
            count_buf, sizeof(count_buf), kStrCountPlaceholder,
            i18n::text(i18n::TextId::StationCountCurrentTotalFormat),
            static_cast<unsigned>(current), static_cast<unsigned>(total));
    } else {
        station_format_checked(
            count_buf, sizeof(count_buf), kStrCountPlaceholder,
            i18n::text(i18n::TextId::StationCountUnknownTotalFormat),
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
    station_list_active::liveReapplyTheme(_list, pal);

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
    station_list_active::releaseAfterTreeDelete(_list);
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
