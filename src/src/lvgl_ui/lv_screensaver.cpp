/*
 * SSCLK-P0: LVGL screensaver overlay — analog clock hand rotation foundation.
 * SSCLK-P0A: pivot cleanup + full-viewport clock layer (clipping fix).
 *
 * Overlay on lv_layer_top(); not ILvglScreen / not PageChain (Stage 5.6 model).
 * Оверлей на lv_layer_top(); не ILvglScreen / не PageChain (модель Stage 5.6).
 *
 * Layout: lv_screensaver_layout_tree.md
 * DspTask-only lv_* — see lv_touch_indev.cpp for wake path.
 */

#include "lv_screensaver.h"

#include "lvgl.h"
#include "assets/ssclk_hand_probe.h"
#include "profiles/lv_profile_select.h"
#include "theme/lv_theme_yoradio.h"
#include "../core/network.h"

#include <cstdint>

namespace lvgl_ui {

namespace {

// ── Handles / state ─────────────────────────────────────────────────────────
// Module-owned LVGL objects; cleared in screensaverHide().
// LVGL-объекты модуля; обнуляются в screensaverHide().

lv_obj_t* s_ss_root       = nullptr;
lv_obj_t* s_clock_layer   = nullptr;
lv_obj_t* s_hand_hour     = nullptr;
lv_obj_t* s_hand_minute   = nullptr;
lv_obj_t* s_hand_second   = nullptr;
lv_obj_t* s_center_cap    = nullptr;

int16_t s_last_angle_hour   = -1;
int16_t s_last_angle_minute = -1;
int16_t s_last_angle_second = -1;

// Canonical clock center in s_clock_layer coords; set once per screensaverShow().
// Канонический центр часов в координатах s_clock_layer; задаётся при screensaverShow().
lv_coord_t s_clock_center_x = 0;
lv_coord_t s_clock_center_y = 0;

// ── Geometry / resource constants ───────────────────────────────────────────

static constexpr lv_coord_t kCenterCapSize = 10;

static const lv_img_dsc_t s_hand_probe_dsc = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .always_zero = 0,
        .reserved = 0,
        .w = SSCLK_HAND_PROBE_W,
        .h = SSCLK_HAND_PROBE_H,
    },
    .data_size = SSCLK_HAND_PROBE_DATA_SIZE,
    .data = ssclk_hand_probe_map,
};

// ── Pure helpers ────────────────────────────────────────────────────────────

static void resetAngleCache() {
    s_last_angle_hour   = -1;
    s_last_angle_minute = -1;
    s_last_angle_second = -1;
}

static void nullAllHandles() {
    s_ss_root         = nullptr;
    s_clock_layer     = nullptr;
    s_hand_hour       = nullptr;
    s_hand_minute     = nullptr;
    s_hand_second     = nullptr;
    s_center_cap      = nullptr;
    s_clock_center_x  = 0;
    s_clock_center_y  = 0;
}

static bool timeIsValid() {
    return network.timeinfo.tm_year > 100;
}

static int16_t calcHourAngleTenths(const tm& t) {
    return static_cast<int16_t>((t.tm_hour % 12) * 300 + t.tm_min * 5 + t.tm_sec / 12);
}

static int16_t calcMinuteAngleTenths(const tm& t) {
    return static_cast<int16_t>(t.tm_min * 60 + t.tm_sec);
}

static int16_t calcSecondAngleTenths(const tm& t) {
    return static_cast<int16_t>(t.tm_sec * 60);
}

static void setHandAngleIfChanged(lv_obj_t* hand, int16_t& last_angle, int16_t angle) {
    if (!hand) return;
    if (last_angle == angle) return;
    lv_img_set_angle(hand, angle);
    last_angle = angle;
}

// Shared placement: one pivot, one screen center for all hands.
// Общее размещение: один pivot и один экранный центр для всех стрелок.
static void setupProbeHandPlacement(lv_obj_t* hand) {
    if (!hand) return;

    lv_img_set_src(hand, &s_hand_probe_dsc);
    lv_obj_set_pos(
        hand,
        s_clock_center_x - static_cast<lv_coord_t>(SSCLK_HAND_PROBE_PIVOT_X),
        s_clock_center_y - static_cast<lv_coord_t>(SSCLK_HAND_PROBE_PIVOT_Y));
    lv_img_set_pivot(hand, SSCLK_HAND_PROBE_PIVOT_X, SSCLK_HAND_PROBE_PIVOT_Y);
    lv_img_set_antialias(hand, true);
    lv_img_set_angle(hand, 0);
}

