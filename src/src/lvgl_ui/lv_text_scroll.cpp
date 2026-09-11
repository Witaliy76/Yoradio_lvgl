/**
 * @file lv_text_scroll.cpp
 * @brief FU6-A shared text-scrolling policy — implementation.
 *
 * @author https://github.com/Witaliy76
 * @license MIT License v1.0, dated 18/04/2026
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "lv_text_scroll.h"

#include "../core/config.h"

// LVGL renders the circular wrap copy at ofs_x + text_w + space_w * LV_LABEL_WAIT_CHAR_COUNT
// (lv_label.c draw_main), and animates the offset over exactly that distance. The macro comes
// from lv_conf.h via lv_conf_internal.h; the fallback mirrors the LVGL default.
// LVGL рисует вторую копию на расстоянии text_w + space_w * LV_LABEL_WAIT_CHAR_COUNT.
#ifndef LV_LABEL_WAIT_CHAR_COUNT
#define LV_LABEL_WAIT_CHAR_COUNT 3
#endif

namespace lvgl_ui {
namespace text_scroll {

namespace {

// LVGL's own turnaround pause for LV_LABEL_LONG_MODE_SCROLL (private LV_LABEL_SCROLL_DELAY in
// lv_label.c). The style template overwrites reverse_delay unconditionally for SCROLL, so we must
// carry this value ourselves or the built-in end-of-travel pause would be lost.
// Пауза LVGL на развороте для SCROLL: шаблон перезаписывает reverse_delay, поэтому значение
// приходится нести самим, иначе штатная пауза на краю пропадёт.
constexpr uint32_t kTurnaroundDelayMs = 300u;

// Upper bound on measured text width before the duration arithmetic is considered nonsensical.
// Keeps travel * 1000 far inside uint32_t and absorbs any bogus measurement.
// Верхняя граница измеренной ширины: держит travel * 1000 внутри uint32_t.
constexpr uint32_t kMaxTravelPx = 200000u;

// FU6-A label registry.
//
// INVARIANT: slots are NEVER compacted or moved. lv_obj_null_on_delete() stores the address of
// `Slot::label`, so that address must stay valid and keep referring to the same registration for
// the whole object lifetime. A freed slot is simply `label == nullptr` and can be reused in place.
//
// ИНВАРИАНТ: слоты НИКОГДА не уплотняются и не перемещаются — lv_obj_null_on_delete() хранит адрес
// поля Slot::label. Освобождённый слот = nullptr и переиспользуется на месте.
constexpr size_t kMaxLabels = 16;

struct Slot {
    lv_obj_t*       label      = nullptr;
    // The screen's own text alignment, so OFF mode can hand it back. Refreshed on every apply while
    // we are NOT overriding it, because screens change alignment at runtime (Main swaps LEFT/CENTER
    // when album art appears or disappears).
    // Исходное выравнивание экрана: обновляется при каждом apply, пока мы его не переопределяем —
    // экраны меняют выравнивание в рантайме (Main переключает LEFT/CENTER при появлении обложки).
    lv_text_align_t align_orig     = LV_TEXT_ALIGN_AUTO;
    bool            align_override = false;
};

Slot s_slots[kMaxLabels];

// Shared style animation template.
//
// Lifetime: function-scope static with static storage duration — alive for the whole program, which
// is required because lv_obj_set_style_anim() stores the POINTER (lv_style_value_t.ptr), not a copy.
// Labels keep dereferencing it on every lv_label_refr_text(), so it must never be a local.
//
// Время жизни: статический объект на весь срок программы. lv_obj_set_style_anim() сохраняет
// УКАЗАТЕЛЬ, а не копию — лейблы разыменовывают его при каждом lv_label_refr_text().
lv_anim_t s_tmpl;
bool      s_tmpl_ready = false;

int findSlot(const lv_obj_t* label) {
    if (label == nullptr) return -1;
    for (size_t i = 0; i < kMaxLabels; ++i) {
        if (s_slots[i].label == label) return static_cast<int>(i);
    }
    return -1;
}

void applyToLabelImpl(lv_obj_t* label, bool may_arm);

// Slot index is passed as user_data, never the object pointer: a deleted label nulls its own slot
// (lv_obj_null_on_delete), so the deferred callback can always tell "gone" from "alive" without
// dereferencing freed memory. +1 so slot 0 is not the NULL user_data wildcard.
// В user_data передаётся индекс слота, а не указатель: удалённый лейбл обнуляет свой слот сам,
// поэтому отложенный колбэк никогда не разыменовывает освобождённую память.
void* slotToken(int idx) { return reinterpret_cast<void*>(static_cast<intptr_t>(idx) + 1); }

void layoutReadyCb(lv_event_t* e) {
    void* token = lv_event_get_user_data(e);
    lv_display_t* disp = static_cast<lv_display_t*>(lv_event_get_current_target(e));

    // One-shot: unhook before doing anything, so this can never become a per-frame loop.
    // Одноразовый: снимаем себя до любых действий — повторов по кадрам не будет.
    if (disp != nullptr) {
        lv_display_remove_event_cb_with_user_data(disp, layoutReadyCb, token);
    }

    const intptr_t idx = reinterpret_cast<intptr_t>(token) - 1;
    if (idx < 0 || idx >= static_cast<intptr_t>(kMaxLabels)) return;

    lv_obj_t* label = s_slots[idx].label;
    if (label == nullptr) return;  // label died before layout ran / лейбл удалён до layout

    // may_arm=false: a single retry. If geometry is still unresolved (e.g. the label is HIDDEN and
    // flex skipped it), do not re-arm — the next real state change (text/settings) will arm again.
    // may_arm=false: одна попытка. Если геометрия всё ещё не готова, не перевзводим.
    applyToLabelImpl(label, /*may_arm=*/false);
}

