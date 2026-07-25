/*
 * LvglStubPage — placeholder PageChain slots (Visual / Station / Weather / Settings), Stage 5.3.
 * LvglStubPage — заглушки слотов карусели, этап 5.3.
 *
 * DspTask-only lv_*; carousel gestures installed from lvgl_ui helper (not on Boot).
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_stub.h"


#include "lvgl.h"
#include "lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

LvglStubPage::LvglStubPage(const char* titleUtf8) : _titleUtf8(titleUtf8 ? titleUtf8 : "?") {}

ScreenType LvglStubPage::screenType() const {
    return ScreenType::Page;
}

void LvglStubPage::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_CLICKABLE);

    _lbl = lv_label_create(_screen);
    if (_lbl) {
        lv_label_set_text(_lbl, _titleUtf8);
        lv_obj_set_style_text_color(_lbl, pal.text_primary, LV_PART_MAIN);
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

void LvglStubPage::liveReapplyTheme() {
    // Stage 6.6R-C1: colors set only in create(); PageChain calls this on runtime theme switch.
    // Этап 6.6R-C1: цвета задавались в create(); PageChain вызывает при смене темы.
    if (!_screen) return;
    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    if (_lbl) {
        lv_obj_set_style_text_color(_lbl, pal.text_primary, LV_PART_MAIN);
    }
    lv_obj_invalidate(_screen);
}

void LvglStubPage::destroy() {
    // Manual delete path: drop the LVGL root tree, then null handles.
    // Ручное удаление: удаляем дерево LVGL, затем обнуляем указатели.
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglStubPage::releaseAfterAutoDelete() {
    // W2F: LVGL already deleted the screen tree (auto_del). Never lv_obj_del here.
    // W2F: дерево уже удалено LVGL (auto_del). Здесь lv_obj_del не вызываем.
    _nullHandles();
}

void LvglStubPage::_nullHandles() {
    _screen = nullptr;
    _lbl = nullptr;
}

lv_obj_t* LvglStubPage::screen() {
    return _screen;
}

} // namespace lvgl_ui

