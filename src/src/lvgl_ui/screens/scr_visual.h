#ifndef SCR_VISUAL_H
#define SCR_VISUAL_H

#include <cstdint>

#include "../lv_screen.h"
#include "lvgl.h"

namespace lvgl_ui {

// Visual Page E1+E2 — Beocord 9000 museum background + one proof segment (L3).
// Visual Page E1+E2 — музейный фон Beocord 9000 + один proof-сегмент (L3).
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
    static void create_proof_segment(LvglVisualPage& self);

    bool _loadBackgroundFromLittlefs();
    void _applyBackgroundImage();
    void _syncBackgroundLayout();
    void _syncOverlayLayout();

    // W2F: null LVGL handles + free Visual-owned PSRAM (never lv_obj_del on auto-delete path).
    // W2F: обнулить указатели LVGL + освободить PSRAM Visual (без lv_obj_del после auto_del).
    void _nullHandlesAndFreeNonLvgl();

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _bg_img = nullptr;
    lv_obj_t* _overlay_layer = nullptr; // E2: transparent 480×480 segment canvas / E2: прозрачный холст сегментов
    lv_obj_t* _proof_segment = nullptr; // E2: single lit L3 mask / E2: один подсвеченный L3

    // Visual-owned PSRAM background — not shared with Main cache (E1).
    // PSRAM-фон принадлежит Visual — не общий кэш Main (E1).
    uint8_t*     _bg_psram_buf = nullptr;
    lv_img_dsc_t _bg_psram_dsc = {};
    bool         _bg_loaded = false;
};

} // namespace lvgl_ui

#endif
