/*
 * LvglPresetScreen — Stage 6.4A Temporary Preset skeleton (PageChain::showTemporary).
 * LvglPresetScreen — скелет Preset Temporary (этап 6.4A).
 *
 * DspTask-only lv_*; tap root dismisses → PageChain::dismissTemporary (return-to-origin).
 * No carousel gestures — horizontal swipe blocked by PageChain while Temporary active.
 */

#include "scr_preset.h"

#include "lvgl.h"
#include "lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

namespace {

static void preset_root_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    dismissActiveTemporary();
}

} // namespace

ScreenType LvglPresetScreen::screenType() const {
    return ScreenType::Temporary;
}

void LvglPresetScreen::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    const uint16_t pad = LV_ACTIVE_PROFILE.frame_padding;

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, pad, LV_PART_MAIN);
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_screen, preset_root_click_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_screen, pad / 2, LV_PART_MAIN);

    _title = lv_label_create(_screen);
    if (_title) {
        lv_label_set_text(_title, "PRESETS");
        lv_obj_set_style_text_color(_title, pal.text_primary, LV_PART_MAIN);
        if (LV_ACTIVE_PROFILE.font_header) {
            lv_obj_set_style_text_font(_title, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_header), LV_PART_MAIN);
        }
    }

    static const char* kStubLabels[kSlotCount] = {
        "1. --",
        "2. --",
        "3. --",
        "4. --",
        "5. --",
        "6. --",
        "7. --",
        "8. --",
    };

    for (int i = 0; i < kSlotCount; ++i) {
        _rows[i] = lv_label_create(_screen);
        if (!_rows[i]) continue;
        lv_label_set_text(_rows[i], kStubLabels[i]);
        lv_obj_set_style_text_color(_rows[i], pal.text_secondary, LV_PART_MAIN);
        if (LV_ACTIVE_PROFILE.font_normal) {
            lv_obj_set_style_text_font(_rows[i], static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_normal), LV_PART_MAIN);
        }
    }
}

void LvglPresetScreen::enter() {}

void LvglPresetScreen::update() {}

void LvglPresetScreen::exit() {}

void LvglPresetScreen::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglPresetScreen::_nullHandles() {
    _screen = nullptr;
    _title = nullptr;
    for (int i = 0; i < kSlotCount; ++i) {
        _rows[i] = nullptr;
    }
}

lv_obj_t* LvglPresetScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui
