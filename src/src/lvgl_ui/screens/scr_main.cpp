/*
 * scr_main.cpp — LvglMainScreen: layout + bindings for Main (LVGL).
 *
 * Layout (flex column on _screen, top → bottom):
 *   wgt_status_line → divider → spacer_top (flex 1) → cont_mid → spacer_bottom (flex 1; with left art: top 1 / bottom 5)
 *   → zone_visual (1px) → zone_bottom: control_band (list | transport | settings) → row_meta_stream → col_vol → heapbar → AI;
 *   FLOATING: control_band edge glows — 4-stop HOR grad (lv_conf LV_GRADIENT_MAX_STOPS), under buttons,
 *   vol_touch_zone, vol_gesture_guard,
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
 * Scroll policy (6.1B): SCROLL_CIRCULAR — station name, track, artist, AI line; CLIP — meta stream-info line, weather mini glyphs/temp.
 * DspTask-only lv_*; main_set_text_if_changed reduces redundant layout; status line RSSI throttled in wgt_status_line.
 * Do not route title logic through display.cpp — LVGL path is independent.
 *
 * Object tree (parent→child): scr_main_layout_tree.md — update when create() layout changes.
 * Дерево объектов: scr_main_layout_tree.md — обновлять при изменении разметки create().
 */

#include "scr_main.h"


#include "lvgl.h"
#include "Arduino.h"
#include <cstdio>
#include <cstring>
#include "../fonts/lv_fonts.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../control_glyph_utf8.h"
#include "lvgl_ui.h"
#include "../lv_page_chain.h"
#include "../../core/config.h"
#include "../../core/display.h"
#include "../../core/network.h"
#include "../../core/player.h"
#include "../../core/art_key.h"  // Station Art MVP: artNormalizeKey()
#include <LittleFS.h>

