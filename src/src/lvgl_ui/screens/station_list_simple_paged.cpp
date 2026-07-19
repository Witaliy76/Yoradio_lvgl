/*
 * station_list_simple_paged — instant page-flip Station list renderer (STATIONPAGED-1).
 * station_list_simple_paged — постраничный renderer без scroll document (STATIONPAGED-1).
 *
 * DspTask-only lv_*; one multiline label per visible page; short heap/PSRAM page buffer.
 * Только DspTask; один label на страницу; короткий page buffer в heap/PSRAM.
 */

#include "station_list_simple_paged.h"

#include <cstdio>
#include <cstring>

#include "Arduino.h"
#include <cstdint>

#include "lvgl.h"
#include "../adapters/station_list_adapter.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../../i18n/i18n.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {
namespace station_list_simple_paged {

namespace {

constexpr size_t kStationLineBytes = 112;
constexpr size_t kPageTextExtraBytes = 96;

constexpr uint32_t kWideProfileMinWidth     = 480u;
constexpr size_t   kStationNameLimitWide    = 64u;
constexpr size_t   kStationNameLimitCompact = 42u;

constexpr lv_coord_t kStationListFontLineHeight = 25;
constexpr lv_coord_t kStationListLineSpace      = 16;
constexpr lv_coord_t kStationLinePitch =
    kStationListFontLineHeight + kStationListLineSpace;

constexpr lv_coord_t kRowOverlayTopPad  = 8;
constexpr lv_coord_t kRowOverlayHeight  = kStationListFontLineHeight + 2 * kRowOverlayTopPad;
constexpr lv_coord_t kRowAccentY        = -2;
constexpr lv_coord_t kRowAccentH        = 29;
constexpr lv_coord_t kMarkerIconLineHeight = 17;
constexpr lv_coord_t kMarkerY =
    (kStationListFontLineHeight - kMarkerIconLineHeight) / 2;

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

static const void* const kFontStationList =
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr);
static const void* const kFontCurrentMarker =
    reinterpret_cast<const void*>(&lv_font_yora_station_icons_22);
static const char* const kIconCurrentStation = station_glyph_utf8_volume_2();

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

static int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }

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
        static_cast<uint32_t>(local_row_zero_based) * static_cast<uint32_t>(kStationLinePitch));
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
    return (LV_ACTIVE_PROFILE.width >= kWideProfileMinWidth)
               ? kStationNameLimitWide
               : kStationNameLimitCompact;
}

static void truncate_utf8_in_place(char* s, size_t max_bytes) {
    if (!s) return;
    const size_t len = strlen(s);
    if (len <= max_bytes) return;
    if (max_bytes == 0u) {
        s[0] = '\0';
        return;
    }
    size_t cut = max_bytes;
    while (cut > 0u && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) {
        --cut;
    }
    if (cut == 0u) cut = max_bytes;
    s[cut] = '\0';
}

static bool format_fallback_station_name(char* out, size_t cap, uint16_t num) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    const int written = snprintf(out, cap,
                                 i18n::text(i18n::TextId::StationFallbackNameFormat),
                                 static_cast<unsigned>(num));
    if (written >= 0 && static_cast<size_t>(written) < cap) return true;
    if (cap >= 3u) {
        memcpy(out, "--", 3u);
    }
    return false;
}