// Arm exactly one deferred re-apply, fired by LVGL's own layout pass.
// lv_refr.c calls lv_obj_update_layout(act_scr) every refresh cycle, which sends
// LV_EVENT_UPDATE_LAYOUT_COMPLETED unconditionally — the same lifecycle lv_label uses internally.
// No timers, no millisecond guesses.
// Взводим ровно один отложенный повтор через собственный layout-проход LVGL. Без таймеров.
void armLayoutReady(lv_obj_t* label, int idx) {
    lv_display_t* disp = lv_obj_get_display(label);
    if (disp == nullptr) return;
    void* token = slotToken(idx);
    lv_display_remove_event_cb_with_user_data(disp, layoutReadyCb, token);  // never stack
    lv_display_add_event_cb(disp, layoutReadyCb, LV_EVENT_UPDATE_LAYOUT_COMPLETED, token);
}

// Rebuild the shared template from the current persisted delay.
//
// FU6-A uses free-running labels: infinite repeat, no completion callback (a completion handoff is
// FU6-B coordinator territory and is deliberately absent here).
//   repeat_delay  -> pause after a COMPLETE pass, at the home position.
//   reverse_delay -> SCROLL only: pause at the far end before the reverse leg (LVGL semantics kept).
// Circular ignores reverse_delay entirely (see overwrite_anim_property in lv_label.c).
//
// FU6-A: бесконечный повтор, без completion-колбэка (это зона координатора FU6-B).
//   repeat_delay  -> пауза после ПОЛНОГО прохода, в домашней позиции.
//   reverse_delay -> только SCROLL: пауза на дальнем крае перед обратным ходом.
void refreshTemplate() {
    if (!s_tmpl_ready) {
        lv_anim_init(&s_tmpl);
        s_tmpl_ready = true;
    }
    lv_anim_set_repeat_count(&s_tmpl, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&s_tmpl, static_cast<uint32_t>(delaySec()) * 1000u);
    lv_anim_set_reverse_delay(&s_tmpl, kTurnaroundDelayMs);
    lv_anim_set_completed_cb(&s_tmpl, nullptr);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Sanitizers / Санитайзеры
// ---------------------------------------------------------------------------------------------

// Clamp a persisted/UI speed byte into the product range and snap it onto the kSpeedStep grid.
//
// Called on every read of config.store.text_scroll_speed, so it must be total: a migrated device
// arrives here with a zero-filled byte, and a downgrade/upgrade cycle can leave any value at all.
//
// Вызывается при каждом чтении config.store.text_scroll_speed — должна быть тотальной: после
// миграции сюда приходит обнулённый байт, а после отката/обновления — любое значение.
uint8_t sanitizeSpeed(uint8_t raw) {
    // Out-of-range bytes are treated as INVALID, not as "clamp me to the nearest edge": a migrated
    // tail arrives as 0 and corruption arrives as 0xFF, and silently turning either into 10 or 120
    // would ship a valid-looking but wrong speed. Fall back to the shipped default instead — the
    // same policy sanitizeType()/sanitizeDelaySec() use below and the same value _setupVersion seeds.
    //
    // Значения вне диапазона считаются НЕВАЛИДНЫМИ, а не «прижать к краю»: после миграции приходит 0,
    // при повреждении — 0xFF, и превращение их в 10 или 120 дало бы правдоподобную, но неверную
    // скорость. Откатываемся к значению по умолчанию.
    if (raw < kSpeedMin || raw > kSpeedMax) return kSpeedDefault;

    // In-range: snap to the nearest step so UI, runtime and NVS always agree on the same grid.
    // kSpeedMin (10) and kSpeedMax (120) are both multiples of kSpeedStep, so rounding to nearest
    // can never leave the range and no post-clamp is needed.
    // В диапазоне: округляем к ближайшему шагу. 10 и 120 кратны шагу, поэтому округление не может
    // вывести за границы — дополнительный clamp не нужен.
    return static_cast<uint8_t>(((raw + kSpeedStep / 2u) / kSpeedStep) * kSpeedStep);
}

uint8_t sanitizeType(uint8_t raw) {
    // Anything outside the known enum (0 on a freshly migrated tail, 0xFF on corruption) falls back
    // to the shipped default rather than silently disabling scrolling.
    // Всё вне известного enum откатывается к значению по умолчанию, а не отключает скролл молча.
    if (raw > static_cast<uint8_t>(Mode::BackAndForth)) return kTypeDefault;
    return raw;
}

uint8_t sanitizeDelaySec(uint8_t raw) {
    // 0 is a legitimate value here (= gapless, the pre-FU6 behaviour), so only the upper bound and
    // clearly-corrupt values are corrected.
    // 0 — легитимное значение (без паузы, поведение до FU6), поэтому правим только верхнюю границу.
    if (raw > kDelayMaxSec) return kDelayDefaultSec;
    return raw;
}

// ---------------------------------------------------------------------------------------------
// Sanitized config reads / Чтение конфигурации с санитайзом
// ---------------------------------------------------------------------------------------------

uint8_t speedPxPerSec() { return sanitizeSpeed(config.store.text_scroll_speed); }

Mode mode() { return static_cast<Mode>(sanitizeType(config.store.text_scroll_type)); }

uint8_t delaySec() { return sanitizeDelaySec(config.store.text_scroll_delay_s); }

void sanitizeStoredValues() {
    // saveValue() is a no-op when the value already matches, so a healthy device writes nothing and
    // the debounced SaveManager path is never armed on a normal boot.
    // saveValue() ничего не делает при совпадении значения — на здоровом устройстве записи нет.
    const uint8_t speed = sanitizeSpeed(config.store.text_scroll_speed);
    const uint8_t type  = sanitizeType(config.store.text_scroll_type);
    const uint8_t delay = sanitizeDelaySec(config.store.text_scroll_delay_s);

    config.saveValue(&config.store.text_scroll_speed, speed, false);
    config.saveValue(&config.store.text_scroll_type, type, false);
    config.saveValue(&config.store.text_scroll_delay_s, delay, true);

    refreshTemplate();
}

// ---------------------------------------------------------------------------------------------
// True px/s -> duration / Реальные px/s -> длительность
// ---------------------------------------------------------------------------------------------

uint32_t travelPx(lv_obj_t* label, Mode m) {
    if (label == nullptr || m == Mode::Off) return 0u;

    // lv_obj_get_self_width() dispatches LV_EVENT_GET_SELF_SIZE, which the label class answers from
    // its cached text_size — the exact `size.x` lv_label_refr_text() uses to build the animation.
    // lv_obj_get_self_width() возвращает ровно тот size.x, что использует lv_label_refr_text().
    const int32_t text_w    = lv_obj_get_self_width(label);
    const int32_t content_w = lv_obj_get_content_width(label);

    // No overflow -> LVGL deletes the offset animation and parks at 0; there is nothing to time.
    // Нет переполнения -> LVGL удаляет анимацию смещения; время считать не из чего.
    if (text_w <= 0 || content_w <= 0 || text_w <= content_w) return 0u;

    int32_t travel = 0;
    if (m == Mode::Circular) {
        // Circular scrolls the whole string plus the wrap gap, independent of the visible width.
        // Круговой режим прокручивает всю строку плюс зазор до второй копии.
        const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        const int32_t space_w = (font != nullptr) ? lv_font_get_glyph_width(font, ' ', ' ') : 0;
        travel = text_w + space_w * LV_LABEL_WAIT_CHAR_COUNT;
    } else {
        // Back-and-forth only travels the overflow; the reverse leg covers the same distance again.
        // Back-and-forth проходит только переполнение; обратный ход — та же дистанция.
        travel = text_w - content_w;
    }

    if (travel <= 0) return 0u;
    if (static_cast<uint32_t>(travel) > kMaxTravelPx) return kMaxTravelPx;
    return static_cast<uint32_t>(travel);
}

// duration_ms = travel_px * 1000 / speed_px_per_sec.
//
// Why not lv_anim_speed_clamped(): the LVGL packed speed encoding stores min/max duration in two
// 10-bit fields at 10 ms resolution, so it cannot express a pass longer than 10230 ms. At 10 px/s
// that ceiling engages after only ~102 px of travel, which would make the low half of the
// 10..120 px/s range inert. A plain millisecond duration has no such ceiling: lv_anim_resolve_speed()
// passes any value without the 0x80000000 marker bit straight through unchanged.
//
// Почему не lv_anim_speed_clamped(): упакованная кодировка LVGL хранит min/max в 10-битных полях с
// шагом 10 мс и не может выразить проход длиннее 10230 мс. При 10 px/s этот потолок срабатывает уже
// после ~102 px, и нижняя половина диапазона 10..120 px/s перестала бы работать. Обычная
// длительность в мс такого потолка не имеет.
uint32_t durationMs(uint32_t travel_px, uint8_t px_per_sec) {
    if (travel_px == 0u) return 0u;
    if (travel_px > kMaxTravelPx) travel_px = kMaxTravelPx;

    // Guard against a zero/short-circuited speed reaching the divide.
    // Защита от нулевой скорости на входе деления.
    uint32_t speed = px_per_sec;
    if (speed < kSpeedMin) speed = kSpeedMin;

    // travel_px <= 200000 -> travel_px * 1000 <= 2.0e8, far inside uint32_t.
    const uint32_t ms = (travel_px * 1000u) / speed;

    // LVGL maps act_time over [0, duration]; a zero duration would divide by zero in lv_map().
    // 0 недопустим: lv_map() делит на duration.
    return (ms == 0u) ? 1u : ms;
}

// ---------------------------------------------------------------------------------------------
// Application / Применение
// ---------------------------------------------------------------------------------------------

namespace {

void applyToLabelImpl(lv_obj_t* label, bool may_arm) {
    const int idx = findSlot(label);
    if (idx < 0) return;  // unregistered (or already nulled on delete) / не зарегистрирован

    // Defensive second guard: a freed pointer could in principle be reused by a new object before
    // our slot is cleared. lv_obj_is_valid() walks the live object tree, and this path runs only on
    // settings/text changes, never per frame.
    // Вторая защита: lv_obj_is_valid() проверяет, что объект действительно жив.
    if (!lv_obj_is_valid(label)) {
        s_slots[idx].label = nullptr;
        return;
    }

    Slot& slot = s_slots[idx];

    // Track the screen's current alignment while we are not overriding it (see Slot::align_orig).
    // Пока не переопределяем — следим за текущим выравниванием экрана.
    if (!slot.align_override) {
        slot.align_orig = lv_obj_get_style_text_align(label, LV_PART_MAIN);
    }

    const Mode m = mode();

    if (m == Mode::Off) {
        // CLIP is the smallest stationary long mode: lv_label_set_long_mode() deletes both offset
        // animations AND zeroes label->offset in one call, so there is no stale animation and the
        // text parks at home.
        //
        // DOTS was rejected on purpose: lv_label_set_dots() rewrites label->text in place, and
        // lv_label_get_text() then returns the truncated string — which would break the
        // `strcmp(lv_label_get_text(...), new)` redraw guards in Main/Info/Weather and cause a
        // lv_label_set_text() on every refresh tick.
        //
        // CLIP — минимальный статичный режим: lv_label_set_long_mode() удаляет обе анимации
        // смещения И обнуляет label->offset. DOTS отклонён намеренно: он переписывает label->text
        // на месте, ломая защиту от лишней перерисовки в Main/Info/Weather.
        if (lv_label_get_long_mode(label) != LV_LABEL_LONG_MODE_CLIP) {
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
        }

        // Alignment parity with the scrolling modes. LVGL forces CENTER/RIGHT to LEFT while
        // scrolling overflowed text (lv_label.c draw_main) but NOT in CLIP, so a centred label
        // would jump to showing the middle of the string with both ends cut. Force LEFT while it
        // overflows so OFF renders the same first characters the scrolling modes start from.
        //
        // Паритет выравнивания: LVGL принудительно ставит LEFT при скролле переполненного текста,
        // но не в CLIP. Без этого центрированный лейбл показал бы середину строки, обрезанную с двух
        // сторон. Ставим LEFT, пока есть переполнение.
        const bool overflows = lv_obj_get_self_width(label) > lv_obj_get_content_width(label);
        if (overflows) {
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            slot.align_override = true;
        } else if (slot.align_override) {
            lv_obj_set_style_text_align(label, slot.align_orig, LV_PART_MAIN);
            slot.align_override = false;
        }
        // Same forced re-read as the scrolling path below: guarantees the stop is applied even when
        // the label was already CLIP and only the alignment changed.
        // Тот же принудительный re-read, что и ниже: гарантирует применение остановки.
        lv_obj_refresh_style(label, LV_PART_MAIN, LV_STYLE_PROP_ANY);
        return;
    }

    // Leaving OFF: hand the alignment back and let LVGL apply its own scrolling override.
    // Выход из OFF: возвращаем выравнивание, дальше LVGL решает сам.
    if (slot.align_override) {
        lv_obj_set_style_text_align(label, slot.align_orig, LV_PART_MAIN);
        slot.align_override = false;
    }

    const lv_label_long_mode_t want = (m == Mode::Circular) ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR
                                                            : LV_LABEL_LONG_MODE_SCROLL;
    if (lv_label_get_long_mode(label) != want) {
        lv_label_set_long_mode(label, want);
    }

    // ---- Geometry gate -----------------------------------------------------------------------
    //
    // Before the first layout pass lv_obj_constructor leaves coords as x2 = x1 - 1, so
    // lv_obj_get_width() is 0 and lv_obj_get_content_width() is 0 or negative. travelPx() then
    // returns 0 and durationMs() returns 0 — and a zero anim_duration is NOT neutral: lv_label.c
    // does `if (anim_time == 0) anim_time = LV_LABEL_DEF_SCROLL_SPEED;`, i.e. it silently falls
    // back to lv_anim_speed_clamped(40, 300, 10000), whose 10 s ceiling makes a long line scroll
    // far faster than any persisted setting. So: never derive a duration from unresolved geometry.
    //
    // Перед первым layout ширина объекта равна 0, поэтому travelPx()/durationMs() дают 0, а нулевая
    // anim_duration НЕ нейтральна — LVGL подставляет собственную скорость по умолчанию. Поэтому
    // длительность не вычисляем, пока геометрия не готова.
    if (lv_obj_get_content_width(label) <= 0) {
        if (may_arm) armLayoutReady(label, idx);
        return;
    }

    // Duration is per-label: it depends on this label's current text width and font.
    // Длительность индивидуальна: зависит от текущей ширины текста и шрифта именно этого лейбла.
    const uint32_t want_ms = durationMs(travelPx(label, m), speedPxPerSec());
    if (lv_obj_get_style_anim_duration(label, LV_PART_MAIN) != want_ms) {
        lv_obj_set_style_anim_duration(label, want_ms, LV_PART_MAIN);
    }

    refreshTemplate();
    lv_obj_set_style_anim(label, &s_tmpl, LV_PART_MAIN);

    // ---- Live-reapply: force the label to re-read the animation style -------------------------
    //
    // Setting LV_STYLE_ANIM_DURATION / LV_STYLE_ANIM is NOT enough on its own. Both properties
    // carry ZERO flags in LVGL's property table (lv_style.c: `[LV_STYLE_ANIM_DURATION] = 0`, and
    // LV_STYLE_ANIM has no entry, so it defaults to 0). lv_obj_refresh_style() only emits
    // LV_EVENT_STYLE_CHANGED inside `if (is_layout_refr)`, i.e. only for properties flagged
    // LV_STYLE_PROP_FLAG_LAYOUT_UPDATE. So writing these two props silently skips the event,
    // lv_label_event() never calls lv_label_mark_need_refr_text(), lv_label_refr_text() never
    // runs, and the RUNNING animation keeps its old duration and old template forever.
    // (That is why leaving and re-entering a screen "fixed" it: creating the label calls
    // lv_label_set_text() -> mark_need_refr_text() directly, which re-reads the current style.)
    //
    // LV_STYLE_PROP_ANY is the documented escape hatch: lv_style_prop_lookup_flags() returns
    // LV_STYLE_PROP_FLAG_ALL for it, so refresh_style() does emit LV_EVENT_STYLE_CHANGED and the
    // label arms its deferred refresh. lv_refr.c already calls lv_obj_update_layout(act_scr) every
    // refresh cycle, which fires LV_EVENT_UPDATE_LAYOUT_COMPLETED and runs lv_label_refr_text().
    // Staying asynchronous matters: refr_text carries the old act_time forward (clamped), so a
    // speed change retimes smoothly instead of snapping the text back home.
    //
    // Установки LV_STYLE_ANIM_DURATION / LV_STYLE_ANIM недостаточно: у обоих свойств нулевые флаги,
    // поэтому lv_obj_refresh_style() не шлёт LV_EVENT_STYLE_CHANGED, lv_label_refr_text() не
    // вызывается, и работающая анимация навсегда сохраняет старую длительность и шаблон. Именно
    // поэтому выход и повторный вход «чинили» скролл. LV_STYLE_PROP_ANY возвращает все флаги —
    // событие уходит, лейбл ставит отложенный refresh, а lv_refr выполняет его на следующем кадре.
    lv_obj_refresh_style(label, LV_PART_MAIN, LV_STYLE_PROP_ANY);
}

}  // namespace

void applyToLabel(lv_obj_t* label) { applyToLabelImpl(label, /*may_arm=*/true); }

void registerLabel(lv_obj_t* label) {
    if (label == nullptr) return;
    if (findSlot(label) >= 0) {
        applyToLabel(label);  // already registered — just refresh / уже зарегистрирован
        return;
    }

    for (size_t i = 0; i < kMaxLabels; ++i) {
        if (s_slots[i].label != nullptr) continue;

        s_slots[i].label = label;
        // Capture the screen's own alignment BEFORE this module ever touches it. Callers must
        // therefore register AFTER they have finished styling the label (width, font, align).
        // Запоминаем исходное выравнивание до наших правок — регистрировать ПОСЛЕ настройки лейбла.
        s_slots[i].align_orig     = lv_obj_get_style_text_align(label, LV_PART_MAIN);
        s_slots[i].align_override = false;

        // LVGL clears the slot for us when the object dies. The address handed over is a static
        // array member, which satisfies lv_obj_null_on_delete()'s "must outlive the object" rule.
        // LVGL сам обнулит слот при удалении объекта; адрес — член статического массива.
        lv_obj_null_on_delete(&s_slots[i].label);

        applyToLabel(label);
        return;
    }
    // Registry full: the label keeps LVGL defaults rather than failing the screen build.
    // Реестр заполнен: лейбл остаётся на значениях LVGL по умолчанию.
    LV_LOG_WARN("text_scroll: registry full, label not registered");
}

void reapplyAll() {
    refreshTemplate();
    for (size_t i = 0; i < kMaxLabels; ++i) {
        if (s_slots[i].label != nullptr) applyToLabel(s_slots[i].label);
    }
}

void notifyTextChanged(lv_obj_t* label) {
    if (label == nullptr) return;
    if (findSlot(label) < 0) return;  // not an FU6 label / не наш лейбл
    applyToLabel(label);
}

}  // namespace text_scroll
}  // namespace lvgl_ui
