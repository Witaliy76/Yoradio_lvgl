/*
 * wgt_footer_pill — shared visual contract for full-width clickable footer pills.
 * Shared visual contract для кликабельных footer-pill (Weather, Station, Preset).
 *
 * Owns: fixed normal/pressed visual states and palette-dependent colors.
 * Не владеет: geometry, padding, text, callbacks, navigation, gesture flags.
 *
 * Users: scr_weather.cpp, scr_station.cpp, scr_preset.cpp.
 */

#include "wgt_footer_pill.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {
namespace wgt_footer_pill {

// ─────────────────────────────────────────────────────────────────────────────
// Shared fixed constants / Общие фиксированные константы
// ─────────────────────────────────────────────────────────────────────────────

static constexpr lv_coord_t kRadius                  = 14;
static constexpr lv_coord_t kBorderWidth             = 2;
static constexpr lv_opa_t   kNormalBackgroundOpacity = LV_OPA_40;
static constexpr lv_opa_t   kPressedBackgroundOpacity = LV_OPA_50;

// ─────────────────────────────────────────────────────────────────────────────
// API implementation / Реализация API
// ─────────────────────────────────────────────────────────────────────────────

void prepare_surface(lv_obj_t* obj) {
    if (!obj) return;

    // Disable gradient and shadow — all screens reset these before or after calling prepare_surface.
    // Выключить gradient и shadow — все экраны сбрасывают их до или после вызова.
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);

    // Fixed visual contract — shared across Weather, Station and Preset.
    // Фиксированный visual contract — общий для Weather, Station и Preset.
    lv_obj_set_style_radius(obj, kRadius, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, kBorderWidth, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(obj, kNormalBackgroundOpacity, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, kPressedBackgroundOpacity, LV_STATE_PRESSED);

    // Make the surface a tap target; not scrollable.
    // Сделать поверхность таргетом тапа; без скролла.
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void apply_palette(lv_obj_t* obj, const YoRadioPalette& pal) {
    if (!obj) return;

    lv_obj_set_style_bg_color(obj, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, pal.text_meta, LV_STATE_PRESSED);
}

void make_child_passive(lv_obj_t* child) {
    if (!child) return;

    lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(child, LV_OBJ_FLAG_SCROLLABLE);
}

} // namespace wgt_footer_pill
} // namespace lvgl_ui
