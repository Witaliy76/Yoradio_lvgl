/*
 * LvglVisualPage — Visual carousel page E5B: Beocord museum background + real PCM hybrid PPM.
 * LvglVisualPage — страница Visual E5B: музейный фон Beocord + гибридная PPM от реального PCM.
 *
 * - ILvglScreen lifecycle via PageChain (W2F auto-delete on carousel switch).
 * - DspTask-only lv_*; E5B: pre-Gain PCM via ppm_pcm_level + accepted E4 ballistics.
 */

#include "scr_visual.h"

#include "beocord_vu_asset_pack.h"

#include "lvgl.h"
#include "Arduino.h"
#include <cmath>
#include <cstring>
#include <LittleFS.h>

#include "../../core/config.h"
#include "../../core/ppm_pcm_level.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "lvgl_ui.h"

namespace lvgl_ui {

namespace {

// E3/E4: fixed museum segment colors (not yoradio_palette()).
// E3/E4: фиксированные музейные цвета сегментов (не из темы).
static const lv_color_t kBeocordMuseumGreen = lv_color_hex(0x59F08A);
static const lv_color_t kBeocordMuseumRed   = lv_color_hex(0xF06868);

static constexpr uint8_t kBeocordChannelCount         = 2u;
static constexpr uint8_t kBeocordSegmentsPerChannel = 8u;
static constexpr uint8_t kBeocordGreenSegmentCount  = 5u; // indices 0..4 green, 5..7 red

// E4/E5B: PPM scale, ballistics and fixed hybrid calibration (shared all stations).
// E4/E5B: шкала PPM, баллистика и фиксированная гибридная калибровка (все станции).
static constexpr float kPpmThresholdDb[8] = {
    -20.0f, -8.0f, -3.0f, -1.0f, 0.0f, 1.0f, 2.0f, 5.0f,
};
static constexpr float    kPpmOffDb                    = -60.0f;
static constexpr float    kPpmTargetMaxDb              = 5.0f;
static constexpr uint32_t kPpmTimerPeriodMs            = 30u;
static constexpr uint32_t kPpmPeakHoldMs                = 120u;
static constexpr float    kPpmReleaseDbPerSecond       = 12.0f;
static constexpr uint32_t kPpmMaxTickDtMs              = 100u;
static constexpr uint32_t kPpmPcmStaleMs               = 300u;

static constexpr float kPpmRmsCalibrationDb            = 14.0f;
static constexpr float kPpmTransientCrestFloorDb       = 10.0f;
static constexpr float kPpmTransientBoostMaxDb         = 4.0f;
static constexpr float kPpmActivityGain                = 1.5f;
static constexpr float kPpmActivityDownLimitDb           = 2.0f;
static constexpr float kPpmActivityUpLimitDb           = 1.0f;

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

static lv_color_t segment_recolor_for_index(uint8_t seg_index) {
    return (seg_index < kBeocordGreenSegmentCount) ? kBeocordMuseumGreen : kBeocordMuseumRed;
}

// E4: map displayed dB to contiguous lit segment count 0..8.
// E4: отображаемый dB → число подсвеченных сегментов 0..8 слева направо.
static uint8_t db_to_segment_count(float db) {
    if (db < kPpmThresholdDb[0]) return 0u;

    uint8_t count = 0u;
    for (uint8_t i = 0u; i < kBeocordSegmentsPerChannel; ++i) {
        if (db >= kPpmThresholdDb[i]) count = static_cast<uint8_t>(i + 1u);
    }
    return count;
}

// E4: attack / hold / release for one channel (L and R are independent).
// E4: attack / hold / release для одного канала (L и R независимы).
static void update_channel_ballistics(PpmChannelState& state, float target_db, uint32_t dt_ms) {
    if (target_db >= state.displayed_db) {
        state.displayed_db      = target_db;
        state.hold_remaining_ms = kPpmPeakHoldMs;
        return;
    }

    if (state.hold_remaining_ms > 0u) {
        if (state.hold_remaining_ms > dt_ms) {
            state.hold_remaining_ms -= dt_ms;
        } else {
            state.hold_remaining_ms = 0u;
        }
        return;
    }

    const float dt_s = static_cast<float>(dt_ms) / 1000.0f;
    state.displayed_db -= kPpmReleaseDbPerSecond * dt_s;
    if (state.displayed_db < target_db) state.displayed_db = target_db;
    if (state.displayed_db < kPpmOffDb) state.displayed_db = kPpmOffDb;
}

static float clamp_measure_dbfs(float db) {
    if (db < -120.0f) return -120.0f;
    if (db > 0.0f) return 0.0f;
    return db;
}

static float clamp_target_db(float db) {
    if (db < kPpmOffDb) return kPpmOffDb;
    if (db > kPpmTargetMaxDb) return kPpmTargetMaxDb;
    return db;
}

static float peak_abs_to_dbfs(uint16_t peak_abs) {
    if (peak_abs == 0u) return -120.0f;
    return clamp_measure_dbfs(20.0f * log10f(static_cast<float>(peak_abs) / 32768.0f));
}

static float rms_to_dbfs(uint64_t sum_squares, uint32_t frames) {
    if (frames == 0u || sum_squares == 0u) return -120.0f;
    const double rms = sqrt(static_cast<double>(sum_squares) / static_cast<double>(frames));
    return clamp_measure_dbfs(20.0f * log10f(static_cast<float>(rms / 32768.0)));
}

// E5B-R1: slow RMS anchor + bounded fast RMS activity + transient crest lift.
// E5B-R1: slow RMS anchor + ограниченная fast RMS activity + transient crest lift.
static float hybrid_target_db(uint16_t short_peak,
                              uint64_t slow_rms_sum_squares,
                              uint32_t slow_rms_frames,
                              uint64_t fast_rms_sum_squares,
                              uint32_t fast_rms_frames) {
    if (slow_rms_frames == 0u) return kPpmOffDb;

    const float slow_rms_dbfs = rms_to_dbfs(slow_rms_sum_squares, slow_rms_frames);
    const float fast_rms_dbfs = rms_to_dbfs(fast_rms_sum_squares, fast_rms_frames);
    const float peak_dbfs = peak_abs_to_dbfs(short_peak);
    const float body_db = slow_rms_dbfs + kPpmRmsCalibrationDb;

    float activity_db = 0.0f;
    const bool slow_silent = (slow_rms_sum_squares == 0u);
    const bool fast_silent = (fast_rms_frames == 0u || fast_rms_sum_squares == 0u);
    if (!slow_silent && !fast_silent) {
        const float activity_delta_db = fast_rms_dbfs - slow_rms_dbfs;
        activity_db = activity_delta_db * kPpmActivityGain;
        if (activity_db < -kPpmActivityDownLimitDb) activity_db = -kPpmActivityDownLimitDb;
        if (activity_db > kPpmActivityUpLimitDb) activity_db = kPpmActivityUpLimitDb;
    }

    const float crest_db = peak_dbfs - slow_rms_dbfs;
    float transient_boost_db = crest_db - kPpmTransientCrestFloorDb;
    if (transient_boost_db < 0.0f) transient_boost_db = 0.0f;
    if (transient_boost_db > kPpmTransientBoostMaxDb) transient_boost_db = kPpmTransientBoostMaxDb;

    return clamp_target_db(body_db + activity_db + transient_boost_db);
}

// ─────────────────────────────────────────────────────────────────────────────
// Background helpers / Вспомогательные функции фона
// ─────────────────────────────────────────────────────────────────────────────

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

