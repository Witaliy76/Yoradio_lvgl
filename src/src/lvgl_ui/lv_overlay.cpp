// Stage 5.7: LOST / UPDATING overlays — centralized on lv_layer_top().
// Этап 5.7: оверлеи LOST / UPDATING — централизованно на lv_layer_top().

#include "lv_overlay.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "profiles/lv_profile_select.h"
#include "../displays/tools/l10n.h"

namespace lvgl_ui {

namespace {

lv_obj_t* s_lost_root    = nullptr;
lv_obj_t* s_update_root = nullptr;

static void copy_pgm_title(char* dst, size_t dst_sz, const char* pgm) {
    if (!dst || dst_sz == 0) return;
    strncpy_P(dst, pgm, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static void apply_overlay_title_font(lv_obj_t* lbl) {
    if (!lbl) return;
    const void* f = LV_ACTIVE_PROFILE.font_large;
    if (f) {
        lv_obj_set_style_text_font(lbl, static_cast<const lv_font_t*>(f), LV_PART_MAIN);
    }
}

static void destroy_if_present(lv_obj_t** p) {
    if (p && *p) {
        lv_obj_del(*p);
        *p = nullptr;
    }
}

} // namespace

void overlayHideAll() {
    destroy_if_present(&s_lost_root);
    destroy_if_present(&s_update_root);
}

void overlayShowLost() {
    lv_disp_t* disp = lv_disp_get_default();
    if (!disp) return;

    overlayHideAll();

    lv_obj_t* top = lv_layer_top();
    if (!top) return;

    const lv_coord_t hor = lv_disp_get_hor_res(disp);
    const lv_coord_t ver = lv_disp_get_ver_res(disp);

    s_lost_root = lv_obj_create(top);
    if (!s_lost_root) return;

    lv_obj_set_size(s_lost_root, hor, ver);
    lv_obj_align(s_lost_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_lost_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lost_root, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_lost_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_lost_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_lost_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_lost_root, LV_OBJ_FLAG_SCROLL_CHAIN);

    lv_obj_t* lbl = lv_label_create(s_lost_root);
    if (lbl) {
        char line[48];
        copy_pgm_title(line, sizeof(line), const_DlgLost);
        lv_label_set_text(lbl, line);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
        apply_overlay_title_font(lbl);
        lv_obj_center(lbl);
    }
}

void overlayShowUpdating() {
    lv_disp_t* disp = lv_disp_get_default();
    if (!disp) return;

    overlayHideAll();

    lv_obj_t* top = lv_layer_top();
    if (!top) return;

    const lv_coord_t hor = lv_disp_get_hor_res(disp);
    const lv_coord_t ver = lv_disp_get_ver_res(disp);

    s_update_root = lv_obj_create(top);
    if (!s_update_root) return;

    lv_obj_set_size(s_update_root, hor, ver);
    lv_obj_align(s_update_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_update_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_update_root, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_update_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_update_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_update_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_update_root, LV_OBJ_FLAG_SCROLL_CHAIN);

    lv_obj_t* lbl = lv_label_create(s_update_root);
    if (lbl) {
        char line[48];
        copy_pgm_title(line, sizeof(line), const_DlgUpdate);
        lv_label_set_text(lbl, line);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
        apply_overlay_title_font(lbl);
        lv_obj_center(lbl);
    }
}

} // namespace lvgl_ui

#else

namespace lvgl_ui {

void overlayHideAll() {}
void overlayShowLost() {}
void overlayShowUpdating() {}

} // namespace lvgl_ui

#endif
