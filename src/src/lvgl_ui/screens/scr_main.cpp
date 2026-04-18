/*
 * scr_main.cpp — LvglMainScreen: layout + bindings for Main (LVGL).
 *
 * Layout (flex column on _screen, top → bottom):
 *   wgt_status_line → divider → spacer_top (flex 1) → cont_mid (text only) → spacer_bottom (flex 1)
 *   → zone_visual (1px) → zone_bottom: control_band → row_meta_stream → col_vol → heapbar → AI;
 *   FLOATING: vol_touch_zone, vol_gesture_guard,
 *   screen_bottom_carousel_guard (dead strip / padding — no carousel to touch bottom)
 *   → (_lbl_vol_popup floating).
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
#include "../control_glyph_utf8.h"
#include "lvgl_ui.h"
#include "../../core/config.h"
#include "../../core/display.h"
#include "../../core/network.h"
#include "../../core/player.h"
#include <LittleFS.h>

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

// Load a .bin (4-byte lv_img_header_t + RGB565 pixels) from LittleFS into PSRAM.
// On success: out_buf is ps_malloc'd buffer, out_dsc is ready for lv_img_set_src().
// Caller owns out_buf and must free() it.
// Загрузить .bin из LittleFS в PSRAM; out_buf — ps_malloc, освобождать через free().
static bool bg_load_into_psram(const char* fs_path, uint8_t*& out_buf, lv_img_dsc_t& out_dsc) {
    out_buf = nullptr;
    File f = LittleFS.open(fs_path, "r");
    if (!f) return false;

    const size_t file_sz = static_cast<size_t>(f.size());
    if (file_sz <= sizeof(lv_img_header_t)) {
        f.close();
        return false;
    }

    lv_img_header_t hdr;
    if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        return false;
    }

    const uint32_t data_size = static_cast<uint32_t>(file_sz - sizeof(hdr));
    uint8_t* buf = static_cast<uint8_t*>(ps_malloc(data_size));
    if (!buf) {
        f.close();
        Serial.printf("[BG] ps_malloc failed (%u bytes) for %s\n", data_size, fs_path);
        return false;
    }

    const int32_t n = f.read(buf, data_size);
    f.close();

    if (n < 0 || static_cast<uint32_t>(n) != data_size) {
        free(buf);
        Serial.printf("[BG] read incomplete: got %d / %u bytes\n", n, data_size);
        return false;
    }

    out_buf           = buf;
    out_dsc.header    = hdr;
    out_dsc.data_size = data_size;
    out_dsc.data      = buf;

    Serial.printf("[BG] preloaded %s -> PSRAM %u bytes\n", fs_path, data_size);
    return true;
}

// Pressed-state opa: transport more readable; utility calmer (secondary) / читаемее на транспорте, utility тише.
constexpr lv_opa_t k_ctrl_pressed_opa_transport = LV_OPA_20;
constexpr lv_opa_t k_ctrl_pressed_opa_utility   = static_cast<lv_opa_t>(36); // ~14% vs ~20% transport

// Stage 6.1F-b: theme slot → /bg/main_*.bin; hide layer if missing (normal case). / Слот темы → bin; скрыть если нет файла.
void main_apply_theme_background(lv_obj_t* bg_img) {
    if (!bg_img) {
        return;
    }
    static const char* const k_fs[] = {
        "/bg/main_dark.bin",
        "/bg/main_light.bin",
        "/bg/main_custom.bin",
    };
    static const char* const k_lvgl[] = {
        "L:/bg/main_dark.bin",
        "L:/bg/main_light.bin",
        "L:/bg/main_custom.bin",
    };
    uint8_t idx = static_cast<uint8_t>(yoradio_theme_active_preset());
    if (idx > 2) {
        idx = 0;
    }
    if (!LittleFS.exists(k_fs[idx])) {
        // Clear decoder/src so LVGL does not keep stale path → "NoData" after WebUI remove / Сброс src при удалении файла
        lv_img_set_src(bg_img, nullptr);
        lv_obj_add_flag(bg_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_img_set_src(bg_img, k_lvgl[idx]);
    lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_HIDDEN);
}

// F-c: black scrim — only when file bg is shown AND Dark preset (readability). / Scrim только Dark + есть фон.
// LVGL v8 named opa (lv_opa.h): LV_OPA_TRANSP=0; LV_OPA_10…LV_OPA_90 (~10%…90% of cover); LV_OPA_100/LV_OPA_COVER=255.
// Именованные ступени: 0, 10…90, 100/COVER; можно любое lv_opa_t 0–255.
// 50% named step — LV_OPA_50 (~127/255); not linear “half brightness” of the bitmap. / не «половина яркости» пикселей.
//если хотите изменить яркость фона, то меняйте значение на другие, чем выше тем темнее фон.
//constexpr lv_opa_t k_main_bg_scrim_opa_dark = LV_OPA_50;
// TEMP (device testing): scrim effectively off — restore `LV_OPA_50` when re-tuning readability / ВРЕМЕННО: scrim визуально выкл.
constexpr lv_opa_t k_main_bg_scrim_opa_dark = LV_OPA_TRANSP;  // was: LV_OPA_50

void main_sync_dark_bg_scrim(lv_obj_t* bg_img, lv_obj_t* scrim) {
    if (!scrim) {
        return;
    }
    // TEMP: skip compositing scrim layer while opacity is fully transparent / Пока opa=0 — не трогаем scrim
    if (k_main_bg_scrim_opa_dark == LV_OPA_TRANSP) {
        lv_obj_add_flag(scrim, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!bg_img || lv_obj_has_flag(bg_img, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(scrim, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (yoradio_theme_active_preset() != ThemePreset::Dark) {
        lv_obj_add_flag(scrim, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

// Transport: only visible control_band buttons (no hidden center hit-zone). PLAYER-only; prev/next match hardware side-button guards.
// Транспорт: только видимые кнопки (без скрытой зоны тапа). Только PLAYER; prev/next — как у боковых кнопок (double-click).
static void main_transport_toggle_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (display.mode() != PLAYER) return;
    player.toggle();
    notifyPageChainActivity();
}

static void main_transport_prev_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (display.mode() != PLAYER) return;
    if (network.status != CONNECTED && network.status != SDREADY) return;
    player.prev();
    notifyPageChainActivity();
}

static void main_transport_next_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (display.mode() != PLAYER) return;
    if (network.status != CONNECTED && network.status != SDREADY) return;
    player.next();
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

// Touch strip: only semantic margins — geometry from lv_obj_get_coords(_bar_volume/_bar_buffer) after layout.
// Полоса тача: только product-margins; геометрия из реальных bounds после layout.
static constexpr lv_coord_t kVolTouchPadAbove    = 14;
static constexpr lv_coord_t kVolTouchOverlapBelow = 4;

// While user drags slider, skip syncing lv_bar from config in update() — config lags behind PR_VOL.
// Пока трогаем слайдер — не перезаписывать бар из config (иначе гонка с асинхронным PR_VOL).
static bool s_vol_touch_active = false;

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
        // X normalization now lives in lv_touch_read_cb — no per-widget workaround needed.
        // Нормализация X теперь в lv_touch_read_cb — локальный workaround больше не нужен.
        int32_t new_vol = (rel_x * 254) / content_w;
        if (new_vol < 0) new_vol = 0;
        if (new_vol > 254) new_vol = 254;

        s_vol_touch_active = true;

        // Visual: always instant — no throttle for UI feedback. Config/audio only via player.setVol → PR_VOL.
        // Визуал сразу; config — только через player.setVol (без прямой записи в config.store).
        lv_bar_set_value(bar, new_vol, LV_ANIM_OFF);

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
        s_vol_touch_active = false;
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

// 6.1E control button: icon inside lv_btn with min hit area; blocks gesture bubble.
// Кнопка управления: иконка в lv_btn с минимальной зоной касания; без всплытия жеста.
// corner_radius: transport slightly rounder (device-like); utility smaller radius = quieter / радиус: транспорт увереннее, utility тише.
// pressed_bg_opa: larger min_side+pad make the pill big; higher opa = clearer acknowledgment / заметнее подсветка при нажатии.
static lv_obj_t* main_create_control_icon_btn(
    lv_obj_t* parent,
    const char* utf8_glyph,
    const lv_font_t* icon_font,
    lv_color_t fg,
    lv_coord_t pad_inner,
    lv_coord_t min_side,
    lv_coord_t corner_radius = 14,
    lv_opa_t pressed_bg_opa = LV_OPA_20) {
    lv_obj_t* btn = lv_btn_create(parent);
    if (!btn) return nullptr;
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_white(), static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, pressed_bg_opa, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, corner_radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, pad_inner, LV_PART_MAIN);
    lv_obj_set_style_min_width(btn, min_side, LV_PART_MAIN);
    lv_obj_set_style_min_height(btn, min_side, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t* lbl = lv_label_create(btn);
    if (lbl) {
        lv_label_set_text(lbl, utf8_glyph);
        lv_obj_set_style_text_font(lbl, icon_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, fg, LV_PART_MAIN);
        lv_obj_center(lbl);
    }
    return btn;
}

ScreenType LvglMainScreen::screenType() const {
    return ScreenType::Page;
}

void LvglMainScreen::_applyBgTheme(bool force) {
    if (!_bg_img) return;

    static const char* const k_fs[] = {
        "/bg/main_dark.bin",
        "/bg/main_light.bin",
        "/bg/main_custom.bin",
    };
    static const char* const k_lvgl[] = {
        "L:/bg/main_dark.bin",
        "L:/bg/main_light.bin",
        "L:/bg/main_custom.bin",
    };

    uint8_t slot = static_cast<uint8_t>(yoradio_theme_active_preset());
    if (slot > 2) slot = 0;

    // Fast path: PSRAM buffer loaded for this slot → just check for deletion, no lv_img_set_src.
    // Быстрый путь: буфер PSRAM уже загружен → только проверка удаления файла.
    if (!force && slot == _bg_last_slot && _bg_psram_buf != nullptr) {
        if (!LittleFS.exists(k_fs[slot])) {
            free(_bg_psram_buf);
            _bg_psram_buf = nullptr;
            lv_img_set_src(_bg_img, nullptr);
            lv_obj_add_flag(_bg_img, LV_OBJ_FLAG_HIDDEN);
        }
        return; // No lv_img_set_src → no LVGL invalidation → no LFS read on next frame
    }

    // Slow path: slot changed, forced reload, or no PSRAM buffer yet.
    // Медленный путь: смена слота, принудительная загрузка или буфер ещё не создан.
    if (_bg_psram_buf) {
        free(_bg_psram_buf);
        _bg_psram_buf = nullptr;
    }
    _bg_last_slot = slot;

    if (!LittleFS.exists(k_fs[slot])) {
        lv_img_set_src(_bg_img, nullptr);
        lv_obj_add_flag(_bg_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (bg_load_into_psram(k_fs[slot], _bg_psram_buf, _bg_psram_dsc)) {
        lv_img_set_src(_bg_img, &_bg_psram_dsc);
    } else {
        // Fallback: file-backed path (LFS reads on DspTask — slow, but better than no image).
        // Резервный путь: читаем из LittleFS напрямую (медленно, но без крэша).
        lv_img_set_src(_bg_img, k_lvgl[slot]);
    }
    lv_obj_clear_flag(_bg_img, LV_OBJ_FLAG_HIDDEN);
}

void LvglMainScreen::create() {
    if (_screen) return;

    // --- Build _screen tree (flex order); floating overlays added where needed (volume popup, touch zones). ---
    const uint16_t W = LV_ACTIVE_PROFILE.width;
    const uint16_t H = LV_ACTIVE_PROFILE.height;
    const int32_t pad = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    // Uniform frame padding. Any Y-gap below zone_bottom (e.g. bottom pad strip) is covered by
    // _screen_bottom_carousel_guard — transparent gesture sink, so we do not need pad_bottom=0.
    // Рамка со всех сторон; зазор под контентом перекрывает прозрачный перехватчик жеста — pad_bottom не обнуляем.
    lv_obj_set_style_pad_all(_screen, pad, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 6.1F-b: bottom-most layer — full-screen img; FLOATING so flex ignores it; hidden if no bin on LittleFS.
    // 6.1F-b: нижний слой — полноэкранный img; FLOATING — вне flex; скрыт если нет .bin в LittleFS.
    _bg_img = lv_img_create(_screen);
    if (_bg_img) {
        lv_obj_add_flag(_bg_img, LV_OBJ_FLAG_FLOATING);
        lv_obj_clear_flag(_bg_img, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(_bg_img, W, H);
        lv_obj_align(_bg_img, LV_ALIGN_TOP_LEFT, 0, 0);
        _applyBgTheme(true); // preload active theme .bin into PSRAM / предзагрузка в PSRAM

        _bg_scrim = lv_obj_create(_screen);
        if (_bg_scrim) {
            lv_obj_add_flag(_bg_scrim, LV_OBJ_FLAG_FLOATING);
            lv_obj_clear_flag(_bg_scrim, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(_bg_scrim, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(_bg_scrim, W, H);
            lv_obj_align(_bg_scrim, LV_ALIGN_TOP_LEFT, 0, 0);
            lv_obj_set_style_bg_color(_bg_scrim, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_bg_scrim, k_main_bg_scrim_opa_dark, LV_PART_MAIN);
            lv_obj_set_style_border_width(_bg_scrim, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(_bg_scrim, 0, LV_PART_MAIN);
            main_sync_dark_bg_scrim(_bg_img, _bg_scrim);
        }
    }

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

    // cont_mid: primary text stack only — meta row moved to zone_bottom (6.1E-c).
    // cont_mid: только стек текста — meta перенесена в нижний stack.
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

    // Invisible flex row: height set after layout so heapbar bottom matches screen bottom inset (divider symmetry).
    // Невидимая строка flex: высота задаётся после layout — симметрия divider’ов (LVGL 8 без margin на объекте).
    lv_obj_t* zone_bottom_sym_spacer = lv_obj_create(_screen);
    if (zone_bottom_sym_spacer) {
        lv_obj_set_width(zone_bottom_sym_spacer, LV_PCT(100));
        lv_obj_set_height(zone_bottom_sym_spacer, 0);
        lv_obj_set_flex_grow(zone_bottom_sym_spacer, 0);
        lv_obj_set_style_bg_opa(zone_bottom_sym_spacer, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(zone_bottom_sym_spacer, 0, LV_PART_MAIN);
        lv_obj_clear_flag(zone_bottom_sym_spacer, LV_OBJ_FLAG_SCROLLABLE);
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

        // 6.1E micro-polish: larger glyphs (28px transport on wide) + bigger pressed pill (pad/min/opa).
        // Крупнее глифы и заметнее pressed; иерархия transport > utility сохранена.
        const lv_font_t* f_ctrl_transport =
            (W <= 320u) ? &lv_font_yora_control_icons_26 : &lv_font_yora_control_icons_28;
        // Utility: small glyphs; hit area still large but slightly under transport so pair can sit closer without overlap.
        // Utility: мелкий глиф; зона нажатия большая, чуть меньше транспорта — пара ближе, без пересечения hit-box.
        const lv_font_t* f_ctrl_utility =
            (W <= 320u) ? &lv_font_yora_control_icons_22 : &lv_font_yora_control_icons_24;
        const lv_coord_t pad_tr  = (W <= 320u) ? static_cast<lv_coord_t>(12) : static_cast<lv_coord_t>(16);
        const lv_coord_t pad_ut  = (W <= 320u) ? static_cast<lv_coord_t>(10) : static_cast<lv_coord_t>(14);
        const lv_coord_t min_tr  = (W <= 320u) ? static_cast<lv_coord_t>(54) : static_cast<lv_coord_t>(58);
        const lv_coord_t min_ut  = (W <= 320u) ? static_cast<lv_coord_t>(48) : static_cast<lv_coord_t>(52);

        // Control band: three-part row = spacer_left + transport_group + utility_group.
        // Left spacer mirrors utility width → transport triad is truly screen-centered.
        // Полка: три части = левый спейсер (зеркало utility) + transport + utility.
        lv_obj_t* control_band = lv_obj_create(zone_bottom);
        if (control_band) {
            lv_obj_set_width(control_band, LV_PCT(100));
            lv_obj_set_height(control_band, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(control_band, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                control_band,
                LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_ver(control_band, 8, LV_PART_MAIN);
            lv_obj_set_style_pad_hor(control_band, 8, LV_PART_MAIN);
            lv_obj_set_style_pad_column(control_band, 0, LV_PART_MAIN);
            // Shelf underlay: white at low opacity reads on dark TFT better than near-black at higher opa.
            // Полка: белая с низкой прозрачностью читаемее на тёмном TFT, чем почти чёрная при большей opa.
            lv_obj_set_style_bg_color(control_band, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(control_band, LV_OPA_10, LV_PART_MAIN);
            lv_obj_set_style_radius(control_band, 16, LV_PART_MAIN);
            lv_obj_set_style_border_width(control_band, 0, LV_PART_MAIN);
            lv_obj_clear_flag(control_band, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(control_band, LV_OBJ_FLAG_GESTURE_BUBBLE);

            // Left balancing spacer — fixed width set after utility_group is built.
            // Левый балансирующий спейсер — ширина задаётся после создания utility_group.
            lv_obj_t* spacer_left = lv_obj_create(control_band);
            if (spacer_left) {
                lv_obj_set_height(spacer_left, 1);
                lv_obj_set_style_bg_opa(spacer_left, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(spacer_left, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(spacer_left, 0, LV_PART_MAIN);
                lv_obj_clear_flag(spacer_left, LV_OBJ_FLAG_SCROLLABLE);
            }

            lv_obj_t* transport_group = lv_obj_create(control_band);
            if (transport_group) {
                lv_obj_set_height(transport_group, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(transport_group, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(
                    transport_group,
                    LV_FLEX_ALIGN_CENTER,
                    LV_FLEX_ALIGN_CENTER,
                    LV_FLEX_ALIGN_CENTER);
                lv_obj_set_flex_grow(transport_group, 1);
                lv_obj_set_style_pad_column(transport_group, 11, LV_PART_MAIN);
                // Inset triad from group bounds so rounded pressed pill is not clipped by default overflow mask.
                // Отступ слева/справа — иначе крайние кнопки визуально «срезаются» у краёв группы.
                lv_obj_set_style_pad_hor(transport_group, 4, LV_PART_MAIN);
                lv_obj_set_style_pad_ver(transport_group, 0, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(transport_group, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(transport_group, 0, LV_PART_MAIN);
                lv_obj_clear_flag(transport_group, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(transport_group, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_flag(transport_group, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

                lv_obj_t* btn_prev = main_create_control_icon_btn(
                    transport_group,
                    control_glyph_utf8_player_skip_back(),
                    f_ctrl_transport, pal.text_primary,
                    pad_tr, min_tr,
                    18,
                    k_ctrl_pressed_opa_transport);
                if (btn_prev) {
                    lv_obj_add_event_cb(btn_prev, main_transport_prev_cb, LV_EVENT_CLICKED, nullptr);
                }
                lv_obj_t* btn_play = main_create_control_icon_btn(
                    transport_group,
                    control_glyph_utf8_player_play(),
                    f_ctrl_transport, pal.text_primary,
                    pad_tr, min_tr,
                    18,
                    k_ctrl_pressed_opa_transport);
                if (btn_play) {
                    lv_obj_add_event_cb(btn_play, main_transport_toggle_cb, LV_EVENT_CLICKED, nullptr);
                    // First child is the icon label — update glyph in update() when playback state changes.
                    // Первый ребёнок — label иконки; текст меняем в update() при смене PLAYING/STOPPED.
                    _lbl_transport_play_stop = lv_obj_get_child(btn_play, 0);
                }
                lv_obj_t* btn_next = main_create_control_icon_btn(
                    transport_group,
                    control_glyph_utf8_player_skip_forward(),
                    f_ctrl_transport, pal.text_primary,
                    pad_tr, min_tr,
                    18,
                    k_ctrl_pressed_opa_transport);
                if (btn_next) {
                    lv_obj_add_event_cb(btn_next, main_transport_next_cb, LV_EVENT_CLICKED, nullptr);
                }
            }

            lv_obj_t* utility_group = lv_obj_create(control_band);
            if (utility_group) {
                lv_obj_set_height(utility_group, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(utility_group, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(
                    utility_group,
                    LV_FLEX_ALIGN_END,
                    LV_FLEX_ALIGN_CENTER,
                    LV_FLEX_ALIGN_CENTER);
                // Minimal gap between list/settings — flex pad_column keeps non-zero space (no overlapping hits).
                // Минимальный зазор list/settings — flex оставляет разрыв между hit-box.
                lv_obj_set_style_pad_column(utility_group, 2, LV_PART_MAIN);
                lv_obj_set_style_pad_all(utility_group, 0, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(utility_group, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(utility_group, 0, LV_PART_MAIN);
                lv_obj_clear_flag(utility_group, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(utility_group, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_flag(utility_group, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

                (void)main_create_control_icon_btn(
                    utility_group,
                    control_glyph_utf8_list(),
                    f_ctrl_utility, pal.text_secondary,
                    pad_ut, min_ut,
                    18,
                    k_ctrl_pressed_opa_utility);
                (void)main_create_control_icon_btn(
                    utility_group,
                    control_glyph_utf8_settings(),
                    f_ctrl_utility, pal.text_secondary,
                    pad_ut, min_ut,
                    18,
                    k_ctrl_pressed_opa_utility);
            }

            // Set left spacer width = utility group actual width → true transport centering.
            // Ширина спейсера = ширина utility → транспорт по центру.
            if (spacer_left && utility_group) {
                lv_obj_update_layout(control_band);
                lv_obj_set_width(spacer_left, lv_obj_get_width(utility_group));
            }
        }

        // row_meta_stream: centered compact tech strip (temporary — final content TBD).
        // Центрированная компактная мета-полоска (временно — финальный контент позже).
        lv_obj_t* row_meta_stream = lv_obj_create(zone_bottom);
        if (row_meta_stream) {
            lv_obj_set_width(row_meta_stream, LV_PCT(100));
            lv_obj_set_height(row_meta_stream, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row_meta_stream, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                row_meta_stream,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row_meta_stream, 12, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row_meta_stream, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row_meta_stream, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row_meta_stream, 0, LV_PART_MAIN);
            lv_obj_clear_flag(row_meta_stream, LV_OBJ_FLAG_SCROLLABLE);

            _lbl_station_num = lv_label_create(row_meta_stream);
            if (_lbl_station_num) {
                lv_label_set_text(_lbl_station_num, "#--");
                lv_label_set_long_mode(_lbl_station_num, LV_LABEL_LONG_CLIP);
                main_set_font(_lbl_station_num, reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr));
                lv_obj_set_style_text_color(_lbl_station_num, pal.text_meta, LV_PART_MAIN);
            }

            _lbl_bitrate = lv_label_create(row_meta_stream);
            if (_lbl_bitrate) {
                lv_label_set_text(_lbl_bitrate, "--- kbps");
                lv_label_set_long_mode(_lbl_bitrate, LV_LABEL_LONG_CLIP);
                main_set_font(_lbl_bitrate, reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr));
                lv_obj_set_style_text_color(_lbl_bitrate, pal.text_meta, LV_PART_MAIN);
            }
        }

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
            const lv_font_t* const f_ai = reinterpret_cast<const lv_font_t*>(&lv_font_yora_montserrat_14_cyr);
            main_set_font(_lbl_ai_line, reinterpret_cast<const void*>(f_ai));
            lv_obj_set_style_text_color(_lbl_ai_line, pal.bottom_ai_text, LV_PART_MAIN);
            // One line from font metrics — slot stability only; lower divider position is NOT from this (symmetry pass).
            // Одна строка по метрикам шрифта — слот не схлопывается; нижний divider задаётся симметрией, не 20 px.
            lv_obj_set_style_min_height(_lbl_ai_line, static_cast<lv_coord_t>(lv_font_get_line_height(f_ai)), LV_PART_MAIN);
            // Swipe must not bubble to _screen — carousel listens on page root (5.3).
            // Свайп по строке AI не должен всплывать на экран с обработчиком карусели.
            lv_obj_clear_flag(_lbl_ai_line, LV_OBJ_FLAG_GESTURE_BUBBLE);
        }

        // Vertical divider contract: screen.y2 - heapbar.y2 == status_divider.y1 - screen.y1 (real coords after layout).
        // Push zone_bottom down via zone_bottom_sym_spacer, or shrink spacer_bottom when delta < 0 (no lv_obj margin API).
        // Симметрия по краям экрана: тот же inset; flex только сверху вниз — спейсер/сжатие нижнего flex-spacer.
        if (_screen && status_divider && zone_bottom && _bar_buffer && zone_bottom_sym_spacer) {
            lv_obj_update_layout(_screen);
            lv_area_t scr{};
            lv_area_t top_div{};
            lv_area_t heap{};
            lv_obj_get_coords(_screen, &scr);
            lv_obj_get_coords(status_divider, &top_div);
            lv_obj_get_coords(_bar_buffer, &heap);
            const lv_coord_t inset_top = top_div.y1 - scr.y1;
            const lv_coord_t target_heap_y2 = scr.y2 - inset_top;
            const lv_coord_t delta = target_heap_y2 - heap.y2;
            if (delta > 0) {
                lv_obj_set_height(zone_bottom_sym_spacer, delta);
            } else if (delta < 0 && spacer_bottom) {
                lv_obj_set_flex_grow(spacer_bottom, 0);
                lv_coord_t sh = lv_obj_get_height(spacer_bottom);
                lv_coord_t nh = sh + delta;
                if (nh < 0) nh = 0;
                lv_obj_set_height(spacer_bottom, nh);
            }
            lv_obj_update_layout(_screen);
        }

        // 6.1D-b: Volume touch strip — FLOATING; size/position from real bounds of bar + heapbar (no layout dup constants).
        // Тач-полоса: FLOATING; размер из coords бар+heapbar после lv_obj_update_layout.
        if (zone_bottom && _bar_volume && _bar_buffer) {
            lv_obj_update_layout(zone_bottom);
            lv_area_t bar_coords{};
            lv_area_t buf_coords{};
            lv_area_t zone_coords{};
            lv_obj_get_coords(_bar_volume, &bar_coords);
            lv_obj_get_coords(_bar_buffer, &buf_coords);
            lv_obj_get_coords(zone_bottom, &zone_coords);
            const lv_coord_t top_rel =
                (bar_coords.y1 - kVolTouchPadAbove) - zone_coords.y1;
            const lv_coord_t bottom_rel =
                (buf_coords.y2 + kVolTouchOverlapBelow) - zone_coords.y1;
            const lv_coord_t strip_h = bottom_rel - top_rel;

            if (strip_h > 0) {
                _vol_touch_zone = lv_obj_create(zone_bottom);
                if (_vol_touch_zone) {
                    lv_obj_add_flag(_vol_touch_zone, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_width(_vol_touch_zone, LV_PCT(100));
                    lv_obj_set_height(_vol_touch_zone, strip_h);
                    lv_obj_set_pos(_vol_touch_zone, 0, top_rel);
                    lv_obj_set_style_bg_opa(_vol_touch_zone, LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_border_width(_vol_touch_zone, 0, LV_PART_MAIN);
                    lv_obj_add_flag(_vol_touch_zone, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_clear_flag(_vol_touch_zone, LV_OBJ_FLAG_GESTURE_BUBBLE);
                    lv_obj_set_user_data(_vol_touch_zone, _bar_volume);
                    lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_PRESSED, this);
                    lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_PRESSING, this);
                    lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_RELEASED, this);
                    lv_obj_add_event_cb(_vol_touch_zone, vol_touch_cb, LV_EVENT_PRESS_LOST, this);
                    lv_obj_move_foreground(_vol_touch_zone);
                }
            }

            // Below volume touch strip: full-width guard — no volume logic; blocks gesture → carousel on _screen.
            // Под полосой громкости: только поглощение жеста (geometry from touch_zone + zone_bottom coords).
            lv_obj_update_layout(zone_bottom);
            if (zone_bottom && _vol_touch_zone) {
                lv_area_t tz_coords{};
                lv_area_t zb_coords{};
                lv_obj_get_coords(_vol_touch_zone, &tz_coords);
                lv_obj_get_coords(zone_bottom, &zb_coords);
                const lv_coord_t guard_y_rel = (tz_coords.y2 + 1) - zb_coords.y1;
                const lv_coord_t guard_h     = zb_coords.y2 - tz_coords.y2;
                if (guard_h > 0) {
                    _vol_gesture_guard = lv_obj_create(zone_bottom);
                    if (_vol_gesture_guard) {
                        lv_obj_add_flag(_vol_gesture_guard, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_width(_vol_gesture_guard, LV_PCT(100));
                        lv_obj_set_height(_vol_gesture_guard, guard_h);
                        lv_obj_set_pos(_vol_gesture_guard, 0, guard_y_rel);
                        lv_obj_set_style_bg_opa(_vol_gesture_guard, LV_OPA_TRANSP, LV_PART_MAIN);
                        lv_obj_set_style_border_width(_vol_gesture_guard, 0, LV_PART_MAIN);
                        lv_obj_add_flag(_vol_gesture_guard, LV_OBJ_FLAG_CLICKABLE);
                        lv_obj_clear_flag(_vol_gesture_guard, LV_OBJ_FLAG_GESTURE_BUBBLE);
                        // Ensure guard is above flex children (e.g. AI label) for hit-testing.
                        // Поверх flex-детей — иначе жест уходит в лейбл и всплывает на экран.
                        lv_obj_move_foreground(_vol_gesture_guard);
                    }
                }
            }
        }

        // Bridge any remaining Y-gap below zone_bottom to _screen bottom (absolute coords; includes ex- padding strip).
        // Закрываем остаток по Y до низа экрана — иначе жест снова попадает на _screen.
        if (_screen && zone_bottom) {
            lv_obj_update_layout(_screen);
            lv_area_t scr_a{};
            lv_area_t zb_a{};
            lv_obj_get_coords(_screen, &scr_a);
            lv_obj_get_coords(zone_bottom, &zb_a);
            const lv_coord_t gap_px = scr_a.y2 - zb_a.y2;
            if (gap_px > 0) {
                _screen_bottom_carousel_guard = lv_obj_create(_screen);
                if (_screen_bottom_carousel_guard) {
                    lv_obj_add_flag(_screen_bottom_carousel_guard, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_width(_screen_bottom_carousel_guard, LV_PCT(100));
                    lv_obj_align_to(_screen_bottom_carousel_guard, zone_bottom, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
                    lv_obj_set_height(_screen_bottom_carousel_guard, gap_px);
                    lv_obj_set_style_bg_opa(_screen_bottom_carousel_guard, LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_border_width(_screen_bottom_carousel_guard, 0, LV_PART_MAIN);
                    lv_obj_add_flag(_screen_bottom_carousel_guard, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_clear_flag(_screen_bottom_carousel_guard, LV_OBJ_FLAG_GESTURE_BUBBLE);
                    lv_obj_move_foreground(_screen_bottom_carousel_guard);
                }
            }
        }
    }

    installCarouselGesturesOnPageRoot(_screen);
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

    // Play/stop button: show stop while playing, play while stopped (same semantics as player.toggle()).
    // Кнопка воспроизведения: при PLAYING — иконка stop, при STOPPED — play (как у toggle).
    if (_lbl_transport_play_stop) {
        const char* play_stop_glyph =
            (player.status() == PLAYING) ? control_glyph_utf8_player_stop() : control_glyph_utf8_player_play();
        main_set_text_if_changed(_lbl_transport_play_stop, play_stop_glyph);
    }

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

    // Sync bar from config when volume was changed elsewhere (encoder, WebUI); not during touch drag.
    // Синхронизация бара из config, если громкость менялась не слайдером; во время drag — только lv_bar в callback.
    if (_bar_volume && !s_vol_touch_active) {
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

    // Re-sync file bg + scrim when LittleFS slot changes (e.g. WebUI remove) — single exists() per tick / Слот фона снят → без NoData
    if (_bg_img) {
        _applyBgTheme(false); // fast path if slot unchanged + PSRAM loaded / быстрый путь — нет LFS при устойчивом слоте
        if (_bg_scrim) {
            main_sync_dark_bg_scrim(_bg_img, _bg_scrim);
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
    _lbl_transport_play_stop = nullptr;
    _lbl_volume = nullptr;
    _bar_volume = nullptr;
    _vol_touch_zone = nullptr;
    _vol_gesture_guard = nullptr;
    _screen_bottom_carousel_guard = nullptr;
    _lbl_vol_popup = nullptr;
    _bar_buffer = nullptr;
    _lbl_ai_line = nullptr;
    if (_bg_psram_buf) { free(_bg_psram_buf); _bg_psram_buf = nullptr; }
    _bg_img = nullptr;
    _bg_scrim = nullptr;
    s_vol_touch_active = false; // matches static used by vol_touch_cb / тот же флаг, что в callback
}

lv_obj_t* LvglMainScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
