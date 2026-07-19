/*
 * station_list_legacy_scroll — continuous-scroll Station list renderer extracted from LvglStationPage.
 * station_list_legacy_scroll — renderer непрерывного списка Station, вынесенный из LvglStationPage.
 *
 * DspTask-only lv_*; one multiline label + native scroll; text buffer outside LVGL heap (PSRAM/heap).
 * Только DspTask; один multiline label + native scroll; буфер текста вне LVGL heap.
 */

#include "station_list_legacy_scroll.h"

#include <cstdio>
#include <cstring>

#include "Arduino.h"
#include <cstdint>

#include "lvgl.h"
#include "../../core/options.h"
#include "../adapters/station_list_adapter.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../../i18n/i18n.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {
namespace station_list_legacy_scroll {

namespace {

constexpr size_t kStationLineBytes      = 112;
constexpr size_t kStationListExtraBytes = 96;

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

static constexpr lv_coord_t kFocusBgRadius    = 8;
static constexpr lv_coord_t kFocusAccentRadius = 2;
static constexpr lv_opa_t   kFocusBgOpa         = LV_OPA_70;

constexpr lv_coord_t kListPadLeft             = 16;
constexpr lv_coord_t kListPadTop              = 8;
constexpr lv_coord_t kListPadBottom           = 8;
constexpr lv_coord_t kFocusAccentStripW       = 3;
constexpr lv_coord_t kMarkerGutterAfterAccent = 5;
constexpr lv_coord_t kMarkerBoxW              = 32;
constexpr lv_coord_t kMarkerToNameGap         = 5;

constexpr int32_t kScrollbarRightIgnorePx = 26;
constexpr lv_coord_t kListTapMaxScrollYDeltaPx = 14;
constexpr lv_coord_t kListTapMaxFingerTravelPx = 24;
static constexpr uint32_t kStationRowSafetyLimit = UINT16_MAX - 10u;

static const void* const kFontStationList =
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr);
static const void* const kFontCurrentMarker =
    reinterpret_cast<const void*>(&lv_font_yora_station_icons_22);
static const char* const kIconCurrentStation = station_glyph_utf8_volume_2();

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

static int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }

static int32_t manhattan_distance(const lv_point_t& from, const lv_point_t& to) {
    const int32_t dx = static_cast<int32_t>(to.x) - static_cast<int32_t>(from.x);
    const int32_t dy = static_cast<int32_t>(to.y) - static_cast<int32_t>(from.y);
    return abs_i32(dx) + abs_i32(dy);
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

static lv_coord_t row_y_for_station_row(uint16_t station_one_based) {
    return static_cast<lv_coord_t>(
        (static_cast<uint32_t>(station_one_based) - 1u) * static_cast<uint32_t>(kStationLinePitch));
}

static void station_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static bool copy_complete(char* out, size_t cap, const char* text) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (!text) return false;
    const size_t bytes = strlen(text) + 1u;
    if (bytes > cap) return false;
    memcpy(out, text, bytes);
    return true;
}

// ── Forward declarations for mutual recursion / forward для взаимных вызовов ──
static void listAreaPressedEvt(lv_event_t* e);
static void listAreaPressingEvt(lv_event_t* e);
static void listAreaReleasedEvt(lv_event_t* e);
static void listAreaShortClickedEvt(lv_event_t* e);

static void resetListStrokeTracking(Instance& instance);
static void resetListInputState(Instance& instance);
static void releaseListTextBuffer(Instance& instance);
static bool ensureListTextBuffer(Instance& instance, uint16_t total);

static void destroyFocusRowOverlays(Instance& instance);
static void destroyCurrentMarkerOverlay(Instance& instance);
static void destroyStationOverlays(Instance& instance);
static void clearStationListVisuals(Instance& instance);
static bool showStationListAllocationError(Instance& instance);
static bool createStationListLabelFromBuffer(Instance& instance);
static void registerListPointerHandlersOnLabel(Instance& instance);
static void cacheListSignature(Instance& instance);

static void clampFocusForTotal(Instance& instance, uint16_t tot);
static void styleFocusRowOverlays(Instance& instance, const YoRadioPalette& pal);
static void positionFocusRowOverlays(Instance& instance, uint16_t focus_station_num);
static void layoutFocusChrome(Instance& instance, uint16_t focus_station);
static bool ensureCurrentMarker(Instance& instance);
static void styleCurrentMarker(Instance& instance, const YoRadioPalette& pal);
static void positionCurrentMarker(Instance& instance, uint16_t current_station_num);
static void layoutMarkerForCurrentStation(Instance& instance, uint16_t current_station);
static void buildStationOverlaysAfterList(Instance& instance, uint16_t current_station_num);
static void setFocusStation(Instance& instance, uint16_t num);