static void styleFullViewportLayer(lv_obj_t* layer) {
    if (!layer) return;

    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(layer, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(layer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
}

// ── Creation helpers ────────────────────────────────────────────────────────
// Z-order (bottom → top): hour, minute, second, cap. Load-bearing create order.
// Z-order (снизу вверх): hour, minute, second, cap. Порядок создания важен.

static lv_obj_t* createProbeHand(lv_obj_t* parent) {
    lv_obj_t* hand = lv_img_create(parent);
    if (!hand) return nullptr;
    setupProbeHandPlacement(hand);
    return hand;
}

static void createCenterCap(lv_obj_t* parent, const YoRadioPalette& pal) {
    s_center_cap = lv_obj_create(parent);
    if (!s_center_cap) return;

    lv_obj_set_size(s_center_cap, kCenterCapSize, kCenterCapSize);
    lv_obj_set_style_radius(s_center_cap, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_center_cap, pal.text_secondary, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_center_cap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_center_cap, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_center_cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_center_cap, LV_ALIGN_CENTER, 0, 0);
}

static void createClockStack(lv_obj_t* root, lv_coord_t hor, lv_coord_t ver, const YoRadioPalette& pal) {
    s_clock_center_x = hor / 2;
    s_clock_center_y = ver / 2;

    s_clock_layer = lv_obj_create(root);
    if (!s_clock_layer) return;

    lv_obj_set_size(s_clock_layer, hor, ver);
    lv_obj_align(s_clock_layer, LV_ALIGN_TOP_LEFT, 0, 0);
    styleFullViewportLayer(s_clock_layer);

    // Hour → minute → second → single cap (seconds on top, cap above all hands).
    // Час → мин → сек → один cap (секунды сверху, cap поверх стрелок).
    s_hand_hour   = createProbeHand(s_clock_layer);
    s_hand_minute = createProbeHand(s_clock_layer);
    s_hand_second = createProbeHand(s_clock_layer);
    createCenterCap(s_clock_layer, pal);
}

static void createScreensaverTree(lv_disp_t* disp, const YoRadioPalette& pal) {
    const lv_coord_t hor = lv_disp_get_hor_res(disp);
    const lv_coord_t ver = lv_disp_get_ver_res(disp);

    lv_obj_t* top = lv_layer_top();
    if (!top) return;

    s_ss_root = lv_obj_create(top);
    if (!s_ss_root) return;

    lv_obj_set_size(s_ss_root, hor, ver);
    lv_obj_align(s_ss_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_ss_root, pal.screensaver_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ss_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ss_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ss_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ss_root, LV_OBJ_FLAG_SCROLL_CHAIN);

    createClockStack(s_ss_root, hor, ver, pal);
}

// ── Angle / update helper ───────────────────────────────────────────────────

static void applyHandAnglesFromTimeinfo() {
    if (!s_hand_hour && !s_hand_minute && !s_hand_second) return;

    if (!timeIsValid()) {
        setHandAngleIfChanged(s_hand_hour, s_last_angle_hour, 0);
        setHandAngleIfChanged(s_hand_minute, s_last_angle_minute, 0);
        setHandAngleIfChanged(s_hand_second, s_last_angle_second, 0);
        return;
    }

    const tm& t = network.timeinfo;
    setHandAngleIfChanged(s_hand_hour, s_last_angle_hour, calcHourAngleTenths(t));
    setHandAngleIfChanged(s_hand_minute, s_last_angle_minute, calcMinuteAngleTenths(t));
    setHandAngleIfChanged(s_hand_second, s_last_angle_second, calcSecondAngleTenths(t));
}

} // namespace

bool screensaverIsVisible() {
    return s_ss_root != nullptr;
}

void screensaverHide() {
    if (s_ss_root) {
        lv_obj_del(s_ss_root);
    }
    nullAllHandles();
    resetAngleCache();
}

void screensaverRefreshClock() {
    applyHandAnglesFromTimeinfo();
}

void screensaverShow() {
    lv_disp_t* disp = lv_disp_get_default();
    if (!disp) return;

    screensaverHide();

    const YoRadioPalette& pal = yoradio_palette();
    createScreensaverTree(disp, pal);
    applyHandAnglesFromTimeinfo();
}

} // namespace lvgl_ui