static bool append_station_line(char* out, size_t cap, size_t& used, uint16_t num, const char* name) {
    if (!out || cap == 0u || used >= cap - 1u || !name) return false;
    const char* fmt = (num < 100u) ? "%02u    %s\n" : "%u    %s\n";
    const int written = snprintf(out + used, cap - used, fmt, static_cast<unsigned>(num), name);
    if (written <= 0) {
        out[used] = '\0';
        return false;
    }
    if (static_cast<size_t>(written) >= cap - used) {
        out[used] = '\0';
        return false;
    }
    used += static_cast<size_t>(written);
    return true;
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
static uint16_t firstStationOnPage(const Instance& instance);
static uint16_t lastStationOnPage(const Instance& instance);
static bool stationOnCurrentPage(const Instance& instance, uint16_t station_num);
static uint16_t localRowForStation(const Instance& instance, uint16_t station_num);
static void clampPageIndex(Instance& instance);
static void clampFocusForTotal(Instance& instance, uint16_t total);

static void ensurePageLabel(Instance& instance);
static void ensureOverlays(Instance& instance);
static void styleFocusRowOverlays(Instance& instance, const YoRadioPalette& pal);
static void positionFocusRowOverlays(Instance& instance, uint16_t local_row);
static void layoutFocusChrome(Instance& instance, uint16_t focus_station);
static void hideFocusOverlays(Instance& instance);
static void styleCurrentMarker(Instance& instance, const YoRadioPalette& pal);
static void positionCurrentMarker(Instance& instance, uint16_t local_row);
static void layoutCurrentMarker(Instance& instance, uint16_t current_station);
static void hideCurrentMarker(Instance& instance);
static void applyOverlayZOrder(Instance& instance);
static void cacheListSignature(Instance& instance);

static void fillCurrentPage(Instance& instance);
static void setPageIndexFromCurrentStation(Instance& instance);
static bool tryPageStep(Instance& instance, int direction);
static bool candidateStationFromScreenPoint(Instance& instance, lv_coord_t screen_py,
                                            uint16_t* out_station);
static void setFocusStation(Instance& instance, uint16_t num);

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
    if (content_h < kStationListFontLineHeight) {
        instance.rows_per_page = 1u;
        return;
    }
    // N rows need (N-1)*pitch + line_height; last row has no trailing line_space.
    // N строк = (N-1)*pitch + line_height; у последней нет line_space снизу.
    instance.rows_per_page = static_cast<uint16_t>(
        1u + static_cast<uint32_t>(content_h - kStationListFontLineHeight)
            / static_cast<uint32_t>(kStationLinePitch));
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

static uint16_t firstStationOnPage(const Instance& instance) {
    if (instance.rows_per_page == 0u) return 1u;
    return static_cast<uint16_t>(
        static_cast<uint32_t>(instance.page_index) * static_cast<uint32_t>(instance.rows_per_page)
        + 1u);
}

static uint16_t lastStationOnPage(const Instance& instance) {
    if (instance.station_total == 0u || instance.rows_per_page == 0u) return 0u;
    const uint32_t first = static_cast<uint32_t>(firstStationOnPage(instance));
    const uint32_t last = first + static_cast<uint32_t>(instance.rows_per_page) - 1u;
    const uint32_t total = static_cast<uint32_t>(instance.station_total);
    return static_cast<uint16_t>(last > total ? total : last);
}

static bool stationOnCurrentPage(const Instance& instance, uint16_t station_num) {
    if (!station_list_adapter::is_valid_station_num(station_num)) return false;
    const uint16_t first = firstStationOnPage(instance);
    const uint16_t last = lastStationOnPage(instance);
    return station_num >= first && station_num <= last;
}

static uint16_t localRowForStation(const Instance& instance, uint16_t station_num) {
    const uint16_t first = firstStationOnPage(instance);
    if (station_num < first) return 0u;
    return static_cast<uint16_t>(station_num - first);
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

static void clampFocusForTotal(Instance& instance, uint16_t total) {
    if (total == 0u) {
        instance.focus_station_num = 0;
        return;
    }
    if (instance.focus_station_num >= 1u && instance.focus_station_num <= total
        && station_list_adapter::is_valid_station_num(instance.focus_station_num)) {
        return;
    }
    const uint16_t cur = station_list_adapter::current_station_num();
    if (station_list_adapter::is_valid_station_num(cur) && cur <= total) {
        instance.focus_station_num = cur;
    } else {
        instance.focus_station_num = 1u;
    }
}

static void ensurePageLabel(Instance& instance) {
    if (instance.lbl_page || !instance.list_area) return;
    instance.lbl_page = lv_label_create(instance.list_area);
    if (!instance.lbl_page) return;
    lv_label_set_long_mode(instance.lbl_page, LV_LABEL_LONG_CLIP);
    station_set_font(instance.lbl_page, kFontStationList);
    lv_obj_set_style_text_color(instance.lbl_page, yoradio_palette().list_row_text, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(instance.lbl_page, kStationListLineSpace, LV_PART_MAIN);
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
            station_set_font(instance.current_marker, kFontCurrentMarker);
            lv_obj_set_style_text_align(instance.current_marker, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_clear_flag(instance.current_marker, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(instance.current_marker, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static void styleFocusRowOverlays(Instance& instance, const YoRadioPalette& pal) {
    if (instance.focus_row_bg) {
        lv_obj_set_width(instance.focus_row_bg,
            static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width - (LV_ACTIVE_PROFILE.frame_padding * 2u)));
        lv_obj_set_height(instance.focus_row_bg, kRowOverlayHeight);
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

static void layoutFocusChrome(Instance& instance, uint16_t focus_station) {
    if (!instance.list_area || !instance.lbl_page) return;
    if (!stationOnCurrentPage(instance, focus_station)) {
        hideFocusOverlays(instance);
        return;
    }

    ensureOverlays(instance);
    const YoRadioPalette& pal = yoradio_palette();
    const uint16_t local_row = localRowForStation(instance, focus_station);

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
    lv_obj_set_pos(instance.current_marker, mx, y + kMarkerY);
}

static void layoutCurrentMarker(Instance& instance, uint16_t current_station) {
    if (!instance.list_area || !instance.lbl_page
        || !station_list_adapter::is_valid_station_num(current_station)) {
        hideCurrentMarker(instance);
        return;
    }
    if (!stationOnCurrentPage(instance, current_station)) {
        hideCurrentMarker(instance);
        return;
    }

    ensureOverlays(instance);
    const YoRadioPalette& pal = yoradio_palette();
    const uint16_t local_row = localRowForStation(instance, current_station);
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

static void cacheListSignature(Instance& instance) {
    (void)station_list_adapter::list_signature(&instance.list_sig_cache);
    instance.list_sig_cache_valid = true;
}

static void fillCurrentPage(Instance& instance) {
    if (!instance.list_area) return;

    computeRowsPerPage(instance);
    instance.station_total = station_list_adapter::station_count();
    instance.page_count = pageCountForTotal(instance.station_total, instance.rows_per_page);
    clampPageIndex(instance);
    clampFocusForTotal(instance, instance.station_total);

    ensurePageLabel(instance);
    if (!instance.lbl_page) return;

    if (instance.station_total == 0u) {
        lv_label_set_text(instance.lbl_page,
                          i18n::text(i18n::TextId::StationEmptyList));
        hideFocusOverlays(instance);
        hideCurrentMarker(instance);
        return;
    }

    if (!ensurePageTextBuffer(instance, instance.rows_per_page)) {
        lv_label_set_text(instance.lbl_page,
                          i18n::text(i18n::TextId::StationListUnavailable));
        hideFocusOverlays(instance);
        hideCurrentMarker(instance);
        instance.list_sig_cache_valid = false;
        return;
    }

    const uint16_t first = firstStationOnPage(instance);
    const uint16_t last = lastStationOnPage(instance);
    const size_t name_limit = name_limit_for_profile();

    size_t used = 0;
    instance.page_text[0] = '\0';
    for (uint16_t num = first; num <= last; ++num) {
        char name_buf[160];
        if (!station_list_adapter::station_name(num, name_buf, sizeof(name_buf))) {
            format_fallback_station_name(name_buf, sizeof(name_buf), num);
        }
        truncate_utf8_in_place(name_buf, name_limit);
        if (!append_station_line(instance.page_text, instance.page_text_cap, used, num, name_buf)) {
            break;
        }
    }

    lv_label_set_text_static(instance.lbl_page, instance.page_text);

    layoutFocusChrome(instance, instance.focus_station_num);
    layoutCurrentMarker(instance, station_list_adapter::current_station_num());
}

static void setPageIndexFromCurrentStation(Instance& instance) {
    computeRowsPerPage(instance);
    instance.station_total = station_list_adapter::station_count();
    instance.page_count = pageCountForTotal(instance.station_total, instance.rows_per_page);
    const uint16_t current = station_list_adapter::current_station_num();
    if (instance.station_total == 0u || instance.rows_per_page == 0u) {
        instance.page_index = 0u;
        return;
    }
    if (station_list_adapter::is_valid_station_num(current) && current <= instance.station_total) {
        instance.page_index = static_cast<uint16_t>(
            (static_cast<uint32_t>(current) - 1u) / static_cast<uint32_t>(instance.rows_per_page));
    } else {
        instance.page_index = 0u;
    }
    clampPageIndex(instance);
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

static bool candidateStationFromScreenPoint(Instance& instance, lv_coord_t screen_py,
                                            uint16_t* out_station) {
    if (!instance.list_area || !instance.lbl_page || !out_station) return false;
    if (instance.station_total == 0u || instance.rows_per_page == 0u) return false;

    lv_area_t list_coords{};
    lv_obj_get_coords(instance.list_area, &list_coords);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(instance.list_area, LV_PART_MAIN);
    const int32_t local_y =
        static_cast<int32_t>(screen_py) - static_cast<int32_t>(list_coords.y1)
        - static_cast<int32_t>(pad_top);
    if (local_y < 0) return false;

    const uint32_t pitch_u = static_cast<uint32_t>(kStationLinePitch);
    const uint32_t row_idx = static_cast<uint32_t>(local_y) / pitch_u;
    if (row_idx > kStationRowSafetyLimit) return false;

    const uint16_t first = firstStationOnPage(instance);
    const uint16_t last = lastStationOnPage(instance);
    const uint32_t filled_rows =
        static_cast<uint32_t>(last >= first ? (last - first + 1u) : 0u);
    if (row_idx >= filled_rows) return false;

    const uint16_t station_one_based = static_cast<uint16_t>(first + row_idx);
    if (!station_list_adapter::is_valid_station_num(station_one_based)
        || station_one_based > instance.station_total) {
        return false;
    }
    *out_station = station_one_based;
    return true;
}

static void setFocusStation(Instance& instance, uint16_t num) {
    if (instance.station_total == 0u
        || !station_list_adapter::is_valid_station_num(num)
        || num > instance.station_total) {
        return;
    }
    instance.focus_station_num = num;
    layoutFocusChrome(instance, num);
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
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) {
        lv_indev_get_point(indev, &instance.press_pt);
    }
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
}

static void onListAreaReleased(Instance& instance, lv_event_t* e) {
    if (!instance.list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (lv_event_get_target(e) != instance.list_area) return;

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

    uint16_t station_num = 0;
    if (!candidateStationFromScreenPoint(instance, p.y, &station_num)) {
        return;
    }

    setFocusStation(instance, station_num);
    station_list_adapter::play_station(station_num);
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

void populate(Instance& instance) {
    if (!instance.list_area) return;
    setPageIndexFromCurrentStation(instance);
    fillCurrentPage(instance);
    cacheListSignature(instance);
}

RefreshOnActivateResult refreshOnActivate(Instance& instance) {
    if (!instance.list_area) return RefreshOnActivateResult::MarkerOnly;

    station_list_adapter::StationListSignature now{};
    if (!station_list_adapter::list_signature(&now)) {
        return RefreshOnActivateResult::MarkerOnly;
    }
    if (instance.list_sig_cache_valid
        && station_list_adapter::list_signature_equal(now, instance.list_sig_cache)) {
        return RefreshOnActivateResult::MarkerOnly;
    }
    populate(instance);
    return RefreshOnActivateResult::Rebuilt;
}

void onEnter(Instance& instance) {
    if (!instance.list_area) return;
    setPageIndexFromCurrentStation(instance);
    fillCurrentPage(instance);
}

void refreshCurrentStationVisuals(Instance& instance) {
    if (!instance.list_area || !instance.lbl_page) return;
    layoutCurrentMarker(instance, station_list_adapter::current_station_num());
}

void liveReapplyTheme(Instance& instance, const YoRadioPalette& palette) {
    if (instance.lbl_page) {
        lv_obj_set_style_text_color(instance.lbl_page, palette.list_row_text, LV_PART_MAIN);
    }
    if (instance.focus_row_bg
        && station_list_adapter::is_valid_station_num(instance.focus_station_num)
        && stationOnCurrentPage(instance, instance.focus_station_num)) {
        layoutFocusChrome(instance, instance.focus_station_num);
    }
    if (instance.current_marker
        && stationOnCurrentPage(instance, station_list_adapter::current_station_num())) {
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
    instance.page_index = 0;
    instance.page_count = 0;
    instance.rows_per_page = 0;
    instance.station_total = 0;
    instance.focus_station_num = 0;
    instance.list_sig_cache_valid = false;
    resetInputState(instance);
    releasePageTextBuffer(instance);
}

} // namespace station_list_simple_paged
} // namespace lvgl_ui
