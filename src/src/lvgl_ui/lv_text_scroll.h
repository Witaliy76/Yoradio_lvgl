/**
 * @file lv_text_scroll.h
 * @brief FU6-A: shared global text-scrolling policy for LVGL auto-scrolling labels.
 *        Owns speed (true px/s), type (Off/Circular/Back-and-forth) and inter-pass delay,
 *        plus the single place that converts px/s into an LVGL animation duration.
 *
 * FU6-A: общая политика автоскролла текста для LVGL-лейблов. Владеет скоростью (реальные px/s),
 * типом (Off/Circular/Back-and-forth) и задержкой между проходами, а также единственным местом
 * пересчёта px/s в длительность анимации LVGL.
 *
 * FU6-A scope: GLOBAL settings only. There is deliberately NO Main sequential coordinator here —
 * Main labels stay independent and may scroll simultaneously, exactly as before this FU.
 * Объём FU6-A: только глобальные настройки. Координатора последовательности Main здесь НЕТ —
 * лейблы Main остаются независимыми и могут скроллиться одновременно, как и до этого FU.
 *
 * @author https://github.com/Witaliy76
 * @license MIT License v1.0, dated 18/04/2026
 */
#ifndef LV_TEXT_SCROLL_H
#define LV_TEXT_SCROLL_H

#include <stdint.h>

#include "lvgl.h"

namespace lvgl_ui {
namespace text_scroll {

// Persisted enum for config.store.text_scroll_type. Values are on-disk contract — do not renumber.
// Значения сохраняются в NVS — не перенумеровывать.
enum class Mode : uint8_t {
    Off          = 0,  // no automatic movement / без автодвижения
    Circular     = 1,  // LV_LABEL_LONG_MODE_SCROLL_CIRCULAR
    BackAndForth = 2,  // LV_LABEL_LONG_MODE_SCROLL
};

// --- Product limits (FU6-A accepted contract) / Границы продукта -------------------------------
inline constexpr uint8_t kSpeedMin     = 10;   // px/s
inline constexpr uint8_t kSpeedMax     = 120;  // px/s
inline constexpr uint8_t kSpeedStep    = 5;    // px/s — UI + persisted snap
inline constexpr uint8_t kSpeedDefault = 40;   // px/s — LVGL's own nominal default

inline constexpr uint8_t kDelayMinSec     = 0;   // 0 = next pass starts immediately
inline constexpr uint8_t kDelayMaxSec     = 10;
inline constexpr uint8_t kDelayDefaultSec = 5;   // matches legacy YoRadio startscrolldelay = 5000 ms

inline constexpr uint8_t kTypeDefault = static_cast<uint8_t>(Mode::Circular);

// --- Sanitizers: total functions, safe on any persisted byte (incl. 0 / 0xFF) ------------------
// Санитайзеры: тотальные функции, безопасны для любого сохранённого байта.
uint8_t sanitizeSpeed(uint8_t raw);
uint8_t sanitizeType(uint8_t raw);
uint8_t sanitizeDelaySec(uint8_t raw);

// --- Sanitized reads of config.store / Чтение config.store с санитайзом ------------------------
uint8_t speedPxPerSec();
Mode    mode();
uint8_t delaySec();

/**
 * Normalize the three persisted bytes in config.store and write back only what actually changed.
 *
 * Every runtime read already sanitizes, so this is belt-and-braces: it exists so the stored bytes
 * and the UI agree after a migration or a corrupted write. Call once at boot, after Config::init().
 *
 * Каждое чтение и так санитизируется; эта функция лишь приводит сохранённые байты в соответствие
 * с UI после миграции или повреждённой записи. Вызывать один раз на старте, после Config::init().
 */
void sanitizeStoredValues();

/**
 * Register an auto-scrolling label and apply the current settings to it immediately.
 *
 * The slot is cleared automatically when the label is deleted (lv_obj_null_on_delete), so screen
 * create/destroy, carousel navigation and Weather/Info rebuilds never leave a stale pointer here.
 * Idempotent: registering the same label twice is a no-op.
 *
 * Регистрирует автоскроллящийся лейбл и сразу применяет текущие настройки. Слот очищается
 * автоматически при удалении лейбла (lv_obj_null_on_delete) — висячих указателей не остаётся.
 */
void registerLabel(lv_obj_t* label);

/** Re-apply current settings to one label. Safe on nullptr / unregistered / deleted objects. */
void applyToLabel(lv_obj_t* label);

/** Re-apply to every live registered label. Call after a settings change (live, no reboot). */
void reapplyAll();

/**
 * Notify that a registered label's text changed.
 *
 * True px/s means the animation duration depends on the CURRENT text width, so it must be
 * recomputed whenever the text changes. No-op for unregistered labels, so screen-wide text
 * funnels can call this unconditionally.
 *
 * Длительность зависит от ТЕКУЩЕЙ ширины текста, поэтому её надо пересчитывать при смене текста.
 * Для незарегистрированных лейблов — no-op.
 */
void notifyTextChanged(lv_obj_t* label);

/**
 * Travel distance in px that the offset animation covers for one pass, 0 when the text fits.
 * Exposed for diagnostics/tests; production callers use applyToLabel().
 */
uint32_t travelPx(lv_obj_t* label, Mode m);

/**
 * True px/s -> plain LVGL duration in ms. Zero travel or zero speed yield 0 (= "LVGL default").
 * Реальные px/s -> обычная длительность LVGL в мс.
 */
uint32_t durationMs(uint32_t travel_px, uint8_t px_per_sec);

}  // namespace text_scroll
}  // namespace lvgl_ui

#endif  // LV_TEXT_SCROLL_H
