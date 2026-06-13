// wgt_presence_rail — Sound Presence Rail (Stage 8 E23A11 product cleanup).
// E23A11: 2-profile active cycle: FenceTremor (default) + OscilloscopeLine (SA switchable).
// Lissajous / Comet / Dense profiles archived to docs/presence_rail_experimental_E23A10c_*.txt.
// Procedural lv_line only — no spectrum renderer / Canvas / sprite / audio buffers.
//
// E23A11: продуктовый cleanup — 2 активных профиля: FenceTremor (основной) + OscilloscopeLine (SA).
// Lissajous/Comet/Dense архивированы в docs/. Только lv_line + один lv_timer.
//
// E22C→E22N runtime control (legacy store semantics preserved):
//   - config.store.vumeter      → rail show/hide (persisted in EEPROM)
//   - config.store.usespectrum  → profile select: false=Fence, true=Oscilloscope (persisted)
//
// E23A: мягкая VU-модуляция обоих профилей — read-only чтение player.get_VUlevel() из
// таймера rail (DspTask), EMA-конверты в Instance. Без FFT / полос / изменений audio path.

#include "wgt_presence_rail.h"

#include <Arduino.h>
#include <math.h>
#include <new>

#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../../core/display.h"
#include "../../core/config.h"
#include "../../core/player.h" // E23A: read-only player.get_VUlevel() / только чтение VU

