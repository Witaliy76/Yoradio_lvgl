/*
 * LvglStubPage — placeholder PageChain slots (Visual / Station / Weather / Settings), Stage 5.3.
 * LvglStubPage — заглушки слотов карусели, этап 5.3.
 *
 * DspTask-only lv_*; carousel gestures installed from lvgl_ui helper (not on Boot).
 */

#include "scr_stub.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "lvgl_ui.h"
#include "../profiles/lv_profile_select.h"

namespace lvgl_ui {

LvglStubPage::LvglStubPage(const char* titleUtf8) : _titleUtf8(titleUtf8 ? titleUtf8 : "?") {}

ScreenType LvglStubPage::screenType() const {
    return ScreenType::Page;
}

void LvglStubPage::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    lv_obj_set_style_bg_color(_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_CLICKABLE);

    _lbl = lv_label_create(_screen);
    if (_lbl) {
        lv_label_set_text(_lbl, _titleUtf8);
        lv_obj_set_style_text_color(_lbl, lv_color_white(), LV_PART_MAIN);
        if (LV_ACTIVE_PROFILE.font_large) {
            lv_obj_set_style_text_font(_lbl, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_large), LV_PART_MAIN);
        }
        lv_obj_center(_lbl);
    }

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglStubPage::enter() {}

void LvglStubPage::update() {}

void LvglStubPage::exit() {}

void LvglStubPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _lbl = nullptr;
}

lv_obj_t* LvglStubPage::screen() {
    return _screen;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