namespace lvgl_ui {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Visual constants / Визуальные константы
// ─────────────────────────────────────────────────────────────────────────────

// Pressed-state opa: transport readable; list/settings calmer (same hit size as transport, softer feedback).
// Транспорт заметнее нажатие; list/settings — тише, размер как у транспорта.
constexpr lv_opa_t k_ctrl_pressed_opa_transport = LV_OPA_20;
constexpr lv_opa_t k_ctrl_pressed_opa_utility = static_cast<lv_opa_t>(36); // ~14% vs ~20% transport
// Stage 6.6R-GB1: Light pressed feedback was washed out (warm-on-ivory @20%). Stronger Light opa; Dark unchanged.
// Этап 6.6R-GB1: на Light нажатие почти невидимо — больше opacity для Light; Dark без изменений.
constexpr lv_opa_t k_ctrl_pressed_opa_transport_light = LV_OPA_60;
constexpr lv_opa_t k_ctrl_pressed_opa_utility_light = LV_OPA_50;

// Stage 6.6R-GB1: control band fill/border opacity is theme-dependent. Light Cloud Ivory shelf at OPA_50
// over a Cloudscape bg nearly vanishes → matte ivory needs higher fill; Dark slate keeps original feel.
// Этап 6.6R-GB1: прозрачность полки зависит от темы. Light ivory при OPA_50 почти исчезает — поднимаем; Dark без изменений.
constexpr lv_opa_t k_cb_bg_opa_dark      = LV_OPA_50;
constexpr lv_opa_t k_cb_bg_opa_light     = LV_OPA_90; // matte ivory shelf, still slightly soft / матовая ивори-полка
constexpr lv_opa_t k_cb_border_opa_dark  = LV_OPA_30;
constexpr lv_opa_t k_cb_border_opa_light = LV_OPA_70; // warm beige edge reads on ivory / тёплая беж-кромка читается

// Meta row: field separator — U+2022 BULLET (in lv_font_yora_montserrat_16_cyr: 0x2022 in .c opts).
// U+00B7 middle dot is NOT in that font subset → would render as wrong glyph on device/sim.
// Разделитель: • из шрифта; средняя точка U+00B7 в подмножестве шрифта нет.
static constexpr char k_meta_field_sep[] = " \xE2\x80\xA2 ";

// Left Art v1: square slot size — local tuning constant (not in display profile yet; Stage 6.1E-visual).
// Размер art_slot — локальная константа настройки, пока не в display profile.
static constexpr lv_coord_t k_art_slot_size = 120; // px, baseline for 480×480
static constexpr lv_coord_t k_art_slot_gap  = 12;  // gap between art_slot and cont_text / зазор между слотом и текстом
// Art slot frame: matches control_band border (same hue); radius clips the image corners.
// Рамка art_slot: тот же цвет, что у control_band border; radius обрезает углы картинки.
static constexpr lv_coord_t k_art_frame_radius = 12; // squircle; tune alongside cont_mid composition
static constexpr lv_coord_t k_art_frame_border_w = 2; // px; same visual weight as control_band (2px)
static constexpr lv_opa_t   k_art_frame_border_opa = LV_OPA_50; // slightly softer than solid / чуть мягче

// Station Art MVP: initial art is determined by artNormalizeKey(stationByNum()) in create().
// Dummy path constants removed — _reloadArtIfNeeded() is the single source of truth.
// Station Art MVP: исходное состояние арта определяется через artNormalizeKey(stationByNum()).
// Dummy-пути удалены — единственный источник истины: _reloadArtIfNeeded().

// cont_mid vertical position: spacers use flex_grow. Equal grow → block centered in free space.
// With left art: more grow below than above → cont_mid sits higher (6.1E-visual tune).
// Вертикаль: равный grow — центр; с артом — больше grow снизу — средний блок выше.
static constexpr int32_t k_spacer_grow_no_art_top    = 1;
static constexpr int32_t k_spacer_grow_no_art_bottom = 1;
static constexpr int32_t k_spacer_grow_with_art_top    = 1;
static constexpr int32_t k_spacer_grow_with_art_bottom  = 5; // 1:5 — выше cont_mid, меньше перекрытие фона (было 1:3)

// cont_text vertical rhythm: pad_row = gap track↔artist; pad_top on track = extra gap name↔track.
// Ритм: pad_row — зазор трек↔артист; pad_top у трека — доп. воздух name↔трек (~4–6 px).
constexpr int32_t k_main_center_pad_row  = 10;
constexpr int32_t k_main_track_pad_top   = 6;

// Stage 8 E22A: Sound Presence Rail height in zone_visual.
// Высота Sound Presence Rail в zone_visual.
constexpr lv_coord_t k_presence_rail_h = 36;

// Control band shelf geometry — keep radius/border and rim-math in sync (glow right stop uses R−border).
// Геометрия скругления полки — radius/border и расчёт правого inset из одних чисел.
constexpr lv_coord_t k_control_band_corner_r  = 22;
constexpr lv_coord_t k_control_band_border_w  = 2;
constexpr lv_coord_t k_glow_inset_left_px     = 3; // rim strip X; sync PASS B + width block / левый inset glow

// Rim glow strip height and right-side extension geometry.
// Высота rim glow и геометрия правого края.
constexpr lv_coord_t kGlowH                       = 2;
constexpr lv_coord_t k_glow_right_extend_px       = 10; // closer to corner / ближе к дуге
constexpr lv_coord_t k_glow_right_extra_width_px  = 2;  // widen strip slightly right / добить правый край
constexpr lv_coord_t k_glow_right_shrink_px       = 3;  // user: strip 3px shorter on the right / короче справа на 3px

// Volume touch strip: semantic margins — geometry from lv_obj_get_coords() after layout.
// Полоса тача громкости: только product-margins; геометрия из реальных bounds после layout.
static constexpr lv_coord_t kVolTouchPadAbove    = 14;
static constexpr lv_coord_t kVolTouchOverlapBelow = 4;

// Throttle: send PR_VOL to audio queue at most once per kVolThrottleMs.
// Bar visual updates instantly; hardware command is rate-limited to avoid queue flood.
// Троттлинг: PR_VOL в очередь аудио не чаще kVolThrottleMs. Визуал обновляется мгновенно.
static constexpr uint32_t kVolThrottleMs = 80;

// Background scrim opacity (F-c). TEMP: scrim effectively off — restore LV_OPA_50 when re-tuning.
// если хотите изменить яркость фона, то меняйте значение на другие, чем выше тем темнее фон.
//constexpr lv_opa_t k_main_bg_scrim_opa_dark = LV_OPA_50;
// TEMP (device testing): scrim effectively off — restore `LV_OPA_50` when re-tuning readability / ВРЕМЕННО: scrim визуально выкл.
constexpr lv_opa_t k_main_bg_scrim_opa_dark = LV_OPA_TRANSP;  // was: LV_OPA_50

// Stage 6.6R-GA: rim glow gradient descriptors. LVGL stores a pointer to lv_grad_dsc_t in the
// style, so the descriptor must outlive create(). Static (Main is single-instance) lets both
// create() and liveReapplyTheme() reach the same descriptors → fix stale glow on theme switch.
// Этап 6.6R-GA: дескрипторы градиента blic'а — static (Main один), чтобы reapply мог
// обновить стопы при смене темы. [0]=top, [1]=bottom.
static lv_grad_dsc_t s_cb_rim_glow_grad[2];

// Volume interaction state: static because vol_touch_cb + vol_flush_pending + update() share them.
// Состояние взаимодействия с громкостью: static — общее для callback, flush и update().
// While user drags slider, skip syncing lv_bar from config in update() — config lags behind PR_VOL.
// Пока трогаем слайдер — не перезаписывать бар из config (иначе гонка с асинхронным PR_VOL).
static bool     s_vol_touch_active  = false;
static uint32_t s_vol_last_send_ms  = 0;
static int32_t  s_vol_pending       = -1;   // -1 = nothing pending / нет отложенного значения

// ─────────────────────────────────────────────────────────────────────────────
// Helpers / Вспомогательные функции
// ─────────────────────────────────────────────────────────────────────────────

// In-place split for a *mutable* copy of station.title (destroys delimiter in buffer).
// Разбор копии title: после вызова первая часть — NUL-terminated префикс, возврат — хвост после sep.
char* split_inplace_at(char* str, const char* sep) {
    if (!str || !sep) return nullptr;
    char* p = strstr(str, sep);
    if (!p) return nullptr;
    *p = '\0';
    return p + strlen(sep);
}

// Append s to buf (NUL-terminated); cap = total buffer size. Portable (no strlcat).
static void meta_append_cstr(char* buf, size_t cap, const char* s) {
    if (!buf || cap == 0u || !s || s[0] == '\0') {
        return;
    }
    const size_t cur = strlen(buf);
    const size_t add = strlen(s);
    if (cur + add + 1u > cap) {
        return;
    }
    memcpy(buf + cur, s, add + 1u);
}

// Compact kHz token only: "48" or "44.1" (no suffix; used in "44.1/16" block).
static void format_sample_rate_khz_compact(uint32_t hz, char* out, size_t out_sz) {
    if (!out || out_sz == 0u) {
        return;
    }
    out[0] = '\0';
    if (hz == 0u) {
        return;
    }
    if ((hz % 1000u) == 0u) {
        snprintf(out, out_sz, "%lu", static_cast<unsigned long>(hz / 1000u));
    } else {
        const unsigned long k     = static_cast<unsigned long>(hz / 1000u);
        const unsigned long tenth = static_cast<unsigned long>((hz % 1000u) / 100u);
        snprintf(out, out_sz, "%lu.%lu", k, tenth);
    }
}

// One composed stream-info line: #N · [SR/bits] · [Nk] · [CODEC]; omit unknown (SR/bits only if both known).
static void main_compose_stream_info_line(char* buf, size_t cap) {
    if (!buf || cap == 0u) {
        return;
    }
    snprintf(buf, cap, "#%u", static_cast<unsigned>(config.lastStation()));

    char piece[40];
    char sr_compact[16];
    if (config.station.stream_sample_rate_hz > 0u && config.station.stream_bits_per_sample > 0u) {
        format_sample_rate_khz_compact(config.station.stream_sample_rate_hz, sr_compact, sizeof(sr_compact));
        snprintf(
            piece,
            sizeof(piece),
            "%s/%u",
            sr_compact,
            static_cast<unsigned>(config.station.stream_bits_per_sample));
        meta_append_cstr(buf, cap, k_meta_field_sep);
        meta_append_cstr(buf, cap, piece);
    }
    if (config.station.bitrate > 0u) {
        snprintf(piece, sizeof(piece), "%uk", static_cast<unsigned>(config.station.bitrate));
        meta_append_cstr(buf, cap, k_meta_field_sep);
        meta_append_cstr(buf, cap, piece);
    }
    const char* codec = player.getCodecname();
    if (codec != nullptr && strcmp(codec, "unknown") != 0) {
        meta_append_cstr(buf, cap, k_meta_field_sep);
        meta_append_cstr(buf, cap, codec);
    }
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

// Fill rim glow stops from the active palette (colors only; frac/dir set once in create()).
// edge = shelf body (main_chrome_bg); peak = top/bottom glow token; plateau back to edge.
// Заполнить цвета стопов из активной палитры (только цвета; геометрия задаётся в create()).
static void main_fill_rim_glow_grad(lv_grad_dsc_t* gd, bool top, const YoRadioPalette& pal) {
    if (!gd) return;
    gd->stops[0].color = pal.main_chrome_bg;
    gd->stops[1].color = top ? pal.main_chrome_glow_top : pal.main_chrome_glow_bottom;
    gd->stops[2].color = pal.main_chrome_bg;
    gd->stops[3].color = pal.main_chrome_bg;
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
    lv_opa_t pressed_bg_opa = LV_OPA_20,
    // Stage 6.6R-GA: pressed bg is now a palette token (pal.main_chrome_pressed_bg); was hardcoded white.
    // Этап 6.6R-GA: цвет pressed-фона — токен палитры (раньше white).
    lv_color_t pressed_bg_color = lv_color_white()) {
    lv_obj_t* btn = lv_btn_create(parent);
    if (!btn) return nullptr;
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, pressed_bg_color, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
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

// Stage 6.6R-B1/GA: icon fg + pressed bg are set in main_create_control_icon_btn(); reapply on theme switch.
// Этап 6.6R-B1/GA: цвет иконки и pressed-фон задаются при create(); обновляем при liveReapplyTheme().
static void main_reapply_control_icon_btn(lv_obj_t* btn, lv_color_t fg, lv_color_t pressed_bg,
                                          lv_opa_t pressed_opa) {
    if (!btn) return;
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl, fg, LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(btn, pressed_bg, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
    // 6.6R-GB1: pressed opa is theme-aware → reapply on switch so Light feedback stays visible.
    lv_obj_set_style_bg_opa(btn, pressed_opa, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
}

// ─────────────────────────────────────────────────────────────────────────────
// Volume helpers / Вспомогательные функции громкости
// ─────────────────────────────────────────────────────────────────────────────

// Calculate volume (0-254) from absolute touch X and bar position (absolute via parent chain).
// touch_x must be absolute screen X (from lv_indev_get_point).
// Вычисление громкости: абсолютный X касания + абсолютная позиция бара (цепочка родителей).
// touch_x — абсолютный X экрана (из lv_indev_get_point).
static int32_t vol_from_touch_x(lv_obj_t* bar, lv_coord_t touch_x) {
    if (!bar) return 0;
    // Walk parent chain to get absolute bar X (bar lives inside col_vol → zone_bottom → _screen).
    // Абсолютный X — сумма смещений по цепочке родителей.
    lv_coord_t bar_abs_x = 0;
    lv_obj_t* obj = bar;
    while (obj) {
        bar_abs_x += lv_obj_get_x(obj);
        obj = lv_obj_get_parent(obj);
    }
    const lv_coord_t bar_w     = lv_obj_get_width(bar);
    const lv_coord_t pad_left  = lv_obj_get_style_pad_left(bar, LV_PART_MAIN);
    const lv_coord_t pad_right = lv_obj_get_style_pad_right(bar, LV_PART_MAIN);
    const lv_coord_t content_x = bar_abs_x + pad_left;
    const lv_coord_t content_w = bar_w - pad_left - pad_right;
    if (content_w <= 0) return 0;
    int32_t rel_x = touch_x - content_x;
    if (rel_x < 0) rel_x = 0;
    if (rel_x > content_w) rel_x = content_w;
    return (rel_x * 254) / content_w;
}

// Flush any pending volume to audio queue (call on RELEASED or from periodic update).
// Отправить отложенное значение громкости (при отпускании или из периодического обновления).
static void vol_flush_pending() {
    if (s_vol_pending >= 0) {
        player.setVol(static_cast<uint8_t>(s_vol_pending));
        s_vol_pending = -1;
        s_vol_last_send_ms = millis();
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
    // X position of fill end (guard matches vol_from_touch_x — avoid div by zero/negative).
    // Позиция конца fill; та же защита, что в vol_from_touch_x — без деления на 0/отриц.
    lv_coord_t fill_x = bar_abs_x + pad_left;
    if (content_w > 0) {
        fill_x += (content_w * vol) / 254;
    }
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

// ─────────────────────────────────────────────────────────────────────────────
// Background helpers / Вспомогательные функции фона
// ─────────────────────────────────────────────────────────────────────────────

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

// ── Main background PSRAM cache (W2G-A) ──────────────────────────────────────────────────────────
//
// Survives Main LVGL object lifecycle (W2F auto-delete). The 480×480 RGB565 background buffer
// (~460 KB) is loaded from LittleFS once and stays in PSRAM until the theme slot changes, the
// user re-uploads a background via WebUI, or memory pressure requires clearing.
//
// Ownership contract:
//   - s_bg_cache owns the ps_malloc'd buffer; Main page only borrows the pointer.
//   - Main destroy/releaseAfterAutoDelete MUST NOT free _bg_psram_buf (it belongs to the cache).
//   - The cache is freed by main_bg_cache_invalidate() or on a path change (theme slot switch).
//
// Контракт владения:
//   - s_bg_cache владеет буфером ps_malloc; страница Main только заимствует указатель.
//   - destroy/releaseAfterAutoDelete НЕ должны освобождать _bg_psram_buf (владелец — кэш).
//   - Кэш освобождается через main_bg_cache_invalidate() или при смене слота темы.

struct MainBgCache {
    char         path[64] = {};      // LittleFS path of cached background / путь к закэшированному фону
    uint8_t*     data     = nullptr; // ps_malloc'd pixel buffer (owned) / пиксельный буфер ps_malloc (владелец)
    lv_img_dsc_t dsc      = {};      // descriptor ready for lv_img_set_src() / дескриптор для LVGL
    bool valid() const { return data != nullptr && path[0] != '\0'; }
};
static MainBgCache s_bg_cache;  // zero-initialised at load time / нулевая инициализация при загрузке

// Free the cached buffer and reset all fields.
// Освободить буфер кэша и сбросить все поля.
static void main_bg_cache_invalidate() {
    if (s_bg_cache.data) {
        Serial.printf("[BG_CACHE] clear %s\n", s_bg_cache.path);
        free(s_bg_cache.data);
        s_bg_cache.data = nullptr;
    }
    s_bg_cache.path[0] = '\0';
    s_bg_cache.dsc = {};
}

// Look up or load fs_path. Returns true if data is available (hit or freshly loaded).
// out_dsc receives a descriptor whose .data points INTO the cache buffer — do NOT free it.
// Поиск или загрузка. Возвращает true если данные доступны (попадание или свежая загрузка).
// out_dsc.data указывает в кэш-буфер — не освобождать.
static bool main_bg_cache_get(const char* fs_path, lv_img_dsc_t& out_dsc) {
    if (!fs_path || fs_path[0] == '\0') return false;

    // Cache hit — same path, buffer present.
    // Попадание — тот же путь, буфер есть.
    if (s_bg_cache.valid() &&
        strncmp(s_bg_cache.path, fs_path, sizeof(s_bg_cache.path) - 1) == 0) {
        Serial.printf("[BG_CACHE] hit %s (%u bytes)\n", fs_path,
                      (unsigned)s_bg_cache.dsc.data_size);
        out_dsc = s_bg_cache.dsc;
        return true;
    }

    // Cache miss or path change — release old buffer and load fresh.
    // Промах или смена пути — освобождаем старый буфер, загружаем новый.
    if (s_bg_cache.data) {
        Serial.printf("[BG_CACHE] reload old=%s new=%s\n", s_bg_cache.path, fs_path);
    } else {
        Serial.printf("[BG_CACHE] miss %s\n", fs_path);
    }
    main_bg_cache_invalidate();

    uint8_t* buf = nullptr;
    lv_img_dsc_t dsc = {};
    if (!bg_load_into_psram(fs_path, buf, dsc)) {
        return false;
    }

    s_bg_cache.data = buf;
    s_bg_cache.dsc  = dsc;
    strlcpy(s_bg_cache.path, fs_path, sizeof(s_bg_cache.path));
    out_dsc = dsc;
    return true;
}

// F-c: black scrim — only when file bg is shown AND Dark preset (readability). / Scrim только Dark + есть фон.
// LVGL v8 named opa (lv_opa.h): LV_OPA_TRANSP=0; LV_OPA_10…LV_OPA_90 (~10%…90% of cover); LV_OPA_100/LV_OPA_COVER=255.
// Именованные ступени: 0, 10…90, 100/COVER; можно любое lv_opa_t 0–255.
// 50% named step — LV_OPA_50 (~127/255); not linear "half brightness" of the bitmap. / не «половина яркости» пикселей.
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

// ─────────────────────────────────────────────────────────────────────────────
// Event callbacks / Обработчики событий LVGL
// ─────────────────────────────────────────────────────────────────────────────

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

// Main utility: List → Station page, Settings → Settings slot (LvglStationPage / LvglStubPage in PageChain).
// Утилиты Main: список → Station, шестерёнка → слот Settings (карусель Stage 4.6 freeze).
static void main_utility_station_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const displayMode_e m = display.mode();
    if (m != PLAYER && m != VOL) return;
    goToCarouselPage(PageChain::STATION_INDEX);
    notifyPageChainActivity();
}

static void main_utility_settings_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const displayMode_e m = display.mode();
    if (m != PLAYER && m != VOL) return;
    goToCarouselPage(PageChain::SETTINGS_INDEX);
    notifyPageChainActivity();
}

// 6.1D-b: Volume touch event callback — handles PRESSED, PRESSING, RELEASED.
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
        // X normalization now lives in lv_touch_read_cb — no per-widget workaround needed.
        // Нормализация X теперь в lv_touch_read_cb — локальный workaround больше не нужен.
        const int32_t new_vol = vol_from_touch_x(bar, p.x);

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

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout builders / Строители секций Main screen
// ─────────────────────────────────────────────────────────────────────────────

// create_status_line: wgt_status_line + thin divider.
// Returns status_divider needed by create_bottom_zone for symmetry calc; nullptr = fatal (caller cleans up).
// Возвращает divider (нужен для симметрии); nullptr — фатальная ошибка (caller удаляет _screen).
lv_obj_t* LvglMainScreen::create_status_line(LvglMainScreen& self, const YoRadioPalette& pal) {
    if (!wgt_status_line::create(self._screen, self._status_line)) {
        return nullptr;
    }

    // Thin separator: status strip vs main content (token divider — Bible §4.1).
    // Тонкий разделитель статуса и контента.
    lv_obj_t* status_divider = lv_obj_create(self._screen);
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
    return status_divider;
}

// create_mid_block: spacer_top + cont_mid (art_slot + text labels) + spacer_bottom + art grow rebalance.
// Центральный блок: спейсеры + ряд арт/текст + начальная инициализация арта.
void LvglMainScreen::create_mid_block(LvglMainScreen& self, const YoRadioPalette& pal) {
    // Flex spacers above/below cont_mid — default 1:1; with left art rebalanced below.
    // Спейсеры сверху/снизу — по умолчанию 1:1; с left art — перебаланс после cont_mid.
    self._spacer_top = lv_obj_create(self._screen);
    if (self._spacer_top) {
        lv_obj_set_width(self._spacer_top, LV_PCT(100));
        lv_obj_set_flex_grow(self._spacer_top, k_spacer_grow_no_art_top);
        lv_obj_set_style_bg_opa(self._spacer_top, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(self._spacer_top, 0, LV_PART_MAIN);
        lv_obj_clear_flag(self._spacer_top, LV_OBJ_FLAG_SCROLLABLE);
    }

    // cont_mid: primary composition — horizontal row inside (6.1E-visual).
    // Outer COLUMN wrapper kept for future vertical siblings (e.g. ticker below text row).
    // cont_mid: горизонтальный ряд внутри (6.1E-visual); внешний COLUMN сохранён для будущих соседей.
    lv_obj_t* cont_mid = lv_obj_create(self._screen);
    self._cont_mid = cont_mid; // stored for runtime alignment update in _reloadArtIfNeeded()
    if (cont_mid) {
        lv_obj_set_width(cont_mid, LV_PCT(100));
        lv_obj_set_height(cont_mid, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cont_mid, LV_FLEX_FLOW_COLUMN);
        // cont_mid flex_align set after art_slot visibility (center without cover, start with cover).
        // Выравнивание обёртки — после решения Mode A/B (без обложки центр, с обложкой — start).
        lv_obj_set_style_pad_all(cont_mid, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(cont_mid, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cont_mid, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cont_mid, 0, LV_PART_MAIN);
        lv_obj_clear_flag(cont_mid, LV_OBJ_FLAG_SCROLLABLE);

        // 6.1E-visual: horizontal row — art_slot (left, dynamic-collapse) + cont_text (right, flex_grow=1).
        // Mode A (no asset): art_slot HIDDEN → LVGL v8 flex skips it → cont_text fills full row width (no reserved hole).
        // Mode B (asset present): art_slot visible (k_art_slot_size sq) → cont_text takes remaining width.
        // Горизонтальный ряд: art_slot (collapse) + cont_text (flex_grow). Mode A: арт скрыт, текст на всю ширину.
        lv_obj_t* cont_mid_row = lv_obj_create(cont_mid);
        if (cont_mid_row) {
            lv_obj_set_width(cont_mid_row, LV_PCT(100));
            lv_obj_set_height(cont_mid_row, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(cont_mid_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                cont_mid_row,
                LV_FLEX_ALIGN_START,   // main: left-to-right
                LV_FLEX_ALIGN_CENTER,  // cross: vertically center items
                LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(cont_mid_row, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_column(cont_mid_row, k_art_slot_gap, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cont_mid_row, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(cont_mid_row, 0, LV_PART_MAIN);
            lv_obj_clear_flag(cont_mid_row, LV_OBJ_FLAG_SCROLLABLE);

            // art_slot: fixed square; HIDDEN by default (Mode A — no asset, no reserved hole).
            // Shown when station art file exists (Mode B); hidden by default (Mode A).
            // art_slot: фиксированный квадрат; по умолчанию скрыт (Mode A — нет заглушки).
            self._art_slot = lv_obj_create(cont_mid_row);
            if (self._art_slot) {
                // Default lv_obj + theme "card" can add border/shadow/pad — looks like a frame; rounded parent + img can leak light pixels on corners in RGB565.
                // Сбрасываем стили: без рамки темы, без clip по radius (иначе по углам «точки»/просветы).
                lv_obj_remove_style_all(self._art_slot);
                lv_obj_set_size(self._art_slot, k_art_slot_size, k_art_slot_size);
                lv_obj_set_style_shadow_width(self._art_slot, 0, LV_PART_MAIN);
                lv_obj_set_style_outline_width(self._art_slot, 0, LV_PART_MAIN);
                // Frame: reuses control_band border token (6.6R-GA: pal.main_chrome_border).
                // radius clips content — image corners follow the same arc.
                // Рамка: тот же токен, что у полки; radius обрезает углы картинки изнутри.
                {
                    lv_obj_set_style_border_color(self._art_slot, pal.main_chrome_border, LV_PART_MAIN);
                    lv_obj_set_style_border_opa(self._art_slot, k_art_frame_border_opa, LV_PART_MAIN);
                    lv_obj_set_style_border_width(self._art_slot, k_art_frame_border_w, LV_PART_MAIN);
                }
                lv_obj_set_style_radius(self._art_slot, k_art_frame_radius, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(self._art_slot, LV_OPA_TRANSP, LV_PART_MAIN);
                // Pad = border width → image sits inside the border ring.
                // Паддинг = ширина рамки → картинка внутри кольца рамки.
                lv_obj_set_style_pad_all(self._art_slot, k_art_frame_border_w, LV_PART_MAIN);
                // DO NOT use clip_corner on art_slot: in LVGL 8 it masks the object's own border arcs
                // → corner pixels of the border itself get clipped → corners look "undrawn".
                // Instead: set inner radius on art_img so image corners follow the border arc from inside.
                // НЕ используем clip_corner: он маскирует дуги самой рамки → углы выглядят «непрорисованными».
                // Вместо: inner radius на art_img = slot_radius − border_w → картинка повторяет дугу изнутри.
                lv_obj_clear_flag(self._art_slot, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(self._art_slot, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(self._art_slot, LV_OBJ_FLAG_HIDDEN); // default: Mode A

                self._art_img = lv_img_create(self._art_slot);
                if (self._art_img) {
                    lv_obj_remove_style_all(self._art_img);
                    // Explicit pixel size = content area; LV_SIZE_CONTENT would be 120px (image) vs 116px (area)
                    // → overflows 2px top/bottom, covers top/bottom border. Fixed: size = slot − 2×border.
                    // LV_SIZE_CONTENT = 120px > content area 116px → верх/низ рамки перекрыты. Фикс: явный размер.
                    const lv_coord_t art_img_size = k_art_slot_size - 2 * k_art_frame_border_w;
                    lv_obj_set_size(self._art_img, art_img_size, art_img_size);
                    lv_obj_set_style_bg_opa(self._art_img, LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_border_width(self._art_img, 0, LV_PART_MAIN);
                    lv_obj_set_style_pad_all(self._art_img, 0, LV_PART_MAIN);
                    // Inner radius = slot_radius − border_width → image corners follow the border arc.
                    // Внутренний radius = slot_radius − border_w → углы картинки совпадают с дугой рамки.
                    const lv_coord_t art_img_radius = k_art_frame_radius - k_art_frame_border_w;
                    lv_obj_set_style_radius(self._art_img, art_img_radius, LV_PART_MAIN);
                    lv_obj_set_style_clip_corner(self._art_img, true, LV_PART_MAIN); // clip img pixels to inner rounded rect
                    lv_obj_set_style_shadow_width(self._art_img, 0, LV_PART_MAIN);
                    lv_obj_align(self._art_img, LV_ALIGN_CENTER, 0, 0);
                    lv_obj_clear_flag(self._art_img, LV_OBJ_FLAG_CLICKABLE);
                }

                // Station Art MVP: initial art state from current station key — same contract
                // as _reloadArtIfNeeded(). Sets _art_current_key/_art_last_station_num so the
                // first update() call skips duplicate I/O when station has not changed.
                // Key source: stationByNum(lastStation()) — never config.station.name.
                // Station Art MVP: исходное состояние арта — тот же контракт что у
                // _reloadArtIfNeeded(). Инициализируем кэш, чтобы первый update() не делал
                // лишний I/O. Источник: stationByNum() — не config.station.name.
                if (self._art_img) {
                    const char* init_pl = config.stationByNum(config.lastStation());
                    char init_key[68]  = {};
                    char init_fs[84]   = {};
                    artNormalizeKey(init_pl, init_key, sizeof(init_key));
                    if (init_key[0] != '\0') {
                        snprintf(init_fs, sizeof(init_fs), "/logo/%s.bin", init_key);
                    }
                    if (init_key[0] != '\0' && LittleFS.exists(init_fs)) {
                        char init_lvgl[88] = {};
                        snprintf(init_lvgl, sizeof(init_lvgl), "L:/logo/%s.bin", init_key);
                        lv_img_set_src(self._art_img, init_lvgl);
                        lv_obj_clear_flag(self._art_slot, LV_OBJ_FLAG_HIDDEN); // Mode B
                        strlcpy(self._art_current_key, init_key, sizeof(self._art_current_key));
                        self._art_last_station_num = config.lastStation();
                        Serial.printf("[ART] create: key='%s'\n", init_key);
                    }
                    // else: _art_slot stays HIDDEN (Mode A); _art_last_station_num stays 0xFFFF
                    // → first update() will call _reloadArtIfNeeded() and confirm no art.
                }
            }

            const bool has_cover =
                (self._art_slot != nullptr && !lv_obj_has_flag(self._art_slot, LV_OBJ_FLAG_HIDDEN));
            if (cont_mid) {
                if (has_cover) {
                    lv_obj_set_flex_align(
                        cont_mid,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
                } else {
                    lv_obj_set_flex_align(
                        cont_mid,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                }
            }

            // cont_text: flex_grow=1 fills remaining row width.
            // Mode A: art_slot hidden → full row width (same visual as before).
            // Mode B: art_slot visible → width = row − art_slot − gap.
            // cont_text: flex_grow=1 — занимает оставшуюся ширину ряда.
            lv_obj_t* cont_text = lv_obj_create(cont_mid_row);
            self._cont_text = cont_text; // stored for runtime alignment update in _reloadArtIfNeeded()
            if (cont_text) {
                lv_obj_set_flex_grow(cont_text, 1);
                lv_obj_set_height(cont_text, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(cont_text, LV_FLEX_FLOW_COLUMN);
                // Label text_align + cont_text flex: set after children (uses has_cover) / После лейблов — центр или лево от обложки.
                lv_obj_set_style_pad_all(cont_text, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_row(cont_text, k_main_center_pad_row, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(cont_text, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(cont_text, 0, LV_PART_MAIN);
                lv_obj_clear_flag(cont_text, LV_OBJ_FLAG_SCROLLABLE);

                self._lbl_station_name = lv_label_create(cont_text);
                if (self._lbl_station_name) {
                    lv_label_set_text(self._lbl_station_name, "---");
                    lv_label_set_long_mode(self._lbl_station_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
                    lv_obj_set_width(self._lbl_station_name, LV_PCT(100));
                    // Main font experiment: 32 px anchor (direct font ref, not profile slot).
                    // Эксперимент: якорь 32 px.
                    main_set_font(self._lbl_station_name, reinterpret_cast<const void*>(&lv_font_yora_montserrat_32_cyr));
                    lv_obj_set_style_text_color(self._lbl_station_name, pal.station_name_text, LV_PART_MAIN);
                }

                self._lbl_track = lv_label_create(cont_text);
                if (self._lbl_track) {
                    lv_label_set_text(self._lbl_track, " ");
                    lv_label_set_long_mode(self._lbl_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
                    lv_obj_set_width(self._lbl_track, LV_PCT(100));
                    // Second tier: 22 px.
                    // Второй уровень: 22 px.
                    main_set_font(self._lbl_track, reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr));
                    lv_obj_set_style_text_color(self._lbl_track, pal.track_text, LV_PART_MAIN);
                    lv_obj_set_style_pad_top(self._lbl_track, k_main_track_pad_top, LV_PART_MAIN);
                    lv_obj_add_flag(self._lbl_track, LV_OBJ_FLAG_HIDDEN);
                }

                self._lbl_artist = lv_label_create(cont_text);
                if (self._lbl_artist) {
                    lv_label_set_text(self._lbl_artist, " ");
                    lv_label_set_long_mode(self._lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
                    lv_obj_set_width(self._lbl_artist, LV_PCT(100));
                    // Third tier: 18 px (experiment ladder).
                    // Третий уровень: 18 px.
                    main_set_font(self._lbl_artist, reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr));
                    lv_obj_set_style_text_color(self._lbl_artist, pal.artist_text, LV_PART_MAIN);
                    lv_obj_add_flag(self._lbl_artist, LV_OBJ_FLAG_HIDDEN);
                }

                if (has_cover) {
                    lv_obj_set_flex_align(
                        cont_text,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
                    if (self._lbl_station_name) {
                        lv_obj_set_style_text_align(self._lbl_station_name, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                    }
                    if (self._lbl_track) {
                        lv_obj_set_style_text_align(self._lbl_track, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                    }
                    if (self._lbl_artist) {
                        lv_obj_set_style_text_align(self._lbl_artist, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                    }
                } else {
                    lv_obj_set_flex_align(
                        cont_text,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                    if (self._lbl_station_name) {
                        lv_obj_set_style_text_align(self._lbl_station_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                    }
                    if (self._lbl_track) {
                        lv_obj_set_style_text_align(self._lbl_track, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                    }
                    if (self._lbl_artist) {
                        lv_obj_set_style_text_align(self._lbl_artist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                    }
                }
            }
        }
    }

    self._spacer_bottom = lv_obj_create(self._screen);
    if (self._spacer_bottom) {
        lv_obj_set_width(self._spacer_bottom, LV_PCT(100));
        lv_obj_set_flex_grow(self._spacer_bottom, k_spacer_grow_no_art_bottom);
        lv_obj_set_style_bg_opa(self._spacer_bottom, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(self._spacer_bottom, 0, LV_PART_MAIN);
        lv_obj_clear_flag(self._spacer_bottom, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Left art visible: more flex below cont_mid than above → main block sits higher (still between spacers, not an overlay).
    // С артом: больше grow снизу — средний композиционный блок выше.
    if (self._art_slot && !lv_obj_has_flag(self._art_slot, LV_OBJ_FLAG_HIDDEN) && self._spacer_top && self._spacer_bottom) {
        lv_obj_set_flex_grow(self._spacer_top, k_spacer_grow_with_art_top);
        lv_obj_set_flex_grow(self._spacer_bottom, k_spacer_grow_with_art_bottom);
    }
}

// create_visual_rail: zone_visual (fixed-height, transparent) + Sound Presence Rail widget.
// Sound Presence Rail: зона zone_visual + виджет wgt_presence_rail.
void LvglMainScreen::create_visual_rail(LvglMainScreen& self, const YoRadioPalette& /*pal*/) {
    // Stage 8 E22A: this formerly 1px reserved slot now hosts the Sound Presence Rail.
    // Fixed height; transparent/non-interactive; rail lines live inside (see wgt_presence_rail).
    // Этап 8 E22A: бывший резерв 1px теперь хост Sound Presence Rail (фикс. высота, прозрачный, не интерактивный).
    lv_obj_t* zone_visual = lv_obj_create(self._screen);
    if (zone_visual) {
        lv_obj_set_width(zone_visual, LV_PCT(100));
        lv_obj_set_height(zone_visual, k_presence_rail_h);
        lv_obj_set_flex_grow(zone_visual, 0);
        lv_obj_set_style_bg_opa(zone_visual, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(zone_visual, 0, LV_PART_MAIN);
        // Zero the theme "card" pad so the rail uses the full zone height/width / убрать pad темы.
        lv_obj_set_style_pad_all(zone_visual, 0, LV_PART_MAIN);
        lv_obj_clear_flag(zone_visual, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(zone_visual, LV_OBJ_FLAG_CLICKABLE);
        // Rail created here: after background/center, before bottom/control zone (correct Z + Y).
        // Rail создаётся здесь: после фона/центра, до нижней зоны/полки (корректный Z и Y).
        wgt_presence_rail::create(zone_visual, self._presence_rail);
    }
}

// create_bottom_zone: symmetry spacer + zone_bottom (control_band, volume, meta, AI) + touch strips + guards.
// status_divider: needed for symmetry calculation (inset_top = top_div.y1 - scr.y1).
// Нижняя зона: спейсер симметрии + zone_bottom + полка управления + громкость + meta + AI + touch-зоны.
// status_divider: нужен для расчёта симметрии (inset_top = top_div.y1 - scr.y1).
void LvglMainScreen::create_bottom_zone(LvglMainScreen& self, const YoRadioPalette& pal, lv_obj_t* status_divider) {
    // Width used for font/pad selection in bottom zone.
    const uint16_t W = LV_ACTIVE_PROFILE.width;

    // Invisible flex row: height set after layout so heapbar bottom matches screen bottom inset (divider symmetry).
    // Невидимая строка flex: высота задаётся после layout — симметрия divider'ов (LVGL 8 без margin на объекте).
    lv_obj_t* zone_bottom_sym_spacer = lv_obj_create(self._screen);
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
    lv_obj_t* zone_bottom = lv_obj_create(self._screen);
    if (zone_bottom) {
        lv_obj_set_width(zone_bottom, LV_PCT(100));
        lv_obj_set_height(zone_bottom, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(zone_bottom, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(zone_bottom, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(zone_bottom, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(zone_bottom, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(zone_bottom, 0, LV_PART_MAIN);
        lv_obj_clear_flag(zone_bottom, LV_OBJ_FLAG_SCROLLABLE);
        // Grandchildren (control_band rim glow) can paint slightly outside row bounds; default clip would eat 1–2px.
        // Внук (rim glow) может выходить за bounds ряда — без этого снизу «пропадает» часть линии.
        lv_obj_add_flag(zone_bottom, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        // 6.1E: list/settings = transport font + pad/min (size); color/pressed stay utility (secondary + calmer opa).
        // Размер list/settings как у транспорта; цвет и pressed — как раньше у utility.
        const lv_font_t* f_ctrl_transport =
            (W <= 320u) ? &lv_font_yora_control_icons_26 : &lv_font_yora_control_icons_28;
        const lv_coord_t pad_tr = (W <= 320u) ? static_cast<lv_coord_t>(12) : static_cast<lv_coord_t>(16);
        const lv_coord_t min_tr = (W <= 320u) ? static_cast<lv_coord_t>(54) : static_cast<lv_coord_t>(58);

        // 6.6R-GB1: theme-aware pressed opacity (Light needs stronger feedback on ivory shelf).
        // 6.6R-GB1: прозрачность нажатия зависит от темы (Light заметнее на ивори-полке).
        const bool cb_light = (yoradio_theme_active_preset() == ThemePreset::Light);
        const lv_opa_t pressed_opa_transport =
            cb_light ? k_ctrl_pressed_opa_transport_light : k_ctrl_pressed_opa_transport;
        const lv_opa_t pressed_opa_utility =
            cb_light ? k_ctrl_pressed_opa_utility_light : k_ctrl_pressed_opa_utility;

        // Control band: list (left inset = pad_hor) | transport (grow, centered) | settings (right inset = pad_hor).
        // Symmetric slot widths keep the triad centered; list/settings no longer share one utility cluster.
        // Полка: list слева | транспорт по центру | settings справа; равные ширины боковых слотов — центр триады.
        self._control_band = lv_obj_create(zone_bottom);
        lv_obj_t* control_band = self._control_band;
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
            // PASS A + salvage S1: shelf — same radius (22); denser matte slab, softer border read.
            // PASS A + S1: тот же radius; плотнее матовая панель, рамка чуть мягче; flex/pad без изменений.
            // Theme: no runtime preset switch yet — product tuning is Dark-first; Light branch kept for future / Переключения темы пока нет, опора на Dark; ветка Light на будущее.
            {
                // Stage 6.6R-GA: shelf body/border are palette tokens; 6.6R-GB1: opacity is theme-aware (local).
                // Этап 6.6R-GA: тело/рамка полки — токены; GB1: прозрачность зависит от темы (локально).
                lv_obj_set_style_bg_color(control_band, pal.main_chrome_bg, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(control_band, cb_light ? k_cb_bg_opa_light : k_cb_bg_opa_dark, LV_PART_MAIN);
                lv_obj_set_style_border_color(control_band, pal.main_chrome_border, LV_PART_MAIN);
                lv_obj_set_style_border_opa(control_band, cb_light ? k_cb_border_opa_light : k_cb_border_opa_dark, LV_PART_MAIN);
                lv_obj_set_style_radius(control_band, k_control_band_corner_r, LV_PART_MAIN);
                // 2px rim trial: unifies shelf edge with rounded cap read (vs 1px cap + thicker glow band).
                // Проба 2px: кромка полки визуально ближе к скруглению; иначе «толстый glow + тонкая дуга».
                lv_obj_set_style_border_width(control_band, k_control_band_border_w, LV_PART_MAIN);
                lv_obj_set_style_shadow_width(control_band, 8, LV_PART_MAIN);
                lv_obj_set_style_shadow_spread(control_band, 0, LV_PART_MAIN);
                lv_obj_set_style_shadow_ofs_y(control_band, 1, LV_PART_MAIN);
                lv_obj_set_style_shadow_ofs_x(control_band, 0, LV_PART_MAIN);
                lv_obj_set_style_shadow_opa(control_band, LV_OPA_10, LV_PART_MAIN);
                lv_obj_set_style_shadow_color(control_band, lv_color_hex(0x000000), LV_PART_MAIN);
            }
            lv_obj_add_flag(control_band, LV_OBJ_FLAG_OVERFLOW_VISIBLE); // glow/shadow not clipped / не клиповать край
            lv_obj_clear_flag(control_band, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(control_band, LV_OBJ_FLAG_GESTURE_BUBBLE);

            // Rim glow handles — width set after flex layout (LV_PCT is content box, misses cap / pad_hor zone).
            // Хэндлы блика — ширина после layout (проценты = content, не доходит до скругления под pad).
            lv_obj_t* edge_glow_top = nullptr;
            lv_obj_t* edge_glow_bot = nullptr;
            // Stage 6.6R-GA: gradient descriptors are file-scope static (s_cb_rim_glow_grad) so
            // liveReapplyTheme() can refresh stops on theme switch. Colors come from palette tokens.
            // Этап 6.6R-GA: дескрипторы — static; цвета из токенов палитры (fix stale glow).

            // PASS B: 4-stop HOR — mat→peak→mat, then mat plateau to x2=x3 so last pixels = shelf (no grey rim).
            // Четыре стопа: после пика снова мат и длинный «плато» тот же мат до правого края — без серого хвоста.
            {
                const bool lightScheme = (yoradio_theme_active_preset() == ThemePreset::Light);
                // Edge = shelf body; peak brighter so center reads as light pool, not uniform stripe.
                // Край = мат полки; пик ярче — центр как пятно света, не однотонная полоса.
                const lv_opa_t glow_bg_top = lightScheme ? LV_OPA_50 : LV_OPA_60;
                const lv_opa_t glow_bg_bot = lightScheme ? LV_OPA_40 : LV_OPA_50;
                auto add_edge_glow = [&](bool top) {
                    lv_obj_t* const g = lv_obj_create(control_band);
                    if (!g) return;
                    lv_obj_add_flag(g, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_height(g, kGlowH);
                    // Placeholder until layout; then nearly full outer shelf width (see post-layout block below).
                    // Заглушка до layout; потом почти полная ширина корпуса полки (см. блок ниже после слотов list/settings).
                    lv_obj_set_width(g, LV_PCT(88));
                    const unsigned gi = top ? 0u : 1u;
                    lv_grad_dsc_t* const gd = &s_cb_rim_glow_grad[gi];
                    std::memset(gd, 0, sizeof(*gd));
                    gd->dir = LV_GRAD_DIR_HOR;
                    gd->dither = LV_DITHER_NONE;
                    gd->stops_count = 4;
                    gd->stops[0].frac = 0;
                    gd->stops[1].frac = 108; // peak left of centre — long gentle falloff to the right / длинный спад вправо
                    gd->stops[2].frac = 172; // touch mat again before final edge (plateau start) / снова мат
                    gd->stops[3].frac = 255;
                    // Colors from active palette tokens (edge=main_chrome_bg, peak=glow_top/bottom).
                    // Цвета — из токенов активной палитры.
                    main_fill_rim_glow_grad(gd, top, pal);
                    lv_obj_set_style_bg_grad(g, gd, LV_PART_MAIN);
                    lv_obj_set_style_bg_opa(g, top ? glow_bg_top : glow_bg_bot, LV_PART_MAIN);
                    lv_obj_set_style_radius(g, (kGlowH + 1) / 2, LV_PART_MAIN);
                    lv_obj_set_style_border_width(g, 0, LV_PART_MAIN);
                    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE);
                    // Final X from TOP_LEFT after layout (asymmetric inset — правый край не в радиус); Y provisional.
                    // Финальный X после layout; Y пока — потом тот же в post-layout.
                    if (top) {
                        lv_obj_align(g, LV_ALIGN_TOP_LEFT, k_glow_inset_left_px, -10);
                    } else {
                        lv_obj_align(g, LV_ALIGN_BOTTOM_LEFT, k_glow_inset_left_px, 10);
                    }
                    if (top) {
                        edge_glow_top = g;
                        self._edge_glow_top = g; // 6.6R-GA: store for liveReapplyTheme() / для reapply
                    } else {
                        edge_glow_bot = g;
                        self._edge_glow_bot = g;
                    }
                };
                add_edge_glow(true);
                add_edge_glow(false);
            }

            // Left slot: list only — same horizontal inset from band edge as settings on the right (control_band pad_hor).
            // Левый слот: только list; отступ от края полки = pad_hor, зеркально settings справа.
            lv_obj_t* utility_left = lv_obj_create(control_band);
            if (utility_left) {
                lv_obj_set_height(utility_left, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(utility_left, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(
                    utility_left,
                    LV_FLEX_ALIGN_START,
                    LV_FLEX_ALIGN_CENTER,
                    LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_all(utility_left, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_column(utility_left, 0, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(utility_left, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(utility_left, 0, LV_PART_MAIN);
                lv_obj_clear_flag(utility_left, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(utility_left, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_flag(utility_left, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                lv_obj_t* btn_list = main_create_control_icon_btn(
                    utility_left,
                    control_glyph_utf8_list(),
                    f_ctrl_transport, pal.text_secondary,
                    pad_tr, min_tr,
                    18,
                    pressed_opa_utility,
                    pal.main_chrome_pressed_bg);
                if (btn_list) {
                    self._ctrl_btn_list = btn_list;
                    lv_obj_add_event_cb(btn_list, main_utility_station_cb, LV_EVENT_CLICKED, nullptr);
                }
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
                    pressed_opa_transport,
                    pal.main_chrome_pressed_bg);
                if (btn_prev) {
                    self._ctrl_btn_prev = btn_prev;
                    lv_obj_add_event_cb(btn_prev, main_transport_prev_cb, LV_EVENT_CLICKED, nullptr);
                }
                lv_obj_t* btn_play = main_create_control_icon_btn(
                    transport_group,
                    control_glyph_utf8_player_play(),
                    f_ctrl_transport, pal.text_primary,
                    pad_tr, min_tr,
                    18,
                    pressed_opa_transport,
                    pal.main_chrome_pressed_bg);
                if (btn_play) {
                    self._ctrl_btn_play = btn_play;
                    lv_obj_add_event_cb(btn_play, main_transport_toggle_cb, LV_EVENT_CLICKED, nullptr);
                    // First child is the icon label — update glyph in update() when playback state changes.
                    // Первый ребёнок — label иконки; текст меняем в update() при смене PLAYING/STOPPED.
                    self._lbl_transport_play_stop = lv_obj_get_child(btn_play, 0);
                }
                lv_obj_t* btn_next = main_create_control_icon_btn(
                    transport_group,
                    control_glyph_utf8_player_skip_forward(),
                    f_ctrl_transport, pal.text_primary,
                    pad_tr, min_tr,
                    18,
                    pressed_opa_transport,
                    pal.main_chrome_pressed_bg);
                if (btn_next) {
                    self._ctrl_btn_next = btn_next;
                    lv_obj_add_event_cb(btn_next, main_transport_next_cb, LV_EVENT_CLICKED, nullptr);
                }
            }

            // Right slot: settings only — inset from band edge matches list (symmetric pad_hor on control_band).
            // Правый слот: только settings; зеркально list слева (общий pad_hor полки).
            lv_obj_t* utility_right = lv_obj_create(control_band);
            if (utility_right) {
                lv_obj_set_height(utility_right, LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(utility_right, LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(
                    utility_right,
                    LV_FLEX_ALIGN_END,
                    LV_FLEX_ALIGN_CENTER,
                    LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_all(utility_right, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_column(utility_right, 0, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(utility_right, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(utility_right, 0, LV_PART_MAIN);
                lv_obj_clear_flag(utility_right, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(utility_right, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_flag(utility_right, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

                lv_obj_t* btn_settings = main_create_control_icon_btn(
                    utility_right,
                    control_glyph_utf8_settings(),
                    f_ctrl_transport, pal.text_secondary,
                    pad_tr, min_tr,
                    18,
                    pressed_opa_utility,
                    pal.main_chrome_pressed_bg);
                if (btn_settings) {
                    self._ctrl_btn_settings = btn_settings;
                    lv_obj_add_event_cb(btn_settings, main_utility_settings_cb, LV_EVENT_CLICKED, nullptr);
                }
            }

            // Equalize left/right slot widths so transport triad stays visually centered.
            // Выровнять ширины слотов list/settings — триада транспорта остаётся по центру полки.
            if (utility_left && utility_right) {
                lv_obj_update_layout(control_band);
                const lv_coord_t wL = lv_obj_get_width(utility_left);
                const lv_coord_t wR = lv_obj_get_width(utility_right);
                const lv_coord_t wBal = LV_MAX(wL, wR);
                lv_obj_set_width(utility_left, wBal);
                lv_obj_set_width(utility_right, wBal);
                lv_obj_update_layout(control_band);
            } else {
                lv_obj_update_layout(control_band);
            }
            // Width from CONTENT: left fixed. Right = geometric base (R−B) minus nudge — extend ~10px toward rounding.
            // Слева как было. Справа: база R−B, минус «дожим» к скруглению (~10px по глазу); не ниже 4 — запас от клипа.
            if (edge_glow_top && edge_glow_bot) {
                constexpr lv_coord_t k_glow_inset_left = k_glow_inset_left_px; // same as band-level constant / см. выше
                const lv_coord_t k_glow_inset_right_base =
                    k_control_band_corner_r - k_control_band_border_w; // 20 @ R22 B2
                lv_coord_t k_glow_inset_right =
                    k_glow_inset_right_base - k_glow_right_extend_px - k_glow_right_extra_width_px + k_glow_right_shrink_px;
                if (k_glow_inset_right < 4) {
                    k_glow_inset_right = 4;
                }
                const lv_coord_t c_w = lv_obj_get_content_width(control_band);
                if (c_w > k_glow_inset_left + k_glow_inset_right + 16) {
                    const lv_coord_t gw = c_w - k_glow_inset_left - k_glow_inset_right;
                    lv_obj_set_width(edge_glow_top, gw);
                    lv_obj_set_width(edge_glow_bot, gw);
                    lv_obj_align(edge_glow_top, LV_ALIGN_TOP_LEFT, k_glow_inset_left, -10);
                    lv_obj_align(edge_glow_bot, LV_ALIGN_BOTTOM_LEFT, k_glow_inset_left, 10);
                    // No extra "feather" rects — straight overlap into the arc looked wrong (line into corner).
                    // Без доп. прямоугольников: прямой «хвост» в зону скругления визуально фигня, только два rim glow.
                }
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
            lv_obj_set_style_pad_column(row_meta_stream, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row_meta_stream, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row_meta_stream, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row_meta_stream, 0, LV_PART_MAIN);
            lv_obj_clear_flag(row_meta_stream, LV_OBJ_FLAG_SCROLLABLE);

            // 6.1E: one composed stream-info line — real facts only; omit unknown fields (no placeholders).
            self._lbl_stream_info = lv_label_create(row_meta_stream);
            if (self._lbl_stream_info) {
                lv_obj_set_width(self._lbl_stream_info, LV_PCT(100));
                lv_label_set_long_mode(self._lbl_stream_info, LV_LABEL_LONG_CLIP);
                lv_obj_set_style_text_align(self._lbl_stream_info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                main_set_font(self._lbl_stream_info, reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr));
                lv_obj_set_style_text_color(self._lbl_stream_info, pal.text_meta, LV_PART_MAIN);
                {
                    char ib[64];
                    main_compose_stream_info_line(ib, sizeof(ib));
                    lv_label_set_text(self._lbl_stream_info, ib);
                }
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

            self._lbl_volume = lv_label_create(col_vol);
            if (self._lbl_volume) {
                lv_label_set_text(self._lbl_volume, "Vol: --");
                main_set_font(self._lbl_volume, LV_ACTIVE_PROFILE.font_small);
                lv_obj_set_style_text_color(self._lbl_volume, pal.text_secondary, LV_PART_MAIN);
            }

            self._bar_volume = lv_bar_create(col_vol);
            if (self._bar_volume) {
                lv_bar_set_range(self._bar_volume, 0, 254);
                lv_bar_set_value(self._bar_volume, static_cast<int32_t>(config.store.volume), LV_ANIM_OFF);
                lv_obj_set_width(self._bar_volume, LV_PCT(100));
                // H=16: compromise between slim 10px bar and test H=20 — gradient+shadow still readable.
                // H=16, radius=8 (capsule), pad 3 top/bottom → fill H=10, fill radius=5; pad 3 left/right.
                // H=16: компромисс между тонким баром и тестом 20 px — эффект всё ещё виден.
                lv_obj_set_height(self._bar_volume, 16);
                // Groove (track): full capsule + 1px border rim + gradient + inner shadow.
                // Вертикальный градиент (темнее сверху) + тень сверху = ощущение канавки в поверхности.
                lv_obj_set_style_radius(self._bar_volume, 8, LV_PART_MAIN);
                lv_obj_set_style_bg_color(self._bar_volume, pal.volume_bar_track, LV_PART_MAIN);
                lv_obj_set_style_bg_grad_color(self._bar_volume, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
                lv_obj_set_style_bg_grad_dir(self._bar_volume, LV_GRAD_DIR_VER, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(self._bar_volume, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(self._bar_volume, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(self._bar_volume, pal.panel_border, LV_PART_MAIN);
                lv_obj_set_style_border_opa(self._bar_volume, LV_OPA_COVER, LV_PART_MAIN);
                // Inner shadow from top: spread=-1 keeps it inside the capsule boundary.
                // Внутренняя тень сверху: spread=-1 не выходит за края капсулы.
                lv_obj_set_style_shadow_color(self._bar_volume, lv_color_black(), LV_PART_MAIN);
                // Slightly softer inner shadow — less "lit" groove vs previous 50%.
                // Чуть мягче внутренняя тень — меньше ощущения яркой подсветки канавки.
                lv_obj_set_style_shadow_opa(self._bar_volume, LV_OPA_30, LV_PART_MAIN);
                lv_obj_set_style_shadow_width(self._bar_volume, 5, LV_PART_MAIN);
                lv_obj_set_style_shadow_spread(self._bar_volume, -1, LV_PART_MAIN);
                lv_obj_set_style_shadow_ofs_y(self._bar_volume, 2, LV_PART_MAIN);
                // Inset fill: 3px top/bottom → fill H=10, centered in 16px groove.
                // left/right 3px keeps fill away from rounded ends.
                // Паддинг: 3 px сверху/снизу → заливка 10 px по центру; 3 px лево/право.
                lv_obj_set_style_pad_all(self._bar_volume, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_top(self._bar_volume, 3, LV_PART_MAIN);
                lv_obj_set_style_pad_bottom(self._bar_volume, 3, LV_PART_MAIN);
                lv_obj_set_style_pad_left(self._bar_volume, 3, LV_PART_MAIN);
                lv_obj_set_style_pad_right(self._bar_volume, 3, LV_PART_MAIN);
                // fill radius = fill_height/2 = 5 → mini-capsule fully inside groove r=8.
                // Заливка-капсула r=5 целиком внутри groove r=8.
                lv_obj_set_style_radius(self._bar_volume, 5, LV_PART_INDICATOR);
                // HOR: full inner-width gradient + clip (lv_bar.c). lv_color_mix(c1,c2,mix): higher mix → more c1 (fill).
                // Слева тоже близко к fill (высокий mix) — «тёмный» конец градиента поярче; справа — чистый volume_bar_fill.
                {
                    const lv_color_t g0 = lv_color_mix(pal.volume_bar_fill, pal.volume_bar_track, LV_OPA_50);
                    lv_obj_set_style_bg_color(self._bar_volume, g0, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_grad_color(self._bar_volume, pal.volume_bar_fill, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_grad_dir(self._bar_volume, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_opa(self._bar_volume, LV_OPA_COVER, LV_PART_INDICATOR);
                }
                lv_obj_clear_flag(self._bar_volume, LV_OBJ_FLAG_CLICKABLE);
            }

            // 6.1D-b: Temporary floating label for volume value during interaction.
            // Popup is parented on _screen (floating) so it can appear above all flex children.
            // Временный label с числом громкости во время взаимодействия. Родитель — _screen (floating).
            self._lbl_vol_popup = lv_label_create(self._screen);
            if (self._lbl_vol_popup && self._bar_volume) {
                lv_label_set_text(self._lbl_vol_popup, "");
                main_set_font(self._lbl_vol_popup, LV_ACTIVE_PROFILE.font_small);
                lv_obj_set_style_text_color(self._lbl_vol_popup, pal.text_primary, LV_PART_MAIN);
                lv_obj_set_style_bg_color(self._lbl_vol_popup, pal.panel_background, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(self._lbl_vol_popup, LV_OPA_80, LV_PART_MAIN);
                lv_obj_set_style_pad_all(self._lbl_vol_popup, 4, LV_PART_MAIN);
                lv_obj_set_style_radius(self._lbl_vol_popup, 4, LV_PART_MAIN);
                lv_obj_add_flag(self._lbl_vol_popup, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(self._lbl_vol_popup, LV_OBJ_FLAG_FLOATING);
                // Store popup pointer in bar user_data for callback access.
                lv_obj_set_user_data(self._bar_volume, self._lbl_vol_popup);
            }
        }

        // Lower divider/meter: symmetric to status_divider above; becomes buffer meter when audioinfo=true.
        // Нижний разделитель/meter: симметрично верхнему status_divider; при audioinfo=true — meter буфера.
        self._bar_buffer = lv_bar_create(zone_bottom);
        if (self._bar_buffer) {
            lv_bar_set_range(self._bar_buffer, 0, 100);
            lv_bar_set_value(self._bar_buffer, 0, LV_ANIM_OFF);
            lv_obj_set_width(self._bar_buffer, LV_PCT(100));
            // 1 px line; meter fill uses brighter token (see theme buffer_meter_fill vs pal.divider).
            // 1 px; заливка meter — из темы, контрастнее divider для читаемости на тонкой линии.
            lv_obj_set_height(self._bar_buffer, 1);
            lv_obj_set_style_radius(self._bar_buffer, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(self._bar_buffer, 0, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(self._bar_buffer, pal.divider, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(self._bar_buffer, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(self._bar_buffer, pal.buffer_meter_fill, LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(self._bar_buffer, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_clear_flag(self._bar_buffer, LV_OBJ_FLAG_CLICKABLE);
        }

        self._lbl_ai_line = lv_label_create(zone_bottom);
        if (self._lbl_ai_line) {
            lv_label_set_text(self._lbl_ai_line, "");
            lv_label_set_long_mode(self._lbl_ai_line, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_width(self._lbl_ai_line, LV_PCT(100));
            lv_obj_set_style_text_align(self._lbl_ai_line, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            // AI line: 16 px (was 14) — one tier up for readability / строка AI: 16 px, чуть крупнее
            const lv_font_t* const f_ai = reinterpret_cast<const lv_font_t*>(&lv_font_yora_montserrat_16_cyr);
            main_set_font(self._lbl_ai_line, reinterpret_cast<const void*>(f_ai));
            lv_obj_set_style_text_color(self._lbl_ai_line, pal.bottom_ai_text, LV_PART_MAIN);
            // One line from font metrics — slot stability only; lower divider position is NOT from this (symmetry pass).
            // Одна строка по метрикам шрифта — слот не схлопывается; нижний divider задаётся симметрией, не 20 px.
            lv_obj_set_style_min_height(self._lbl_ai_line, static_cast<lv_coord_t>(lv_font_get_line_height(f_ai)), LV_PART_MAIN);
            // Swipe must not bubble to _screen — carousel listens on page root (5.3).
            // Свайп по строке AI не должен всплывать на экран с обработчиком карусели.
            lv_obj_clear_flag(self._lbl_ai_line, LV_OBJ_FLAG_GESTURE_BUBBLE);
        }

        // Vertical divider contract: screen.y2 - heapbar.y2 == status_divider.y1 - screen.y1 (real coords after layout).
        // Push zone_bottom down via zone_bottom_sym_spacer, or shrink spacer_bottom when delta < 0 (no lv_obj margin API).
        // Симметрия по краям экрана: тот же inset; flex только сверху вниз — спейсер/сжатие нижнего flex-spacer.
        if (self._screen && status_divider && zone_bottom && self._bar_buffer && zone_bottom_sym_spacer) {
            lv_obj_update_layout(self._screen);
            lv_area_t scr{};
            lv_area_t top_div{};
            lv_area_t heap{};
            lv_obj_get_coords(self._screen, &scr);
            lv_obj_get_coords(status_divider, &top_div);
            lv_obj_get_coords(self._bar_buffer, &heap);
            const lv_coord_t inset_top = top_div.y1 - scr.y1;
            const lv_coord_t target_heap_y2 = scr.y2 - inset_top;
            const lv_coord_t delta = target_heap_y2 - heap.y2;
            if (delta > 0) {
                lv_obj_set_height(zone_bottom_sym_spacer, delta);
            } else if (delta < 0 && self._spacer_bottom) {
                lv_obj_set_flex_grow(self._spacer_bottom, 0);
                lv_coord_t sh = lv_obj_get_height(self._spacer_bottom);
                lv_coord_t nh = sh + delta;
                if (nh < 0) nh = 0;
                lv_obj_set_height(self._spacer_bottom, nh);
            }
            lv_obj_update_layout(self._screen);
        }

        // 6.1D-b: Volume touch strip — FLOATING; size/position from real bounds of bar + heapbar (no layout dup constants).
        // Тач-полоса: FLOATING; размер из coords бар+heapbar после lv_obj_update_layout.
        if (zone_bottom && self._bar_volume && self._bar_buffer) {
            lv_obj_update_layout(zone_bottom);
            lv_area_t bar_coords{};
            lv_area_t buf_coords{};
            lv_area_t zone_coords{};
            lv_obj_get_coords(self._bar_volume, &bar_coords);
            lv_obj_get_coords(self._bar_buffer, &buf_coords);
            lv_obj_get_coords(zone_bottom, &zone_coords);
            const lv_coord_t top_rel =
                (bar_coords.y1 - kVolTouchPadAbove) - zone_coords.y1;
            const lv_coord_t bottom_rel =
                (buf_coords.y2 + kVolTouchOverlapBelow) - zone_coords.y1;
            const lv_coord_t strip_h = bottom_rel - top_rel;

            if (strip_h > 0) {
                self._vol_touch_zone = lv_obj_create(zone_bottom);
                if (self._vol_touch_zone) {
                    lv_obj_add_flag(self._vol_touch_zone, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_width(self._vol_touch_zone, LV_PCT(100));
                    lv_obj_set_height(self._vol_touch_zone, strip_h);
                    lv_obj_set_pos(self._vol_touch_zone, 0, top_rel);
                    lv_obj_set_style_bg_opa(self._vol_touch_zone, LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_border_width(self._vol_touch_zone, 0, LV_PART_MAIN);
                    lv_obj_add_flag(self._vol_touch_zone, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_clear_flag(self._vol_touch_zone, LV_OBJ_FLAG_GESTURE_BUBBLE);
                    lv_obj_set_user_data(self._vol_touch_zone, self._bar_volume);
                    lv_obj_add_event_cb(self._vol_touch_zone, vol_touch_cb, LV_EVENT_PRESSED, &self);
                    lv_obj_add_event_cb(self._vol_touch_zone, vol_touch_cb, LV_EVENT_PRESSING, &self);
                    lv_obj_add_event_cb(self._vol_touch_zone, vol_touch_cb, LV_EVENT_RELEASED, &self);
                    lv_obj_add_event_cb(self._vol_touch_zone, vol_touch_cb, LV_EVENT_PRESS_LOST, &self);
                    lv_obj_move_foreground(self._vol_touch_zone);
                }
            }

            // Below volume touch strip: full-width guard — no volume logic; blocks gesture → carousel on _screen.
            // Под полосой громкости: только поглощение жеста (geometry from touch_zone + zone_bottom coords).
            lv_obj_update_layout(zone_bottom);
            if (zone_bottom && self._vol_touch_zone) {
                lv_area_t tz_coords{};
                lv_area_t zb_coords{};
                lv_obj_get_coords(self._vol_touch_zone, &tz_coords);
                lv_obj_get_coords(zone_bottom, &zb_coords);
                const lv_coord_t guard_y_rel = (tz_coords.y2 + 1) - zb_coords.y1;
                const lv_coord_t guard_h     = zb_coords.y2 - tz_coords.y2;
                if (guard_h > 0) {
                    self._vol_gesture_guard = lv_obj_create(zone_bottom);
                    if (self._vol_gesture_guard) {
                        lv_obj_add_flag(self._vol_gesture_guard, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_width(self._vol_gesture_guard, LV_PCT(100));
                        lv_obj_set_height(self._vol_gesture_guard, guard_h);
                        lv_obj_set_pos(self._vol_gesture_guard, 0, guard_y_rel);
                        lv_obj_set_style_bg_opa(self._vol_gesture_guard, LV_OPA_TRANSP, LV_PART_MAIN);
                        lv_obj_set_style_border_width(self._vol_gesture_guard, 0, LV_PART_MAIN);
                        lv_obj_add_flag(self._vol_gesture_guard, LV_OBJ_FLAG_CLICKABLE);
                        lv_obj_clear_flag(self._vol_gesture_guard, LV_OBJ_FLAG_GESTURE_BUBBLE);
                        // Ensure guard is above flex children (e.g. AI label) for hit-testing.
                        // Поверх flex-детей — иначе жест уходит в лейбл и всплывает на экран.
                        lv_obj_move_foreground(self._vol_gesture_guard);
                    }
                }
            }
        }

        // Bridge any remaining Y-gap below zone_bottom to _screen bottom (absolute coords; includes ex- padding strip).
        // Закрываем остаток по Y до низа экрана — иначе жест снова попадает на _screen.
        if (self._screen && zone_bottom) {
            lv_obj_update_layout(self._screen);
            lv_area_t scr_a{};
            lv_area_t zb_a{};
            lv_obj_get_coords(self._screen, &scr_a);
            lv_obj_get_coords(zone_bottom, &zb_a);
            const lv_coord_t gap_px = scr_a.y2 - zb_a.y2;
            if (gap_px > 0) {
                self._screen_bottom_carousel_guard = lv_obj_create(self._screen);
                if (self._screen_bottom_carousel_guard) {
                    lv_obj_add_flag(self._screen_bottom_carousel_guard, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_width(self._screen_bottom_carousel_guard, LV_PCT(100));
                    lv_obj_align_to(self._screen_bottom_carousel_guard, zone_bottom, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
                    lv_obj_set_height(self._screen_bottom_carousel_guard, gap_px);
                    lv_obj_set_style_bg_opa(self._screen_bottom_carousel_guard, LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_border_width(self._screen_bottom_carousel_guard, 0, LV_PART_MAIN);
                    lv_obj_add_flag(self._screen_bottom_carousel_guard, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_clear_flag(self._screen_bottom_carousel_guard, LV_OBJ_FLAG_GESTURE_BUBBLE);
                    lv_obj_move_foreground(self._screen_bottom_carousel_guard);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Class methods / Методы класса LvglMainScreen
// ─────────────────────────────────────────────────────────────────────────────

ScreenType LvglMainScreen::screenType() const {
    return ScreenType::Page;
}

void LvglMainScreen::reloadFileBackgroundFromLittlefs() {
    if (!_bg_img) {
        return;
    }
    // W2G-A: WebUI uploaded a new background file → must bypass the cache and reload from LittleFS.
    // W2G-A: WebUI загрузил новый фон → инвалидируем кэш и принудительно перезагружаем из LittleFS.
    main_bg_cache_invalidate();
    _bg_psram_buf = nullptr;
    _bg_psram_dsc = {};
    _applyBgTheme(true);
    if (_bg_scrim) {
        main_sync_dark_bg_scrim(_bg_img, _bg_scrim);
    }
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

    // E36FS1a: missing .bin — hide only; LVGL 8 warns on lv_img_set_src(nullptr).
    // E36FS1a: нет файла — только скрыть; nullptr в set_src даёт warn в LVGL 8.
    auto hide_bg_missing_file = [this]() {
        lv_obj_add_flag(_bg_img, LV_OBJ_FLAG_HIDDEN);
        _syncBgImgLayout();
    };

    // Periodic update: slot still missing and already hidden — no lv_* churn.
    // Периодический update: файл по-прежнему отсутствует, фон уже скрыт — без лишних lv_*.
    if (!force && slot == _bg_last_slot && _bg_absent_latched && !LittleFS.exists(k_fs[slot])) {
        return;
    }

    // Fast path: buffer for this slot already borrowed from cache → just check for deletion.
    // No lv_img_set_src → no LVGL invalidation → no LFS read on next frame.
    // Быстрый путь: буфер этого слота уже заимствован из кэша → только проверка удаления файла.
    if (!force && slot == _bg_last_slot && _bg_psram_buf != nullptr) {
        if (!LittleFS.exists(k_fs[slot])) {
            // File deleted from LittleFS: invalidate cache + clear borrowed pointer.
            // Файл удалён из LittleFS: инвалидировать кэш + сбросить заимствованный указатель.
            main_bg_cache_invalidate();
            _bg_psram_buf = nullptr;
            _bg_psram_dsc = {};
            hide_bg_missing_file();
            _bg_absent_latched = true;
        }
        return; // No lv_img_set_src → no LVGL invalidation → no LFS read on next frame
    }

    // Slow path: slot changed, forced reload, or no buffer yet.
    // W2G-A: do NOT free _bg_psram_buf here — the cache owns the allocation.
    //        Just clear the borrowed pointer and update the slot tracker.
    // Медленный путь: смена слота, принудительная загрузка или буфер ещё не создан.
    // W2G-A: не освобождать _bg_psram_buf — буфер принадлежит кэшу; только очищаем заимствованный указатель.
    _bg_psram_buf = nullptr;
    _bg_psram_dsc = {};
    _bg_last_slot = slot;

    if (!LittleFS.exists(k_fs[slot])) {
        hide_bg_missing_file();
        _bg_absent_latched = true;
        return;
    }

    // W2G-A: ask the cache for the background buffer (hit = PSRAM reuse, miss = LFS load + cache).
    // _bg_psram_buf borrows the pointer from the cache; must NOT be freed in destroy/release.
    // W2G-A: запрашиваем буфер у кэша (попадание = reuse PSRAM, промах = LFS-загрузка + кэш).
    // _bg_psram_buf заимствует указатель у кэша; в destroy/release НЕ освобождать.
    _bg_absent_latched = false;
    if (main_bg_cache_get(k_fs[slot], _bg_psram_dsc)) {
        _bg_psram_buf = const_cast<uint8_t*>(_bg_psram_dsc.data); // borrowed / заимствован
        lv_img_set_src(_bg_img, &_bg_psram_dsc);
    } else {
        // Fallback: file-backed path (LFS reads on DspTask — slow, but better than no image).
        // Резервный путь: читаем из LittleFS напрямую (медленно, но без крэша).
        lv_img_set_src(_bg_img, k_lvgl[slot]);
    }
    lv_obj_clear_flag(_bg_img, LV_OBJ_FLAG_HIDDEN);
    _syncBgImgLayout();
}

void LvglMainScreen::_syncBgImgLayout() {
    // Full-bleed bg under frame_padding: _screen has pad_all(frame_padding); LVGL child (0,0) is content origin.
    // B1 restored size after lv_img_set_src but align(0,0) still inset ~frame_padding px — compensate here.
    // Полноэкранный фон: pad на _screen сдвигает content area; отрицательный offset = физический (0,0).
    if (!_screen) return;
    const lv_coord_t W = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width);
    const lv_coord_t H = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.height);
    const lv_coord_t frame_pad = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);
    if (_bg_img) {
        lv_obj_set_size(_bg_img, W, H);
        lv_obj_set_pos(_bg_img, -frame_pad, -frame_pad);
    }
    if (_bg_scrim) {
        lv_obj_set_size(_bg_scrim, W, H);
        lv_obj_set_pos(_bg_scrim, -frame_pad, -frame_pad);
    }
}

void LvglMainScreen::create() {
    if (_screen) return;

    // Root screen: dimensions + flex column + uniform frame padding.
    // Корневой экран: размеры + flex-колонка + рамка со всех сторон.
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
        _applyBgTheme(true); // preload + _syncBgImgLayout() / предзагрузка + геометрия фона

        _bg_scrim = lv_obj_create(_screen);
        if (_bg_scrim) {
            lv_obj_add_flag(_bg_scrim, LV_OBJ_FLAG_FLOATING);
            lv_obj_clear_flag(_bg_scrim, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(_bg_scrim, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(_bg_scrim, W, H);
            lv_obj_set_style_bg_color(_bg_scrim, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_bg_scrim, k_main_bg_scrim_opa_dark, LV_PART_MAIN);
            lv_obj_set_style_border_width(_bg_scrim, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(_bg_scrim, 0, LV_PART_MAIN);
            main_sync_dark_bg_scrim(_bg_img, _bg_scrim);
            _syncBgImgLayout();
        }
    }

    lv_obj_t* status_divider = create_status_line(*this, pal);
    if (!status_divider) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    create_mid_block(*this, pal);
    create_visual_rail(*this, pal);
    create_bottom_zone(*this, pal, status_divider);

    installCarouselGesturesOnPageRoot(_screen);
}

// ---------------------------------------------------------------------------
// Station Art MVP: runtime reload helpers (DspTask only, lv_* safe).
// Station Art MVP: runtime-перезагрузка арта (только DspTask).
// ---------------------------------------------------------------------------

void LvglMainScreen::_reloadArtIfNeeded() {
    if (!_art_slot || !_art_img) return;

    const uint16_t current_num = config.lastStation();

    // Re-evaluate only when station changes or forced (avoids stationByNum() I/O every update tick).
    // Пересчитываем только при смене станции или принудительном флаге (минимальный I/O).
    if (current_num == _art_last_station_num && !_art_reload_forced) return;
    _art_last_station_num = current_num;
    _art_reload_forced    = false;

    // Stable source: playlist name via stationByNum — never config.station.name (may be ICY-overwritten).
    // Стабильный источник: плейлистное имя через stationByNum, не config.station.name.
    const char* playlist_name = config.stationByNum(current_num);

    char key[68]  = {};
    char fs_path[84]   = {};
    char lvgl_path[88] = {};
    artNormalizeKey(playlist_name, key, sizeof(key));
    if (key[0] != '\0') {
        snprintf(fs_path,   sizeof(fs_path),   "/logo/%s.bin",   key);
        snprintf(lvgl_path, sizeof(lvgl_path), "L:/logo/%s.bin", key);
    }

    const bool file_exists      = (key[0] != '\0') && LittleFS.exists(fs_path);
    const bool currently_visible = !lv_obj_has_flag(_art_slot, LV_OBJ_FLAG_HIDDEN);
    const bool key_changed      = (strncmp(_art_current_key, key, sizeof(_art_current_key)) != 0);

    if (!key_changed && (file_exists == currently_visible)) {
        return; // nothing changed — skip all lv_* calls
    }

    strlcpy(_art_current_key, key, sizeof(_art_current_key));

    if (file_exists) {
        lv_img_set_src(_art_img, lvgl_path);
        lv_obj_clear_flag(_art_slot, LV_OBJ_FLAG_HIDDEN); // Mode B: art visible
    } else {
        // Do NOT call lv_img_set_src(nullptr) — LVGL 8.x warns "unknown type" for NULL src.
        // Hiding the slot is sufficient: LVGL skips rendering hidden objects entirely.
        // lv_img_set_src(nullptr) не вызываем — LVGL 8 даёт warn "unknown type" для NULL.
        // Скрытие слота достаточно: LVGL не рендерит скрытые объекты.
        lv_obj_add_flag(_art_slot, LV_OBJ_FLAG_HIDDEN);   // Mode A: no art, cont_text expands
    }

    // Sync flex + text alignment to art mode (mirrors has_cover logic from create()).
    // CENTER when no art; START/LEFT when art is visible.
    // Синхронизируем выравнивание flex/text с Mode A/B (зеркало has_cover из create()).
    {
        const lv_flex_align_t fa = file_exists ? LV_FLEX_ALIGN_START  : LV_FLEX_ALIGN_CENTER;
        const lv_text_align_t ta = file_exists ? LV_TEXT_ALIGN_LEFT   : LV_TEXT_ALIGN_CENTER;
        if (_cont_mid) {
            lv_obj_set_flex_align(_cont_mid,  fa, fa, fa);
        }
        if (_cont_text) {
            lv_obj_set_flex_align(_cont_text, fa, fa, fa);
        }
        if (_lbl_station_name) {
            lv_obj_set_style_text_align(_lbl_station_name, ta, LV_PART_MAIN);
        }
        if (_lbl_track) {
            lv_obj_set_style_text_align(_lbl_track, ta, LV_PART_MAIN);
        }
        if (_lbl_artist) {
            lv_obj_set_style_text_align(_lbl_artist, ta, LV_PART_MAIN);
        }
    }

    // Update flex grow for art/no-art vertical composition (spacers around cont_mid).
    // Обновляем grow спейсеров для вертикальной компоновки с артом и без.
    if (_spacer_top && _spacer_bottom) {
        if (file_exists) {
            lv_obj_set_flex_grow(_spacer_top,    k_spacer_grow_with_art_top);
            lv_obj_set_flex_grow(_spacer_bottom, k_spacer_grow_with_art_bottom);
        } else {
            lv_obj_set_flex_grow(_spacer_top,    k_spacer_grow_no_art_top);
            lv_obj_set_flex_grow(_spacer_bottom, k_spacer_grow_no_art_bottom);
        }
    }

    Serial.printf("[ART] reload: num=%u key='%s' present=%d\n",
        (unsigned)current_num, key, (int)file_exists);
}

void LvglMainScreen::reloadStationArtFromLittlefs() {
    // Called from DspTask via ART_FS_UPDATED queue event after WebUI upload_art / remove_art.
    // Вызывается из DspTask после ART_FS_UPDATED (upload_art / remove_art через WebUI).
    _art_reload_forced = true;
    _reloadArtIfNeeded();
}

void LvglMainScreen::enter() {
    // Stage 8 E22A: resume the presence-rail animation while Main is the live carousel page.
    // Timer callback still early-outs on screensaver/blank (mode != PLAYER/VOL).
    // Этап 8 E22A: возобновляем анимацию rail, пока Main — активная страница карусели.
    wgt_presence_rail::start(_presence_rail);
}

void LvglMainScreen::exit() {
    // Stage 8 E22A: pause the rail when leaving Main (carousel swipe to another slot).
    // Этап 8 E22A: пауза rail при уходе с Main (свайп карусели на другой слот).
    wgt_presence_rail::stop(_presence_rail);
}

void LvglMainScreen::update() {
    if (!_lbl_station_name) return;

    static char buf[64];
    static char title_work[BUFLEN];

    main_set_text_if_changed(_lbl_station_name, config.station.name);

    // E36AUD0A: transport error renders in track area; station.title keeps last real metadata.
    // Ошибка — из player.lastError(); title не перезаписывается ошибкой.
    const char* st_title = player.hasError() ? player.lastError() : config.station.title;
    const bool showing_player_error = player.hasError();
    const bool has_stream_meta =
        showing_player_error ||
        (st_title != nullptr && strlen(st_title) > 0 && strcmp(st_title, config.station.name) != 0);

    if (_lbl_track && _lbl_artist) {
        if (!has_stream_meta) {
            lv_obj_add_flag(_lbl_track, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_lbl_artist, LV_OBJ_FLAG_HIDDEN);
        } else if (showing_player_error) {
            lv_obj_clear_flag(_lbl_track, LV_OBJ_FLAG_HIDDEN);
            main_set_text_if_changed(_lbl_track, st_title);
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

    if (_lbl_stream_info) {
        static char stream_info_buf[64];
        main_compose_stream_info_line(stream_info_buf, sizeof(stream_info_buf));
        main_set_text_if_changed(_lbl_stream_info, stream_info_buf);
    }

    // Play/stop button: show stop while playing, play while stopped (same semantics as player.toggle()).
    // Кнопка воспроизведения: при PLAYING — иконка stop, при STOPPED — play (как у toggle).
    if (_lbl_transport_play_stop) {
        const char* play_stop_glyph =
            (player.status() == PLAYING) ? control_glyph_utf8_player_stop() : control_glyph_utf8_player_play();
        main_set_text_if_changed(_lbl_transport_play_stop, play_stop_glyph);
    }

    // Status line: Wi‑Fi / clock / weather — delegated to wgt_status_line (Stage 6.2 Patch A).
    // Верхняя полоса — делегирование в wgt_status_line.
    wgt_status_line::update(_status_line);

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

    // Station Art MVP: reload art when station changes.
    // Key: stationByNum(lastStation()) — not config.station.name.
    // Station Art MVP: перезагрузка арта при смене станции (ключ из плейлиста, не runtime name).
    _reloadArtIfNeeded();
}

void LvglMainScreen::liveReapplyTheme() {
    // Stage 6.6R-B: update all palette-bound and local chrome colors without recreating the screen.
    // Called from DspTask after yoradio_theme_set_preset() + yoradio_theme_reinit().
    // Layout/structure is preserved; only colors are updated.
    // Known follow-up: rim glow gradient (static local in create()) is not updated here.
    //
    // Этап 6.6R-B: обновить palette-цвета + local chrome без пересоздания экрана.
    // Из DspTask после смены пресета + реинит LVGL темы. Layout не меняется.
    // Follow-up: glow-градиент (static local в create()) не обновляется.
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    const ThemePreset preset = yoradio_theme_active_preset();
    const bool lightScheme = (preset == ThemePreset::Light);

    // Root screen background / Фон корневого экрана
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);

    // Center text stack / Центральный стек текста
    if (_lbl_station_name) lv_obj_set_style_text_color(_lbl_station_name, pal.station_name_text, LV_PART_MAIN);
    if (_lbl_track)        lv_obj_set_style_text_color(_lbl_track,        pal.track_text,         LV_PART_MAIN);
    if (_lbl_artist)       lv_obj_set_style_text_color(_lbl_artist,       pal.artist_text,        LV_PART_MAIN);

    // Stream/meta row / Строка потока
    if (_lbl_stream_info)  lv_obj_set_style_text_color(_lbl_stream_info,  pal.text_meta,          LV_PART_MAIN);

    // AI line / Строка AI
    if (_lbl_ai_line)      lv_obj_set_style_text_color(_lbl_ai_line,      pal.bottom_ai_text,     LV_PART_MAIN);

    // Volume label (text_secondary; update() also does this per-tick, here for immediate visual)
    // Vol label — text_secondary; update() синхронизирует его каждый тик, здесь — немедленно.
    if (_lbl_volume)       lv_obj_set_style_text_color(_lbl_volume,       pal.text_secondary,     LV_PART_MAIN);

    // Volume bar groove + indicator / Канавка + заливка volume bar
    if (_bar_volume) {
        lv_obj_set_style_bg_color(_bar_volume,   pal.volume_bar_track, LV_PART_MAIN);
        lv_obj_set_style_border_color(_bar_volume, pal.panel_border,   LV_PART_MAIN);
        const lv_color_t g0 = lv_color_mix(pal.volume_bar_fill, pal.volume_bar_track, LV_OPA_50);
        lv_obj_set_style_bg_color(_bar_volume,      g0,                    LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(_bar_volume, pal.volume_bar_fill,   LV_PART_INDICATOR);
    }

    // Buffer meter (lower divider) / Нижний divider/meter
    if (_bar_buffer) {
        lv_obj_set_style_bg_color(_bar_buffer, pal.divider,           LV_PART_MAIN);
        lv_obj_set_style_bg_color(_bar_buffer, pal.buffer_meter_fill, LV_PART_INDICATOR);
    }

    // Volume popup (floating label during drag) / Плавающий label громкости при drag
    if (_lbl_vol_popup) {
        lv_obj_set_style_text_color(_lbl_vol_popup, pal.text_primary,    LV_PART_MAIN);
        lv_obj_set_style_bg_color(_lbl_vol_popup,   pal.panel_background, LV_PART_MAIN);
    }

    // Art slot frame (Main chrome): reuses control_band border token (6.6R-GA) / Рамка арта
    if (_art_slot) {
        lv_obj_set_style_border_color(_art_slot, pal.main_chrome_border, LV_PART_MAIN);
    }

    // Control band bg/border (Main chrome tokens) + 6.6R-GB1 theme-aware opacity / Полка: bg/border + прозрачность
    if (_control_band) {
        lv_obj_set_style_bg_color(_control_band,     pal.main_chrome_bg,     LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_control_band,       lightScheme ? k_cb_bg_opa_light : k_cb_bg_opa_dark, LV_PART_MAIN);
        lv_obj_set_style_border_color(_control_band, pal.main_chrome_border, LV_PART_MAIN);
        lv_obj_set_style_border_opa(_control_band,   lightScheme ? k_cb_border_opa_light : k_cb_border_opa_dark, LV_PART_MAIN);
    }

    // Rim glow (6.6R-GA): refresh gradient stops + per-theme opacity → no stale glow on switch.
    // Descriptors are static (outlive create); update colors in place and invalidate.
    // Rim glow: обновляем стопы градиента и прозрачность под тему — больше нет «застрявшего» блика.
    {
        const lv_opa_t glow_bg_top = lightScheme ? LV_OPA_50 : LV_OPA_60;
        const lv_opa_t glow_bg_bot = lightScheme ? LV_OPA_40 : LV_OPA_50;
        if (_edge_glow_top) {
            main_fill_rim_glow_grad(&s_cb_rim_glow_grad[0], true, pal);
            lv_obj_set_style_bg_opa(_edge_glow_top, glow_bg_top, LV_PART_MAIN);
            lv_obj_invalidate(_edge_glow_top);
        }
        if (_edge_glow_bot) {
            main_fill_rim_glow_grad(&s_cb_rim_glow_grad[1], false, pal);
            lv_obj_set_style_bg_opa(_edge_glow_bot, glow_bg_bot, LV_PART_MAIN);
            lv_obj_invalidate(_edge_glow_bot);
        }
    }

    // Control icon buttons: fg + pressed bg/opa were set in create() only — reapply for theme switch.
    // 6.6R-GB1: pressed opa is theme-aware (Light stronger) so feedback stays visible after switch.
    // Иконки полки: цвет/pressed-фон/прозрачность задавались в create(); обновляем при смене темы.
    const lv_opa_t re_pressed_transport =
        lightScheme ? k_ctrl_pressed_opa_transport_light : k_ctrl_pressed_opa_transport;
    const lv_opa_t re_pressed_utility =
        lightScheme ? k_ctrl_pressed_opa_utility_light : k_ctrl_pressed_opa_utility;
    main_reapply_control_icon_btn(_ctrl_btn_list,     pal.text_secondary, pal.main_chrome_pressed_bg, re_pressed_utility);
    main_reapply_control_icon_btn(_ctrl_btn_prev,     pal.text_primary,   pal.main_chrome_pressed_bg, re_pressed_transport);
    main_reapply_control_icon_btn(_ctrl_btn_play,     pal.text_primary,   pal.main_chrome_pressed_bg, re_pressed_transport);
    main_reapply_control_icon_btn(_ctrl_btn_next,     pal.text_primary,   pal.main_chrome_pressed_bg, re_pressed_transport);
    main_reapply_control_icon_btn(_ctrl_btn_settings, pal.text_secondary, pal.main_chrome_pressed_bg, re_pressed_utility);

    // Status line widget colors / Цвета виджета status line
    wgt_status_line::reapplyTheme(_status_line);

    // Stage 8 E22A: presence rail recolor from active palette (divider / accent_soft / accent).
    // Этап 8 E22A: перекраска presence rail из активной палитры.
    wgt_presence_rail::reapplyTheme(_presence_rail);

    // Background image slot: reload for new preset (PSRAM free + reload from LittleFS slot).
    // Scrim: preset-aware visibility (Dark-only; handled by main_sync_dark_bg_scrim).
    // Фоновый слот: перезагрузить для нового пресета. Scrim: только Dark.
    _applyBgTheme(true);
    if (_bg_scrim) main_sync_dark_bg_scrim(_bg_img, _bg_scrim);

    // Trigger full redraw / Принудительный перерисов
    lv_obj_invalidate(_screen);
}

void LvglMainScreen::destroy() {
    // Manual delete path (Stage 8 E22A): delete the presence-rail timer (NOT owned by the LVGL tree)
    // + its subtree first, then drop the full screen tree, then null handles + free non-LVGL.
    // Ручное удаление: сначала таймер rail (он не в дереве) + поддерево, затем дерево экрана, затем сброс.
    wgt_presence_rail::destroy(_presence_rail);

    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandlesAndFreeNonLvgl();
}

void LvglMainScreen::prepareForAutoDelete() {
    // W2F: PageChain is about to load the next screen with auto_del=true while Main is STILL the
    // active LVGL screen. Tear down the PresenceRail lv_timer (and its subtree) NOW — the timer is
    // not part of the screen tree, so LVGL's auto-delete would leave a live timer pointing at freed
    // objects. wgt_presence_rail::destroy() is safe/idempotent (guards timer!=null, lv_obj_is_valid).
    // W2F: до auto_del гасим таймер rail (он вне дерева) — иначе он переживёт удаление экрана.
    wgt_presence_rail::destroy(_presence_rail);
}

void LvglMainScreen::releaseAfterAutoDelete() {
    // W2F: LVGL has ALREADY deleted the Main screen tree (auto_del). NEVER lv_obj_del here.
    // The rail timer was stopped in prepareForAutoDelete(); call destroy() again defensively
    // (idempotent) in case this is ever reached without a prior prepare. Then null + free non-LVGL.
    // W2F: дерево Main уже удалено LVGL; lv_obj_del не звать. Таймер rail погашен в prepare
    // (повторный вызов идемпотентен). Затем обнуляем указатели + free PSRAM-фон.
    wgt_presence_rail::destroy(_presence_rail);
    _nullHandlesAndFreeNonLvgl();
}

void LvglMainScreen::_nullHandlesAndFreeNonLvgl() {
    _screen = nullptr;  // dangling after auto_del; already nulled on the manual destroy() path
    _status_line = {};
    _lbl_stream_info = nullptr;
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
    // W2G-A: _bg_psram_buf borrows from s_bg_cache — do NOT free here; the cache owns it.
    // Clear the borrowed pointer and descriptor (cache buffer remains alive for next create()).
    // W2G-A: _bg_psram_buf заимствован из s_bg_cache — не освобождать; очищаем заимствованный указатель.
    _bg_psram_buf = nullptr;
    _bg_psram_dsc = {};
    _bg_last_slot         = 255;
    _bg_absent_latched    = false;
    _bg_img = nullptr;
    _bg_scrim = nullptr;
    _control_band = nullptr; // deleted with _screen tree / удалено вместе с деревом
    _edge_glow_top = _edge_glow_bot = nullptr; // deleted with _screen tree / удалено вместе с деревом
    _ctrl_btn_list = _ctrl_btn_prev = _ctrl_btn_play = _ctrl_btn_next = _ctrl_btn_settings = nullptr;
    _art_slot  = nullptr;
    _art_img   = nullptr;
    _cont_mid  = nullptr;
    _cont_text = nullptr;
    _art_last_station_num = 0xFFFF; // reset sentinel so next create()+update() re-evaluates
    _art_reload_forced    = false;
    _art_current_key[0]   = '\0';
    _spacer_top    = nullptr;
    _spacer_bottom = nullptr;
    s_vol_touch_active = false; // matches static used by vol_touch_cb / тот же флаг, что в callback
}

lv_obj_t* LvglMainScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui
