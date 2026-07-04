#ifndef SCR_VISUAL_H
#define SCR_VISUAL_H

#include <cstdint>

#include "../lv_screen.h"
#include "lvgl.h"

namespace lvgl_ui {

// Visual Page E3 — Beocord museum background + static 16-segment diagnostic grid.
// Visual Page E3 — музейный фон Beocord + статичная диагностическая сетка 16 сегментов.
class LvglVisualPage final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;
    void liveReapplyTheme() override;
    void prepareForAutoDelete() override;
    void releaseAfterAutoDelete() override;

private:
    static void create_background(LvglVisualPage& self);
    static void create_overlay_layer(LvglVisualPage& self);
    static void create_segment_grid(LvglVisualPage& self);

    bool _loadBackgroundFromLittlefs();
    void _applyBackgroundImage();
    void _syncBackgroundLayout();
    void _syncOverlayLayout();

    // W2F: null LVGL handles + free Visual-owned PSRAM (never lv_obj_del on auto-delete path).
    // W2F: обнулить указатели LVGL + освободить PSRAM Visual (без lv_obj_del после auto_del).
    void _nullHandlesAndFreeNonLvgl();

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _bg_img = nullptr;
    lv_obj_t* _overlay_layer = nullptr; // E2/E3: transparent 480×480 segment canvas
    lv_obj_t* _segment_img[2][8] = {};  // E3: L/R × 8 static diagnostic segments

    // Visual-owned PSRAM background — not shared with Main cache (E1).
    // PSRAM-фон принадлежит Visual — не общий кэш Main (E1).
    uint8_t*     _bg_psram_buf = nullptr;
    lv_img_dsc_t _bg_psram_dsc = {};
    bool         _bg_loaded = false;
};

} // namespace lvgl_ui

#endif
