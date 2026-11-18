/*
 * station_list_simple_paged — instant page-flip Station list renderer (STATIONPAGED-1).
 * station_list_simple_paged — постраничный renderer без scroll document (STATIONPAGED-1).
 *
 * DspTask-only lv_*; one multiline label per visible page; short heap/PSRAM page buffer.
 * Только DspTask; один label на страницу; короткий page buffer в heap/PSRAM.
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "station_list_simple_paged.h"

#include <cstdio>
#include <cstring>

#include "Arduino.h"
#include <cstdint>

#include "lvgl.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../font_provider.h"
#include "../../i18n/i18n.h"
#include "../profiles/resolved_ui_metrics.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {
namespace station_list_simple_paged {

namespace {

constexpr size_t kStationLineBytes = 112;
constexpr size_t kPageTextExtraBytes = 96;

constexpr uint32_t kWideProfileMinWidth     = 480u;
constexpr size_t   kStationNameLimitWide    = 64u;
constexpr size_t   kStationNameLimitCompact = 42u;

// Product layout: keep 8 rows on 480×480 by fixing target pitch; line_space follows real font lh.
// Продуктовый layout: 8 строк на 480×480 через target pitch; line_space от реального font lh.
static constexpr lv_coord_t kStationTargetRowPitch = 41;

constexpr lv_coord_t kRowOverlayTopPad  = 8;
constexpr lv_coord_t kRowAccentY        = -2;
constexpr lv_coord_t kRowAccentH        = 29;

static constexpr lv_coord_t kFocusBgRadius     = 8;
static constexpr lv_coord_t kFocusAccentRadius = 2;
static constexpr lv_opa_t   kFocusBgOpa          = LV_OPA_70;

constexpr lv_coord_t kListPadLeft             = 16;
constexpr lv_coord_t kListPadTop              = 8;
constexpr lv_coord_t kListPadBottom           = 8;
constexpr lv_coord_t kFocusAccentStripW       = 3;
constexpr lv_coord_t kMarkerGutterAfterAccent = 5;
constexpr lv_coord_t kMarkerBoxW              = 32;
constexpr lv_coord_t kMarkerToNameGap         = 5;

constexpr int32_t kScrollbarRightIgnorePx = 26;
constexpr lv_coord_t kTapMaxFingerTravelPx  = 24;
constexpr lv_coord_t kPageSwipeMinTravelPx  = 24;
static constexpr uint32_t kStationRowSafetyLimit = UINT16_MAX - 10u;

static const void* font_station_list_slot() { return FontProvider::text(22); }
static const void* font_current_marker() { return FontProvider::icon(22); }
static const char* const kIconCurrentStation = station_glyph_utf8_volume_2();

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

static int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }

static const lv_font_t* station_list_font() {
    return FontProvider::text(22);
}

static const lv_font_t* station_marker_font() {
    return FontProvider::icon(22);
}

// Shared row metrics: label, overlay, hit-test, and rows_per_page must use the same values.
// Общая геометрия строки: label, overlay, hit-test и rows_per_page — одни и те же значения.
static lv_coord_t station_line_height() {
    return lv_font_get_line_height(station_list_font());
}

static lv_coord_t station_line_space() {
    const lv_coord_t lh = station_line_height();
    const int32_t gap = static_cast<int32_t>(kStationTargetRowPitch) - static_cast<int32_t>(lh);
    return static_cast<lv_coord_t>(gap > 0 ? gap : 0);
}

static lv_coord_t station_row_pitch() {
    return static_cast<lv_coord_t>(station_line_height() + station_line_space());
}

static lv_coord_t row_overlay_height() {
    return static_cast<lv_coord_t>(station_line_height() + 2 * kRowOverlayTopPad);
}

static lv_coord_t marker_y_offset() {
    const int32_t text_h = static_cast<int32_t>(station_line_height());
    const int32_t icon_h = static_cast<int32_t>(lv_font_get_line_height(station_marker_font()));
    return static_cast<lv_coord_t>((text_h - icon_h) / 2);
}

static lv_coord_t marker_left_x_in_list() {
    return -kListPadLeft + 1 + kFocusAccentStripW + kMarkerGutterAfterAccent;
}

static lv_coord_t list_label_pad_left_for_marker_gutter() {
    const int32_t mleft = static_cast<int32_t>(marker_left_x_in_list());
    const int32_t mright = mleft + static_cast<int32_t>(kMarkerBoxW);
    const int32_t pad = mright + static_cast<int32_t>(kMarkerToNameGap);
    return static_cast<lv_coord_t>(pad > 0 ? pad : 0);
}

static lv_coord_t row_y_for_local_row(uint16_t local_row_zero_based) {
    return static_cast<lv_coord_t>(
        static_cast<uint32_t>(local_row_zero_based)
        * static_cast<uint32_t>(station_row_pitch()));
}

static void station_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void make_child_passive(lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static size_t name_limit_for_profile() {
    return (kResolvedUiMetrics.viewport_width >= kWideProfileMinWidth)
               ? kStationNameLimitWide
               : kStationNameLimitCompact;
}

static void listAreaPressedEvt(lv_event_t* e);
static void listAreaPressingEvt(lv_event_t* e);
static void listAreaReleasedEvt(lv_event_t* e);
static void listAreaShortClickedEvt(lv_event_t* e);

static void resetStrokeTracking(Instance& instance);
static void resetInputState(Instance& instance);
static void releasePageTextBuffer(Instance& instance);
static bool ensurePageTextBuffer(Instance& instance, uint16_t rows_per_page);

static void computeRowsPerPage(Instance& instance);
static uint16_t pageCountForTotal(uint16_t total, uint16_t rows_per_page);
static uint16_t firstRowOnPage(const Instance& instance);
static uint16_t lastRowOnPage(const Instance& instance);
static bool rowOnCurrentPage(const Instance& instance, uint16_t row);
static uint16_t localIndexForRow(const Instance& instance, uint16_t row);
static int32_t soundingRow(const Instance& instance);
static void clampPageIndex(Instance& instance);
static void clampFocus(Instance& instance);

static void ensurePageLabel(Instance& instance);
static void ensureOverlays(Instance& instance);
static void styleFocusRowOverlays(Instance& instance, const YoRadioPalette& pal);
static void positionFocusRowOverlays(Instance& instance, uint16_t local_row);
static void layoutFocusChrome(Instance& instance, uint16_t focus_row);
static void hideFocusOverlays(Instance& instance);
static void styleCurrentMarker(Instance& instance, const YoRadioPalette& pal);
static void positionCurrentMarker(Instance& instance, uint16_t local_row);
static void layoutCurrentMarker(Instance& instance, int32_t sounding_row);
static void hideCurrentMarker(Instance& instance);
static void applyOverlayZOrder(Instance& instance);
static void rememberRevision(Instance& instance);

static void fillCurrentPage(Instance& instance);
static bool tryPageStep(Instance& instance, int direction);
static bool candidateRowFromScreenPoint(Instance& instance, lv_coord_t screen_py,
                                        uint16_t* out_row);
static void setFocusRow(Instance& instance, uint16_t row);

static void onListAreaPressed(Instance& instance, lv_event_t* e);
static void onListAreaPressing(Instance& instance, lv_event_t* e);
static void onListAreaReleased(Instance& instance, lv_event_t* e);
static void onListAreaShortClicked(Instance& instance, lv_event_t* e);

static void resetStrokeTracking(Instance& instance) {
    instance.press_pt         = {};
    instance.stroke_max_dx    = 0;
    instance.stroke_max_dy    = 0;
    instance.completed_vertical_page_gesture = false;
}

static void resetInputState(Instance& instance) {
    resetStrokeTracking(instance);
    instance.arm_suppress_next_tap = false;
    instance.press_focus_active = false;
    instance.focus_before_press = 0;
}

static void releasePageTextBuffer(Instance& instance) {
    if (instance.page_text) {
        free(instance.page_text);
        instance.page_text = nullptr;
    }
    instance.page_text_cap = 0;
}

static bool ensurePageTextBuffer(Instance& instance, uint16_t rows_per_page) {
    const uint16_t rows = rows_per_page > 0u ? rows_per_page : 1u;
    const size_t needed =
        static_cast<size_t>(rows) * kStationLineBytes + kPageTextExtraBytes;
    if (instance.page_text && instance.page_text_cap >= needed) return true;

    releasePageTextBuffer(instance);
    instance.page_text = static_cast<char*>(malloc(needed));
    if (!instance.page_text) {
        instance.page_text = static_cast<char*>(ps_malloc(needed));
    }
    if (!instance.page_text) {
        instance.page_text_cap = 0;
        return false;
    }
    instance.page_text_cap = needed;
    instance.page_text[0] = '\0';
    return true;
}

static void computeRowsPerPage(Instance& instance) {
    if (!instance.list_area) return;
    if (instance.parent_screen) {
        lv_obj_update_layout(instance.parent_screen);
    }
    // LVGL content height already excludes list_area pad_top/pad_bottom.
    // content height LVGL уже без pad_top/pad_bottom list_area.
    const lv_coord_t content_h = lv_obj_get_content_height(instance.list_area);
    const lv_coord_t line_h = station_line_height();
    const lv_coord_t pitch = station_row_pitch();
    if (content_h < line_h || pitch <= 0) {
        instance.rows_per_page = 1u;
        return;
    }
    // N rows need (N-1)*pitch + line_height; last row has no trailing line_space.
    // N строк = (N-1)*pitch + line_height; у последней нет line_space снизу.
    instance.rows_per_page = static_cast<uint16_t>(
        1u + static_cast<uint32_t>(content_h - line_h)
            / static_cast<uint32_t>(pitch));
    if (instance.rows_per_page == 0u) {
        instance.rows_per_page = 1u;
    }
}

static uint16_t pageCountForTotal(uint16_t total, uint16_t rows_per_page) {
    if (total == 0u || rows_per_page == 0u) return 0u;
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(total) + static_cast<uint32_t>(rows_per_page) - 1u)
        / static_cast<uint32_t>(rows_per_page));
}

static uint16_t firstRowOnPage(const Instance& instance) {
    if (instance.rows_per_page == 0u) return 0u;
    return static_cast<uint16_t>(
        static_cast<uint32_t>(instance.page_index) * static_cast<uint32_t>(instance.rows_per_page));
}

static uint16_t lastRowOnPage(const Instance& instance) {
    if (instance.row_count == 0u || instance.rows_per_page == 0u) return 0u;
    const uint32_t first = static_cast<uint32_t>(firstRowOnPage(instance));
    uint32_t last = first + static_cast<uint32_t>(instance.rows_per_page) - 1u;
    const uint32_t total = static_cast<uint32_t>(instance.row_count);
    if (last >= total) last = total - 1u;
    return static_cast<uint16_t>(last);
}

static bool rowOnCurrentPage(const Instance& instance, uint16_t row) {
    if (row >= instance.row_count) return false;
    const uint16_t first = firstRowOnPage(instance);
    const uint16_t last = lastRowOnPage(instance);
    return row >= first && row <= last;
}

static uint16_t localIndexForRow(const Instance& instance, uint16_t row) {
    const uint16_t first = firstRowOnPage(instance);
    if (row < first) return 0u;
    return static_cast<uint16_t>(row - first);
}

static int32_t soundingRow(const Instance& instance) {
    if (!instance.feed.sounding_row) return -1;
    const int32_t row = instance.feed.sounding_row(instance.feed.ctx);
    if (row < 0 || static_cast<uint32_t>(row) >= instance.row_count) return -1;
    return row;
}

static void clampPageIndex(Instance& instance) {
    if (instance.page_count == 0u) {
        instance.page_index = 0u;
        return;
    }
    if (instance.page_index >= instance.page_count) {
        instance.page_index = static_cast<uint16_t>(instance.page_count - 1u);
    }
}

static void clampFocus(Instance& instance) {
    if (instance.row_count == 0u || instance.focus_row >= instance.row_count) {
        instance.focus_row = 0u;
    }
}

static void ensurePageLabel(Instance& instance) {
    if (instance.lbl_page || !instance.list_area) return;
    instance.lbl_page = lv_label_create(instance.list_area);
    if (!instance.lbl_page) return;
    lv_label_set_long_mode(instance.lbl_page, LV_LABEL_LONG_CLIP);
    station_set_font(instance.lbl_page, font_station_list_slot());
    lv_obj_set_style_text_color(instance.lbl_page, yoradio_palette().list_row_text, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(instance.lbl_page, station_line_space(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(instance.lbl_page, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);
    make_child_passive(instance.lbl_page);
}

static void ensureOverlays(Instance& instance) {
    if (!instance.list_area) return;
    if (!instance.focus_row_bg) {
        instance.focus_row_bg = lv_obj_create(instance.list_area);
        if (instance.focus_row_bg) {
            lv_obj_clear_flag(instance.focus_row_bg, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(instance.focus_row_bg, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (!instance.focus_row_accent) {
        instance.focus_row_accent = lv_obj_create(instance.list_area);
        if (instance.focus_row_accent) {
            lv_obj_clear_flag(instance.focus_row_accent, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(instance.focus_row_accent, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (!instance.current_marker) {
        instance.current_marker = lv_label_create(instance.list_area);
        if (instance.current_marker) {
            lv_obj_set_width(instance.current_marker, kMarkerBoxW);
            lv_label_set_long_mode(instance.current_marker, LV_LABEL_LONG_CLIP);
            lv_label_set_text(instance.current_marker, kIconCurrentStation);
            station_set_font(instance.current_marker, font_current_marker());
            lv_obj_set_style_text_align(instance.current_marker, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_clear_flag(instance.current_marker, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(instance.current_marker, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static void styleFocusRowOverlays(Instance& instance, const YoRadioPalette& pal) {
    if (instance.focus_row_bg) {
        lv_obj_set_width(instance.focus_row_bg,
            static_cast<lv_coord_t>(
                kResolvedUiMetrics.viewport_width - (kResolvedUiMetrics.factory.frame_padding * 2u)));
        lv_obj_set_height(instance.focus_row_bg, row_overlay_height());
        lv_obj_set_style_bg_color(instance.focus_row_bg, pal.list_row_selected_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(instance.focus_row_bg, kFocusBgOpa, LV_PART_MAIN);
        lv_obj_set_style_border_width(instance.focus_row_bg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(instance.focus_row_bg, kFocusBgRadius, LV_PART_MAIN);
        lv_obj_set_style_pad_all(instance.focus_row_bg, 0, LV_PART_MAIN);
    }
    if (instance.focus_row_accent) {
        lv_obj_set_width(instance.focus_row_accent, kFocusAccentStripW);
        lv_obj_set_height(instance.focus_row_accent, kRowAccentH);
        lv_obj_set_style_bg_color(instance.focus_row_accent, pal.accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(instance.focus_row_accent, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(instance.focus_row_accent, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(instance.focus_row_accent, kFocusAccentRadius, LV_PART_MAIN);
        lv_obj_set_style_pad_all(instance.focus_row_accent, 0, LV_PART_MAIN);
    }
}

static void positionFocusRowOverlays(Instance& instance, uint16_t local_row) {
    const lv_coord_t y = row_y_for_local_row(local_row);
    if (instance.focus_row_bg) {
        lv_obj_set_pos(instance.focus_row_bg, -kListPadLeft, y - kRowOverlayTopPad);
    }
    if (instance.focus_row_accent) {
        lv_obj_set_pos(instance.focus_row_accent, -kListPadLeft + 1, y + kRowAccentY);
    }
}

static void hideFocusOverlays(Instance& instance) {
    if (instance.focus_row_bg) {
        lv_obj_add_flag(instance.focus_row_bg, LV_OBJ_FLAG_HIDDEN);
    }
    if (instance.focus_row_accent) {
        lv_obj_add_flag(instance.focus_row_accent, LV_OBJ_FLAG_HIDDEN);
    }
}

static void layoutFocusChrome(Instance& instance, uint16_t focus_row) {
    if (!instance.list_area || !instance.lbl_page) return;
    if (!rowOnCurrentPage(instance, focus_row)) {
        hideFocusOverlays(instance);
        return;
    }

    ensureOverlays(instance);
    const YoRadioPalette& pal = yoradio_palette();
    const uint16_t local_row = localIndexForRow(instance, focus_row);

    styleFocusRowOverlays(instance, pal);
    positionFocusRowOverlays(instance, local_row);
    if (instance.focus_row_bg) {
        lv_obj_clear_flag(instance.focus_row_bg, LV_OBJ_FLAG_HIDDEN);
    }
    if (instance.focus_row_accent) {
        lv_obj_clear_flag(instance.focus_row_accent, LV_OBJ_FLAG_HIDDEN);
    }
    applyOverlayZOrder(instance);
}

static void hideCurrentMarker(Instance& instance) {
    if (instance.current_marker) {
        lv_obj_add_flag(instance.current_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

static void styleCurrentMarker(Instance& instance, const YoRadioPalette& pal) {
    if (!instance.current_marker) return;
    lv_obj_set_style_text_color(instance.current_marker, pal.accent, LV_PART_MAIN);
}

static void positionCurrentMarker(Instance& instance, uint16_t local_row) {
    if (!instance.current_marker) return;
    const lv_coord_t y = row_y_for_local_row(local_row);
    const lv_coord_t mx = marker_left_x_in_list();
    lv_obj_set_pos(instance.current_marker, mx, y + marker_y_offset());
}

static void layoutCurrentMarker(Instance& instance, int32_t current_row) {
    if (!instance.list_area || !instance.lbl_page || current_row < 0) {
        hideCurrentMarker(instance);
        return;
    }
    const uint16_t row = static_cast<uint16_t>(current_row);
    if (!rowOnCurrentPage(instance, row)) {
        hideCurrentMarker(instance);
        return;
    }

    ensureOverlays(instance);
    const YoRadioPalette& pal = yoradio_palette();
    const uint16_t local_row = localIndexForRow(instance, row);
    styleCurrentMarker(instance, pal);
    positionCurrentMarker(instance, local_row);
    lv_obj_clear_flag(instance.current_marker, LV_OBJ_FLAG_HIDDEN);
    applyOverlayZOrder(instance);
}

static void applyOverlayZOrder(Instance& instance) {
    if (instance.focus_row_bg) {
        lv_obj_move_background(instance.focus_row_bg);
    }
    if (instance.lbl_page) {
        lv_obj_move_foreground(instance.lbl_page);
    }
    if (instance.current_marker) {
        lv_obj_move_foreground(instance.current_marker);
    }
}

static void rememberRevision(Instance& instance) {
    if (!instance.feed.revision) {
        instance.revision_valid = false;
        return;
    }
    instance.applied_revision = instance.feed.revision(instance.feed.ctx);
    instance.revision_valid = true;
}

static void applySoundingJump(Instance& instance) {
    if (!instance.jump_to_sounding) return;
    instance.jump_to_sounding = false;
    const int32_t sounding = soundingRow(instance);
    if (sounding >= 0 && instance.rows_per_page > 0u) {
        instance.page_index = static_cast<uint16_t>(
            static_cast<uint32_t>(sounding) / static_cast<uint32_t>(instance.rows_per_page));
        instance.focus_row = static_cast<uint16_t>(sounding);
        return;
    }
    instance.page_index = 0u;
    instance.focus_row = instance.row_count > 1u ? 1u : 0u;
}

static void fillCurrentPage(Instance& instance) {
    if (!instance.list_area) return;
    ++instance.content_version;
    instance.press_focus_active = false;

    computeRowsPerPage(instance);
    instance.row_count = instance.feed.count ? instance.feed.count(instance.feed.ctx) : 0u;
    instance.page_count = pageCountForTotal(instance.row_count, instance.rows_per_page);
    applySoundingJump(instance);
    clampPageIndex(instance);
    clampFocus(instance);

    ensurePageLabel(instance);
    if (!instance.lbl_page) return;

    if (instance.row_count == 0u) {
        lv_label_set_text(instance.lbl_page,
                          i18n::text(i18n::TextId::StationEmptyList));
        hideFocusOverlays(instance);
        hideCurrentMarker(instance);
        instance.revision_valid = true;
        return;
    }

    if (!ensurePageTextBuffer(instance, instance.rows_per_page)) {
        lv_label_set_text(instance.lbl_page,
                          i18n::text(i18n::TextId::StationListUnavailable));
        hideFocusOverlays(instance);
        hideCurrentMarker(instance);
        instance.revision_valid = false;
        return;
    }

    const uint16_t first = firstRowOnPage(instance);
    const uint16_t last = lastRowOnPage(instance);
    const size_t name_limit = name_limit_for_profile();
    bool filled = false;
    if (instance.feed.page_text) {
        filled = instance.feed.page_text(instance.feed.ctx, first, last,
                                         instance.page_text, instance.page_text_cap, name_limit);
    }
    if (!filled) {
        lv_label_set_text(instance.lbl_page,
                          i18n::text(i18n::TextId::StationListUnavailable));
        hideFocusOverlays(instance);
        hideCurrentMarker(instance);
        instance.revision_valid = false;
        return;
    }

    lv_label_set_text_static(instance.lbl_page, instance.page_text);

    layoutFocusChrome(instance, instance.focus_row);
    layoutCurrentMarker(instance, soundingRow(instance));
    instance.revision_valid = true;
}

static bool tryPageStep(Instance& instance, int direction) {
    if (instance.page_count <= 1u) return false;
    const uint16_t old = instance.page_index;
    if (direction > 0) {
        if (instance.page_index + 1u >= instance.page_count) return false;
        instance.page_index++;
    } else if (direction < 0) {
        if (instance.page_index == 0u) return false;
        instance.page_index--;
    } else {
        return false;
    }
    if (instance.page_index == old) return false;
    fillCurrentPage(instance);
    return true;
}

static bool candidateRowFromScreenPoint(Instance& instance, lv_coord_t screen_py,
                                        uint16_t* out_row) {
    if (!instance.list_area || !instance.lbl_page || !out_row) return false;
    if (instance.row_count == 0u || instance.rows_per_page == 0u) return false;

    lv_area_t list_coords{};
    lv_obj_get_coords(instance.list_area, &list_coords);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(instance.list_area, LV_PART_MAIN);
    const int32_t local_y =
        static_cast<int32_t>(screen_py) - static_cast<int32_t>(list_coords.y1)
        - static_cast<int32_t>(pad_top);
    if (local_y < 0) return false;

    const lv_coord_t pitch = station_row_pitch();
    if (pitch <= 0) return false;
    const uint32_t pitch_u = static_cast<uint32_t>(pitch);
    const uint32_t row_idx = static_cast<uint32_t>(local_y) / pitch_u;
    if (row_idx > kStationRowSafetyLimit) return false;

    const uint16_t first = firstRowOnPage(instance);
    const uint16_t last = lastRowOnPage(instance);
    const uint32_t filled_rows =
        static_cast<uint32_t>(last >= first ? (last - first + 1u) : 0u);
    if (row_idx >= filled_rows) return false;

    const uint16_t row = static_cast<uint16_t>(first + row_idx);
    if (row >= instance.row_count) return false;
    *out_row = row;
    return true;
}

static void setFocusRow(Instance& instance, uint16_t row) {
    if (row >= instance.row_count) return;
    instance.focus_row = row;
    layoutFocusChrome(instance, row);
}

static bool wantsPressFocus(const Instance& instance, uint16_t row) {
    return instance.feed.press_focus && instance.feed.press_focus(instance.feed.ctx, row);
}

static void restorePressFocus(Instance& instance) {
    if (!instance.press_focus_active) return;
    const uint16_t previous = instance.focus_before_press;
    instance.press_focus_active = false;
    if (instance.focus_row != previous) setFocusRow(instance, previous);
}

static bool pressContentCurrent(const Instance& instance) {
    return instance.press_content_version == instance.content_version
        && instance.revision_valid
        && (!instance.feed.revision
            || (instance.press_revision == instance.applied_revision
                && instance.feed.revision(instance.feed.ctx) == instance.press_revision));
}

// Folder rows light the existing focus chrome while the finger is down.
// A swipe or a non-folder row puts that chrome back before the click commits.
// Папка зажигает уже существующий фокус, пока палец удерживается.
// Свайп или не-папка возвращают его до подтверждения клика.
static void trackPressFocus(Instance& instance, lv_coord_t screen_y) {
    if (!pressContentCurrent(instance)) {
        restorePressFocus(instance);
        return;
    }
    if (instance.stroke_max_dx >= static_cast<int32_t>(kTapMaxFingerTravelPx)
        || instance.stroke_max_dy >= static_cast<int32_t>(kTapMaxFingerTravelPx)) {
        restorePressFocus(instance);
        return;
    }
    uint16_t row = 0;
    if (!candidateRowFromScreenPoint(instance, screen_y, &row) || !wantsPressFocus(instance, row)) {
        restorePressFocus(instance);
        return;
    }
    if (!instance.press_focus_active) {
        instance.focus_before_press = instance.focus_row;
        instance.press_focus_active = true;
    }
    if (instance.focus_row != row) setFocusRow(instance, row);
}

static void listAreaPressedEvt(lv_event_t* e) {
    auto* instance = static_cast<Instance*>(lv_event_get_user_data(e));
    if (instance) onListAreaPressed(*instance, e);
}

static void listAreaPressingEvt(lv_event_t* e) {
    auto* instance = static_cast<Instance*>(lv_event_get_user_data(e));
    if (instance) onListAreaPressing(*instance, e);
}

static void listAreaReleasedEvt(lv_event_t* e) {
    auto* instance = static_cast<Instance*>(lv_event_get_user_data(e));
    if (instance) onListAreaReleased(*instance, e);
}

static void listAreaShortClickedEvt(lv_event_t* e) {
    auto* instance = static_cast<Instance*>(lv_event_get_user_data(e));
    if (instance) onListAreaShortClicked(*instance, e);
}

static void onListAreaPressed(Instance& instance, lv_event_t* e) {
    if (!instance.list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (lv_event_get_target(e) != instance.list_area) return;

    resetStrokeTracking(instance);
    instance.press_content_version = instance.content_version;
    instance.press_revision = instance.applied_revision;
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) {
        lv_indev_get_point(indev, &instance.press_pt);
    }
    trackPressFocus(instance, instance.press_pt.y);
}

static void onListAreaPressing(Instance& instance, lv_event_t* e) {
    if (!instance.list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_PRESSING) return;
    if (lv_event_get_target(e) != instance.list_area) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t cur{};
    lv_indev_get_point(indev, &cur);
    const int32_t dx = abs_i32(static_cast<int32_t>(cur.x) - static_cast<int32_t>(instance.press_pt.x));
    const int32_t dy = abs_i32(static_cast<int32_t>(cur.y) - static_cast<int32_t>(instance.press_pt.y));
    instance.stroke_max_dx = max_i32(instance.stroke_max_dx, dx);
    instance.stroke_max_dy = max_i32(instance.stroke_max_dy, dy);
    trackPressFocus(instance, cur.y);
}

static void onListAreaReleased(Instance& instance, lv_event_t* e) {
    if (!instance.list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (lv_event_get_target(e) != instance.list_area) return;
    restorePressFocus(instance);

    if (!pressContentCurrent(instance)) return;

    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t cur = instance.press_pt;
    if (indev) {
        lv_indev_get_point(indev, &cur);
    }

    const int32_t dx_end =
        static_cast<int32_t>(cur.x) - static_cast<int32_t>(instance.press_pt.x);
    const int32_t dy_end =
        static_cast<int32_t>(cur.y) - static_cast<int32_t>(instance.press_pt.y);
    const int32_t adx = max_i32(abs_i32(dx_end), instance.stroke_max_dx);
    const int32_t ady = max_i32(abs_i32(dy_end), instance.stroke_max_dy);

    if (adx >= static_cast<int32_t>(kTapMaxFingerTravelPx)
        || ady >= static_cast<int32_t>(kTapMaxFingerTravelPx)) {
        if (ady > adx && ady >= static_cast<int32_t>(kPageSwipeMinTravelPx)) {
            const int direction = (dy_end < 0) ? 1 : ((dy_end > 0) ? -1 : 0);
            if (direction != 0 && tryPageStep(instance, direction)) {
                instance.completed_vertical_page_gesture = true;
                instance.arm_suppress_next_tap = true;
            } else if (direction != 0) {
                instance.completed_vertical_page_gesture = true;
                instance.arm_suppress_next_tap = true;
            }
        }
    }
}

static void onListAreaShortClicked(Instance& instance, lv_event_t* e) {
    if (!instance.list_area || !instance.lbl_page || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    if (lv_event_get_target(e) != instance.list_area) return;
    if (!pressContentCurrent(instance)) return;

    if (instance.completed_vertical_page_gesture) {
        instance.completed_vertical_page_gesture = false;
        return;
    }

    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p{};
    lv_dir_t gd = LV_DIR_NONE;
    if (indev) {
        gd = lv_indev_get_gesture_dir(indev);
        lv_indev_get_point(indev, &p);
    }

    if (!indev) return;
    if ((gd & LV_DIR_HOR) != 0) return;
    if ((gd & LV_DIR_VER) != 0) return;

    const int32_t dx_end =
        static_cast<int32_t>(p.x) - static_cast<int32_t>(instance.press_pt.x);
    const int32_t dy_end =
        static_cast<int32_t>(p.y) - static_cast<int32_t>(instance.press_pt.y);
    const int32_t adx = max_i32(abs_i32(dx_end), instance.stroke_max_dx);
    const int32_t ady = max_i32(abs_i32(dy_end), instance.stroke_max_dy);
    if (adx >= static_cast<int32_t>(kTapMaxFingerTravelPx)
        || ady >= static_cast<int32_t>(kTapMaxFingerTravelPx)) {
        return;
    }

    {
        lv_area_t ar{};
        lv_obj_get_coords(instance.list_area, &ar);
        if (static_cast<int32_t>(p.x) > static_cast<int32_t>(ar.x2) - kScrollbarRightIgnorePx) {
            return;
        }
    }

    if (instance.arm_suppress_next_tap) {
        instance.arm_suppress_next_tap = false;
        return;
    }

    uint16_t row = 0;
    if (!candidateRowFromScreenPoint(instance, p.y, &row)) {
        return;
    }

    setFocusRow(instance, row);
    if (instance.feed.on_tap) instance.feed.on_tap(instance.feed.ctx, row);
}

} // namespace

bool create(Instance& instance, lv_obj_t* parent_screen, const YoRadioPalette& palette) {
    (void)palette;
    if (!parent_screen) return false;
    instance.parent_screen = parent_screen;

    instance.list_area = lv_obj_create(parent_screen);
    if (!instance.list_area) return false;

    lv_obj_set_width(instance.list_area, LV_PCT(100));
    lv_obj_set_flex_grow(instance.list_area, 1);
    lv_obj_set_style_bg_opa(instance.list_area, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(instance.list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(instance.list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(instance.list_area, kListPadLeft, LV_PART_MAIN);
    lv_obj_set_style_pad_right(instance.list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(instance.list_area, kListPadTop, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(instance.list_area, kListPadBottom, LV_PART_MAIN);
    lv_obj_clear_flag(instance.list_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(instance.list_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(instance.list_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scrollbar_mode(instance.list_area, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(instance.list_area, listAreaPressedEvt,      LV_EVENT_PRESSED,       &instance);
    lv_obj_add_event_cb(instance.list_area, listAreaPressingEvt,     LV_EVENT_PRESSING,      &instance);
    lv_obj_add_event_cb(instance.list_area, listAreaReleasedEvt,     LV_EVENT_RELEASED,      &instance);
    lv_obj_add_event_cb(instance.list_area, listAreaShortClickedEvt, LV_EVENT_SHORT_CLICKED, &instance);

    return true;
}

void setFeed(Instance& instance, const BrowserRowFeed& feed) {
    ++instance.content_version;
    instance.press_focus_active = false;
    instance.feed = feed;
    instance.revision_valid = false;
}

void populate(Instance& instance) {
    if (!instance.list_area) return;
    fillCurrentPage(instance);
    if (instance.revision_valid) rememberRevision(instance);
    instance.page_filled_for_pending_enter = true;
}

RefreshOnActivateResult refreshOnActivate(Instance& instance) {
    if (!instance.list_area || !instance.feed.revision) {
        return RefreshOnActivateResult::MarkerOnly;
    }
    const uint32_t now = instance.feed.revision(instance.feed.ctx);
    if (instance.revision_valid && now == instance.applied_revision) {
        return RefreshOnActivateResult::MarkerOnly;
    }
    instance.jump_to_sounding = false;
    fillCurrentPage(instance);
    if (instance.revision_valid) rememberRevision(instance);
    return RefreshOnActivateResult::Rebuilt;
}

void onEnter(Instance& instance) {
    if (!instance.list_area) return;
    const bool already_filled = instance.page_filled_for_pending_enter;
    instance.page_filled_for_pending_enter = false;
    if (already_filled) {
        layoutCurrentMarker(instance, soundingRow(instance));
        return;
    }
    fillCurrentPage(instance);
    rememberRevision(instance);
}

void refreshCurrentStationVisuals(Instance& instance) {
    if (!instance.list_area || !instance.lbl_page) return;
    layoutCurrentMarker(instance, soundingRow(instance));
}

void liveReapplyTheme(Instance& instance, const YoRadioPalette& palette) {
    if (instance.lbl_page) {
        lv_obj_set_style_text_color(instance.lbl_page, palette.list_row_text, LV_PART_MAIN);
    }
    if (instance.focus_row_bg && rowOnCurrentPage(instance, instance.focus_row)) {
        layoutFocusChrome(instance, instance.focus_row);
    }
    const int32_t sounding = soundingRow(instance);
    if (instance.current_marker && sounding >= 0
        && rowOnCurrentPage(instance, static_cast<uint16_t>(sounding))) {
        lv_obj_set_style_text_color(instance.current_marker, palette.accent, LV_PART_MAIN);
    }
}

void releaseAfterTreeDelete(Instance& instance) {
    instance.list_area = nullptr;
    instance.lbl_page = nullptr;
    instance.focus_row_bg = nullptr;
    instance.focus_row_accent = nullptr;
    instance.current_marker = nullptr;
    instance.parent_screen = nullptr;
    instance.feed = {};
    instance.page_index = 0;
    instance.page_count = 0;
    instance.rows_per_page = 0;
    instance.row_count = 0;
    instance.focus_row = 0;
    instance.applied_revision = 0;
    instance.revision_valid = false;
    instance.jump_to_sounding = false;
    instance.page_filled_for_pending_enter = false;
    resetInputState(instance);
    releasePageTextBuffer(instance);
}

} // namespace station_list_simple_paged
} // namespace lvgl_ui