            init_segment_img(img, rect, pack.segment_mask, segment_recolor_for_index(seg));
            lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
            self._segment_img[ch][seg] = img;
        }
    }
}

void LvglVisualPage::create_ppm_timer(LvglVisualPage& self) {
    if (!kBeocordVuAssetPack.segment_mask) return;
    if (self._ppm_timer) return;

    self._ppm_timer = lv_timer_create(_ppmTimerCallback, kPpmTimerPeriodMs, &self);
    if (!self._ppm_timer) return;

    lv_timer_pause(self._ppm_timer);
}

// ─────────────────────────────────────────────────────────────────────────────
// E4 PPM timer + rendering / E4 таймер PPM и отрисовка видимости
// ─────────────────────────────────────────────────────────────────────────────

void LvglVisualPage::_ppmTimerCallback(lv_timer_t* timer) {
    if (!timer) return;
    auto* self = static_cast<LvglVisualPage*>(timer->user_data);
    if (self) self->_onPpmTimerTick();
}

void LvglVisualPage::_renderChannel(uint8_t channel, uint8_t count) {
    if (channel >= kBeocordChannelCount) return;
    if (count > kBeocordSegmentsPerChannel) count = kBeocordSegmentsPerChannel;
    if (count == _rendered_count[channel]) return;

    _rendered_count[channel] = count;
    for (uint8_t seg = 0u; seg < kBeocordSegmentsPerChannel; ++seg) {
        lv_obj_t* img = _segment_img[channel][seg];
        if (!img) continue;

        if (seg < count) {
            lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void LvglVisualPage::_onPpmTimerTick() {
    const uint32_t now = lv_tick_get();
    uint32_t dt_ms = lv_tick_elaps(_last_timer_tick);
    _last_timer_tick = now;
    if (dt_ms > kPpmMaxTickDtMs) dt_ms = kPpmMaxTickDtMs;

    _handleStationChange(static_cast<int>(config.lastStation()));

    PpmPcmLevelSnapshot snap;
    if (ppmPcmLevelReadSnapshot(snap) && snap.valid && snap.block_id != _last_pcm_block_id) {
        _last_pcm_block_id = snap.block_id;
        _last_pcm_seen_tick = now;
        _last_pcm_sample_rate = snap.sample_rate_hz;
        _pcm_target_db[0] = hybrid_target_db(
            snap.short_peak_left,
            snap.rms_sum_squares_left,
            snap.rms_frame_count,
            snap.fast_rms_sum_squares_left,
            snap.fast_rms_frame_count);
        _pcm_target_db[1] = hybrid_target_db(
            snap.short_peak_right,
            snap.rms_sum_squares_right,
            snap.rms_frame_count,
            snap.fast_rms_sum_squares_right,
            snap.fast_rms_frame_count);
    }

    float target_l = _pcm_target_db[0];
    float target_r = _pcm_target_db[1];
    if (_last_pcm_seen_tick == 0u || lv_tick_elaps(_last_pcm_seen_tick) > kPpmPcmStaleMs) {
        target_l = kPpmOffDb;
        target_r = kPpmOffDb;
    }

    update_channel_ballistics(_ppm_state[0], target_l, dt_ms);
    update_channel_ballistics(_ppm_state[1], target_r, dt_ms);

    _renderChannel(0u, db_to_segment_count(_ppm_state[0].displayed_db));
    _renderChannel(1u, db_to_segment_count(_ppm_state[1].displayed_db));
}

void LvglVisualPage::_handleStationChange(int station_id) {
    if (station_id == _last_station_id) return;

    ppmPcmLevelRequestReset();
    _last_station_id = station_id;
    _last_pcm_block_id = 0u;
    _last_pcm_seen_tick = 0u;
    _last_pcm_sample_rate = 0u;
    _pcm_target_db[0] = kPpmOffDb;
    _pcm_target_db[1] = kPpmOffDb;
    _resetPpmState();
}

void LvglVisualPage::_resetPpmState() {
    for (uint8_t ch = 0u; ch < kBeocordChannelCount; ++ch) {
        _ppm_state[ch].displayed_db      = kPpmOffDb;
        _ppm_state[ch].hold_remaining_ms = 0u;
        _rendered_count[ch]              = 0u;
    }

    _last_timer_tick = lv_tick_get();

    for (uint8_t ch = 0u; ch < kBeocordChannelCount; ++ch) {
        for (uint8_t seg = 0u; seg < kBeocordSegmentsPerChannel; ++seg) {
            if (_segment_img[ch][seg]) {
                lv_obj_add_flag(_segment_img[ch][seg], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void LvglVisualPage::_deletePpmTimer() {
    if (_ppm_timer) {
        lv_timer_del(_ppm_timer);
        _ppm_timer = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Background methods / Методы фона
// ─────────────────────────────────────────────────────────────────────────────

bool LvglVisualPage::_loadBackgroundFromLittlefs() {
    if (!kBeocordVuAssetPack.segment_mask) return false;

    const ThemePreset active_theme = yoradio_theme_active_preset();
    if (_bg_loaded && _bg_psram_buf != nullptr && _loaded_bg_theme == active_theme) {
        return true;
    }

    _releaseBackgroundBuffer();

    const char* fs_path = beocord_background_path_for_preset(active_theme);
    if (!fs_path || fs_path[0] == '\0') return false;

    if (!LittleFS.exists(fs_path)) {
        Serial.printf("[VISUAL_BG] missing %s\n", fs_path);
        return false;
    }

    uint8_t* buf = nullptr;
    lv_img_dsc_t dsc = {};
    if (!bg_load_into_psram(fs_path, buf, dsc)) {
        return false;
    }

    _bg_psram_buf   = buf;
    _bg_psram_dsc   = dsc;
    _bg_loaded      = true;
    _loaded_bg_theme = active_theme;
    return true;
}

void LvglVisualPage::_releaseBackgroundBuffer() {
    if (_bg_psram_buf) {
        free(_bg_psram_buf);
        _bg_psram_buf = nullptr;
    }
    _bg_psram_dsc = {};
    _bg_loaded = false;
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
    if (!_screen || !_bg_img) return;

    const lv_coord_t W = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width);
    const lv_coord_t H = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.height);
    const lv_coord_t frame_pad = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);

    lv_obj_set_size(_bg_img, W, H);
    lv_obj_set_pos(_bg_img, -frame_pad, -frame_pad);
}

void LvglVisualPage::_syncOverlayLayout() {
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
    create_ppm_timer(*this);
    _resetPpmState();
    installCarouselGesturesOnPageRoot(_screen);
}

void LvglVisualPage::enter() {
    const int station_id = static_cast<int>(config.lastStation());
    if (!_pcm_source_enabled) {
        ppmPcmLevelSetEnabled(true);
        ppmPcmLevelRequestReset();
        _pcm_source_enabled = true;
        _last_station_id = station_id;
        _last_pcm_block_id = 0u;
        _last_pcm_seen_tick = 0u;
        _last_pcm_sample_rate = 0u;
        _pcm_target_db[0] = kPpmOffDb;
        _pcm_target_db[1] = kPpmOffDb;
        _resetPpmState();
    } else if (station_id != _last_station_id) {
        _handleStationChange(station_id);
    }

    _last_timer_tick = lv_tick_get();
    if (_ppm_timer) lv_timer_resume(_ppm_timer);
    _applyBackgroundImage();
}

void LvglVisualPage::update() {}

void LvglVisualPage::exit() {
    if (_ppm_timer) lv_timer_pause(_ppm_timer);
}

void LvglVisualPage::liveReapplyTheme() {
    // E6B: museum background follows active theme; segment colors stay fixed museum green/red.
    // E6B: музейный фон по активной теме; цвета сегментов — фиксированные museum green/red.
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);

    const ThemePreset active_theme = yoradio_theme_active_preset();
    if (!_bg_loaded || _loaded_bg_theme != active_theme) {
        _applyBackgroundImage();
    } else {
        lv_obj_invalidate(_screen);
    }
}

void LvglVisualPage::destroy() {
    _deletePpmTimer();
    ppmPcmLevelSetEnabled(false);
    _pcm_source_enabled = false;
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandlesAndFreeNonLvgl();
}

void LvglVisualPage::prepareForAutoDelete() {
    _deletePpmTimer();
    ppmPcmLevelSetEnabled(false);
    _pcm_source_enabled = false;
}

void LvglVisualPage::releaseAfterAutoDelete() {
    _nullHandlesAndFreeNonLvgl();
}

void LvglVisualPage::_nullHandlesAndFreeNonLvgl() {
    _screen = nullptr;
    _bg_img = nullptr;
    _overlay_layer = nullptr;
    _ppm_timer = nullptr;

    for (uint8_t ch = 0u; ch < kBeocordChannelCount; ++ch) {
        for (uint8_t seg = 0u; seg < kBeocordSegmentsPerChannel; ++seg) {
            _segment_img[ch][seg] = nullptr;
        }
        _ppm_state[ch] = {};
        _rendered_count[ch] = 0u;
    }

    _last_timer_tick           = 0u;
    _last_pcm_block_id         = 0u;
    _last_pcm_seen_tick        = 0u;
    _last_pcm_sample_rate      = 0u;
    _last_station_id           = -1;
    _pcm_source_enabled        = false;
    _pcm_target_db[0]          = kPpmOffDb;
    _pcm_target_db[1]          = kPpmOffDb;

    _releaseBackgroundBuffer();
    _loaded_bg_theme = ThemePreset::Dark;
}

lv_obj_t* LvglVisualPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