static void captureListPressBaseline(Instance& instance, lv_indev_t* indev);
static void updateListStrokePeaks(Instance& instance, const lv_point_t& cur, lv_coord_t scroll_y);
static bool isTrackedStrokeScrollLike(const Instance& instance);
static bool consumeListFocusSuppression(Instance& instance);
static bool candidateStationFromScreenPoint(Instance& instance, lv_coord_t screen_px,
                                            lv_coord_t screen_py, uint16_t* out_station);

static void onListAreaPressed(Instance& instance, lv_event_t* e);
static void onListAreaPressing(Instance& instance, lv_event_t* e);
static void onListAreaReleased(Instance& instance, lv_event_t* e);
static void onListAreaShortClicked(Instance& instance, lv_event_t* e);

static void resetListStrokeTracking(Instance& instance) {
    instance.list_press_pt            = {};
    instance.list_press_scroll_y      = 0;
    instance.list_stroke_max_manhattan    = 0;
    instance.list_stroke_max_scroll_y_abs = 0;
}

static void resetListInputState(Instance& instance) {
    resetListStrokeTracking(instance);
    instance.list_arm_suppress_next_focus = false;
}

static void releaseListTextBuffer(Instance& instance) {
    if (instance.list_text) {
        free(instance.list_text);
        instance.list_text = nullptr;
    }
    instance.list_text_cap = 0;
}

static bool ensureListTextBuffer(Instance& instance, uint16_t total) {
    const size_t needed =
        static_cast<size_t>(total > 0u ? total : 1u) * kStationLineBytes + kStationListExtraBytes;
    if (instance.list_text && instance.list_text_cap >= needed) return true;

    releaseListTextBuffer(instance);
    instance.list_text = static_cast<char*>(ps_malloc(needed));
    if (!instance.list_text) {
        instance.list_text = static_cast<char*>(malloc(needed));
    }
    if (!instance.list_text) {
        instance.list_text_cap = 0;
        return false;
    }
    instance.list_text_cap = needed;
    instance.list_text[0] = '\0';
    return true;
}

static void destroyFocusRowOverlays(Instance& instance) {
    if (instance.focus_row_bg) {
        lv_obj_del(instance.focus_row_bg);
        instance.focus_row_bg = nullptr;
    }
    if (instance.focus_row_accent) {
        lv_obj_del(instance.focus_row_accent);
        instance.focus_row_accent = nullptr;
    }
}

static void destroyCurrentMarkerOverlay(Instance& instance) {
    if (instance.current_marker) {
        lv_obj_del(instance.current_marker);
        instance.current_marker = nullptr;
    }
}

static void destroyStationOverlays(Instance& instance) {
    destroyFocusRowOverlays(instance);
    destroyCurrentMarkerOverlay(instance);
}

static void clearStationListVisuals(Instance& instance) {
    destroyStationOverlays(instance);
    if (instance.list_area) {
        lv_obj_clean(instance.list_area);
    }
    instance.lbl_list = nullptr;
}

static bool showStationListAllocationError(Instance& instance) {
    instance.lbl_list = lv_label_create(instance.list_area);
    if (!instance.lbl_list) return false;
    lv_label_set_text(instance.lbl_list,
                      i18n::text(i18n::TextId::StationListUnavailable));
    station_set_font(instance.lbl_list, kFontStationList);
    lv_obj_set_style_text_color(instance.lbl_list, yoradio_palette().list_row_text, LV_PART_MAIN);
    lv_obj_set_style_pad_left(instance.lbl_list, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);
    registerListPointerHandlersOnLabel(instance);
    return true;
}

