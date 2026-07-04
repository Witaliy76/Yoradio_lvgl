/*
 * LvglVisualPage — Visual carousel page E1: static Beocord 9000 museum background.
 * LvglVisualPage — страница Visual E1: статичный музейный фон Beocord 9000.
 *
 * - ILvglScreen lifecycle via PageChain (W2F auto-delete on carousel switch).
 * - DspTask-only lv_*; background loaded once in create() into Visual-owned PSRAM.
 * - Museum artwork is theme-independent; fallback uses yoradio_palette().device_background.
 *
 * E1 scope: background only — no segments, timers, ballistics, or metadata.
 * E1: только фон — без сегментов, таймеров, баллистики и метаданных.
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
    installCarouselGesturesOnPageRoot(_screen);
}

void LvglVisualPage::enter() {}

void LvglVisualPage::update() {}

void LvglVisualPage::exit() {}

void LvglVisualPage::liveReapplyTheme() {
    // Museum artwork stays unchanged; only root fallback background may track theme.
    // Музейный арт не меняется; только fallback-фон корня может следовать теме.
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
    // E1: no lv_timer or other non-LVGL resources to stop.
    // E1: нет таймеров или других ресурсов вне дерева LVGL.
}

void LvglVisualPage::releaseAfterAutoDelete() {
    // W2F: LVGL already deleted the screen tree — never lv_obj_del here.
    // W2F: дерево уже удалено LVGL — lv_obj_del здесь не вызываем.
    _nullHandlesAndFreeNonLvgl();
}

void LvglVisualPage::_nullHandlesAndFreeNonLvgl() {
    _screen = nullptr;
    _bg_img = nullptr;

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
