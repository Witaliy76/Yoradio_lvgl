/*
 * LvglVisualPage — Visual carousel page E3: Beocord museum background + static 16-segment grid.
 * LvglVisualPage — страница Visual E3: музейный фон Beocord + статичная сетка 16 сегментов.
 *
 * - ILvglScreen lifecycle via PageChain (W2F auto-delete on carousel switch).
 * - DspTask-only lv_*; background loaded once in create() into Visual-owned PSRAM.
 * - E3: shared Flash A8 mask + recolor; overlay uses canonical overlay_rect positions.
 *
 * E3 scope: all 16 segments always on (5 green + 3 red per row) — diagnostic only.
 * E3: все 16 сегментов постоянно включены — только визуальная диагностика сетки.
 */

#include "scr_visual.h"

#include "beocord_vu_asset_pack.h"

#include "lvgl.h"
#include "Arduino.h"
#include <cstring>
#include <LittleFS.h>

#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "lvgl_ui.h"

namespace lvgl_ui {

namespace {

// E3 diagnostic grid — fixed museum colors (not yoradio_palette()).
// E3: фиксированные музейные цвета диагностики (не из темы).
static const lv_color_t kBeocordMuseumGreen = lv_color_hex(0x59F08A);
static const lv_color_t kBeocordMuseumRed   = lv_color_hex(0xF06868);

static constexpr uint8_t kBeocordChannelCount         = 2u;
static constexpr uint8_t kBeocordSegmentsPerChannel = 8u;
static constexpr uint8_t kBeocordGreenSegmentCount  = 5u; // indices 0..4 green, 5..7 red

// E3: configure one segment image at canonical overlay_rect (no position offsets).
// E3: настройка одного сегмента по overlay_rect (без смещений позиции).
static void init_segment_img(lv_obj_t* img, const lv_area_t& rect, const lv_img_dsc_t* mask, lv_color_t recolor) {
    if (!img || !mask) return;

    lv_img_set_src(img, mask);
    lv_obj_add_flag(img, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(img, rect.x1, rect.y1);
    lv_obj_set_style_img_recolor(img, recolor, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(img, LV_OPA_COVER, LV_PART_MAIN);
}

static lv_color_t diagnostic_recolor_for_segment(uint8_t seg_index) {
    return (seg_index < kBeocordGreenSegmentCount) ? kBeocordMuseumGreen : kBeocordMuseumRed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Background helpers / Вспомогательные функции фона
// ─────────────────────────────────────────────────────────────────────────────

// Load a .bin (4-byte lv_img_header_t + RGB565 pixels) from LittleFS into PSRAM.
// Visual-local copy of Main bg_load_into_psram — Visual owns the buffer (no shared cache in E1).
// Загрузить .bin из LittleFS в PSRAM; Visual владеет буфером (без общего кэша Main в E1).
static bool bg_load_into_psram(const char* fs_path, uint8_t*& out_buf, lv_img_dsc_t& out_dsc) {
    out_buf = nullptr;
    if (!fs_path || fs_path[0] == '\0') return false;

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
        Serial.printf("[VISUAL_BG] ps_malloc failed (%u bytes) for %s\n", data_size, fs_path);
        return false;
    }

    const int32_t n = f.read(buf, data_size);
    f.close();

    if (n < 0 || static_cast<uint32_t>(n) != data_size) {
        free(buf);
        Serial.printf("[VISUAL_BG] read incomplete: got %d / %u bytes\n", n, data_size);
        return false;
    }

    out_buf           = buf;
    out_dsc.header    = hdr;
    out_dsc.data_size = data_size;
    out_dsc.data      = buf;

    Serial.printf("[VISUAL_BG] preloaded %s -> PSRAM %u bytes\n", fs_path, data_size);
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout builders / Билдеры разметки
// ─────────────────────────────────────────────────────────────────────────────

void LvglVisualPage::create_background(LvglVisualPage& self) {
    if (!self._screen) return;

    const uint16_t W = LV_ACTIVE_PROFILE.width;
    const uint16_t H = LV_ACTIVE_PROFILE.height;

    self._bg_img = lv_img_create(self._screen);
    if (!self._bg_img) return;

    lv_obj_add_flag(self._bg_img, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(self._bg_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(self._bg_img, W, H);

    self._applyBackgroundImage();
}

void LvglVisualPage::create_overlay_layer(LvglVisualPage& self) {
    if (!self._screen) return;
    if (!kBeocordVuAssetPack.segment_mask) return;

    const lv_coord_t W = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width);
    const lv_coord_t H = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.height);

    self._overlay_layer = lv_obj_create(self._screen);
    if (!self._overlay_layer) return;

    lv_obj_add_flag(self._overlay_layer, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(self._overlay_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(self._overlay_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(self._overlay_layer, W, H);
    lv_obj_set_style_bg_opa(self._overlay_layer, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(self._overlay_layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(self._overlay_layer, 0, LV_PART_MAIN);

    self._syncOverlayLayout();
}

void LvglVisualPage::create_segment_grid(LvglVisualPage& self) {
    if (!self._overlay_layer) return;

    const BeocordVuAssetPack& pack = kBeocordVuAssetPack;
    if (!pack.segment_mask) return;

    for (uint8_t ch = 0u; ch < kBeocordChannelCount; ++ch) {
        for (uint8_t seg = 0u; seg < kBeocordSegmentsPerChannel; ++seg) {
            const lv_area_t& rect = pack.overlay_rect[ch][seg];
            lv_obj_t* img = lv_img_create(self._overlay_layer);
            if (!img) continue;

            init_segment_img(img, rect, pack.segment_mask, diagnostic_recolor_for_segment(seg));
            self._segment_img[ch][seg] = img;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Background methods / Методы фона
// ─────────────────────────────────────────────────────────────────────────────

bool LvglVisualPage::_loadBackgroundFromLittlefs() {
    if (_bg_loaded && _bg_psram_buf != nullptr) return true;

    const BeocordVuAssetPack& pack = kBeocordVuAssetPack;
    if (!pack.background_path) return false;

    if (!LittleFS.exists(pack.background_path)) {
        Serial.printf("[VISUAL_BG] missing %s\n", pack.background_path);
        return false;
    }

    uint8_t* buf = nullptr;
    lv_img_dsc_t dsc = {};
    if (!bg_load_into_psram(pack.background_path, buf, dsc)) {
        return false;
    }

    _bg_psram_buf  = buf;
    _bg_psram_dsc  = dsc;
    _bg_loaded     = true;
    return true;
}

void LvglVisualPage::_applyBackgroundImage() {
    if (!_bg_img) return;

    if (_loadBackgroundFromLittlefs()) {
        lv_img_set_src(_bg_img, &_bg_psram_dsc);
        lv_obj_clear_flag(_bg_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_bg_img, LV_OBJ_FLAG_HIDDEN);
    }

    _syncBackgroundLayout();
}

void LvglVisualPage::_syncBackgroundLayout() {
    // Full-bleed under frame_padding — same compensation as Main _syncBgImgLayout().
    // Полноэкранный фон под frame_padding — та же компенсация, что у Main _syncBgImgLayout().
    if (!_screen || !_bg_img) return;

    const lv_coord_t W = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width);
    const lv_coord_t H = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.height);
    const lv_coord_t frame_pad = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);

    lv_obj_set_size(_bg_img, W, H);
    lv_obj_set_pos(_bg_img, -frame_pad, -frame_pad);
}

void LvglVisualPage::_syncOverlayLayout() {
    // Same full-bleed canvas origin as background (frame_padding compensation).
    // Тот же origin холста 480×480, что и у фона (компенсация frame_padding).
    if (!_screen || !_overlay_layer) return;

    const lv_coord_t W = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width);
    const lv_coord_t H = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.height);
    const lv_coord_t frame_pad = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);

    lv_obj_set_size(_overlay_layer, W, H);
    lv_obj_set_pos(_overlay_layer, -frame_pad, -frame_pad);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle / Жизненный цикл
// ─────────────────────────────────────────────────────────────────────────────

ScreenType LvglVisualPage::screenType() const {
    return ScreenType::Page;
}

void LvglVisualPage::create() {
    if (_screen) return;

    const uint16_t W = LV_ACTIVE_PROFILE.width;
    const uint16_t H = LV_ACTIVE_PROFILE.height;
    const int32_t pad = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, pad, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_background(*this);
    create_overlay_layer(*this);
    create_segment_grid(*this);
    installCarouselGesturesOnPageRoot(_screen);
}

void LvglVisualPage::enter() {}

void LvglVisualPage::update() {}

void LvglVisualPage::exit() {}

void LvglVisualPage::liveReapplyTheme() {
    // Museum artwork and diagnostic segments stay unchanged; only root fallback bg may track theme.
    // Музейный арт и диагностические сегменты не меняются; только fallback-фон корня.
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_invalidate(_screen);
}

void LvglVisualPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandlesAndFreeNonLvgl();
}

void LvglVisualPage::prepareForAutoDelete() {
    // E3: no lv_timer or other non-LVGL resources to stop.
    // E3: нет таймеров или других ресурсов вне дерева LVGL.
}

void LvglVisualPage::releaseAfterAutoDelete() {
    // W2F: LVGL already deleted the screen tree — never lv_obj_del here.
    // W2F: дерево уже удалено LVGL — lv_obj_del здесь не вызываем.
    _nullHandlesAndFreeNonLvgl();
}

void LvglVisualPage::_nullHandlesAndFreeNonLvgl() {
    _screen = nullptr;
    _bg_img = nullptr;
    _overlay_layer = nullptr;
    for (uint8_t ch = 0u; ch < kBeocordChannelCount; ++ch) {
        for (uint8_t seg = 0u; seg < kBeocordSegmentsPerChannel; ++seg) {
            _segment_img[ch][seg] = nullptr;
        }
    }

    if (_bg_psram_buf) {
        free(_bg_psram_buf);
        _bg_psram_buf = nullptr;
    }
    _bg_psram_dsc = {};
    _bg_loaded = false;
}

lv_obj_t* LvglVisualPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
