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
#include "lvgl_ui.h"
#include "../../core/config.h"
#include "../../core/display.h"
#include "../../core/player.h"

namespace lvgl_ui {

// Stage 5.3: LVGL tap → player.toggle() only when PLAYER (intentionally not full onBtnClick parity).
// Этап 5.3: тап → player.toggle() только в PLAYER (без полной семантики onBtnClick).
static void main_play_hit_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (display.mode() != PLAYER) return;
    player.toggle();
    notifyPageChainActivity();
}


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

    // Bottom volume zone: label row + thin bar (Stage 5.7 inline VOL).
    // Нижняя зона громкости: строка label + узкий bar (5.7 inline VOL).
    const int32_t auxY = H - pad - 36;

    _lbl_volume = lv_label_create(_screen);
    if (_lbl_volume) {
        lv_label_set_text(_lbl_volume, "Vol: --");
        lv_obj_set_pos(_lbl_volume, pad, auxY);
        main_set_font(_lbl_volume, LV_ACTIVE_PROFILE.font_small);
        lv_obj_set_style_text_color(_lbl_volume, lv_color_make(0x80, 0x80, 0x80), LV_PART_MAIN);
    }

    _bar_volume = lv_bar_create(_screen);
    if (_bar_volume) {
        lv_bar_set_range(_bar_volume, 0, 254);
        lv_bar_set_value(_bar_volume, static_cast<int32_t>(config.store.volume), LV_ANIM_OFF);
        const int32_t barW = static_cast<int32_t>(W) - pad * 2;
        lv_obj_set_size(_bar_volume, barW, 8);
        lv_obj_set_pos(_bar_volume, pad, auxY + 18);
        lv_obj_set_style_radius(_bar_volume, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(_bar_volume, 4, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_bar_volume, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_bar_volume, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(_bar_volume, lv_color_make(0xE7, 0xD3, 0x2A), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(_bar_volume, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_clear_flag(_bar_volume, LV_OBJ_FLAG_CLICKABLE);
    }

    installCarouselGesturesOnPageRoot(_screen);

    _hit_play = lv_obj_create(_screen);
    if (_hit_play) {
        const int32_t hitW = static_cast<int32_t>(W) - pad * 6;
        const int32_t hitH = static_cast<int32_t>(H) / 3;
        lv_obj_set_size(_hit_play, hitW, hitH);
        lv_obj_align(_hit_play, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_opa(_hit_play, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(_hit_play, 0, LV_PART_MAIN);
        lv_obj_add_flag(_hit_play, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(_hit_play, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(_hit_play, main_play_hit_cb, LV_EVENT_CLICKED, nullptr);
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

    if (_bar_volume) {
        lv_bar_set_value(_bar_volume, static_cast<int32_t>(config.store.volume), LV_ANIM_OFF);
    }

    // VOL mode: slightly brighter label — inline UX, not a separate page / не отдельная страница
    if (_lbl_volume) {
        const bool volMode = (display.mode() == VOL);
        lv_obj_set_style_text_color(
            _lbl_volume,
            volMode ? lv_color_white() : lv_color_make(0x80, 0x80, 0x80),
            LV_PART_MAIN);
    }
}

void LvglMainScreen::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _lbl_station_num = _lbl_bitrate = _lbl_rssi = nullptr;
    _lbl_station_name = _lbl_title = nullptr;
    _lbl_volume = nullptr;
    _bar_volume = nullptr;
    _hit_play = nullptr;
}

lv_obj_t* LvglMainScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
