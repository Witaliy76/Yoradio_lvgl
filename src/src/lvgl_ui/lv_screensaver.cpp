/*
 * SSCLK: LVGL screensaver overlay — production analog clock (hands + PSRAM background).
 * SSCLK: оверлей screensaver — production стрелки + фон из LittleFS/PSRAM.
 *
 * Overlay on lv_layer_top(); not ILvglScreen / not PageChain (Stage 5.6 model).
 * Оверлей на lv_layer_top(); не ILvglScreen / не PageChain (модель Stage 5.6).
 *
 * Layout: lv_screensaver_layout_tree.md
 * DspTask-only lv_* — see lv_touch_indev.cpp for wake path.
 */

#include "lv_screensaver.h"

#include "assets/ssclk_production_asset_pack.h"
#include "lvgl.h"
#include "profiles/lv_profile_select.h"
#include "theme/lv_theme_yoradio.h"
#include "../core/network.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstdint>
#include <cstring>

namespace lvgl_ui {

namespace {

// ── Handles / state ─────────────────────────────────────────────────────────
// Module-owned LVGL objects; cleared in screensaverHide().
// LVGL-объекты модуля; обнуляются в screensaverHide().

lv_obj_t* s_ss_root       = nullptr;
lv_obj_t* s_clock_layer   = nullptr;
lv_obj_t* s_background    = nullptr;
lv_obj_t* s_hand_hour     = nullptr;
lv_obj_t* s_hand_minute   = nullptr;
lv_obj_t* s_hand_second   = nullptr;
lv_obj_t* s_center_cap    = nullptr;

int16_t s_last_angle_hour   = -1;
int16_t s_last_angle_minute = -1;
int16_t s_last_angle_second = -1;

// Canonical clock center in s_clock_layer coords; set once per screensaverShow().
// Канонический центр часов в координатах s_clock_layer; задаётся при screensaverShow().
lv_coord_t s_clock_center_x = 0;
lv_coord_t s_clock_center_y = 0;

// Active theme pack for current show.
// Активный theme pack для текущего show.
const ScreensaverClockThemeAssets* s_active_assets = nullptr;

// Per-show runtime descriptor — recreated each show; points into retained cache buffer.
// Runtime-дескриптор на show — пересоздаётся; указывает в retained cache buffer.
lv_img_dsc_t s_bg_runtime_dsc = {};
bool s_bg_loaded = false;

// SSCLK-A3F-C: single retained PSRAM background (460800 B); survives hide/show until reboot.
// SSCLK-A3F-C: один retained PSRAM-фон (460800 B); живёт между hide/show до reboot.
static constexpr uint32_t kSsclkBgPixelBytes = 480u * 480u * 2u;

struct SsclkBgCache {
    char         path[64] = {};
    uint8_t*     data     = nullptr;
    lv_img_header_t hdr   = {};
    uint32_t     data_size = 0;
    bool         valid    = false;
};

static SsclkBgCache s_bg_cache;

static constexpr int16_t kCapPivotX = 8;
static constexpr int16_t kCapPivotY = 8;

// ── Pure helpers ────────────────────────────────────────────────────────────

static void resetAngleCache() {
    s_last_angle_hour   = -1;
    s_last_angle_minute = -1;
    s_last_angle_second = -1;
}

static void nullAllHandles() {
    s_ss_root         = nullptr;
    s_clock_layer     = nullptr;
    s_background      = nullptr;
    s_hand_hour       = nullptr;
    s_hand_minute     = nullptr;
    s_hand_second     = nullptr;
    s_center_cap      = nullptr;
    s_clock_center_x  = 0;
    s_clock_center_y  = 0;
    s_active_assets   = nullptr;
    s_bg_loaded       = false;
}

static bool timeIsValid() {
    return network.timeinfo.tm_year > 100;
}

static int16_t calcHourAngleTenths(const tm& t) {
    return static_cast<int16_t>((t.tm_hour % 12) * 300 + t.tm_min * 5 + t.tm_sec / 12);
}

static int16_t calcMinuteAngleTenths(const tm& t) {
    return static_cast<int16_t>(t.tm_min * 60 + t.tm_sec);
}

static int16_t calcSecondAngleTenths(const tm& t) {
    return static_cast<int16_t>(t.tm_sec * 60);
}

static void setHandAngleIfChanged(lv_obj_t* hand, int16_t& last_angle, int16_t angle) {
    if (!hand) return;
    if (last_angle == angle) return;
    lv_img_set_angle(hand, angle);
    last_angle = angle;
}

// Theme default adds "card" pad to bare lv_obj — content area inset breaks screen (0,0) origin.
// Тема добавляет card-pad к lv_obj — inset content area ломает экранный origin (0,0).
static void applyExactOverlayContainer(lv_obj_t* obj, lv_coord_t w, lv_coord_t h) {
    if (!obj) return;

    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_height(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_translate_x(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_scroll_to(obj, 0, 0, LV_ANIM_OFF);
}

static void applyRootFallbackBackground(lv_obj_t* root, const YoRadioPalette& pal) {
    if (!root) return;
    lv_obj_set_style_bg_color(root, pal.screensaver_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
}

static void applyTransparentClockLayer(lv_obj_t* layer) {
    if (!layer) return;
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, LV_PART_MAIN);
}

// Drop per-show runtime descriptor only — retained cache buffer is NOT freed.
// Сброс runtime-дескриптора show — retained cache buffer НЕ освобождается.
static void detachRuntimeBackgroundDescriptor() {
    s_bg_runtime_dsc = {};
    s_bg_loaded      = false;
}

static void ssclkBgCacheInvalidate() {
    s_bg_cache.valid = false;
}

static bool ssclkBgCacheHit(const char* fs_path) {
    if (!fs_path || fs_path[0] == '\0') return false;
    return s_bg_cache.valid &&
           s_bg_cache.data != nullptr &&
           strncmp(s_bg_cache.path, fs_path, sizeof(s_bg_cache.path) - 1) == 0;
}

static void attachRuntimeDescriptorFromCache() {
    if (!s_bg_cache.valid || !s_bg_cache.data) {
        detachRuntimeBackgroundDescriptor();
        return;
    }

    s_bg_runtime_dsc.header    = s_bg_cache.hdr;
    s_bg_runtime_dsc.data_size = s_bg_cache.data_size;
    s_bg_runtime_dsc.data      = s_bg_cache.data;
    s_bg_loaded                = true;
}

// Read LittleFS background into retained buffer (allocate once, reuse on theme change).
// Чтение фона в retained buffer (один ps_malloc, reuse при смене темы).
static bool bgCacheLoadFromLittlefs(const char* fs_path) {
    if (!fs_path || fs_path[0] == '\0') return false;

    File f = LittleFS.open(fs_path, "r");
    if (!f) {
        Serial.printf("[SSCLK] open failed: %s\n", fs_path);
        ssclkBgCacheInvalidate();
        return false;
    }

    const size_t file_sz = static_cast<size_t>(f.size());
    if (file_sz <= sizeof(lv_img_header_t)) {
        f.close();
        Serial.printf("[SSCLK] file too small: %s (%u bytes)\n", fs_path, static_cast<unsigned>(file_sz));
        ssclkBgCacheInvalidate();
        return false;
    }

    lv_img_header_t hdr;
    if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        Serial.printf("[SSCLK] header read failed: %s\n", fs_path);
        ssclkBgCacheInvalidate();
        return false;
    }

    if (hdr.cf != LV_IMG_CF_TRUE_COLOR) {
        f.close();
        Serial.printf("[SSCLK] unexpected CF=%u for %s\n", static_cast<unsigned>(hdr.cf), fs_path);
        ssclkBgCacheInvalidate();
        return false;
    }

    const uint32_t data_size = static_cast<uint32_t>(file_sz - sizeof(hdr));

    if (data_size != kSsclkBgPixelBytes) {
        f.close();
        Serial.printf("[SSCLK] unexpected payload %u bytes (expected %u) for %s\n",
                      data_size,
                      kSsclkBgPixelBytes,
                      fs_path);
        ssclkBgCacheInvalidate();
        return false;
    }

    if (!s_bg_cache.data) {
        s_bg_cache.data = static_cast<uint8_t*>(ps_malloc(data_size));
        if (!s_bg_cache.data) {
            f.close();
            Serial.printf("[SSCLK] ps_malloc failed (%u bytes) for %s\n", data_size, fs_path);
            ssclkBgCacheInvalidate();
            return false;
        }
        s_bg_cache.data_size = data_size;
    } else if (s_bg_cache.data_size != data_size) {
        f.close();
        Serial.printf("[SSCLK] cached buffer size mismatch (%u vs %u) for %s\n",
                      static_cast<unsigned>(s_bg_cache.data_size),
                      data_size,
                      fs_path);
        ssclkBgCacheInvalidate();
        return false;
    }

    const int32_t n = f.read(s_bg_cache.data, data_size);
    f.close();

    if (n < 0 || static_cast<uint32_t>(n) != data_size) {
        Serial.printf("[SSCLK] read incomplete: got %d / %u bytes for %s\n", n, data_size, fs_path);
        ssclkBgCacheInvalidate();
        return false;
    }

    s_bg_cache.hdr = hdr;
    strlcpy(s_bg_cache.path, fs_path, sizeof(s_bg_cache.path));
    s_bg_cache.valid = true;
    return true;
}

static bool loadBackgroundForActiveTheme() {
    detachRuntimeBackgroundDescriptor();

    const ThemePreset preset = yoradio_theme_active_preset();
    s_active_assets = ssclk_clock_assets_for_preset(preset);
    if (!s_active_assets || !s_active_assets->background_path) {
        Serial.println("[SSCLK] no asset pack for active theme");
        ssclkBgCacheInvalidate();
        return false;
    }

    const char* fs_path = s_active_assets->background_path;
    if (!LittleFS.exists(fs_path)) {
        Serial.printf("[SSCLK] missing %s\n", fs_path);
        ssclkBgCacheInvalidate();
        return false;
    }

    if (ssclkBgCacheHit(fs_path)) {
        attachRuntimeDescriptorFromCache();
        return true;
    }

    if (!bgCacheLoadFromLittlefs(fs_path)) {
        return false;
    }

    attachRuntimeDescriptorFromCache();
    return true;
}

// Production hand placement: pivot from asset pack, center at (240, 240) on 480×480.
// Размещение production-стрелки: pivot из pack, центр (240, 240) на 480×480.
static void setupProductionHandPlacement(lv_obj_t* hand,
                                          const lv_img_dsc_t* dsc,
                                          int16_t pivot_x,
                                          int16_t pivot_y) {
    if (!hand || !dsc) return;

    lv_img_set_src(hand, dsc);
    lv_obj_set_pos(hand,
                   s_clock_center_x - pivot_x,
                   s_clock_center_y - pivot_y);
    lv_img_set_pivot(hand, pivot_x, pivot_y);
    lv_img_set_antialias(hand, true);
    lv_img_set_angle(hand, 0);
}

static lv_obj_t* createProductionHand(lv_obj_t* parent,
                                      const lv_img_dsc_t* dsc,
                                      int16_t pivot_x,
                                      int16_t pivot_y) {
    lv_obj_t* hand = lv_img_create(parent);
    if (!hand) return nullptr;
    setupProductionHandPlacement(hand, dsc, pivot_x, pivot_y);
    return hand;
}

static void createBackgroundImage(lv_obj_t* parent) {
    s_background = lv_img_create(parent);
    if (!s_background) return;

    if (s_bg_loaded && s_bg_runtime_dsc.data != nullptr) {
        lv_img_set_src(s_background, &s_bg_runtime_dsc);
        lv_obj_clear_flag(s_background, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_background, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_pos(s_background, 0, 0);

    lv_obj_set_style_img_opa(s_background, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(s_background, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(s_background, LV_OPA_TRANSP, LV_PART_MAIN);
}

static void createCenterCap(lv_obj_t* parent, const ScreensaverClockThemeAssets* assets) {
    if (!assets || !assets->cap) return;

    s_center_cap = lv_img_create(parent);
    if (!s_center_cap) return;

    lv_img_set_src(s_center_cap, assets->cap);
    lv_obj_set_pos(s_center_cap,
                   s_clock_center_x - kCapPivotX,
                   s_clock_center_y - kCapPivotY);
    lv_img_set_pivot(s_center_cap, kCapPivotX, kCapPivotY);
    lv_img_set_antialias(s_center_cap, true);
}

static void createClockStack(lv_obj_t* root, lv_coord_t hor, lv_coord_t ver, const YoRadioPalette& pal) {
    (void)pal;

    s_clock_center_x = hor / 2;
    s_clock_center_y = ver / 2;

    s_active_assets = ssclk_clock_assets_for_preset(yoradio_theme_active_preset());
    if (!s_active_assets) return;

    s_clock_layer = lv_obj_create(root);
    if (!s_clock_layer) return;

    applyExactOverlayContainer(s_clock_layer, hor, ver);
    applyTransparentClockLayer(s_clock_layer);

    // Retained PSRAM cache — LFS read only on miss / theme change.
    // Retained PSRAM cache — LFS read только при miss / смене темы.
    loadBackgroundForActiveTheme();

    // Z-order (bottom → top): background, hour, minute, second, cap17.
    // Z-order (снизу вверх): фон, час, мин, сек, cap17.
    createBackgroundImage(s_clock_layer);

    s_hand_hour = createProductionHand(s_clock_layer,
                                       s_active_assets->hour,
                                       s_active_assets->hour_pivot_x,
                                       s_active_assets->hour_pivot_y);
    s_hand_minute = createProductionHand(s_clock_layer,
                                         s_active_assets->minute,
                                         s_active_assets->minute_pivot_x,
                                         s_active_assets->minute_pivot_y);
    s_hand_second = createProductionHand(s_clock_layer,
                                         s_active_assets->second,
                                         s_active_assets->second_pivot_x,
                                         s_active_assets->second_pivot_y);
    createCenterCap(s_clock_layer, s_active_assets);
}

static void createScreensaverTree(lv_disp_t* disp, const YoRadioPalette& pal) {
    const lv_coord_t hor = lv_disp_get_hor_res(disp);
    const lv_coord_t ver = lv_disp_get_ver_res(disp);

    lv_obj_t* top = lv_layer_top();
    if (!top) return;

    s_ss_root = lv_obj_create(top);
    if (!s_ss_root) return;

    applyExactOverlayContainer(s_ss_root, hor, ver);
    applyRootFallbackBackground(s_ss_root, pal);
    lv_obj_clear_flag(s_ss_root, LV_OBJ_FLAG_SCROLL_CHAIN);

    createClockStack(s_ss_root, hor, ver, pal);

    if (s_ss_root) {
        lv_obj_move_foreground(s_ss_root);
    }
}

// ── Angle / update helper ───────────────────────────────────────────────────

static void applyHandAnglesFromTimeinfo() {
    if (!s_hand_hour && !s_hand_minute && !s_hand_second) return;

    if (!timeIsValid()) {
        setHandAngleIfChanged(s_hand_hour, s_last_angle_hour, 0);
        setHandAngleIfChanged(s_hand_minute, s_last_angle_minute, 0);
        setHandAngleIfChanged(s_hand_second, s_last_angle_second, 0);
        return;
    }

    const tm& t = network.timeinfo;
    setHandAngleIfChanged(s_hand_hour, s_last_angle_hour, calcHourAngleTenths(t));
    setHandAngleIfChanged(s_hand_minute, s_last_angle_minute, calcMinuteAngleTenths(t));
    setHandAngleIfChanged(s_hand_second, s_last_angle_second, calcSecondAngleTenths(t));
}

} // namespace

bool screensaverIsVisible() {
    return s_ss_root != nullptr;
}

void screensaverHide() {
    // 1) Drop LVGL tree while retained cache buffer stays valid (no lv_img refs).
    // 1) Удалить LVGL-дерево; retained cache buffer остаётся (нет ссылок lv_img).
    if (s_ss_root) {
        lv_obj_del(s_ss_root);
    }

    detachRuntimeBackgroundDescriptor();

    nullAllHandles();
    resetAngleCache();
}

void screensaverRefreshClock() {
    applyHandAnglesFromTimeinfo();
}

void screensaverShow() {
    lv_disp_t* disp = lv_disp_get_default();
    if (!disp) return;

    screensaverHide();

    const YoRadioPalette& pal = yoradio_palette();
    createScreensaverTree(disp, pal);
    applyHandAnglesFromTimeinfo();
}

} // namespace lvgl_ui