// E23A14 preflight: temporary perf/memory probe. Default 0 (preflight closed: CPU/mem OK).
// Set to 1 to re-measure tick_profile() cost + core/task + heap; no visual change, no tick spam.
// E23A14 preflight: временная проба. По умолчанию 0 (preflight закрыт: CPU/память OK).
// Поставить 1 для повторного замера стоимости tick_profile() + core/task + heap.
#define PRESENCE_RAIL_PERF_PROBE 0
#if PRESENCE_RAIL_PERF_PROBE
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace lvgl_ui {
namespace wgt_presence_rail {

// E23A11: default when usespectrum=false / дефолт при usespectrum=false.
static constexpr PresenceRailStyle kDefaultRailStyle = PresenceRailStyle::FenceTremor;

namespace {

// Legacy YoRadio mapping: persisted usespectrum selects profile (not a cycle button).
// Как в upstream: usespectrum=false → Fence, true → OscilloscopeLine.
static PresenceRailStyle style_from_usespectrum(bool usespectrum) {
    return usespectrum ? PresenceRailStyle::DoubleFullWaveWhisper
                       : PresenceRailStyle::FenceTremor;
}

constexpr float k_wave_freq_px   = 0.045f;
constexpr float k_wave_freq2_mul = 2.1f;
constexpr float k_two_pi         = 6.28318530717959f;
constexpr float k_pi             = 3.14159265358979f;
constexpr uint8_t k_y_margin     = 2;

// Rendering mode of a gallery profile / способ отрисовки профиля галереи.
// E23A11: only FullWave and Fence remain; Dash/CometFan/Lissajous removed with their profiles.
enum class RailMode : uint8_t {
    FullWave,  // full-width polyline (OscilloscopeLine)
    Fence,     // vertical linelets (FenceTremor)
};

constexpr lv_opa_t opa_pct(int p) {
    return static_cast<lv_opa_t>((p * 255 + 50) / 100);
}

// E22M: theme-aware opacity — percent value scaled by pal.rail_opa_scale (100 = 1.0).
// All rail opacities go through this helper; themes tune loudness without touching profiles.
// Вся прозрачность rail проходит через этот хелпер: тема глушит/усиливает эффекты целиком.
static lv_opa_t rail_opa(const YoRadioPalette& pal, int pct) {
    int p = pct * static_cast<int>(pal.rail_opa_scale) / 100;
    if (p < 0)   p = 0;
    if (p > 100) p = 100;
    return opa_pct(p);
}

// E23A11: stripped RailProfileConfig — only fields used by Fence and OscilloscopeLine.
// E23A11: упрощённый конфиг профиля — только поля нужные Fence и OscilloscopeLine.
struct RailProfileConfig {
    RailMode mode;
    float    phase_step; // wave/fence phase advance per tick
    uint16_t timer_ms;   // timer period
};

RailProfileConfig getProfileConfig(PresenceRailStyle style) {
    RailProfileConfig c{};
    switch (style) {
        // 0 — FenceTremor (E23A11 new default): calm pseudo-frequency field.
        // 0 — FenceTremor (новый дефолт E23A11): поле псевдочастотных вертикальных реек.
        case PresenceRailStyle::FenceTremor:
            c.mode = RailMode::Fence;
            c.phase_step = 0.145f; c.timer_ms = 150;
            break;

        // 1 — OscilloscopeLine (enum DoubleFullWaveWhisper, SA switchable).
        // E23A4c accepted behavior preserved / поведение E23A4c сохранено.
        case PresenceRailStyle::DoubleFullWaveWhisper:
            c.mode = RailMode::FullWave;
            c.phase_step = 0.16f; c.timer_ms = 130;
            break;
    }
    return c;
}

// ---- Gallery metadata for Serial logging / метаданные галереи для Serial ----

static const char* effectName(PresenceRailStyle style) {
    switch (style) {
        case PresenceRailStyle::FenceTremor:           return "FenceTremor";
        case PresenceRailStyle::DoubleFullWaveWhisper: return "OscilloscopeLine";
    }
    return "Unknown";
}

static const char* effectDescription(PresenceRailStyle style) {
    switch (style) {
        case PresenceRailStyle::FenceTremor:           return "calm vertical tremor field";
        case PresenceRailStyle::DoubleFullWaveWhisper: return "static full-width audio waveform";
    }
    return "?";
}

// Log only on profile/VU change, never per tick / лог только на смену профиля/VU, не на tick.
static void logProfile(const Instance& inst, const char* prefix) {
    Serial.printf("[PresenceRail] %sprofile %u/%u: %s - %s\n",
                  prefix,
                  static_cast<unsigned>(inst.style),
                  static_cast<unsigned>(kProfileCount),
                  effectName(inst.style),
                  effectDescription(inst.style));
}

// ---- Small helpers / мелкие хелперы ----

inline void show_obj(lv_obj_t* o, bool on) {
    if (!o) return;
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float y_min_bound(const Instance& inst) {
    (void)inst;
    return static_cast<float>(k_y_margin);
}

static float y_max_bound(const Instance& inst) {
    return static_cast<float>(inst.rail_h - 1 - k_y_margin);
}

static lv_point_t make_point(float x, float y) {
    lv_point_t p;
    p.x = static_cast<lv_coord_t>(lroundf(x));
    p.y = static_cast<lv_coord_t>(lroundf(y));
    return p;
}

// ---- E23A2: VU feature extraction with local AGC / извлечение VU-фич с локальным AGC ----
// Read-only polling of the audio library's built-in VU (already computed per-sample in the
// audio task). E23A2 chain: noise floor → adaptive AGC normalization (slow-decay peak
// reference) → gamma → level envelope → fast PEAK/hit envelope → music_gate (STOP collapse).
// AGC lives entirely inside this widget; the audio library is untouched.
// Called ONLY from rail_timer_cb (DspTask). No heap, no mutex.
// Read-only чтение встроенного VU (уже считается в audio task). AGC — целиком внутри виджета.

// Set to 1 manually for once-per-second AGC diagnostics. Default OFF.
// Вручную 1 — печать диагностики AGC раз в секунду. По умолчанию ВЫКЛ.
#define PRESENCE_RAIL_AUDIO_DEBUG 0

// ================= E23A3 VU modulation tuning (single block) =================
// All modulation behavior is tuned from here / вся модуляция тюнится отсюда.
// E23A3 semantics: audio.gate is the GLOBAL visual energy — every profile scales
// opacity/height/scale by it, so STOP fades the rail to (near) invisible.
// E23A3: audio.gate — ГЛОБАЛЬНАЯ визуальная энергия: все профили умножают на неё,
// поэтому STOP гасит rail почти в ноль.

// -- Feature extraction / извлечение фич --
static constexpr float kAudioFloor   = 0.03f;  // noise gate / шумовой порог
static constexpr float kAgcPeakMin   = 0.08f;  // AGC reference clamp low / нижний предел AGC
static constexpr float kAgcPeakRise  = 0.35f;  // fast peak learn / быстрое обучение референса
static constexpr float kAgcPeakFall  = 0.005f; // very slow reference decay / медленный спад
static constexpr float kAudioGamma   = 0.65f;  // perceptual curve after AGC / гамма после AGC
static constexpr float kLevelAttack  = 0.70f;  // level rise / подъём уровня
static constexpr float kLevelRelease = 0.30f;  // E23A3: faster fall ≈ 2-4 ticks / быстрее спад
static constexpr float kPeakGain     = 3.5f;   // (norm-level) → hit gain / усиление удара
static constexpr float kPeakDecay    = 0.35f;  // E23A3: peak falls in ~2-3 ticks / быстрее спад
static constexpr float kOnsetSlow    = 0.08f;  // slow mean for onset / медленное среднее
static constexpr float kOnsetGain    = 4.0f;   // onset detector gain / усиление onset
static constexpr float kOnsetDecay   = 0.55f;  // onset pulse decay / спад импульса
static constexpr float kGateRise     = 0.60f;  // gate opens fast on play / открытие гейта
static constexpr float kGateFall     = 0.45f;  // gate ≈0 in 3-4 ticks on STOP / схлопывание
static constexpr float kLevelEps     = 0.015f; // valid threshold / порог валидности
static constexpr float kPeakEps      = 0.010f; // valid threshold / порог валидности
static constexpr float kGateHideEps  = 0.01f;  // below this profiles hide objects / порог скрытия

// -- Slot 1 OscilloscopeLine tuning (E23A4c accepted) --
// Static waveform: shape is a deterministic function of x only — NO phase scroll.
// Статичная форма: функция только от x — горизонтального ползания нет.
//
// Manual tuning guide / руководство по ручной настройке:
//   too often maxed out          → kScopeActivityMax ↓  or  kScopeActivityGamma ↑
//   too quiet / not alive        → kScopeAmpActivityPx ↑  or  kScopeActivityGamma ↓
//   too much same-shape breathing→ kScopeMorphMix ↑  or  dent mixes ↑
//   too jagged / cheap           → kScopeDentActivityMix ↓  or  kScopeDentPeakMix ↓
//   peak not sharp enough        → kScopeDentPeakMix ↑  or  kScopeAmpPeakPx ↑
//   more body on normal music    → kScopeAmpLevelPx ↑
//   flatter quiet baseline       → kScopeAmpLevelPx ↓  and  kScopeAmpIdlePx ↓  (min ~0.25)
//   too dim on Amber/Dark        → kScopeOpacityBase ↑  (safe range 70..82)
//   faster raw activity fall     → kActivityDecay ↓  (0.3 = very fast, 0.55 = slower)
//   too thin                     → test kScopeLineWidth=2
//   too thick / toy-like         → keep kScopeLineWidth=1, raise opacity/amplitude instead
//   STOP residue                 → ensure gate multiplies env_px + opacity (already does)

// -- Activity (feature extraction) --
static constexpr float kActivityRiseGain  = 7.0f;  // delta-rise gain / усиление роста дельты
static constexpr float kActivityDeltaGain = 2.5f;  // abs-delta gain / усиление абсолютной дельты
static constexpr float kActivityDecay     = 0.35f; // per-tick decay / спад за tick

// -- Display soft-limit (OscilloscopeLine only; raw audio.activity unchanged globally) --
static constexpr float kScopeActivityGamma = 1.35f; // gamma>1 softens mid activity / смягчение
static constexpr float kScopeActivityMax   = 0.85f; // cap display activity / потолок scope_act

// -- Shape A (base) + Shape B (morph target), both purely x-anchored --
static constexpr float kScopeFreq1       = 0.050f;  // shape A spatial freq 1 rad/px
static constexpr float kScopeFreq2       = 0.117f;  // shape A spatial freq 2 rad/px
static constexpr float kScopeFreq3       = 0.231f;  // shape A spatial freq 3 rad/px
static constexpr float kScopePhaseA      = 0.0f;    // fixed x-phase for shape A freq1
static constexpr float kScopePhaseB      = 1.4f;    // fixed x-phase for shape A freq2
static constexpr float kScopePhaseC      = 3.1f;    // fixed x-phase for shape A freq3
static constexpr float kScopeMorphMix    = 0.55f;   // scope_act → morph blend weight
static constexpr float kScopeMorphFreq1  = 0.037f;  // shape B spatial freq 1 rad/px
static constexpr float kScopeMorphFreq2  = 0.091f;  // shape B spatial freq 2 rad/px
static constexpr float kScopeMorphFreq3  = 0.177f;  // shape B spatial freq 3 rad/px
static constexpr float kScopeMorphPhaseA = 2.2f;    // fixed x-phase for shape B freq1
static constexpr float kScopeMorphPhaseB = 0.4f;    // fixed x-phase for shape B freq2
static constexpr float kScopeMorphPhaseC = 4.1f;    // fixed x-phase for shape B freq3

// -- Local fixed dents (normalized u=0..1, triangular kernel) --
static constexpr float kScopeDent1Pos   = 0.22f;
static constexpr float kScopeDent1Width = 0.055f;
static constexpr float kScopeDent1Amp   = 0.70f;
static constexpr float kScopeDent2Pos   = 0.49f;
static constexpr float kScopeDent2Width = 0.070f;
static constexpr float kScopeDent2Amp   = 0.55f;
static constexpr float kScopeDent3Pos   = 0.74f;
static constexpr float kScopeDent3Width = 0.045f;
static constexpr float kScopeDent3Amp   = 0.80f;
static constexpr float kScopeDentActivityMix = 0.55f; // dent strength from scope_act
static constexpr float kScopeDentPeakMix     = 0.85f; // dent strength from peak

// -- Amplitude --
static constexpr float kScopeAmpIdlePx     = 0.25f;  // px: baseline (near-flat at idle)
static constexpr float kScopeAmpLevelPx    = 2.5f;   // px: sustained body per level
static constexpr float kScopeAmpActivityPx = 16.0f;  // px: main oscilloscope movement
static constexpr float kScopeAmpPeakPx     = 18.0f;  // px: short burst on peak

// -- Opacity --
// E23A13: base 70→76 — SA profile must read clearly on Amber/Dark without dominating.
// E23A13: база 70→76 — SA-профиль должен читаться на Amber/Dark, но не доминировать.
static constexpr float kScopeOpacityBase     = 76.0f; // % base while playing (safe range 70..82)
static constexpr float kScopeOpacityActivity = 14.0f; // % scope_act punch
static constexpr float kScopeOpacityPeak     = 18.0f; // % peak punch
static constexpr int   kScopeLineWidth       = 1;     // px (1=crisp scope; 2=thick/toy — keep 1)

// -- OscilloscopeLine geometry helpers --
constexpr float k_edge_taper_frac = 0.14f; // taper zone = 14% of width per side / зона затухания
constexpr int   k_fw_main_opa     = 38;    // initial style base opacity / начальный opacity стиля

// -- Slot 0 FenceTremor tuning (E23A12 product polish) --
// Whole-field vertical rails breathe together — NOT per-column spectrum bars.
// Вертикальные «рейки» дышат единым полем — НЕ спектр-бары по колонкам.
//
// Manual tuning guide / руководство по ручной настройке:
//   Too dim           → raise kFenceOpacityBase
//   Too bright        → lower kFenceOpacityActivity/kFenceOpacityPeak first
//   Too tall          → lower kFenceHeightPeak or kFenceVisualRange
//   Too small         → raise kFenceHeightActivity
//   Too nervous       → increase kFenceActivityGamma or lower kFenceHeightActivity
//   Too rigid         → lower kFenceSyncPeak / sync mix
//   Too random        → increase sync or reduce irregularity
//   Line too thin     → test kFenceLineWidth=2
//   Line too thick    → keep kFenceLineWidth=1
//   STOP residue      → ensure gate multiplies opacity/height

// Display soft-limit (FenceTremor only; raw audio.activity unchanged globally) --
static constexpr float kFenceActivityGamma = 1.20f; // gamma>1 softens mid activity
static constexpr float kFenceActivityMax   = 0.90f; // cap fence_act — calmer than raw

// Height drive: h_drive = gate * clamp01(quiet + level + activity + peak) --
static constexpr float kFenceHeightQuiet    = 0.07f; // quiet baseline share
static constexpr float kFenceHeightLevel    = 0.26f; // sustained body (slow)
static constexpr float kFenceHeightActivity = 0.50f; // fence_act → main breathing
static constexpr float kFenceHeightPeak     = 0.52f; // peak punch (reduced vs E23A5)

// Map h_drive to fraction of rail half-height: min + range * h_drive --
static constexpr float kFenceVisualMin   = 0.12f; // floor at quiet play
static constexpr float kFenceVisualRange = 0.82f; // span at peak (max scale ≈ 0.94)

// Field coherence: active/peak syncs columns; quiet keeps organic spread --
static constexpr float kFenceSyncActivity = 0.65f; // fence_act → column alignment
static constexpr float kFenceSyncPeak     = 1.00f; // peak → full-field sync
static constexpr float kFenceIrregReduce  = 0.85f; // sync → damp per-column spread

// Opacity (gate-scaled; vignette per column applied in tick_fence) --
static constexpr float kFenceOpacityBase     = 80.0f; // % base while playing
static constexpr float kFenceOpacityActivity = 24.0f; // % fence_act punch
static constexpr float kFenceOpacityPeak     = 18.0f; // % peak punch
static constexpr int   kFenceOpacityMin      = 10;    // % floor per column after vignette
static constexpr int   kFenceLineWidth       = 1;     // px, applied in apply_styles (Fence only)

// FenceTremor geometry --
constexpr int   kFenceCount        = 30;    // number of vertical columns / столбцов
constexpr float k_fence_vign_floor   = 0.68f;  // edge column height share / доля высоты у краёв
constexpr float k_fence_height_peak  = 1.00f;  // E22P1: center → max rail_h / центр на максимум
constexpr float k_fence_height_floor = 0.42f;  // E22P1: taller edge columns
constexpr float k_fence_drift_a      = 1.3f;   // primary drift amplitude px / основной дрейф
constexpr float k_fence_drift_b      = 0.6f;   // secondary drift px (quasi-periodic mix)
constexpr float k_fence_lfo_a_step   = 0.079f; // ~12 s at 150 ms tick / период ~12 с
constexpr float k_fence_lfo_b_step   = 0.135f; // ~7 s at 150 ms tick / период ~7 с

// Width vignette factor (height) — not perfectly symmetric; per-column irregular term mixed by caller.
// Виньетка по ширине — идеальная симметрия размывается нерегулярным членом у вызывающего.
static float fence_vignette(float u) {
    return k_fence_vign_floor + (1.0f - k_fence_vign_floor) * sinf(k_pi * u);
}

// ---- clamp01 / soft-limit helpers ----

static inline float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// OscilloscopeLine display soft-limit: compress raw activity for calmer visuals / смягчение activity
static inline float scope_display_activity(float activity) {
    float a = clamp01(activity);
    float s = powf(a, kScopeActivityGamma);
    return (s < kScopeActivityMax) ? s : kScopeActivityMax;
}

// Cheap triangular dent kernel (u normalized 0..1) / дешёвый треугольный dent
static inline float scope_tri(float u, float center, float width) {
    const float d = fabsf(u - center) / width;
    return (d < 1.0f) ? (1.0f - d) : 0.0f;
}

// FenceTremor display soft-limit: calmer than raw activity for field breathing / смягчение activity
static inline float fence_display_activity(float activity) {
    float a = clamp01(activity);
    float f = powf(a, kFenceActivityGamma);
    return (f < kFenceActivityMax) ? f : kFenceActivityMax;
}

// ---- E23A2: VU feature extraction with local AGC ----

static void update_audio_features(Instance& inst) {
    // get_VUlevel(dim) is INVERTED: map(vu, threshold, 0, 0, dim) → 0 = loud, dim = silence.
    // It also returns 0 when stopped / not calibrated — that would read as "loud", so the
    // running state feeds an explicit music_gate (fast decay on STOP).
    // get_VUlevel(dim) ИНВЕРТИРОВАН: 0 = громко, dim = тишина; при стопе тоже 0 (читалось
    // бы как «громко») — поэтому состояние потока идёт в явный music_gate.
    const bool running = player.isRunning() && config.vuThreshold > 0;
    float raw = 0.0f;
    if (running) {
        const uint16_t lr = player.get_VUlevel(255);
        const uint8_t  l  = static_cast<uint8_t>(lr >> 8);
        const uint8_t  r  = static_cast<uint8_t>(lr & 0xFF);
        // Louder channel = smaller inverted value → invert back to 0..1 loudness.
        const uint8_t quiet = (l < r) ? l : r;
        raw = 1.0f - static_cast<float>(quiet) / 255.0f;
    }

    // Noise floor / шумовой порог.
    float x = raw - kAudioFloor;
    if (x < 0.0f) x = 0.0f;

    // Local AGC: adaptive peak reference — fast learn on rises, very slow decay.
    // AGC never makes silence loud: with raw==0 (STOP) x==0 → norm==0 regardless of ref.
    if (running) {
        const float k_ref = (x > inst.agc_peak_ref) ? kAgcPeakRise : kAgcPeakFall;
        inst.agc_peak_ref += (x - inst.agc_peak_ref) * k_ref;
        if (inst.agc_peak_ref < kAgcPeakMin) inst.agc_peak_ref = kAgcPeakMin;
        if (inst.agc_peak_ref > 1.0f)        inst.agc_peak_ref = 1.0f;
    }

    float norm = clamp01(x / inst.agc_peak_ref);
    norm = powf(norm, kAudioGamma); // perceptual curve / перцептивная кривая

    // Level envelope: follows loudness body, not too nervous / конверт «тела» громкости.
    const float k_lvl = (norm > inst.level_env) ? kLevelAttack : kLevelRelease;
    inst.level_env += (norm - inst.level_env) * k_lvl;

    // PEAK/hit envelope: fires on transients, falls in 2-4 ticks.
    const float hit = clamp01((norm - inst.level_env) * kPeakGain);
    inst.peak_env *= kPeakDecay;
    if (hit > inst.peak_env) inst.peak_env = hit;

    // Onset (slower attack detector) / медленный детектор атаки.
    inst.audio_slow += (inst.level_env - inst.audio_slow) * kOnsetSlow;
    const float onset_raw = clamp01((inst.level_env - inst.audio_slow) * kOnsetGain);
    inst.onset_pulse *= kOnsetDecay;
    if (onset_raw > inst.onset_pulse) inst.onset_pulse = onset_raw;

    // E23A4b Activity: fast norm-delta envelope — jumps on any level change, decays in
    // 2-4 ticks. This is what gives OscilloscopeLine its "sharp movement" character.
    // On STOP: env decays quickly AND prev_norm is zeroed to prevent a false spike on
    // next PLAY (norm will rise from 0 → prev_norm=0 → delta is real, not an artifact).
    // E23A4b Activity: быстрый конверт дельты norm. На STOP prev_norm обнуляется —
    // при следующем PLAY не будет ложного всплеска.
    if (!running) {
        inst.audio_activity_env *= 0.35f;  // fast reset on STOP / быстрый сброс на STOP
        inst.audio_prev_norm = 0.0f;
    } else {
        const float delta    = norm - inst.audio_prev_norm;
        const float rise     = (delta > 0.0f) ? delta : 0.0f;
        const float motion   = (delta < 0.0f) ? -delta : delta;
        const float act_raw  = clamp01(kActivityRiseGain * rise + kActivityDeltaGain * motion);
        // Peak-hold with per-tick decay / peak-hold с per-tick спадом.
        const float held = inst.audio_activity_env * kActivityDecay;
        inst.audio_activity_env = (act_raw > held) ? act_raw : held;
        inst.audio_prev_norm = norm;
    }

    // Music gate: 1 while stream runs; ≈0 in 3-4 ticks on STOP — geometry collapses.
    // Гейт музыки: 1 пока играет; ≈0 за 3-4 tick'а на STOP — геометрия схлопывается.
    if (running) inst.music_gate += (1.0f - inst.music_gate) * kGateRise;
    else         inst.music_gate *= kGateFall;
    if (inst.music_gate < 0.01f) inst.music_gate = 0.0f;
    if (!running) inst.peak_env *= kGateFall; // peak resets quickly on STOP

    inst.audio.gate     = inst.music_gate;
    inst.audio.raw      = x;
    inst.audio.level    = clamp01(inst.level_env) * inst.music_gate;
    inst.audio.peak     = clamp01(inst.peak_env) * inst.music_gate;
    inst.audio.activity = clamp01(inst.audio_activity_env) * inst.music_gate;
    inst.audio.onset    = clamp01(inst.onset_pulse) * inst.music_gate;
    inst.audio.valid = (inst.audio.level > kLevelEps) || (inst.audio.peak > kPeakEps);
    if (!inst.audio.valid) {
        inst.audio.level    = 0.0f; // neutral multipliers for all profiles
        inst.audio.peak     = 0.0f;
        inst.audio.activity = 0.0f;
        inst.audio.onset    = 0.0f;
    } else if (!inst.audio_logged) {
        inst.audio_logged = true; // edge-triggered, once per Instance / одноразовый лог
        Serial.printf("[PresenceRail] audio modulation: VU source active\n");
    }

#if PRESENCE_RAIL_AUDIO_DEBUG
    {
        static uint32_t s_dbg_ms = 0;
        const uint32_t now = millis();
        if (now - s_dbg_ms >= 1000) {
            s_dbg_ms = now;
            if (inst.style == PresenceRailStyle::FenceTremor) {
                const float fact = fence_display_activity(inst.audio.activity);
                const float h_drv = clamp01(kFenceHeightQuiet
                                             + kFenceHeightLevel    * inst.audio.level
                                             + kFenceHeightActivity * fact
                                             + kFenceHeightPeak     * inst.audio.peak)
                                    * inst.audio.gate;
                const float sync_dbg = clamp01(kFenceSyncActivity * fact
                                               + kFenceSyncPeak * inst.audio.peak);
                Serial.printf("[PresenceRail] dbg raw=%.2f lvl=%.2f peak=%.2f act=%.2f fact=%.2f h=%.2f sync=%.2f gate=%.2f\n",
                              raw, inst.audio.level, inst.audio.peak,
                              inst.audio.activity, fact, h_drv, sync_dbg, inst.audio.gate);
            } else {
                const float sact = scope_display_activity(inst.audio.activity);
                Serial.printf("[PresenceRail] dbg raw=%.2f lvl=%.2f peak=%.2f act=%.2f sact=%.2f gate=%.2f ref=%.2f\n",
                              raw, inst.audio.level, inst.audio.peak,
                              inst.audio.activity, sact, inst.audio.gate, inst.agc_peak_ref);
            }
        }
    }
#endif
}

// ---- Reset per-effect state on profile enter / VU re-enable ----
// Сброс состояния эффекта при входе в профиль / повторном включении VU.
void reset_effect_state(Instance& inst, const RailProfileConfig& cfg) {
    // E23A11: no history ring, no comet/dash state to reset.
    // Slow LFOs restart so Fence drift is deterministic per profile enter.
    // E23A11: нет кольца истории, нет Comet/Dash. LFO начинается с нуля.
    inst.lfo_a = 0.0f;
    inst.lfo_b = 0.0f;

    // E23A: opacity-boost cache must be re-applied after apply_styles resets base styles.
    // Audio envelopes themselves survive profile switches (no re-attack on toggle).
    // Кэш opacity-буста сбрасываем — apply_styles вернёт базовые стили.
    inst.opa_q = -1;
    (void)cfg;
}

// ---- Profile tick functions / тики профилей ----

void tick_full_wave(Instance& inst, const RailProfileConfig& cfg) {
    (void)cfg;
    // E23A4 OscilloscopeLine — STATIC shape anchored in X: no inst.phase in spatial args.
    // Shape = f(x) only; audio changes AMPLITUDE and OPACITY, not horizontal position.
    // gate-scaled: STOP → amplitude 0 + opacity 0 (line invisible within ~400-600 ms).
    //
    // E23A4 OscilloscopeLine — СТАТИЧНАЯ форма, привязанная к X: inst.phase НЕ входит
    // в пространственные аргументы. Форма = f(x); звук меняет АМПЛИТУДУ и ЯРКОСТЬ.
    const float w    = static_cast<float>(inst.rail_w);
    const float y_lo = y_min_bound(inst);
    const float y_hi = y_max_bound(inst);
    const float cy   = static_cast<float>(inst.center_y);
    const float edge = w * k_edge_taper_frac;
    // Amplitude envelope: gate × (idle + level_body + peak_burst).
    const float rail_half = (y_hi - y_lo) * 0.5f - 1.0f;
    // E23A4c: scope_act soft-limits raw activity for OscilloscopeLine display only.
    const float scope_act = scope_display_activity(inst.audio.activity);
    float env_px = inst.audio.gate * (kScopeAmpIdlePx
                                      + kScopeAmpLevelPx    * inst.audio.level
                                      + kScopeAmpActivityPx * scope_act
                                      + kScopeAmpPeakPx     * inst.audio.peak);
    if (env_px > rail_half) env_px = rail_half;

    const float morph = kScopeMorphMix * scope_act;
    const float dent_mix = kScopeDentActivityMix * scope_act
                           + kScopeDentPeakMix * inst.audio.peak;

    for (int j = 0; j < kPointCount; ++j) {
        const float x = w * static_cast<float>(j) / static_cast<float>(kPointCount - 1);
        const float u = static_cast<float>(j) / static_cast<float>(kPointCount - 1);
        // Edge taper near rail ends / затухание у краёв.
        float taper = 1.0f;
        const float d = (x < w - x) ? x : (w - x);
        if (d < edge && edge > 1.0f) {
            const float ut = d / edge;
            taper = ut * (2.0f - ut);
        }
        // Static morph: blend shape_a ↔ shape_b by scope_act (no time phase, no crawl).
        const float shape_a = sinf(kScopeFreq1 * x + kScopePhaseA)
                              + 0.35f * sinf(kScopeFreq2 * x + kScopePhaseB)
                              + 0.18f * sinf(kScopeFreq3 * x + kScopePhaseC);
        const float shape_b = sinf(kScopeMorphFreq1 * x + kScopeMorphPhaseA)
                              + 0.42f * sinf(kScopeMorphFreq2 * x + kScopeMorphPhaseB)
                              + 0.22f * sinf(kScopeMorphFreq3 * x + kScopeMorphPhaseC);
        float shape = shape_a * (1.0f - morph) + shape_b * morph;
        // Local fixed dents at u-positions — break uniform "bitmap breathing" look.
        const float dent =
            kScopeDent1Amp * scope_tri(u, kScopeDent1Pos, kScopeDent1Width)
            - kScopeDent2Amp * scope_tri(u, kScopeDent2Pos, kScopeDent2Width)
            + kScopeDent3Amp * scope_tri(u, kScopeDent3Pos, kScopeDent3Width);
        shape += dent * dent_mix;
        const float y = clampf(cy + env_px * shape * taper, y_lo, y_hi);
        inst.wave_points[j] = make_point(x, y);
    }
    if (inst.wave_line) {
        lv_line_set_points(inst.wave_line, inst.wave_points, kPointCount);
    }

    // Opacity = gate × (base + scope_act punch + peak punch); clamped to 95%.
    // Quantized → restyle only on step change. / Яркость квантована — рестайл на смену шага.
    int opa_p = static_cast<int>(
        inst.audio.gate * (kScopeOpacityBase
                           + kScopeOpacityActivity * scope_act
                           + kScopeOpacityPeak     * inst.audio.peak) + 0.5f);
    if (opa_p > 95) opa_p = 95;
    const int8_t q = static_cast<int8_t>(opa_p);
    if (q != inst.opa_q) {
        inst.opa_q = q;
        const YoRadioPalette& pal = yoradio_palette();
        if (inst.wave_line)
            lv_obj_set_style_line_opa(inst.wave_line, rail_opa(pal, opa_p), LV_PART_MAIN);
    }
    // inst.phase not used for OscilloscopeLine; do NOT advance it here.
    // inst.phase не используется для OscilloscopeLine — не двигаем.
}

void tick_fence(Instance& inst, const RailProfileConfig& cfg) {
    const float w  = static_cast<float>(inst.rail_w);
    const float dx = w / static_cast<float>(kFenceCount);
    const float y_lo = y_min_bound(inst);
    const float y_hi = y_max_bound(inst);

    // E22N: slow center-of-gravity drift — whole field breathes up/down ±~1.9 px.
    // Two incommensurate LFO periods → quasi-periodic, not mechanical.
    // E22N: медленный дрейф центра тяжести (±~1.9 px); два несоизмеримых периода LFO —
    // движение квазипериодическое, не механическое.
    inst.lfo_a += k_fence_lfo_a_step;
    if (inst.lfo_a >= k_two_pi) inst.lfo_a -= k_two_pi;
    inst.lfo_b += k_fence_lfo_b_step;
    if (inst.lfo_b >= k_two_pi) inst.lfo_b -= k_two_pi;
    const float cy = static_cast<float>(inst.center_y)
                     + k_fence_drift_a * sinf(inst.lfo_a)
                     + k_fence_drift_b * sinf(inst.lfo_b + 0.9f);

    // E23A5: fence_act soft-limits raw activity; h_drive uses activity/peak for fast
    // breathing (level kept low — slow envelope). gate→0 on STOP: height+opacity→0.
    const float fence_act = fence_display_activity(inst.audio.activity);
    float h_drive = clamp01(kFenceHeightQuiet
                            + kFenceHeightLevel    * inst.audio.level
                            + kFenceHeightActivity * fence_act
                            + kFenceHeightPeak     * inst.audio.peak);
    h_drive *= inst.audio.gate;
    const float visual_scale = kFenceVisualMin + kFenceVisualRange * h_drive;
    const bool field_visible = (inst.audio.gate > kGateHideEps);

    // Active/peak syncs columns; quiet keeps organic per-column variation.
    const float sync = clamp01(kFenceSyncActivity * fence_act
                               + kFenceSyncPeak * inst.audio.peak);
    const float irreg_mix = 1.0f - kFenceIrregReduce * sync;

    // Field opacity — quantized; vignette per column on restyle only.
    int field_opa = static_cast<int>(
        inst.audio.gate * (kFenceOpacityBase
                           + kFenceOpacityActivity * fence_act
                           + kFenceOpacityPeak * inst.audio.peak) + 0.5f);
    if (field_opa > 95) field_opa = 95;
    const int8_t opa_q = static_cast<int8_t>(field_opa);
    const bool opa_changed = (opa_q != inst.opa_q);
    if (opa_changed) inst.opa_q = opa_q;

    const YoRadioPalette& pal = yoradio_palette();

    for (int i = 0; i < kFenceCount; ++i) {
        if (!inst.seg_line[i]) continue;
        show_obj(inst.seg_line[i], field_visible);
        if (!field_visible) continue;
        const float fi = static_cast<float>(i);
        const float x = dx * (fi + 0.5f);
        const float u = (fi + 0.5f) / static_cast<float>(kFenceCount);
        // Two travelling waves + static per-column variation / две бегущие волны + статика.
        const float s1 = sinf(0.45f * fi + inst.phase);
        const float s2 = sinf(0.17f * fi - 0.7f * inst.phase + 1.3f);
        // E22N: irregularity + vignette; E23A2: irregular terms are damped by irreg_mix
        // under peak so the field visibly moves together on hits.
        const float hf = (0.72f + (0.20f * sinf(fi * 1.7f) + 0.08f * sinf(fi * 4.3f + 0.7f)) * irreg_mix)
                         * fence_vignette(u);
        const float rail_half = (y_hi - y_lo) * 0.5f;
        const float wave = 0.5f + 0.5f * (0.58f * (0.5f + 0.5f * s1) + 0.42f * (0.5f + 0.5f * s2));
        const float half_frac = k_fence_height_floor
                                + (k_fence_height_peak - k_fence_height_floor) * wave;
        // E23A5: visual_scale maps h_drive to rail fraction; wave+hf give organic spread.
        float half = rail_half * half_frac * hf * visual_scale;
        if (half > rail_half) half = rail_half;
        inst.seg_points[i][0] = make_point(x, clampf(cy - half, y_lo, y_hi));
        inst.seg_points[i][1] = make_point(x, clampf(cy + half, y_lo, y_hi));
        lv_line_set_points(inst.seg_line[i], inst.seg_points[i], 2);
        if (opa_changed) {
            int col_op = static_cast<int>(static_cast<float>(field_opa) * fence_vignette(u) + 0.5f);
            if (col_op < kFenceOpacityMin) col_op = kFenceOpacityMin;
            lv_obj_set_style_line_opa(inst.seg_line[i], rail_opa(pal, col_op), LV_PART_MAIN);
        }
    }
    inst.phase += cfg.phase_step;
    if (inst.phase >= k_two_pi) inst.phase -= k_two_pi;
}

// ---- Per-profile tick dispatch / диспетчер tick по режимам ----

void tick_profile(Instance& inst, const RailProfileConfig& cfg) {
    switch (cfg.mode) {
        case RailMode::FullWave: tick_full_wave(inst, cfg); break;
        case RailMode::Fence:    tick_fence(inst, cfg);     break;
    }
}

// ---- Static styles + visibility on profile enter / theme change ----
// Статика (цвет/ширина/opacity) задаётся при входе в профиль и смене темы;
// tick меняет только координаты.
// E22M: цвет = pal.rail_accent, вся прозрачность через rail_opa (множитель темы).

static void style_seg_line(lv_obj_t* line, lv_color_t col, lv_opa_t opa) {
    lv_obj_set_style_line_color(line, col, LV_PART_MAIN);
    lv_obj_set_style_line_width(line, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(line, opa, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
}

void apply_styles_and_visibility(Instance& inst, const RailProfileConfig& cfg) {
    const YoRadioPalette& pal = yoradio_palette();
    const lv_color_t      col = pal.rail_accent;

    show_obj(inst.wave_line, false);

    switch (cfg.mode) {
        case RailMode::FullWave:
            // OscilloscopeLine — single wave_line, all seg_lines hidden.
            if (inst.wave_line) {
                lv_obj_set_style_line_color(inst.wave_line, col, LV_PART_MAIN);
                lv_obj_set_style_line_width(inst.wave_line, kScopeLineWidth, LV_PART_MAIN);
                lv_obj_set_style_line_opa(inst.wave_line, rail_opa(pal, k_fw_main_opa), LV_PART_MAIN);
                lv_obj_set_style_line_rounded(inst.wave_line, true, LV_PART_MAIN);
                show_obj(inst.wave_line, true);
            }
            for (int i = 0; i < kSegLines; ++i) show_obj(inst.seg_line[i], false);
            break;

        case RailMode::Fence:
            // FenceTremor — kFenceCount seg_lines, wave_line hidden.
            show_obj(inst.wave_line, false);
            for (int i = 0; i < kFenceCount; ++i) {
                if (!inst.seg_line[i]) continue;
                // E22P1: opacity slightly down — taller columns carry visibility.
                const int base = 20 + ((i * 7) % 3) * 4; // 20/24/28%
                const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(kFenceCount);
                const int op = static_cast<int>(static_cast<float>(base) * fence_vignette(u));
                style_seg_line(inst.seg_line[i], col, rail_opa(pal, op < kFenceOpacityMin ? kFenceOpacityMin : op));
                lv_obj_set_style_line_width(inst.seg_line[i], kFenceLineWidth, LV_PART_MAIN);
            }
            // Hide any extra seg_lines beyond kFenceCount (should not exist, but guard).
            for (int i = kFenceCount; i < kSegLines; ++i) show_obj(inst.seg_line[i], false);
            // Show fence columns (tick will set initial geometry immediately after).
            for (int i = 0; i < kFenceCount; ++i) {
                if (inst.seg_line[i]) show_obj(inst.seg_line[i], true);
            }
            break;
    }
}

// Reset Instance in place. `inst = Instance{}` would materialize a ~2 KB
// temporary on the DspTask stack. Placement new avoids the temp.
// Сброс Instance на месте: `inst = Instance{}` создаёт временный объект на стеке DspTask.
static void reset_instance(Instance& inst) {
    new (&inst) Instance();
}

void prep_line(lv_obj_t* line) {
    if (!line) return;
    lv_obj_remove_style_all(line);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_size(line, LV_PCT(100), LV_PCT(100));
}

#if PRESENCE_RAIL_PERF_PROBE
// E23A14 preflight: accumulate tick_profile() cost per active profile; print a 5 s summary
// with core id + task name + heap snapshot. Static-local accumulators only — no heap.
// E23A14: накапливаем стоимость tick_profile() по активному профилю; раз в 5 с печатаем
// сводку (core/task/heap). Только статические локальные — без heap.
static void rail_perf_account(const Instance& inst, uint32_t dt_us) {
    static uint32_t s_min[kProfileCount] = {0xFFFFFFFFu, 0xFFFFFFFFu};
    static uint32_t s_max[kProfileCount] = {0u, 0u};
    static uint64_t s_sum[kProfileCount] = {0u, 0u};
    static uint32_t s_cnt[kProfileCount] = {0u, 0u};
    static uint32_t s_last_ms = 0u;

    const uint8_t p = static_cast<uint8_t>(inst.style) % kProfileCount;
    if (dt_us < s_min[p]) s_min[p] = dt_us;
    if (dt_us > s_max[p]) s_max[p] = dt_us;
    s_sum[p] += dt_us;
    s_cnt[p] += 1u;

    const uint32_t now = millis();
    if (s_last_ms == 0u) s_last_ms = now;
    if (now - s_last_ms < 5000u) return;
    s_last_ms = now;

    const RailProfileConfig cfg = getProfileConfig(inst.style);
    const uint32_t avg = s_cnt[p] ? static_cast<uint32_t>(s_sum[p] / s_cnt[p]) : 0u;
    const float load = (cfg.timer_ms > 0)
        ? (100.0f * static_cast<float>(avg) / (static_cast<float>(cfg.timer_ms) * 1000.0f))
        : 0.0f;
    Serial.printf("[RailPerf] profile %u/%u %s core=%d task=%s tick_us avg=%u max=%u min=%u n=%u period=%ums load=%.2f%%\n",
                  static_cast<unsigned>(p), static_cast<unsigned>(kProfileCount), effectName(inst.style),
                  xPortGetCoreID(), pcTaskGetName(nullptr),
                  avg, s_max[p], (s_min[p] == 0xFFFFFFFFu ? 0u : s_min[p]), s_cnt[p],
                  static_cast<unsigned>(cfg.timer_ms), load);
    Serial.printf("[RailMem] internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    // Reset only the active profile's window so each summary reflects recent samples.
    s_min[p] = 0xFFFFFFFFu; s_max[p] = 0u; s_sum[p] = 0u; s_cnt[p] = 0u;
}
#endif

void rail_timer_cb(lv_timer_t* t) {
    Instance* inst = static_cast<Instance*>(t->user_data);
    if (!inst || !inst->root) return;

    const bool vu = config.store.vumeter;
    const bool sa = config.store.usespectrum;

    if (vu != inst->vu_on) {
        inst->vu_on = vu;
        show_obj(inst->root, vu);
        if (vu) logProfile(*inst, "VU=ON ");
        else    Serial.printf("[PresenceRail] VU=OFF hidden\n");
    }

    // usespectrum change → direct profile select (legacy EEPROM semantics).
    // Смена usespectrum → прямой выбор профиля (как в старом YoRadio).
    if (sa != inst->sa_last) {
        inst->sa_last = sa;
        const PresenceRailStyle target = style_from_usespectrum(sa);
        if (target != inst->style) {
            setStyle(*inst, target);
            logProfile(*inst, "");
        }
    }

    if (!vu) return;
    if (!inst->active) return;
    if (lv_obj_has_flag(inst->root, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_get_screen(inst->root) != lv_scr_act()) return;
    const displayMode_e m = display.mode();
    if (m != PLAYER && m != VOL) return;

    // E23A: refresh VU envelopes right before the tick (DspTask only, read-only getter).
    update_audio_features(*inst);

    const RailProfileConfig cfg = getProfileConfig(inst->style);
#if PRESENCE_RAIL_PERF_PROBE
    const int64_t _t0 = esp_timer_get_time();
    tick_profile(*inst, cfg);
    rail_perf_account(*inst, static_cast<uint32_t>(esp_timer_get_time() - _t0));
#else
    tick_profile(*inst, cfg);
#endif
}

} // namespace

void create(lv_obj_t* parent, Instance& out) {
    reset_instance(out);
    if (!parent) return;

    lv_obj_update_layout(parent);
    lv_coord_t w = lv_obj_get_content_width(parent);
    lv_coord_t h = lv_obj_get_content_height(parent);
    if (w <= 0) {
        w = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width)
            - 2 * static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);
    }
    if (h <= 0) h = 36;
    out.rail_w   = w;
    out.rail_h   = h;
    out.center_y = static_cast<lv_coord_t>(h / 2);

    out.root = lv_obj_create(parent);
    if (!out.root) { reset_instance(out); return; }
    lv_obj_remove_style_all(out.root);
    lv_obj_set_size(out.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(out.root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(out.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(out.root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(out.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(out.root, LV_OBJ_FLAG_CLICKABLE);

    // E23A11: 1 wave_line (OscilloscopeLine) + kSegLines seg_lines (FenceTremor).
    // E23A11: 1 wave_line (осциллограф) + kSegLines seg_lines (забор).
    out.wave_line = lv_line_create(out.root);
    bool ok = (out.wave_line != nullptr);
    for (int s = 0; s < kSegLines; ++s) {
        out.seg_line[s] = lv_line_create(out.root);
        if (!out.seg_line[s]) ok = false;
    }
    if (!ok) { destroy(out); return; }
    prep_line(out.wave_line);
    for (int s = 0; s < kSegLines; ++s) prep_line(out.seg_line[s]);

    out.style   = style_from_usespectrum(config.store.usespectrum);
    out.vu_on   = config.store.vumeter;
    out.sa_last = config.store.usespectrum;

    const RailProfileConfig cfg = getProfileConfig(out.style);
    reset_effect_state(out, cfg);
    apply_styles_and_visibility(out, cfg);
    tick_profile(out, cfg); // set initial points / первичная установка координат

    show_obj(out.root, out.vu_on);

    if (out.vu_on) logProfile(out, "init VU=ON ");
    else           Serial.printf("[PresenceRail] init VU=OFF hidden\n");

    out.timer = lv_timer_create(rail_timer_cb, cfg.timer_ms, &out);
    if (out.timer) lv_timer_pause(out.timer);
    out.active = false;

#if PRESENCE_RAIL_PERF_PROBE
    // E23A14 preflight: one-shot footprint snapshot right after objects exist.
    // E23A14: одноразовый снимок памяти сразу после создания объектов.
    Serial.printf("[RailMem] after_create instance_bytes=%u objects=%d (1 wave + %d seg) internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
                  static_cast<unsigned>(sizeof(Instance)), 1 + kSegLines, kSegLines,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
#endif
}

void destroy(Instance& inst) {
    if (inst.timer) {
        lv_timer_del(inst.timer);
        inst.timer = nullptr;
    }
    if (inst.root && lv_obj_is_valid(inst.root)) {
        lv_obj_del(inst.root);
    }
    reset_instance(inst);
}

void start(Instance& inst) {
    if (!inst.timer) return;
    inst.active = true;
    lv_timer_resume(inst.timer);
}

void stop(Instance& inst) {
    if (!inst.timer) return;
    inst.active = false;
    lv_timer_pause(inst.timer);
}

void setStyle(Instance& inst, PresenceRailStyle style) {
    if (!inst.root) return;
    // E23A11: clamp style index to 0..1 for safety (handles any stale in-RAM value).
    // E23A11: clamp индекса стиля к 0..1 на случай устаревшего значения в RAM.
    const uint8_t idx = static_cast<uint8_t>(style) % kProfileCount;
    inst.style = static_cast<PresenceRailStyle>(idx);
    const RailProfileConfig cfg = getProfileConfig(inst.style);

    reset_effect_state(inst, cfg);
    apply_styles_and_visibility(inst, cfg);
    if (inst.timer) lv_timer_set_period(inst.timer, cfg.timer_ms);

    // Both remaining profiles set geometry immediately (no empty first period).
    // Оба профиля устанавливают геометрию сразу (без пустого первого периода).
    tick_profile(inst, cfg);

    lv_obj_invalidate(inst.root);
}

void reapplyTheme(Instance& inst) {
    if (!inst.root) return;
    const RailProfileConfig cfg = getProfileConfig(inst.style);
    apply_styles_and_visibility(inst, cfg);
    lv_obj_invalidate(inst.root);
}

} // namespace wgt_presence_rail
} // namespace lvgl_ui
