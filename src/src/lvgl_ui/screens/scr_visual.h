#ifndef SCR_VISUAL_H
#define SCR_VISUAL_H

#include <cstdint>

#include "../lv_screen.h"
#include "lvgl.h"

namespace lvgl_ui {

// E4: per-channel PPM ballistics state (synthetic targets only in this stage).
// E4: состояние PPM-баллистики на канал (только синтетические targets на этом этапе).
struct PpmChannelState {
    float    displayed_db;
    uint32_t hold_remaining_ms;
};

// Visual Page E4 — Beocord museum background + synthetic stereo PPM ballistics.
// Visual Page E4 — музейный фон Beocord + синтетическая стерео PPM-баллистика.
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
    static void create_ppm_timer(LvglVisualPage& self);

    static void _ppmTimerCallback(lv_timer_t* timer);
    void _onPpmTimerTick();
    void _resetPpmState();
    void _deletePpmTimer();
    void _renderChannel(uint8_t channel, uint8_t count);

    bool _loadBackgroundFromLittlefs();
    void _applyBackgroundImage();
    void _syncBackgroundLayout();
    void _syncOverlayLayout();

    // W2F: null LVGL handles + free Visual-owned PSRAM (never lv_obj_del on auto-delete path).
    // W2F: обнулить указатели LVGL + освободить PSRAM Visual (без lv_obj_del после auto_del).
    void _nullHandlesAndFreeNonLvgl();

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _bg_img = nullptr;
    lv_obj_t* _overlay_layer = nullptr;
    lv_obj_t* _segment_img[2][8] = {};

    PpmChannelState _ppm_state[2] = {};
    uint8_t         _rendered_count[2] = {};
    lv_timer_t*     _ppm_timer = nullptr;
    uint32_t        _last_timer_tick = 0;
    uint32_t        _synthetic_step_elapsed_ms = 0;
    uint8_t         _synthetic_step_index = 0;

    // Visual-owned PSRAM background — not shared with Main cache (E1).
    // PSRAM-фон принадлежит Visual — не общий кэш Main (E1).
    uint8_t*     _bg_psram_buf = nullptr;
    lv_img_dsc_t _bg_psram_dsc = {};
    bool         _bg_loaded = false;
};

} // namespace lvgl_ui

#endif
