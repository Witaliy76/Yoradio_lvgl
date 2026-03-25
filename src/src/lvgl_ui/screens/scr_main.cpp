/*
 * LvglMainScreen — Main player screen skeleton (Stage 5.5 / 5.5a).
 * LvglMainScreen — скелет главного экрана плейера (этап 5.5 / 5.5a).
 *
 * Three semantic zones:
 *   tech_bar (top)   — station #, bitrate, RSSI
 *   center           — station name, title/artist (clip, no scroll in 5.5)
 *   aux_bar (bottom) — volume only (calm skeleton, not a debug dashboard)
 * Три смысловых зоны: тех-полоса, центр, низ — только громкость.
 *
 * 5.5a: compare-before-set on labels to avoid redundant lv_label_set_text → fewer layout passes.
 * 5.5a: сравнение текста перед установкой — меньше лишних проходов layout в LVGL.
 *
 * Lifecycle: ILvglScreen; DspTask only. update() throttled ~1 Hz from Display::loop().
 */

#include "scr_main.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "Arduino.h"
#include <cstring>
#include "WiFi.h"
#include "../profiles/lv_profile_select.h"
#include "../../core/config.h"

namespace lvgl_ui {

// Set label text only if different from current (reduces invalidations / layout work).
// Меняем текст только если отличается — меньше инвалидаций и работы layout.
static void main_set_text_if_changed(lv_obj_t* lbl, const char* s) {
    if (!lbl || !s) return;
    const char* cur = lv_label_get_text(lbl);
    if (cur != nullptr && strcmp(cur, s) == 0) return;
    lv_label_set_text(lbl, s);
}

static void main_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

ScreenType LvglMainScreen::screenType() const {
    return ScreenType::Page;
}

void LvglMainScreen::create() {
    if (_screen) return;

    const uint16_t W = LV_ACTIVE_PROFILE.width;
    const uint16_t H = LV_ACTIVE_PROFILE.height;
    const int32_t pad = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;
    lv_obj_set_style_bg_color(_screen, lv_color_black(), LV_PART_MAIN);

    const int32_t techY = pad;

    _lbl_station_num = lv_label_create(_screen);
    if (_lbl_station_num) {
        lv_label_set_text(_lbl_station_num, "#--");
        lv_obj_set_pos(_lbl_station_num, pad, techY);
        main_set_font(_lbl_station_num, LV_ACTIVE_PROFILE.font_small);
        lv_obj_set_style_text_color(_lbl_station_num, lv_color_white(), LV_PART_MAIN);
    }

    _lbl_bitrate = lv_label_create(_screen);
    if (_lbl_bitrate) {
        lv_label_set_text(_lbl_bitrate, "--- kbps");
        lv_obj_set_pos(_lbl_bitrate, W / 2 - 40, techY);
        main_set_font(_lbl_bitrate, LV_ACTIVE_PROFILE.font_small);
        lv_obj_set_style_text_color(_lbl_bitrate, lv_color_white(), LV_PART_MAIN);
    }

    _lbl_rssi = lv_label_create(_screen);
    if (_lbl_rssi) {
        lv_label_set_text(_lbl_rssi, "-- dBm");
        lv_obj_align(_lbl_rssi, LV_ALIGN_TOP_RIGHT, -pad, techY);
        main_set_font(_lbl_rssi, LV_ACTIVE_PROFILE.font_small);
        lv_obj_set_style_text_color(_lbl_rssi, lv_color_white(), LV_PART_MAIN);
    }

    _lbl_station_name = lv_label_create(_screen);
    if (_lbl_station_name) {
        lv_label_set_text(_lbl_station_name, "---");
        lv_label_set_long_mode(_lbl_station_name, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(_lbl_station_name, W - pad * 4);
        lv_obj_align(_lbl_station_name, LV_ALIGN_CENTER, 0, -30);
        lv_obj_set_style_text_align(_lbl_station_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        main_set_font(_lbl_station_name, LV_ACTIVE_PROFILE.font_large);
        lv_obj_set_style_text_color(_lbl_station_name, lv_color_white(), LV_PART_MAIN);
    }

    _lbl_title = lv_label_create(_screen);
    if (_lbl_title) {
        lv_label_set_text(_lbl_title, " ");
        lv_label_set_long_mode(_lbl_title, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(_lbl_title, W - pad * 4);
        lv_obj_align(_lbl_title, LV_ALIGN_CENTER, 0, 20);
        lv_obj_set_style_text_align(_lbl_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        main_set_font(_lbl_title, LV_ACTIVE_PROFILE.font_normal);
        lv_obj_set_style_text_color(_lbl_title, lv_color_make(0xCC, 0xCC, 0xCC), LV_PART_MAIN);
    }

    const int32_t auxY = H - pad - 20;

    _lbl_volume = lv_label_create(_screen);
    if (_lbl_volume) {
        lv_label_set_text(_lbl_volume, "Vol: --");
        lv_obj_set_pos(_lbl_volume, pad, auxY);
        main_set_font(_lbl_volume, LV_ACTIVE_PROFILE.font_small);
        lv_obj_set_style_text_color(_lbl_volume, lv_color_make(0x80, 0x80, 0x80), LV_PART_MAIN);
    }

    // No lv_obj_update_layout here: first lv_scr_load / lv_timer_handler will layout; avoids extra pass during boot + anim.
    // Без lv_obj_update_layout: первый load/timer сделает layout; лишний проход при старте + анимации не нужен.
}

void LvglMainScreen::enter() {
}

void LvglMainScreen::exit() {
}

void LvglMainScreen::update() {
    if (!_lbl_station_name) return;

    static char buf[64];

    snprintf(buf, sizeof(buf), "#%d", config.store.lastStation);
    main_set_text_if_changed(_lbl_station_num, buf);

    main_set_text_if_changed(_lbl_station_name, config.station.name);

    if (strlen(config.station.title) > 0 &&
        strcmp(config.station.title, config.station.name) != 0) {
        main_set_text_if_changed(_lbl_title, config.station.title);
    } else {
        main_set_text_if_changed(_lbl_title, " ");
    }

    if (config.station.bitrate > 0) {
        snprintf(buf, sizeof(buf), "%u kbps", config.station.bitrate);
    } else {
        snprintf(buf, sizeof(buf), "--- kbps");
    }
    main_set_text_if_changed(_lbl_bitrate, buf);

    // RSSI: refresh at most every 3s — WiFi stack calls from DspTask can be lengthy; 1 Hz was overkill.
    // RSSI: не чаще 3 с — вызовы WiFi из DspTask могут быть тяжёлыми; 1 Гц избыточен.
    static uint32_t s_last_rssi_ms = 0;
    static char     s_rssi_cached[20] = "-- dBm";
    const uint32_t now = millis();
    if (s_last_rssi_ms == 0u || (now - s_last_rssi_ms >= 3000u)) {
        s_last_rssi_ms = now;
        if (WiFi.status() == WL_CONNECTED) {
            snprintf(s_rssi_cached, sizeof(s_rssi_cached), "%d dBm", WiFi.RSSI());
        } else {
            snprintf(s_rssi_cached, sizeof(s_rssi_cached), "-- dBm");
        }
    }
    main_set_text_if_changed(_lbl_rssi, s_rssi_cached);

    snprintf(buf, sizeof(buf), "Vol: %d", config.store.volume);
    main_set_text_if_changed(_lbl_volume, buf);
}

void LvglMainScreen::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _lbl_station_num = _lbl_bitrate = _lbl_rssi = nullptr;
    _lbl_station_name = _lbl_title = nullptr;
    _lbl_volume = nullptr;
}

lv_obj_t* LvglMainScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
