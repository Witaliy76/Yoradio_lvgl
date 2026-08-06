// Stage 5.7: LOST / UPDATING overlays — centralized on lv_layer_top().
// Этап 5.7: оверлеи LOST / UPDATING — централизованно на lv_layer_top().
// Author: Witaliy76 - https://github.com/Witaliy76

#include "lv_overlay.h"
#include "lv_screensaver.h"

#include "lvgl.h"
#include <cstring>
#include "profiles/lv_profile_select.h"
#include "theme/lv_theme_yoradio.h"
#include "../i18n/i18n.h"

namespace lvgl_ui {

namespace {

lv_obj_t* s_lost_root       = nullptr;
// S6V8A + S6V9I: second label — reconnect hint + Recovery escalation copy (multiline OK). / Вторая строка LOST.
lv_obj_t* s_lost_status_lbl = nullptr;
lv_obj_t* s_update_root     = nullptr;

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
    screensaverHide();
    // S6V8A: s_lost_status_lbl is a child of s_lost_root; lv_obj_del(s_lost_root) destroys it.
    // S6V8A: s_lost_status_lbl — дочерний объект s_lost_root; уничтожается вместе с родителем.
    s_lost_status_lbl = nullptr;
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

    const YoRadioPalette& pal = yoradio_palette();

    lv_obj_set_size(s_lost_root, hor, ver);
    lv_obj_align(s_lost_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_lost_root, pal.overlay_scrim, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lost_root, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_lost_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_lost_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_lost_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_lost_root, LV_OBJ_FLAG_SCROLL_CHAIN);

    // Container to hold both labels vertically centred together.
    // Контейнер для вертикальной группировки двух label'ов по центру.
    lv_obj_t* col = lv_obj_create(s_lost_root);
    if (col) {
        lv_obj_set_size(col, LV_PCT(90), LV_SIZE_CONTENT);
        lv_obj_center(col);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 10, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(col);
        if (lbl) {
            lv_label_set_text_static(lbl, i18n::text(i18n::TextId::OverlayConnectionLost));
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl, LV_PCT(100));
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, pal.overlay_title_text, LV_PART_MAIN);
            apply_overlay_title_font(lbl);
        }

        // S6V8A + S6V9I: status sub-label — initially empty; Display sets multiline text at milestones.
        // S6V8A + S6V9I: статусная строка — пусто до первого тика; Display задаёт многострочный текст.
        s_lost_status_lbl = lv_label_create(col);
        if (s_lost_status_lbl) {
            lv_label_set_text(s_lost_status_lbl, "");
            lv_label_set_long_mode(s_lost_status_lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(s_lost_status_lbl, LV_PCT(100));
            lv_obj_set_style_text_align(s_lost_status_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_lost_status_lbl, pal.overlay_title_text, LV_PART_MAIN);
            if (LV_ACTIVE_PROFILE.font_small) {
                lv_obj_set_style_text_font(s_lost_status_lbl,
                                           static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_small),
                                           LV_PART_MAIN);
            }
        }
    }
}

// S6V8A: set LOST overlay status text; safe if overlay not shown; strcmp guard to avoid redraw.
// S6V8A: задать текст статусной строки; безопасно без overlay; guard против лишнего redraw.
void overlayLostSetStatusText(const char* text) {
    if (!s_lost_status_lbl) return;
    if (!text) text = "";
    const char* cur = lv_label_get_text(s_lost_status_lbl);
    if (cur && strcmp(cur, text) == 0) return;
    lv_label_set_text(s_lost_status_lbl, text);
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

    const YoRadioPalette& pal = yoradio_palette();

    lv_obj_set_size(s_update_root, hor, ver);
    lv_obj_align(s_update_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_update_root, pal.overlay_scrim, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_update_root, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_update_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_update_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_update_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_update_root, LV_OBJ_FLAG_SCROLL_CHAIN);

    lv_obj_t* lbl = lv_label_create(s_update_root);
    if (lbl) {
        lv_label_set_text_static(lbl, i18n::text(i18n::TextId::OverlayUpdating));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, pal.overlay_title_text, LV_PART_MAIN);
        apply_overlay_title_font(lbl);
        lv_obj_center(lbl);
    }
}

} // namespace lvgl_ui
