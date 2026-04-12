/*
 * scr_main.cpp — LvglMainScreen: layout + bindings for Main (LVGL).
 *
 * Layout (flex column on _screen, top → bottom):
 *   wgt_status_line → divider → spacer_top (flex 1) → cont_mid → spacer_bottom (flex 1)
 *   → zone_visual (1px) → zone_bottom: [col_vol: label+bar+touch_zone] → [_bar_buffer: lower divider/meter] → AI line
 *   → (hit_play floating), (_lbl_vol_popup floating).
 * 6.1D-a / a2: volume capsule + inset rim; lower 1px divider/meter; stable AI slot (min_height).
 * 6.1D-b: real touch slider interaction — tap-to-position + drag-to-adjust; temporary numeric popup.
 * 6.1D-a / a2: громкость + ободок groove; нижний divider 1px; слот AI с min_height.
 * 6.1D-b: интерактивная громкость — tap/drag; временный popup с числом.
 *
 * Data (read-only here): config.station.name, .title, .bitrate; config.store.* — no station_t artist field.
 * Split stream title: first " - " → artist string (before) + track (after), same heuristic as legacy Display::_title.
 * Визуальный порядок строк: name → track → artist (artist скрыт, если split не дал первой части).
 *
 * Scroll policy (6.1B): SCROLL_CIRCULAR — station name, track, artist, AI line; CLIP — meta row (#/kbps), weather mini glyphs/temp.
 * DspTask-only lv_*; main_set_text_if_changed reduces redundant layout; RSSI throttled ~3s.
 * Do not route title logic through display.cpp — LVGL path is independent.
 *
 * Object tree (parent→child): scr_main_layout_tree.md — update when create() layout changes.
 * Дерево объектов: scr_main_layout_tree.md — обновлять при изменении разметки create().
 */

#include "scr_main.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "Arduino.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include "WiFi.h"
#include "../fonts/lv_fonts.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../wifi_signal_map.h"
#include "../weather_owm_glyph.h"
#include "lvgl_ui.h"
#include "../../core/config.h"
#include "../../core/display.h"
#include "../../core/network.h"
#include "../../core/player.h"

namespace lvgl_ui {

namespace {

// In-place split for a *mutable* copy of station.title (destroys delimiter in buffer).
// Разбор копии title: после вызова первая часть — NUL-terminated префикс, возврат — хвост после sep.
char* split_inplace_at(char* str, const char* sep) {
    if (!str || !sep) return nullptr;
    char* p = strstr(str, sep);
    if (!p) return nullptr;
    *p = '\0';
    return p + strlen(sep);
}

} // namespace

// Stage 5.3: tap → player.toggle() in PLAYER only; gestures wake screensaver via touch indev, not here.
// Этап 5.3: тап — toggle только в режиме PLAYER.
static void main_play_hit_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (display.mode() != PLAYER) return;
    player.toggle();
    notifyPageChainActivity();
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.1D-b: Volume touch interaction — tap-to-position + drag-to-adjust.
// Интерактивная громкость: tap в точку + drag для плавной регулировки.
// ─────────────────────────────────────────────────────────────────────────────

// Forward declaration for volume popup positioning.
static void vol_popup_update_position(lv_obj_t* popup, lv_obj_t* bar, int32_t vol);

// Calculate volume (0-254) from touch X position relative to bar bounds.
// Вычисление громкости по X-позиции касания относительно границ бара.
static int32_t vol_from_touch_x(lv_obj_t* bar, lv_coord_t touch_x) {
    if (!bar) return 0;
    const lv_coord_t bar_x = lv_obj_get_x(bar);
    const lv_coord_t bar_w = lv_obj_get_width(bar);
    // Account for left/right padding (content area is smaller).
    const lv_coord_t pad_left = lv_obj_get_style_pad_left(bar, LV_PART_MAIN);
    const lv_coord_t pad_right = lv_obj_get_style_pad_right(bar, LV_PART_MAIN);
    const lv_coord_t content_x = bar_x + pad_left;
    const lv_coord_t content_w = bar_w - pad_left - pad_right;
    if (content_w <= 0) return 0;
    // Relative position within content area.
    int32_t rel_x = touch_x - content_x;
    if (rel_x < 0) rel_x = 0;
    if (rel_x > content_w) rel_x = content_w;
    // Map to 0-254 range.
    return (rel_x * 254) / content_w;
}

// Floating touch strip: pad above bar + bar + zone_bottom row gap + heapbar + small overlap (heapbar not clickable).
// Плавающая полоса тача: над баром + бар + зазор + heapbar + лёгкое перекрытие heapbar.
static constexpr lv_coord_t kVolTouchPadTop      = 14;
static constexpr lv_coord_t kVolTouchBarH        = 16;
static constexpr lv_coord_t kVolTouchGapToHeap  = 6;  // zone_bottom pad_row / отступ между col_vol и heapbar
static constexpr lv_coord_t kVolTouchHeapH       = 1;
static constexpr lv_coord_t kVolTouchOverlapHeap = 4;
static constexpr lv_coord_t kVolTouchStripH =
    kVolTouchPadTop + kVolTouchBarH + kVolTouchGapToHeap + kVolTouchHeapH + kVolTouchOverlapHeap;

// Throttle: send PR_VOL to audio queue at most once per kVolThrottleMs.
// Bar visual updates instantly; hardware command is rate-limited to avoid queue flood.
// Троттлинг: PR_VOL в очередь аудио не чаще kVolThrottleMs. Визуал обновляется мгновенно.
static constexpr uint32_t kVolThrottleMs = 80;
static uint32_t s_vol_last_send_ms = 0;
static int32_t  s_vol_pending = -1;   // -1 = nothing pending / нет отложенного значения

// Flush any pending volume to audio queue (call on RELEASED or from periodic update).
// Отправить отложенное значение громкости (при отпускании или из периодического обновления).
static void vol_flush_pending() {
    if (s_vol_pending >= 0) {
        player.setVol(static_cast<uint8_t>(s_vol_pending));
        s_vol_pending = -1;
        s_vol_last_send_ms = millis();
    }
}

// Volume touch event callback — handles PRESSED, PRESSING, RELEASED.
// Callback касания громкости: PRESSED, PRESSING, RELEASED.
static void vol_touch_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    LvglMainScreen* self = static_cast<LvglMainScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    lv_obj_t* touch_zone = lv_event_get_target(e);
    lv_obj_t* bar = static_cast<lv_obj_t*>(lv_obj_get_user_data(touch_zone));
    if (!bar) return;
    lv_obj_t* popup = static_cast<lv_obj_t*>(lv_obj_get_user_data(bar));

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t* indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        // Bar absolute X via parent chain (bar sits inside col_vol inside zone_bottom).
        lv_coord_t bar_abs_x = 0;
        lv_obj_t* obj = bar;
        while (obj) {
            bar_abs_x += lv_obj_get_x(obj);
            obj = lv_obj_get_parent(obj);
        }
        const lv_coord_t bar_w = lv_obj_get_width(bar);
        const lv_coord_t pad_left = lv_obj_get_style_pad_left(bar, LV_PART_MAIN);
        const lv_coord_t pad_right = lv_obj_get_style_pad_right(bar, LV_PART_MAIN);
        const lv_coord_t content_x = bar_abs_x + pad_left;
        const lv_coord_t content_w = bar_w - pad_left - pad_right;
        if (content_w <= 0) return;
        int32_t rel_x = p.x - content_x;
        if (rel_x < 0) rel_x = 0;
        if (rel_x > content_w) rel_x = content_w;
        if (LV_ACTIVE_PROFILE.touch_swap_horizontal_carousel) {
            rel_x = content_w - rel_x;
        }
        int32_t new_vol = (rel_x * 254) / content_w;
        if (new_vol < 0) new_vol = 0;
        if (new_vol > 254) new_vol = 254;

        // Visual: always instant — no throttle for UI feedback.
        lv_bar_set_value(bar, new_vol, LV_ANIM_OFF);
        config.store.volume = static_cast<uint8_t>(new_vol);

        // Hardware: throttled to avoid flooding playerQueue.
        // Аппаратная громкость: троттлинг, чтобы не забить очередь audio task.
        const uint32_t now = millis();
        if (code == LV_EVENT_PRESSED || (now - s_vol_last_send_ms >= kVolThrottleMs)) {
            player.setVol(static_cast<uint8_t>(new_vol));
            s_vol_last_send_ms = now;
            s_vol_pending = -1;
        } else {
            s_vol_pending = new_vol;
        }

        if (popup) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(new_vol));
            lv_label_set_text(popup, buf);
            lv_obj_clear_flag(popup, LV_OBJ_FLAG_HIDDEN);
            vol_popup_update_position(popup, bar, new_vol);
        }
        notifyPageChainActivity();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        // Flush last pending value so final finger position is always applied.
        // Отправить последнее значение — финальная позиция пальца всегда применяется.
        vol_flush_pending();
        if (popup) {
            lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Position popup label above current fill position, clamped to bar bounds.
// Позиционирование popup над текущей позицией fill, ограничено краями бара.
static void vol_popup_update_position(lv_obj_t* popup, lv_obj_t* bar, int32_t vol) {
    if (!popup || !bar) return;
    // Get bar absolute position.
    lv_coord_t bar_abs_x = 0, bar_abs_y = 0;
    lv_obj_t* obj = bar;
    while (obj) {
        bar_abs_x += lv_obj_get_x(obj);
        bar_abs_y += lv_obj_get_y(obj);
        obj = lv_obj_get_parent(obj);
    }
    const lv_coord_t bar_w = lv_obj_get_width(bar);
    const lv_coord_t pad_left = lv_obj_get_style_pad_left(bar, LV_PART_MAIN);
    const lv_coord_t pad_right = lv_obj_get_style_pad_right(bar, LV_PART_MAIN);
    const lv_coord_t content_w = bar_w - pad_left - pad_right;
    // X position of fill end.
    lv_coord_t fill_x = bar_abs_x + pad_left + (content_w * vol) / 254;
    // Popup width.
    const lv_coord_t popup_w = lv_obj_get_width(popup);
    // Center popup over fill_x, but clamp to bar bounds.
    lv_coord_t popup_x = fill_x - popup_w / 2;
    if (popup_x < bar_abs_x) popup_x = bar_abs_x;
    if (popup_x + popup_w > bar_abs_x + bar_w) popup_x = bar_abs_x + bar_w - popup_w;
    // Y position: above bar.
    const lv_coord_t popup_y = bar_abs_y - lv_obj_get_height(popup) - 4;
    lv_obj_set_pos(popup, popup_x, popup_y);
}

// Avoid lv_label_set_text when unchanged — fewer layout passes (5.5a pattern).
// Не дергать set_text без изменения текста — меньше лишнего layout.
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

    // --- Build _screen tree (order = flex order); _hit_play last + FLOATING for z-order / tap above flex. ---
    const uint16_t W = LV_ACTIVE_PROFILE.width;
    const uint16_t H = LV_ACTIVE_PROFILE.height;
    const int32_t pad = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, pad, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (!wgt_status_line::create(_screen, _status_line)) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    // Thin separator: status strip vs main content (token divider — Bible §4.1).
    // Тонкий разделитель статуса и контента.
    lv_obj_t* status_divider = lv_obj_create(_screen);
    if (status_divider) {
        lv_obj_set_width(status_divider, LV_PCT(100));
        lv_obj_set_height(status_divider, 1);
        lv_obj_set_style_bg_color(status_divider, pal.divider, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(status_divider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(status_divider, 0, LV_PART_MAIN);
        // lv_theme_default adds "card" pad to base lv_obj — zero so line matches content width below.
        // Тема LVGL даёт lv_obj лишний pad — обнуляем, линия совпадает с шириной контента.
        lv_obj_set_style_pad_all(status_divider, 0, LV_PART_MAIN);
        lv_obj_clear_flag(status_divider, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Equal flex spacers above/below cont_mid → vertical balance vs 6.1A single lower flex blob.
    // Два равных спейсера — баланс центра по вертикали.
    lv_obj_t* spacer_top = lv_obj_create(_screen);
    if (spacer_top) {
        lv_obj_set_width(spacer_top, LV_PCT(100));
        lv_obj_set_flex_grow(spacer_top, 1);
        lv_obj_set_style_bg_opa(spacer_top, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(spacer_top, 0, LV_PART_MAIN);
        lv_obj_clear_flag(spacer_top, LV_OBJ_FLAG_SCROLLABLE);
    }

    // cont_mid: primary text + meta row (not under wgt_status_line — quiet meta layer).
    // cont_mid: основной текст и meta-строка ниже блока названия/трека.
    lv_obj_t* cont_mid = lv_obj_create(_screen);
    if (cont_mid) {
        lv_obj_set_width(cont_mid, LV_PCT(100));
        lv_obj_set_height(cont_mid, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cont_mid, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            cont_mid,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(cont_mid, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(cont_mid, 16, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cont_mid, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_mid, 0, LV_PART_MAIN);
        lv_obj_clear_flag(cont_mid, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* cont_text = lv_obj_create(cont_mid);
        if (cont_text) {
            lv_obj_set_width(cont_text, LV_PCT(100));
            lv_obj_set_height(cont_text, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(cont_text, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(
                cont_text,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
            // Vertical rhythm: anchor → track → artist (6.1B). pad_row = gap track↔artist; pad_top on track = extra name↔track (LVGL 8 has no margin_top helper).
            // Ритм: pad_row — зазор трек↔артист; pad_top у трека — доп. воздух name↔трек (~4–6 px), кегли без изменений.
            constexpr int32_t k_main_center_pad_row = 10;
            constexpr int32_t k_main_track_pad_top = 6;
            lv_obj_set_style_pad_all(cont_text, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_row(cont_text, k_main_center_pad_row, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cont_text, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(cont_text, 0, LV_PART_MAIN);
            lv_obj_clear_flag(cont_text, LV_OBJ_FLAG_SCROLLABLE);

            _lbl_station_name = lv_label_create(cont_text);
            if (_lbl_station_name) {
                lv_label_set_text(_lbl_station_name, "---");
                lv_label_set_long_mode(_lbl_station_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_obj_set_width(_lbl_station_name, LV_PCT(100));
                lv_obj_set_style_text_align(_lbl_station_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                // Main font experiment: 32 px anchor (direct font ref, not profile slot).
                // Эксперимент: якорь 32 px.
                main_set_font(_lbl_station_name, reinterpret_cast<const void*>(&lv_font_yora_montserrat_32_cyr));
                lv_obj_set_style_text_color(_lbl_station_name, pal.station_name_text, LV_PART_MAIN);
            }

            _lbl_track = lv_label_create(cont_text);
            if (_lbl_track) {
                lv_label_set_text(_lbl_track, " ");
                lv_label_set_long_mode(_lbl_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_obj_set_width(_lbl_track, LV_PCT(100));
                lv_obj_set_style_text_align(_lbl_track, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                // Second tier: 22 px.
                // Второй уровень: 22 px.
                main_set_font(_lbl_track, reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr));
                lv_obj_set_style_text_color(_lbl_track, pal.track_text, LV_PART_MAIN);
                lv_obj_set_style_pad_top(_lbl_track, k_main_track_pad_top, LV_PART_MAIN);
                lv_obj_add_flag(_lbl_track, LV_OBJ_FLAG_HIDDEN);
            }

            _lbl_artist = lv_label_create(cont_text);
            if (_lbl_artist) {
                lv_label_set_text(_lbl_artist, " ");
                lv_label_set_long_mode(_lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_obj_set_width(_lbl_artist, LV_PCT(100));
                lv_obj_set_style_text_align(_lbl_artist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                // Third tier: 18 px (experiment ladder).
                // Третий уровень: 18 px.
                main_set_font(_lbl_artist, reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr));
                lv_obj_set_style_text_color(_lbl_artist, pal.artist_text, LV_PART_MAIN);
                lv_obj_add_flag(_lbl_artist, LV_OBJ_FLAG_HIDDEN);
            }
        }

        lv_obj_t* row_meta = lv_obj_create(cont_mid);
        if (row_meta) {
            lv_obj_set_width(row_meta, LV_PCT(100));
            lv_obj_set_height(row_meta, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row_meta, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                row_meta,
                LV_FLEX_ALIGN_SPACE_BETWEEN,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_bg_opa(row_meta, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row_meta, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row_meta, 0, LV_PART_MAIN);
            lv_obj_clear_flag(row_meta, LV_OBJ_FLAG_SCROLLABLE);

            _lbl_station_num = lv_label_create(row_meta);
            if (_lbl_station_num) {
                lv_label_set_text(_lbl_station_num, "#--");
                lv_label_set_long_mode(_lbl_station_num, LV_LABEL_LONG_CLIP);
                // Meta experiment: 14 px max — quiet vs center stack.
                // Meta: 14 px — тихо относительно центра.
                main_set_font(_lbl_station_num, reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr));
                // Quieter technical layer: foundation text_meta (dimmer than meta_row_text on Dark).
                // Тихий слой: text_meta приглушённее, чем центральный стек.
                lv_obj_set_style_text_color(_lbl_station_num, pal.text_meta, LV_PART_MAIN);
            }

            _lbl_bitrate = lv_label_create(row_meta);
            if (_lbl_bitrate) {
                lv_label_set_text(_lbl_bitrate, "--- kbps");
                lv_label_set_long_mode(_lbl_bitrate, LV_LABEL_LONG_CLIP);
                main_set_font(_lbl_bitrate, reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr));
                lv_obj_set_style_text_color(_lbl_bitrate, pal.text_meta, LV_PART_MAIN);
            }
        }
    }

    lv_obj_t* spacer_bottom = lv_obj_create(_screen);
    if (spacer_bottom) {
        lv_obj_set_width(spacer_bottom, LV_PCT(100));
        lv_obj_set_flex_grow(spacer_bottom, 1);
        lv_obj_set_style_bg_opa(spacer_bottom, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(spacer_bottom, 0, LV_PART_MAIN);
        lv_obj_clear_flag(spacer_bottom, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Reserved height slot for Stage 6.1E / 7 visual widget — no drawing in 6.1B.
    // Резерв под visual zone (6.1E / 7) — пока 1 px, без контента.
    lv_obj_t* zone_visual = lv_obj_create(_screen);
    if (zone_visual) {
        lv_obj_set_width(zone_visual, LV_PCT(100));
        lv_obj_set_height(zone_visual, 1);
        lv_obj_set_flex_grow(zone_visual, 0);
        lv_obj_set_style_bg_opa(zone_visual, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(zone_visual, 0, LV_PART_MAIN);
        lv_obj_clear_flag(zone_visual, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Bottom: full-width volume column; weather glance moved to wgt_status_line (experiment).
    // Низ: громкость на всю ширину; погода в верхней полосе.
    lv_obj_t* zone_bottom = lv_obj_create(_screen);
    if (zone_bottom) {
        lv_obj_set_width(zone_bottom, LV_PCT(100));
        lv_obj_set_height(zone_bottom, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(zone_bottom, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(zone_bottom, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(zone_bottom, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(zone_bottom, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(zone_bottom, 0, LV_PART_MAIN);
        lv_obj_clear_flag(zone_bottom, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* col_vol = lv_obj_create(zone_bottom);
        if (col_vol) {
            lv_obj_set_width(col_vol, LV_PCT(100));
            lv_obj_set_height(col_vol, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(col_vol, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(
                col_vol,
                LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(col_vol, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_row(col_vol, 4, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(col_vol, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(col_vol, 0, LV_PART_MAIN);
            lv_obj_clear_flag(col_vol, LV_OBJ_FLAG_SCROLLABLE);

            _lbl_volume = lv_label_create(col_vol);
            if (_lbl_volume) {
                lv_label_set_text(_lbl_volume, "Vol: --");
                main_set_font(_lbl_volume, LV_ACTIVE_PROFILE.font_small);
                lv_obj_set_style_text_color(_lbl_volume, pal.text_secondary, LV_PART_MAIN);
            }

            _bar_volume = lv_bar_create(col_vol);
            if (_bar_volume) {
                lv_bar_set_range(_bar_volume, 0, 254);
                lv_bar_set_value(_bar_volume, static_cast<int32_t>(config.store.volume), LV_ANIM_OFF);
                lv_obj_set_width(_bar_volume, LV_PCT(100));
                // H=16: compromise between slim 10px bar and test H=20 — gradient+shadow still readable.
                // H=16, radius=8 (capsule), pad 3 top/bottom → fill H=10, fill radius=5; pad 3 left/right.
                // H=16: компромисс между тонким баром и тестом 20 px — эффект всё ещё виден.
                lv_obj_set_height(_bar_volume, 16);
                // Groove (track): full capsule + 1px border rim + gradient + inner shadow.
                // Вертикальный градиент (темнее сверху) + тень сверху = ощущение канавки в поверхности.
                lv_obj_set_style_radius(_bar_volume, 8, LV_PART_MAIN);
                lv_obj_set_style_bg_color(_bar_volume, pal.volume_bar_track, LV_PART_MAIN);
                lv_obj_set_style_bg_grad_color(_bar_volume, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
                lv_obj_set_style_bg_grad_dir(_bar_volume, LV_GRAD_DIR_VER, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(_bar_volume, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(_bar_volume, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(_bar_volume, pal.panel_border, LV_PART_MAIN);
                lv_obj_set_style_border_opa(_bar_volume, LV_OPA_COVER, LV_PART_MAIN);
                // Inner shadow from top: spread=-1 keeps it inside the capsule boundary.
                // Внутренняя тень сверху: spread=-1 не выходит за края капсулы.
                lv_obj_set_style_shadow_color(_bar_volume, lv_color_black(), LV_PART_MAIN);
                lv_obj_set_style_shadow_opa(_bar_volume, LV_OPA_50, LV_PART_MAIN);
                lv_obj_set_style_shadow_width(_bar_volume, 5, LV_PART_MAIN);
                lv_obj_set_style_shadow_spread(_bar_volume, -1, LV_PART_MAIN);
                lv_obj_set_style_shadow_ofs_y(_bar_volume, 2, LV_PART_MAIN);
                // Inset fill: 3px top/bottom → fill H=10, centered in 16px groove.
                // left/right 3px keeps fill away from rounded ends.
                // Паддинг: 3 px сверху/снизу → заливка 10 px по центру; 3 px лево/право.
                lv_obj_set_style_pad_all(_bar_volume, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_top(_bar_volume, 3, LV_PART_MAIN);
                lv_obj_set_style_pad_bottom(_bar_volume, 3, LV_PART_MAIN);
                lv_obj_set_style_pad_left(_bar_volume, 3, LV_PART_MAIN);
                lv_obj_set_style_pad_right(_bar_volume, 3, LV_PART_MAIN);
                // fill radius = fill_height/2 = 5 → mini-capsule fully inside groove r=8.
                // Заливка-капсула r=5 целиком внутри groove r=8.
                lv_obj_set_style_radius(_bar_volume, 5, LV_PART_INDICATOR);
                lv_obj_set_style_bg_color(_bar_volume, pal.volume_bar_fill, LV_PART_INDICATOR);
                lv_obj_set_style_bg_opa(_bar_volume, LV_OPA_COVER, LV_PART_INDICATOR);
                lv_obj_clear_flag(_bar_volume, LV_OBJ_FLAG_CLICKABLE);
            }

            // 6.1D-b: Temporary floating label for volume value during interaction.
            // Временный label с числом громкости во время взаимодействия.
            _lbl_vol_popup = lv_label_create(_screen);
            if (_lbl_vol_popup && _bar_volume) {
                lv_label_set_text(_lbl_vol_popup, "");
                main_set_font(_lbl_vol_popup, LV_ACTIVE_PROFILE.font_small);
                lv_obj_set_style_text_color(_lbl_vol_popup, pal.text_primary, LV_PART_MAIN);
                lv_obj_set_style_bg_color(_lbl_vol_popup, pal.panel_background, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(_lbl_vol_popup, LV_OPA_80, LV_PART_MAIN);
                lv_obj_set_style_pad_all(_lbl_vol_popup, 4, LV_PART_MAIN);
                lv_obj_set_style_radius(_lbl_vol_popup, 4, LV_PART_MAIN);
                lv_obj_add_flag(_lbl_vol_popup, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(_lbl_vol_popup, LV_OBJ_FLAG_FLOATING);
                // Store popup pointer in bar user_data for callback access.
                lv_obj_set_user_data(_bar_volume, _lbl_vol_popup);
            }
        }

        // Lower divider/meter: symmetric to status_divider above; becomes buffer meter when audioinfo=true.
        // Нижний разделитель/meter: симметрично верхнему status_divider; при audioinfo=true — meter буфера.
        _bar_buffer = lv_bar_create(zone_bottom);
        if (_bar_buffer) {
            lv_bar_set_range(_bar_buffer, 0, 100);
            lv_bar_set_value(_bar_buffer, 0, LV_ANIM_OFF);
            lv_obj_set_width(_bar_buffer, LV_PCT(100));
            // 1 px line; meter fill uses brighter token (see theme buffer_meter_fill vs pal.divider).
            // 1 px; заливка meter — из темы, контрастнее divider для читаемости на тонкой линии.
            lv_obj_set_height(_bar_buffer, 1);
            lv_obj_set_style_radius(_bar_buffer, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(_bar_buffer, 0, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(_bar_buffer, pal.divider, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_bar_buffer, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(_bar_buffer, pal.buffer_meter_fill, LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(_bar_buffer, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_clear_flag(_bar_buffer, LV_OBJ_FLAG_CLICKABLE);
        }

        _lbl_ai_line = lv_label_create(zone_bottom);
        if (_lbl_ai_line) {
            lv_label_set_text(_lbl_ai_line, "");
            lv_label_set_long_mode(_lbl_ai_line, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_width(_lbl_ai_line, LV_PCT(100));
            lv_obj_set_style_text_align(_lbl_ai_line, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            main_set_font(_lbl_ai_line, reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr));
            lv_obj_set_style_text_color(_lbl_ai_line, pal.bottom_ai_text, LV_PART_MAIN);
            // Reserve one line height so lower zone does not collapse when AI text is empty (6.1D-a2).
            // Резерв высоты строки — нижняя зона не схлопывается без текста AI (6.1D-a2).
            lv_obj_set_style_min_height(_lbl_ai_line, 20, LV_PART_MAIN);
        }

        // 6.1D-b: Volume touch strip — FLOATING on zone_bottom (not in flex): no extra gap to heapbar.
        // Align to _bar_volume: pad above bar → through heapbar line (slight overlap; heapbar not interactive).
        // Тач-полоса поверх разметки: не раздвигает flex; можно заехать на heapbar.
        if (zone_bottom && _bar_volume) {
            _vol_touch_zone = lv_obj_create(zone_bottom);
            if (_vol_touch_zone) {
                lv_obj_add_flag(_vol_touch_zone, LV_OBJ_FLAG_FLOATING);
                lv_obj_set_width(_vol_touch_zone, LV_PCT(100));
                lv_obj_set_height(_vol_touch_zone, kVolTouchStripH);
                lv_obj_set_style_bg_opa(_vol_touch_zone, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(_vol_touch_zone, 0, LV_PART_MAIN);
                lv_obj_add_flag(_vol_touch_zone, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_clear_flag(_vol_touch_zone, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_set_user_data(_vol_touch_zone, _bar_volume);
                lv_obj_align_to(_vol_touch_zone, _bar_volume, LV_ALIGN_TOP_LEFT, 0, -kVolTouchPadTop);
                lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_PRESSED, this);
                lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_PRESSING, this);
                lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_RELEASED, this);
                lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_PRESS_LOST, this);
            }
        }
    }

    installCarouselGesturesOnPageRoot(_screen);

    // Centered hit box; FLOATING excludes it from parent flex so layout matches 5.x semantics.
    // Плавающая зона тапа — flex не двигает её; размер как в прежних этапах.
    _hit_play = lv_obj_create(_screen);
    if (_hit_play) {
        const int32_t hitW = static_cast<int32_t>(W) - pad * 6;
        const int32_t hitH = static_cast<int32_t>(H) / 3;
        lv_obj_set_size(_hit_play, hitW, hitH);
        lv_obj_align(_hit_play, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(_hit_play, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_style_bg_opa(_hit_play, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(_hit_play, 0, LV_PART_MAIN);
        lv_obj_add_flag(_hit_play, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(_hit_play, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(_hit_play, main_play_hit_cb, LV_EVENT_CLICKED, nullptr);
    }
}

void LvglMainScreen::enter() {
}

void LvglMainScreen::exit() {
}

void LvglMainScreen::update() {
    if (!_lbl_station_name) return;

    static char buf[64];
    static char title_work[BUFLEN];

    main_set_text_if_changed(_lbl_station_name, config.station.name);

    // Stream lines: hide track/artist when title empty or equals station name (same as pre-6.1B behavior).
    // Стрим-метаданные: скрываем трек/артиста, если title пустой или совпадает с именем станции.
    const char* st_title = config.station.title;
    const bool has_stream_meta =
        (st_title != nullptr && strlen(st_title) > 0 && strcmp(st_title, config.station.name) != 0);

    if (_lbl_track && _lbl_artist) {
        if (!has_stream_meta) {
            lv_obj_add_flag(_lbl_track, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_lbl_artist, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(_lbl_track, LV_OBJ_FLAG_HIDDEN);
            strlcpy(title_work, st_title, sizeof(title_work));
            char* second = split_inplace_at(title_work, " - ");
            if (second) {
                main_set_text_if_changed(_lbl_track, second);
                if (strlen(title_work) > 0) {
                    main_set_text_if_changed(_lbl_artist, title_work);
                    lv_obj_clear_flag(_lbl_artist, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(_lbl_artist, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                main_set_text_if_changed(_lbl_track, st_title);
                lv_obj_add_flag(_lbl_artist, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    snprintf(buf, sizeof(buf), "#%d", config.store.lastStation);
    main_set_text_if_changed(_lbl_station_num, buf);

    if (config.station.bitrate > 0) {
        snprintf(buf, sizeof(buf), "%u kbps", config.station.bitrate);
    } else {
        snprintf(buf, sizeof(buf), "--- kbps");
    }
    main_set_text_if_changed(_lbl_bitrate, buf);

    // Wi-Fi icon: Tabler subset glyphs; RSSI sampled at most every 3s when connected.
    // Иконка Wi‑Fi; RSSI не чаще 3 с при подключении.
    static uint32_t s_last_rssi_ms = 0;
    static int      s_rssi_cached_dbm = -100;
    static bool     s_wifi_was_connected = false;
    const bool      wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected != s_wifi_was_connected) {
        s_wifi_was_connected = wifi_connected;
        s_last_rssi_ms = 0;
    }
    const uint32_t now = millis();
    if (!wifi_connected) {
        main_set_text_if_changed(_status_line.lbl_wifi, wifi_status_glyph_utf8_disconnected());
    } else {
        if (s_last_rssi_ms == 0u || (now - s_last_rssi_ms >= 3000u)) {
            s_last_rssi_ms = now;
            s_rssi_cached_dbm = WiFi.RSSI();
        }
        const int lvl = wifi_rssi_to_level(s_rssi_cached_dbm);
        main_set_text_if_changed(_status_line.lbl_wifi, wifi_status_glyph_utf8_for_level(lvl));
    }

    // Clock from network.timeinfo (filled by existing stack — no new bridge in 6.1).
    // Часы из network.timeinfo — тот же источник, что и screensaver.
    static char s_clock_line[16];
    if (network.timeinfo.tm_year > 100) {
        if (strftime(s_clock_line, sizeof(s_clock_line), "%H:%M", &network.timeinfo) == 0) {
            strncpy(s_clock_line, "--:--", sizeof(s_clock_line) - 1);
            s_clock_line[sizeof(s_clock_line) - 1] = '\0';
        }
    } else {
        strncpy(s_clock_line, "--:--", sizeof(s_clock_line) - 1);
        s_clock_line[sizeof(s_clock_line) - 1] = '\0';
    }
    main_set_text_if_changed(_status_line.lbl_clock, s_clock_line);

    snprintf(buf, sizeof(buf), "Vol: %d", config.store.volume);
    main_set_text_if_changed(_lbl_volume, buf);

    if (_bar_volume) {
        lv_bar_set_value(_bar_volume, static_cast<int32_t>(config.store.volume), LV_ANIM_OFF);
    }

    // Lower divider/meter: fill = buffer % when audioinfo enabled AND player running; 0 otherwise.
    // FIX: inBufferFilled() returns bytes, not %. Need (filled*100)/size for correct 0-100 range.
    // WORKAROUND: getInBufferSize() is commented out in Audio.h, use typical default buffer size.
    // Нижний разделитель: fill = % буфера при audioinfo И плеер активен; 0 иначе.
    // FIX: inBufferFilled() — байты, не %. Нужен (filled*100)/size для корректного 0-100.
    // WORKAROUND: getInBufferSize() закомментирован в Audio.h, используем типичный default размер буфера.
    if (_bar_buffer) {
        int32_t bufPercent = 0;
        if (config.store.audioinfo && player.isRunning()) {
            // Default audio buffer size from Audio.h: m_buffSize = UINT16_MAX * 10 = 655350 bytes.
            // This is typical size if user did not change buffer config.
            // Типичный размер буфера из Audio.h: 655350 байт (если не менялся в конфигурации).
            constexpr uint32_t kTypicalBufferSize = 65535U * 10U; // UINT16_MAX * 10
            const uint32_t filled = player.inBufferFilled();
            // Use 64-bit intermediate to avoid overflow.
            bufPercent = static_cast<int32_t>((static_cast<uint64_t>(filled) * 100ULL) / kTypicalBufferSize);
            // Clamp to 0-100.
            if (bufPercent < 0) bufPercent = 0;
            if (bufPercent > 100) bufPercent = 100;
        }
        lv_bar_set_value(_bar_buffer, bufPercent, LV_ANIM_OFF);
    }

    if (_lbl_volume) {
        const YoRadioPalette& pal = yoradio_palette();
        const bool volMode = (display.mode() == VOL);
        lv_obj_set_style_text_color(
            _lbl_volume,
            volMode ? pal.text_primary : pal.text_secondary,
            LV_PART_MAIN);
    }

    // Compact weather in status line (glyph + °C); same data as 6.1C glance fields.
    // Компактная погода в status line — те же network.weather* поля.
    static char weather_temp[16];
    if (_status_line.cont_weather && _status_line.lbl_weather_glyph && _status_line.lbl_weather_temp) {
        const bool wantWx =
            config.store.showweather && (strlen(config.store.weatherkey) > 0) && (network.weatherBuf != nullptr);
        if (wantWx && network.weatherGlanceValid) {
            main_set_text_if_changed(
                _status_line.lbl_weather_glyph,
                weather_owm_icon_to_glyph_utf8(network.weatherOwmIcon));
            snprintf(
                weather_temp,
                sizeof(weather_temp),
                "%+.0f°C",
                static_cast<double>(network.weatherLastTempC));
            main_set_text_if_changed(_status_line.lbl_weather_temp, weather_temp);
            lv_obj_clear_flag(_status_line.lbl_weather_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(_status_line.cont_weather, LV_OBJ_FLAG_HIDDEN);
        } else if (wantWx) {
            lv_obj_add_flag(_status_line.lbl_weather_glyph, LV_OBJ_FLAG_HIDDEN);
            main_set_text_if_changed(_status_line.lbl_weather_temp, "…");
            lv_obj_clear_flag(_status_line.cont_weather, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_status_line.cont_weather, LV_OBJ_FLAG_HIDDEN);
            main_set_text_if_changed(_status_line.lbl_weather_glyph, "");
            main_set_text_if_changed(_status_line.lbl_weather_temp, "");
        }
    }

    // 6.1C AI line: read-only from Display::_aiPendingText via thin API; no AIPlugin changes.
    // 6.1D-a2: slot always occupies layout (min_height) — no HIDDEN when empty.
    // 6.1D-a2: слот всегда в разметке — без HIDDEN при пустом тексте.
    static char ai_compact[256];
    if (_lbl_ai_line) {
        display.copyAIInterpretationForLvgl(ai_compact, sizeof(ai_compact));
        if (ai_compact[0] != '\0') {
            main_set_text_if_changed(_lbl_ai_line, ai_compact);
        } else {
            main_set_text_if_changed(_lbl_ai_line, "");
        }
    }
}

void LvglMainScreen::destroy() {
    // Single lv_obj_del(_screen) drops full tree; null handles to avoid stale pointers.
    // Удаляем экран целиком; обнуляем указатели.
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _status_line = {};
    _lbl_station_num = _lbl_bitrate = nullptr;
    _lbl_station_name = _lbl_track = _lbl_artist = nullptr;
    _lbl_volume = nullptr;
    _bar_volume = nullptr;
    _vol_touch_zone = nullptr;
    _lbl_vol_popup = nullptr;
    _bar_buffer = nullptr;
    _lbl_ai_line = nullptr;
    _hit_play = nullptr;
    _vol_touch_active = false;
}

lv_obj_t* LvglMainScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
