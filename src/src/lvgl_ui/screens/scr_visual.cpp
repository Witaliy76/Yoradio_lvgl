/*
 * LvglVisualPage — Visual carousel page: Beocord museum VU + status chrome + static metadata.
 * LvglVisualPage — страница Visual: Beocord VU + status row + статические метаданные.
 *
 * Layout: background (PSRAM LittleFS) + overlay (segment grid 2×8) + metadata layer +
 *         status chrome. Two independent update cadences:
 *   PPM timer: 30 ms (LVGL timer, DspTask) — segment visibility.
 *   page update: Display loop cadence — status line + metadata.
 *
 * DspTask-only lv_*; PCM PPM via hybrid RMS/peak; ILvglScreen lifecycle via PageChain.
 * PageChain auto-delete on carousel switch; PSRAM freed in releaseAfterAutoDelete().
 *
 * Макет: фон (PSRAM + LittleFS) + overlay (2×8 сегментов) + слой метаданных + status chrome.
 * Два независимых cadence: PPM таймер 30 мс (сегменты), Display loop (status + metadata).
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_visual.h"

#include "beocord_vu_asset_pack.h"

#include "lvgl.h"
#include "Arduino.h"
#include <cmath>
#include <cstring>
#include <LittleFS.h>

#include "../../core/config.h"
#include "../../core/player.h"
#include "../../core/ppm_pcm_level.h"
#include "../fonts/lv_fonts.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../widgets/wgt_status_line.h"
#include "lvgl_ui.h"

namespace lvgl_ui {

namespace {

// Fixed museum segment colors — not from yoradio_palette(); remain constant across all themes.
// Фиксированные музейные цвета сегментов — не из темы; постоянны при любой теме.
static const lv_color_t kBeocordMuseumGreen = lv_color_hex(0x59F08A);
static const lv_color_t kBeocordMuseumRed   = lv_color_hex(0xF06868);

static constexpr uint8_t kBeocordChannelCount         = 2u;
static constexpr uint8_t kBeocordSegmentsPerChannel = 8u;
static constexpr uint8_t kBeocordGreenSegmentCount  = 5u; // indices 0..4 green, 5..7 red

// PPM scale, ballistics and hybrid calibration — shared by all stations, not configurable at runtime.
// Шкала PPM, баллистика и гибридная калибровка — общие для всех станций, не меняются в рантайме.
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

// Metadata geometry — screen-centered artist/song labels; small optical x offset for station name.
// Геометрия метаданных — artist/song по центру экрана; небольшая оптическая поправка x для station.
static constexpr lv_coord_t kMetaStationX = 19;
static constexpr lv_coord_t kMetaStationY = 74;
static constexpr lv_coord_t kMetaStationW = 432;
static constexpr lv_coord_t kMetaStationH = 28;
static constexpr lv_coord_t kMetaArtistX  = 40;
static constexpr lv_coord_t kMetaArtistY  = 352;
static constexpr lv_coord_t kMetaArtistW  = 400;
static constexpr lv_coord_t kMetaArtistH  = 28;
static constexpr lv_coord_t kMetaSongX    = 40;
static constexpr lv_coord_t kMetaSongY    = 384;
static constexpr lv_coord_t kMetaSongW    = 400;
static constexpr lv_coord_t kMetaSongH    = 22;

static void visual_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void visual_set_text_if_changed(lv_obj_t* lbl, const char* s) {
    if (!lbl) return;
    if (!s) s = "";
    const char* cur = lv_label_get_text(lbl);
    if (cur != nullptr && strcmp(cur, s) == 0) return;
    lv_label_set_text(lbl, s);
}

// In-place split for mutable copy of title (Main UI pattern). / Разбор копии title как на Main.
static char* visual_split_inplace_at(char* str, const char* sep) {
    if (!str || !sep) return nullptr;
    char* p = strstr(str, sep);
    if (!p) return nullptr;
    *p = '\0';
    return p + strlen(sep);
}

// Transport / service titles must not appear as artist/song. / Служебные строки не в artist/song.
static bool visual_is_transport_title(const char* title) {
    if (!title || title[0] == '\0') return true;
    if (strstr(title, "[соединение]") != nullptr) return true;
    if (strstr(title, "[connecting]") != nullptr) return true;
    if (strstr(title, "(connection)") != nullptr) return true;
    if (strstr(title, "[готов]") != nullptr) return true;
    if (strstr(title, "[ready]") != nullptr) return true;
    if (strstr(title, "[остановлено]") != nullptr) return true;
    if (strstr(title, "[stopped]") != nullptr) return true;
    if (strstr(title, "timeout") != nullptr) return true;
    return false;
}

static void visual_style_metadata_label(lv_obj_t* lbl,
                                        lv_coord_t x,
                                        lv_coord_t y,
                                        lv_coord_t w,
                                        lv_coord_t h,
                                        const void* font_slot,
                                        lv_color_t color,
                                        lv_text_align_t align) {
    if (!lbl) return;
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_size(lbl, w, h);
    visual_set_font(lbl, font_slot);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, align, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(lbl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
}

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

// Map displayed dB to contiguous lit segment count 0..8 (left-to-right).
// Перевод отображаемого dB в количество подсвеченных сегментов 0..8 (слева направо).
static uint8_t db_to_segment_count(float db) {
    if (db < kPpmThresholdDb[0]) return 0u;

    uint8_t count = 0u;
    for (uint8_t i = 0u; i < kBeocordSegmentsPerChannel; ++i) {
        if (db >= kPpmThresholdDb[i]) count = static_cast<uint8_t>(i + 1u);
    }
    return count;
}

// Attack / hold / release for one channel; L and R are fully independent.
// Attack / hold / release для одного канала; L и R полностью независимы.
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

// Hybrid target: slow RMS anchor + bounded fast-RMS activity offset + transient crest lift.
// Гибридный target: slow RMS база + ограниченный fast-RMS offset + transient crest lift.
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

// ─────────────────────────────────────────────────────────────────────────────
// Layout factory helpers / Вспомогательные фабрики layout
// ─────────────────────────────────────────────────────────────────────────────

// 1 px status divider — matches Main status-divider geometry.
// Theme default adds "card" padding to bare lv_obj; zeroed so the line aligns with content width.
// 1 px разделитель — соответствует геометрии status divider на Main.
// Тема LVGL добавляет "card" padding к lv_obj; обнуляем, чтобы линия совпадала с шириной контента.
static lv_obj_t* visual_create_status_divider(lv_obj_t* parent, const YoRadioPalette& pal) {
    lv_obj_t* status_divider = lv_obj_create(parent);
    if (!status_divider) return nullptr;
    lv_obj_set_width(status_divider, LV_PCT(100));
    lv_obj_set_height(status_divider, 1);
    lv_obj_set_style_bg_color(status_divider, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(status_divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(status_divider, 0, LV_PART_MAIN);
    lv_obj_clear_flag(status_divider, LV_OBJ_FLAG_SCROLLABLE);
    return status_divider;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout builders / Билдеры разметки
// ─────────────────────────────────────────────────────────────────────────────

void LvglVisualPage::create_status_chrome(LvglVisualPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;

    if (!wgt_status_line::create(self._screen, self._status_line)) {
        return;
    }

    self._status_divider = visual_create_status_divider(self._screen, pal);
}

void LvglVisualPage::create_metadata_layer(LvglVisualPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;

    const uint16_t W = LV_ACTIVE_PROFILE.width;
    const uint16_t H = LV_ACTIVE_PROFILE.height;

    self._metadata_layer = lv_obj_create(self._screen);
    if (!self._metadata_layer) return;

    // Coordinate-neutral absolute layer — panel coords 0…480; not inside status flex column.
    // Нейтральный слой координат — абсолютные координаты панели; вне flex status row.
    lv_obj_add_flag(self._metadata_layer, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(self._metadata_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(self._metadata_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(self._metadata_layer, 0, 0);
    lv_obj_set_size(self._metadata_layer, static_cast<lv_coord_t>(W), static_cast<lv_coord_t>(H));
    lv_obj_set_style_bg_opa(self._metadata_layer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(self._metadata_layer, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(self._metadata_layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(self._metadata_layer, 0, LV_PART_MAIN);
    // Metadata is currently static at full opacity; animation is not implemented.
    // Метаданные сейчас со статической полной непрозрачностью; анимация не реализована.
    lv_obj_set_style_opa(self._metadata_layer, LV_OPA_COVER, LV_PART_MAIN);

    self._lbl_station = lv_label_create(self._metadata_layer);
    if (self._lbl_station) {
        visual_style_metadata_label(
            self._lbl_station,
            kMetaStationX,
            kMetaStationY,
            kMetaStationW,
            kMetaStationH,
            reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr),
            pal.text_secondary,
            LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(self._lbl_station, "");
    }

    self._lbl_artist = lv_label_create(self._metadata_layer);
    if (self._lbl_artist) {
        visual_style_metadata_label(
            self._lbl_artist,
            kMetaArtistX,
            kMetaArtistY,
            kMetaArtistW,
            kMetaArtistH,
            reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr),
            pal.text_secondary,
            LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(self._lbl_artist, "");
    }

    self._lbl_song = lv_label_create(self._metadata_layer);
    if (self._lbl_song) {
        visual_style_metadata_label(
            self._lbl_song,
            kMetaSongX,
            kMetaSongY,
            kMetaSongW,
            kMetaSongH,
            reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr),
            pal.text_meta,
            LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(self._lbl_song, "");
    }
}

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

// Creates and pauses the LVGL PPM timer; runtime resource, not a visual layer.
// Guard: no-op if asset pack absent or timer already exists.
// Создаёт и ставит на паузу LVGL PPM таймер — runtime ресурс, а не визуальный слой.
// Защита: no-op при отсутствии asset pack или уже созданном таймере.
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

void LvglVisualPage::_onStationIdentityChanged(int station_id) {
    // Genuine station transition only — snapshot title as stale barrier until NEWTITLE.
    // Только реальная смена станции — снимок title как барьер до NEWTITLE.
    _cached_station_id = station_id;
    strlcpy(_title_at_station_switch, config.station.title, sizeof(_title_at_station_switch));
    _cached_raw_title[0] = '\0';
}

void LvglVisualPage::_refreshMetadata(bool force) {
    if (!_metadata_layer || !_lbl_station) return;

    const int         station_id = static_cast<int>(config.lastStation());
    const char* const st_name   = config.station.name;
    const char* const st_title  = config.station.title;

    const bool is_initial =
        (_cached_station_id < 0);
    const bool genuine_station_change =
        !is_initial && (station_id != _cached_station_id);

    const bool name_dirty =
        force || is_initial || genuine_station_change ||
        (strcmp(_cached_station_name, st_name) != 0);
    const bool title_dirty =
        force || is_initial || genuine_station_change ||
        (_cached_raw_title[0] == '\0') ||
        (strcmp(_cached_raw_title, st_title) != 0);

    if (!name_dirty && !title_dirty) return;

    if (genuine_station_change) {
        _onStationIdentityChanged(station_id);
    } else if (is_initial) {
        // Cache bootstrap — not a station transition; do not arm stale guard.
        // Инициализация кэша — не смена станции; stale guard не включаем.
        _title_at_station_switch[0] = '\0';
        _cached_station_id = station_id;
    }

    if (name_dirty) {
        visual_set_text_if_changed(_lbl_station, st_name);
        strlcpy(_cached_station_name, st_name, sizeof(_cached_station_name));
    }

    if (!title_dirty) return;

    strlcpy(_cached_raw_title, st_title, sizeof(_cached_raw_title));

    char artist_buf[BUFLEN];
    char song_buf[BUFLEN];
    artist_buf[0] = '\0';
    song_buf[0]   = '\0';

    const bool stale_guard_active =
        (_title_at_station_switch[0] != '\0') &&
        (strcmp(st_title, _title_at_station_switch) == 0);

    if (player.hasError()) {
        strlcpy(song_buf, player.lastError(), sizeof(song_buf));
    } else if (stale_guard_active) {
        // Title not yet updated for the new station — keep artist/song blank.
        // Title ещё не обновился для новой станции — artist/song пустые.
    } else if (visual_is_transport_title(st_title)) {
        // Service strings hidden / Служебные строки скрыты.
    } else if (strlen(st_title) == 0u || strcmp(st_title, st_name) == 0) {
        // Empty or ICY duplicate of station name / Пусто или дубликат имени станции.
    } else {
        char title_work[BUFLEN];
        strlcpy(title_work, st_title, sizeof(title_work));
        char* second = visual_split_inplace_at(title_work, " - ");
        if (second) {
            strlcpy(artist_buf, title_work, sizeof(artist_buf));
            strlcpy(song_buf, second, sizeof(song_buf));
        } else {
            strlcpy(song_buf, st_title, sizeof(song_buf));
        }
    }

    if (_lbl_artist) visual_set_text_if_changed(_lbl_artist, artist_buf);
    if (_lbl_song) visual_set_text_if_changed(_lbl_song, song_buf);
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
    // Shared root chrome contract: pad_all, pad_row, flex COLUMN — matches Main root.
    // Общий контракт корневого chrome: pad_all, pad_row, flex COLUMN — как на Main.
    lv_obj_set_style_pad_all(_screen, pad, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Floating layers first (bg, overlay, metadata), then flex-managed chrome — matches Main root contract.
    // Сначала floating-слои (bg, overlay, metadata), затем flex-управляемый chrome — контракт как на Main.
    create_background(*this);
    create_overlay_layer(*this);
    create_segment_grid(*this);
    create_metadata_layer(*this, pal);
    create_status_chrome(*this, pal);

    lv_obj_update_layout(_screen);

    // Status chrome is flex-managed (not FLOATING); explicit move_foreground makes it render above
    // the floating layers (bg, overlay, metadata) which were created first.
    // Status chrome управляется flex (не FLOATING); явный move_foreground делает его поверх
    // floating слоёв (bg, overlay, metadata), созданных раньше.
    if (_status_line.root) lv_obj_move_foreground(_status_line.root);
    if (_status_divider) lv_obj_move_foreground(_status_divider);

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

    // Immediate chrome/metadata snapshot on enter — do not wait for page update tick.
    // Немедленный снимок chrome/metadata при входе — не ждать следующего page update тика.
    if (_status_line.root) wgt_status_line::update(_status_line);
    _refreshMetadata(true);
}

void LvglVisualPage::update() {
    if (!_screen || !_status_line.root) return;

    wgt_status_line::update(_status_line);
    _refreshMetadata(false);
}

void LvglVisualPage::exit() {
    if (_ppm_timer) lv_timer_pause(_ppm_timer);
}

void LvglVisualPage::liveReapplyTheme() {
    // Background asset follows active theme; segment colors stay fixed museum green/red.
    // Фон следует активной теме; цвета сегментов остаются фиксированными museum green/red.
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);

    wgt_status_line::reapplyTheme(_status_line);
    if (_status_divider) {
        lv_obj_set_style_bg_color(_status_divider, pal.divider, LV_PART_MAIN);
    }
    if (_lbl_station) {
        lv_obj_set_style_text_color(_lbl_station, pal.text_secondary, LV_PART_MAIN);
    }
    if (_lbl_artist) {
        lv_obj_set_style_text_color(_lbl_artist, pal.text_secondary, LV_PART_MAIN);
    }
    if (_lbl_song) {
        lv_obj_set_style_text_color(_lbl_song, pal.text_meta, LV_PART_MAIN);
    }

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
    _status_line = {};
    _status_divider = nullptr;
    _metadata_layer = nullptr;
    _lbl_station = nullptr;
    _lbl_artist = nullptr;
    _lbl_song = nullptr;

    _cached_station_id = -1;
    _cached_station_name[0] = '\0';
    _cached_raw_title[0] = '\0';
    _title_at_station_switch[0] = '\0';

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
