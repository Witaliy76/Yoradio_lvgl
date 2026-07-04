#ifndef SCR_VISUAL_H
#define SCR_VISUAL_H

#include <cstdint>

#include "../lv_screen.h"
#include "../theme/lv_theme_yoradio.h"
#include "lvgl.h"

namespace lvgl_ui {

// E4/E5B: per-channel PPM ballistics state (visual attack/hold/release).
// E4/E5B: состояние PPM-баллистики на канал (visual attack/hold/release).
struct PpmChannelState {
    float    displayed_db;
    uint32_t hold_remaining_ms;
};

// Visual Page E5B — Beocord museum background + real PCM hybrid PPM ballistics.
// Visual Page E5B — музейный фон Beocord + гибридная PPM от реального PCM.
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
    void _handleStationChange(int station_id);

    bool _loadBackgroundFromLittlefs();
    void _releaseBackgroundBuffer();
    void _applyBackgroundImage();
    void _syncBackgroundLayout();
    void _syncOverlayLayout();

    void _nullHandlesAndFreeNonLvgl();

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _bg_img = nullptr;
    lv_obj_t* _overlay_layer = nullptr;
    lv_obj_t* _segment_img[2][8] = {};

    PpmChannelState _ppm_state[2] = {};
    uint8_t         _rendered_count[2] = {};
    lv_timer_t*     _ppm_timer = nullptr;
    uint32_t        _last_timer_tick = 0;

    // E5B: real PCM hybrid source state (DspTask consumer only).
    // E5B: состояние гибридного PCM source (только consumer в DspTask).
    uint32_t _last_pcm_block_id = 0u;
    uint32_t _last_pcm_seen_tick = 0u;
    uint32_t _last_pcm_sample_rate = 0u;
    int      _last_station_id = -1;
    bool     _pcm_source_enabled = false;
    float    _pcm_target_db[2] = {};

    uint8_t*     _bg_psram_buf = nullptr;
    lv_img_dsc_t _bg_psram_dsc = {};
    bool         _bg_loaded = false;
    ThemePreset  _loaded_bg_theme = ThemePreset::Dark;
};

} // namespace lvgl_ui

#endif
