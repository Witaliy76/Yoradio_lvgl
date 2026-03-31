// Stage 5.6: LVGL screensaver — black layer + clock on lv_layer_top(); DspTask-only lv_*.
// Этап 5.6: LVGL screensaver — чёрный слой + часы на lv_layer_top(); только lv_* из DspTask.

#include "lv_screensaver.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "profiles/lv_profile_select.h"
#include "../core/network.h"
#include "../core/config.h"
#include "../core/options.h"

#include <cstdio>
#include <cstring>
#include <time.h>

namespace lvgl_ui {

namespace {

lv_obj_t* s_ss_root = nullptr;
lv_obj_t* s_ss_lbl  = nullptr;

// Wake from SCREENSAVER: lv_touch_read_cb (any press/move) — no duplicate handlers here.
// Пробуждение: lv_touch_read_cb (любое нажатие/движение) — дублирующих handler'ов здесь нет.

static void apply_clock_font(lv_obj_t* lbl) {
    if (!lbl) return;
    const void* f = LV_ACTIVE_PROFILE.font_large;
    if (f) {
        lv_obj_set_style_text_font(lbl, static_cast<const lv_font_t*>(f), LV_PART_MAIN);
    }
}

} // namespace

bool screensaverIsVisible() {
    return s_ss_root != nullptr;
}

void screensaverHide() {
    if (s_ss_root) {
        lv_obj_del(s_ss_root);
        s_ss_root = nullptr;
        s_ss_lbl = nullptr;
    }
}

void screensaverRefreshClock() {
    if (!s_ss_lbl) return;
    char line[16];
    if (network.timeinfo.tm_year > 100) {
        if (strftime(line, sizeof(line), "%H:%M", &network.timeinfo) == 0) {
            strncpy(line, "--:--", sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
        }
    } else {
        strncpy(line, "--:--", sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
    }
    lv_label_set_text(s_ss_lbl, line);
}

void screensaverShow() {
    lv_disp_t* disp = lv_disp_get_default();
    if (!disp) return;

    screensaverHide();

    lv_obj_t* top = lv_layer_top();
    if (!top) return;

    const lv_coord_t hor = lv_disp_get_hor_res(disp);
    const lv_coord_t ver = lv_disp_get_ver_res(disp);

    s_ss_root = lv_obj_create(top);
    if (!s_ss_root) return;

    lv_obj_set_size(s_ss_root, hor, ver);
    lv_obj_align(s_ss_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_ss_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ss_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ss_root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_ss_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ss_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ss_root, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(s_ss_root, LV_OBJ_FLAG_CLICKABLE);

    s_ss_lbl = lv_label_create(s_ss_root);
    if (s_ss_lbl) {
        lv_obj_set_style_text_color(s_ss_lbl, lv_color_white(), LV_PART_MAIN);
        apply_clock_font(s_ss_lbl);
        lv_obj_set_style_text_align(s_ss_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(s_ss_lbl, LV_LABEL_LONG_CLIP);
        screensaverRefreshClock();
        lv_obj_center(s_ss_lbl);
    }
}

} // namespace lvgl_ui

#else

namespace lvgl_ui {

void screensaverHide() {}
void screensaverShow() {}
void screensaverRefreshClock() {}
bool screensaverIsVisible() { return false; }

} // namespace lvgl_ui

#endif
