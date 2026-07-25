// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef SCR_VISUAL_H
#define SCR_VISUAL_H

#include <cstdint>

#include "../../core/config.h"
#include "../lv_screen.h"
#include "../theme/lv_theme_yoradio.h"
#include "../widgets/wgt_status_line.h"
#include "lvgl.h"

namespace lvgl_ui {

// Per-channel PPM ballistics state — attack/hold/release for one VU channel (L or R).
// Состояние PPM-баллистики на канал — attack/hold/release для одного VU канала (L или R).
struct PpmChannelState {
    float    displayed_db;
    uint32_t hold_remaining_ms;
};

// Visual Page — Beocord museum VU + status chrome + metadata (station/artist/song).
// Metadata is static at full opacity; fade animation is not implemented.
// Visual Page — Beocord музейный VU + status row + метаданные (станция/артист/трек).
// Метаданные статические с полной непрозрачностью; fade анимация не реализована.
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
    static void create_status_chrome(LvglVisualPage& self, const YoRadioPalette& pal);
    static void create_metadata_layer(LvglVisualPage& self, const YoRadioPalette& pal);
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

    void _refreshMetadata(bool force);
    void _onStationIdentityChanged(int station_id);

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

    // Status chrome (status line + divider) and floating metadata layer.
    // Status chrome (status line + разделитель) и floating слой метаданных.
    wgt_status_line::Instance _status_line{};
    lv_obj_t*               _status_divider = nullptr;
    lv_obj_t*               _metadata_layer = nullptr;
    lv_obj_t*               _lbl_station    = nullptr;
    lv_obj_t*               _lbl_artist     = nullptr;
    lv_obj_t*               _lbl_song       = nullptr;

    int  _cached_station_id = -1;
    char _cached_station_name[BUFLEN] = {};
    char _cached_raw_title[BUFLEN]    = {};
    // Title snapshot at station switch — suppress stale artist/song until title changes.
    // Снимок title при смене станции — не показывать старый artist/song до нового title.
    char _title_at_station_switch[BUFLEN] = {};

    PpmChannelState _ppm_state[2] = {};
    uint8_t         _rendered_count[2] = {};
    lv_timer_t*     _ppm_timer = nullptr;
    uint32_t        _last_timer_tick = 0;

    // PCM hybrid source state — consumed exclusively in DspTask via PPM timer callback.
    // Состояние PCM hybrid source — потребляется только в DspTask через PPM timer callback.
    uint32_t _last_pcm_block_id = 0u;
    uint32_t _last_pcm_seen_tick = 0u;
    uint32_t _last_pcm_sample_rate = 0u;  // Diagnostic/future-use; currently write-only.
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
