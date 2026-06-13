#ifndef WGT_PRESENCE_RAIL_H
#define WGT_PRESENCE_RAIL_H

#include "lvgl.h"

namespace lvgl_ui {
namespace wgt_presence_rail {

// Sound Presence Rail (Stage 8 E23A11) — product cleanup to 2 profiles.
// E23A11: archived Lissajous/Comet/Dense experiments; active cycle = FenceTremor + OscilloscopeLine.
// NOT a VU-meter/spectrum/FFT/Canvas/sprite — only lv_line + a single lv_timer.
//
// Sound Presence Rail — продуктовый cleanup E23A11: 2 активных профиля.
// FenceTremor = основной/дефолтный. OscilloscopeLine = SA switchable.
// Lissajous/Comet/Dense архивированы в docs/, удалены из активного кода.
// Это НЕ VU-метр/спектр/FFT/Canvas/sprite — только lv_line + один lv_timer.
//
// Threading: all lv_* (create/start/stop/setStyle/reapplyTheme/destroy + timer cb) only from DspTask.
// Потоки: все lv_* (включая callback таймера) — только из DspTask.

// Full-width polyline resolution / разрешение полноширинной полилинии (OscilloscopeLine).
static constexpr int kPointCount = 128;

// Segment pool = kFenceCount (30 columns for FenceTremor; OscilloscopeLine does not use seg_lines).
// Пул сегментов = kFenceCount (30 столбцов Fence; OscilloscopeLine seg_lines не использует).
static constexpr int kSegLines = 30;

// E23A11: active profile list — 2 profiles only.
// FenceTremor = 0 (default when usespectrum=false). DoubleFullWaveWhisper = 1 (OscilloscopeLine when usespectrum=true).
// Enum name DoubleFullWaveWhisper preserved for store/history compatibility.
// E23A11: два активных профиля. usespectrum в EEPROM выбирает профиль (как раньше).
enum class PresenceRailStyle : uint8_t {
    FenceTremor           = 0, // main/default: calm vertical tremor field / «забор», основной
    DoubleFullWaveWhisper = 1, // SA switchable: OscilloscopeLine / осциллограф
};

static constexpr uint8_t kProfileCount = 2;

// E23A2: audio features for rail modulation. level/peak/onset are AGC-normalized and
// gate-scaled (0 on STOP); gate is exposed for profiles that collapse geometry when
// the stream stops. `level` = sustained body loudness, `peak` = short visible hit.
// Local to this widget by design — no global bus in this slice.
// E23A2: аудио-фичи модуляции rail. level/peak/onset нормализованы AGC и умножены на
// gate (0 на STOP); gate отдаётся профилям, схлопывающим геометрию при остановке.
// `level` = «тело» громкости, `peak` = короткий видимый удар. Локально для виджета.
struct RailAudioFeatures {
    float raw      = 0.0f; // 0..1 transformed VU before AGC / VU до AGC
    float level    = 0.0f; // 0..1 AGC-normalized smoothed loudness / нормализованный уровень
    float peak     = 0.0f; // 0..1 fast hit/transient envelope / быстрый конверт удара
    // E23A4b: activity = fast norm-delta envelope — jumps on any level change, decays
    // in 2-4 ticks. Use for oscilloscope-style sharp movement. 0 on STOP.
    // E23A4b: activity = конверт быстрой дельты norm — скачет на любом изменении уровня,
    // спадает за 2-4 tick'а. Для осциллографического резкого движения. 0 на STOP.
    float activity = 0.0f; // 0..1 fast level-delta / motion energy / быстрая дельта уровня
    float onset    = 0.0f; // 0..1 slower attack detector / медленный детектор атаки
    float gate     = 0.0f; // 0..1 music gate: 1 while stream runs, fast decay on STOP
    bool  valid    = false;
};

// E23A11: Instance stripped of Lissajous/Comet/Dense fields.
// Removed: wave_line2, wave_points2, hist_a/b, hist_head/count,
//          breathe_phase, trail_x/y/t, progress, dash_opa_q.
// E23A11: Instance очищен от полей Lissajous/Comet/Dense.
struct Instance {
    lv_obj_t* root      = nullptr;
    lv_obj_t* wave_line = nullptr;               // OscilloscopeLine full-width polyline
    lv_obj_t* seg_line[kSegLines] = {};          // FenceTremor column lines

    lv_timer_t* timer = nullptr;

    lv_point_t wave_points[kPointCount] = {};    // OscilloscopeLine wave geometry
    lv_point_t seg_points[kSegLines][2] = {};    // FenceTremor: [col][top/bottom]

    lv_coord_t rail_w   = 0;
    lv_coord_t rail_h   = 0;
    lv_coord_t center_y = 0;

    PresenceRailStyle style = PresenceRailStyle::FenceTremor;

    float phase = 0.0f; // wave/fence phase / фаза волны и забора

    // E22N: two slow incommensurate LFOs for Fence vertical drift (quasi-periodic).
    // E22N: два несоизмеримых медленных LFO для дрейфа центра тяжести Fence.
    float lfo_a = 0.0f;
    float lfo_b = 0.0f;

    // E23A2: VU envelopes + local AGC (updated only from rail timer / DspTask).
    // AGC reference survives profile switches; music_gate collapses audio-driven
    // geometry on STOP in ~300-600 ms.
    // E23A2: VU-конверты + локальный AGC (только из таймера rail / DspTask).
    // Референс AGC переживает смену профиля; music_gate схлопывает геометрию на STOP.
    RailAudioFeatures audio;
    float agc_peak_ref       = 0.08f; // adaptive peak reference / адаптивный референс пика
    float level_env          = 0.0f;  // smoothed AGC-normalized level / сглаженный уровень
    float peak_env           = 0.0f;  // fast hit envelope / быстрый конверт удара
    float audio_slow         = 0.0f;  // slow mean for onset / медленное среднее для onset
    float onset_pulse        = 0.0f;  // decaying attack pulse / затухающий импульс атаки
    float music_gate         = 0.0f;  // smoothed stream-running gate / сглаженный гейт потока
    // E23A4b: activity fields — fast delta envelope for OscilloscopeLine sharp movement.
    // E23A4b: поля activity — быстрый конверт дельты для резкого движения осциллографа.
    float audio_prev_norm    = 0.0f;  // last norm value for delta / предыдущий norm для дельты
    float audio_activity_env = 0.0f;  // decaying activity envelope / спадающий конверт активности

    // Quantized opacity-boost cache — restyle only when the step changes (bounded cost).
    // Кэш квантованного opacity-буста — рестайл только при смене ступени.
    int8_t opa_q = -1;

    bool audio_logged = false; // one-time "VU source active" log latch / одноразовый лог

    bool active  = false;
    bool vu_on   = false;
    bool sa_last = false;
};

void create(lv_obj_t* parent, Instance& out);
void destroy(Instance& inst);
void start(Instance& inst);
void stop(Instance& inst);
void setStyle(Instance& inst, PresenceRailStyle style);
void reapplyTheme(Instance& inst);

} // namespace wgt_presence_rail
} // namespace lvgl_ui

#endif // WGT_PRESENCE_RAIL_H