static bool createStationListLabelFromBuffer(Instance& instance) {
    instance.lbl_list = lv_label_create(instance.list_area);
    if (!instance.lbl_list) return false;
    lv_label_set_long_mode(instance.lbl_list, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(instance.lbl_list, instance.list_text);
    station_set_font(instance.lbl_list, kFontStationList);
    lv_obj_set_style_text_color(instance.lbl_list, yoradio_palette().list_row_text, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(instance.lbl_list, kStationListLineSpace, LV_PART_MAIN);
    lv_obj_set_style_pad_left(instance.lbl_list, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);
    return true;
}

static void registerListPointerHandlersOnLabel(Instance& instance) {
    if (!instance.lbl_list) return;
    lv_obj_clear_flag(instance.lbl_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(instance.lbl_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(instance.lbl_list, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void cacheListSignature(Instance& instance) {
    (void)station_list_adapter::list_signature(&instance.list_sig_cache);
    instance.list_sig_cache_valid = true;
}

static void clampFocusForTotal(Instance& instance, uint16_t tot) {
    if (tot == 0u) {
        instance.focus_station_num = 0;
        return;
    }
    if (instance.focus_station_num >= 1u && instance.focus_station_num <= tot
        && station_list_adapter::is_valid_station_num(instance.focus_station_num)) {
        return;
    }
    const uint16_t cur = station_list_adapter::current_station_num();
    if (station_list_adapter::is_valid_station_num(cur) && cur <= tot) {
        instance.focus_station_num = cur;
    } else {
        instance.focus_station_num = 1u;
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
        lv_obj_clear_flag(instance.focus_row_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(instance.focus_row_bg, LV_OBJ_FLAG_CLICKABLE);
    }
    if (instance.focus_row_accent) {
        lv_obj_set_width(instance.focus_row_accent, kFocusAccentStripW);
        lv_obj_set_height(instance.focus_row_accent, kRowAccentH);
        lv_obj_set_style_bg_color(instance.focus_row_accent, pal.accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(instance.focus_row_accent, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(instance.focus_row_accent, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(instance.focus_row_accent, kFocusAccentRadius, LV_PART_MAIN);
        lv_obj_set_style_pad_all(instance.focus_row_accent, 0, LV_PART_MAIN);
        lv_obj_clear_flag(instance.focus_row_accent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(instance.focus_row_accent, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void positionFocusRowOverlays(Instance& instance, uint16_t focus_station_num) {
    const lv_coord_t y = row_y_for_station_row(focus_station_num);
    if (instance.focus_row_bg) {
        lv_obj_set_pos(instance.focus_row_bg, -kListPadLeft, y - kRowOverlayTopPad);
    }
    if (instance.focus_row_accent) {
        lv_obj_set_pos(instance.focus_row_accent, -kListPadLeft + 1, y + kRowAccentY);
    }
}

static void layoutFocusChrome(Instance& instance, uint16_t focus_station) {
    if (!instance.list_area || !instance.lbl_list) return;
    if (!station_list_adapter::is_valid_station_num(focus_station)) {
        destroyFocusRowOverlays(instance);
        return;
    }
    const uint16_t tot = station_list_adapter::station_count();
    if (tot == 0u || focus_station > tot) {
        destroyFocusRowOverlays(instance);
        return;
    }

    const YoRadioPalette& pal = yoradio_palette();

    if (!instance.focus_row_bg) {
        instance.focus_row_bg = lv_obj_create(instance.list_area);
    }
    if (!instance.focus_row_bg) return;

    if (!instance.focus_row_accent) {
        instance.focus_row_accent = lv_obj_create(instance.list_area);
    }
    if (!instance.focus_row_accent) return;

    styleFocusRowOverlays(instance, pal);
    positionFocusRowOverlays(instance, focus_station);

    lv_obj_move_background(instance.focus_row_bg);
    lv_obj_move_foreground(instance.lbl_list);
    if (instance.current_marker) {
        lv_obj_move_foreground(instance.current_marker);
    }
}

static bool ensureCurrentMarker(Instance& instance) {
    if (instance.current_marker) return true;
    instance.current_marker = lv_label_create(instance.list_area);
    if (!instance.current_marker) return false;
    lv_obj_set_width(instance.current_marker, kMarkerBoxW);
    lv_label_set_long_mode(instance.current_marker, LV_LABEL_LONG_CLIP);
    lv_label_set_text(instance.current_marker, kIconCurrentStation);
    station_set_font(instance.current_marker, kFontCurrentMarker);
    lv_obj_set_style_text_align(instance.current_marker, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_flag(instance.current_marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(instance.current_marker, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

static void styleCurrentMarker(Instance& instance, const YoRadioPalette& pal) {
    if (!instance.current_marker) return;
    lv_obj_set_style_text_color(instance.current_marker, pal.accent, LV_PART_MAIN);
}

static void positionCurrentMarker(Instance& instance, uint16_t current_station_num) {
    if (!instance.current_marker) return;
    const lv_coord_t y = row_y_for_station_row(current_station_num);
    const lv_coord_t mx = marker_left_x_in_list();
    lv_obj_set_pos(instance.current_marker, mx, y + kMarkerY);
}

static void layoutMarkerForCurrentStation(Instance& instance, uint16_t current_station) {
    if (!instance.list_area || !instance.lbl_list
        || !station_list_adapter::is_valid_station_num(current_station)) {
        destroyCurrentMarkerOverlay(instance);
        return;
    }

    if (!ensureCurrentMarker(instance)) return;

    const YoRadioPalette& pal = yoradio_palette();
    styleCurrentMarker(instance, pal);
    positionCurrentMarker(instance, current_station);

    lv_obj_move_foreground(instance.lbl_list);
    lv_obj_move_foreground(instance.current_marker);
}

static void buildStationOverlaysAfterList(Instance& instance, uint16_t current_station_num) {
    clampFocusForTotal(instance, station_list_adapter::station_count());
    const uint16_t tot = station_list_adapter::station_count();
    if (tot > 0u && station_list_adapter::is_valid_station_num(instance.focus_station_num)) {
        layoutFocusChrome(instance, instance.focus_station_num);
    } else {
        destroyFocusRowOverlays(instance);
    }
    layoutMarkerForCurrentStation(instance, current_station_num);
}

static void setFocusStation(Instance& instance, uint16_t num) {
    const uint16_t tot = station_list_adapter::station_count();
    if (tot == 0u || !station_list_adapter::is_valid_station_num(num) || num > tot) return;
    instance.focus_station_num = num;
    layoutFocusChrome(instance, num);
}

static void captureListPressBaseline(Instance& instance, lv_indev_t* indev) {
    resetListStrokeTracking(instance);
    if (indev) {
        lv_indev_get_point(indev, &instance.list_press_pt);
    }
    instance.list_press_scroll_y = lv_obj_get_scroll_y(instance.list_area);
}

static void updateListStrokePeaks(Instance& instance, const lv_point_t& cur, lv_coord_t scroll_y) {
    const int32_t manh = manhattan_distance(instance.list_press_pt, cur);
    instance.list_stroke_max_manhattan = max_i32(instance.list_stroke_max_manhattan, manh);

    const int32_t ds = abs_i32(
        static_cast<int32_t>(scroll_y) - static_cast<int32_t>(instance.list_press_scroll_y));
    instance.list_stroke_max_scroll_y_abs = max_i32(instance.list_stroke_max_scroll_y_abs, ds);
}

static bool isTrackedStrokeScrollLike(const Instance& instance) {
    return (instance.list_stroke_max_manhattan    >= static_cast<int32_t>(kListTapMaxFingerTravelPx))
        || (instance.list_stroke_max_scroll_y_abs >= static_cast<int32_t>(kListTapMaxScrollYDeltaPx));
}

static bool consumeListFocusSuppression(Instance& instance) {
    if (!instance.list_arm_suppress_next_focus) return false;
    instance.list_arm_suppress_next_focus = false;
    return true;
}

static bool candidateStationFromScreenPoint(Instance& instance, lv_coord_t screen_px,
                                            lv_coord_t screen_py, uint16_t* out_station) {
    (void)screen_px;
    if (!instance.list_area || !instance.lbl_list || !out_station) {
        return false;
    }

    lv_area_t list_coords{};
    lv_obj_get_coords(instance.list_area, &list_coords);

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(instance.list_area);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(instance.list_area, LV_PART_MAIN);
    const lv_coord_t local_touch_y =
        static_cast<lv_coord_t>(static_cast<int32_t>(screen_py) - static_cast<int32_t>(list_coords.y1));

    const int32_t y_doc =
        static_cast<int32_t>(scroll_y) + static_cast<int32_t>(local_touch_y) - static_cast<int32_t>(pad_top);
    if (y_doc < 0) {
        return false;
    }

    const uint32_t pitch_u = static_cast<uint32_t>(kStationLinePitch);
    if (pitch_u == 0u) {
        return false;
    }

    const uint32_t y_doc_u = static_cast<uint32_t>(y_doc);
    const uint32_t row_idx = y_doc_u / pitch_u;
    if (row_idx > kStationRowSafetyLimit) {
        return false;
    }

    const uint16_t station_one_based = static_cast<uint16_t>(row_idx + 1u);
    const uint16_t tot = station_list_adapter::station_count();
    if (!station_list_adapter::is_valid_station_num(station_one_based) || station_one_based > tot) {
        return false;
    }
    *out_station = station_one_based;
    return true;
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

    lv_indev_t* indev = lv_indev_get_act();
    captureListPressBaseline(instance, indev);

    if (lv_obj_is_scrolling(instance.list_area)) {
        instance.list_arm_suppress_next_focus = true;
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
    updateListStrokePeaks(instance, cur, lv_obj_get_scroll_y(instance.list_area));
}

static void onListAreaReleased(Instance& instance, lv_event_t* e) {
    if (!instance.list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (lv_event_get_target(e) != instance.list_area) return;

    if (isTrackedStrokeScrollLike(instance)) {
        instance.list_arm_suppress_next_focus = true;
    }
}

static void onListAreaShortClicked(Instance& instance, lv_event_t* e) {
    if (!instance.list_area || !instance.lbl_list || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    if (lv_event_get_target(e) != instance.list_area) return;

    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p{};
    lv_dir_t gd = LV_DIR_NONE;
    if (indev) {
        gd = lv_indev_get_gesture_dir(indev);
        lv_indev_get_point(indev, &p);
    }

    if (lv_obj_is_scrolling(instance.list_area)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip scrolling gd=%d p=(%d,%d)\n",
                      static_cast<int>(gd), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    if (!indev) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.println("[station] SHORT_CLICKED skip no_indev");
#endif
        return;
    }
    if ((gd & LV_DIR_HOR) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip horiz_gesture gd=%u p=(%d,%d)\n",
                      static_cast<unsigned>(gd), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    if ((gd & LV_DIR_VER) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip vert_gesture gd=%u p=(%d,%d)\n",
                      static_cast<unsigned>(gd), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    const lv_dir_t scroll_dir = lv_indev_get_scroll_dir(indev);
    if ((scroll_dir & LV_DIR_VER) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip indev_scroll_ver sd=%u\n",
                      static_cast<unsigned>(scroll_dir));
#endif
        return;
    }

    const lv_coord_t scroll_y_now = lv_obj_get_scroll_y(instance.list_area);
    const int32_t d_scroll_end = abs_i32(
        static_cast<int32_t>(scroll_y_now) - static_cast<int32_t>(instance.list_press_scroll_y));
    const int32_t ad_scroll = max_i32(d_scroll_end, instance.list_stroke_max_scroll_y_abs);
    if (ad_scroll >= static_cast<int32_t>(kListTapMaxScrollYDeltaPx)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip scroll_ydelta end=%d peak=%d\n",
                      static_cast<int>(d_scroll_end),
                      static_cast<int>(instance.list_stroke_max_scroll_y_abs));
#endif
        return;
    }
    const int32_t travel_end = manhattan_distance(instance.list_press_pt, p);
    const int32_t travel = max_i32(travel_end, instance.list_stroke_max_manhattan);
    if (travel >= static_cast<int32_t>(kListTapMaxFingerTravelPx)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip finger_travel end=%d peak=%d p=(%d,%d)\n",
                      static_cast<int>(travel_end),
                      static_cast<int>(instance.list_stroke_max_manhattan),
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    {
        lv_area_t ar{};
        lv_obj_get_coords(instance.list_area, &ar);
        if (static_cast<int32_t>(p.x) > static_cast<int32_t>(ar.x2) - kScrollbarRightIgnorePx) {
#if YORADIO_LVGL_TOUCH_DEBUG
            Serial.printf("[station] SHORT_CLICKED skip scrollbar p.x=%d\n", static_cast<int>(p.x));
#endif
            return;
        }
    }
    uint16_t station_num = 0;
    if (!candidateStationFromScreenPoint(instance, p.x, p.y, &station_num)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip no_row p=(%d,%d)\n",
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }

    if (consumeListFocusSuppression(instance)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED arm consumed (no focus) st=%u p=(%d,%d)\n",
                      static_cast<unsigned>(station_num), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }

#if YORADIO_LVGL_TOUCH_DEBUG
    Serial.printf("[station] SHORT_CLICKED focus st=%u p=(%d,%d)\n",
                  static_cast<unsigned>(station_num), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
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
    lv_obj_add_flag(instance.list_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(instance.list_area, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(instance.list_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(instance.list_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(instance.list_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(instance.list_area, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(instance.list_area, listAreaPressedEvt,      LV_EVENT_PRESSED,       &instance);
    lv_obj_add_event_cb(instance.list_area, listAreaPressingEvt,     LV_EVENT_PRESSING,      &instance);
    lv_obj_add_event_cb(instance.list_area, listAreaReleasedEvt,     LV_EVENT_RELEASED,      &instance);
    lv_obj_add_event_cb(instance.list_area, listAreaShortClickedEvt, LV_EVENT_SHORT_CLICKED, &instance);
    return true;
}

void populate(Instance& instance) {
    if (!instance.list_area) return;

    clearStationListVisuals(instance);

    const uint16_t current = station_list_adapter::current_station_num();
    const uint16_t tot     = station_list_adapter::station_count();
    instance.station_total = tot;

    if (!ensureListTextBuffer(instance, tot)) {
        instance.list_sig_cache_valid = false;
        showStationListAllocationError(instance);
        return;
    }

    const size_t name_limit = (LV_ACTIVE_PROFILE.width >= kWideProfileMinWidth)
                              ? kStationNameLimitWide : kStationNameLimitCompact;
    if (!station_list_adapter::station_list_text(instance.list_text, instance.list_text_cap, name_limit)) {
        copy_complete(instance.list_text, instance.list_text_cap,
                      i18n::text(i18n::TextId::StationListUnavailable));
    }

    if (!createStationListLabelFromBuffer(instance)) {
        return;
    }

    buildStationOverlaysAfterList(instance, current);
    registerListPointerHandlersOnLabel(instance);
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
    if (!instance.list_area || !instance.lbl_list) {
        return;
    }
    const uint16_t total = station_list_adapter::station_count();
    if (total == 0u) {
        return;
    }
    if (instance.parent_screen) {
        lv_obj_update_layout(instance.parent_screen);
    }
    const uint16_t current = station_list_adapter::current_station_num();
    if (!station_list_adapter::is_valid_station_num(current) || current > total) {
        lv_obj_scroll_to_y(instance.list_area, 0, LV_ANIM_OFF);
        return;
    }
    const lv_coord_t row_y = row_y_for_station_row(current);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(instance.list_area, LV_PART_MAIN);
    const lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(instance.list_area, LV_PART_MAIN);
    const lv_coord_t h = lv_obj_get_height(instance.list_area);
    const lv_coord_t view_h = h - pad_top - pad_bottom;
    if (view_h <= 0) {
        return;
    }
    lv_coord_t target = row_y - (view_h - kStationLinePitch) / 2;
    if (target < 0) {
        target = 0;
    }
    lv_obj_scroll_to_y(instance.list_area, target, LV_ANIM_OFF);
}

void refreshCurrentStationVisuals(Instance& instance) {
    if (!instance.list_area || !instance.lbl_list) return;

    const uint16_t current = station_list_adapter::current_station_num();
    if (!station_list_adapter::is_valid_station_num(current)) {
        destroyCurrentMarkerOverlay(instance);
        return;
    }
    if (instance.current_marker) {
        positionCurrentMarker(instance, current);
        return;
    }
    layoutMarkerForCurrentStation(instance, current);
}

void liveReapplyTheme(Instance& instance, const YoRadioPalette& palette) {
    if (instance.lbl_list) {
        lv_obj_set_style_text_color(instance.lbl_list, palette.list_row_text, LV_PART_MAIN);
    }
    if (instance.focus_row_bg
        && station_list_adapter::is_valid_station_num(instance.focus_station_num)) {
        layoutFocusChrome(instance, instance.focus_station_num);
    }
    if (instance.current_marker) {
        lv_obj_set_style_text_color(instance.current_marker, palette.accent, LV_PART_MAIN);
    }
}

void releaseAfterTreeDelete(Instance& instance) {
    instance.list_area = nullptr;
    instance.lbl_list = nullptr;
    instance.focus_row_bg = nullptr;
    instance.focus_row_accent = nullptr;
    instance.current_marker = nullptr;
    instance.parent_screen = nullptr;
    instance.station_total = 0;
    instance.focus_station_num = 0;
    instance.list_sig_cache_valid = false;
    resetListInputState(instance);
    releaseListTextBuffer(instance);
}

} // namespace station_list_legacy_scroll
} // namespace lvgl_ui
