/*
 * LvglBootScreen — Stage 5.4 structural boot UI (special PageChain mode, not a carousel page).
 * Boot image: RGB565 in lvgl_ui/assets (see bootlogo.md §3 — asset, dimensions, descriptor).
 * Битмап boot: RGB565 в lvgl_ui/assets; замена — bootlogo.md §3 / bootlogo_rus.md §3.
 * Column block: LV_ALIGN_CENTER on screen + kRootLiftY; profile = LV_ACTIVE_PROFILE.
 * Колонка: центр экрана + вертикальный сдвиг kRootLiftY; профиль — LV_ACTIVE_PROFILE.
 * Layout policy: bootlogo.md (EN) / bootlogo_rus.md (RU).
 * Политика раскладки: bootlogo.md (EN) / bootlogo_rus.md (RU).
 */

#include "scr_boot.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "Arduino.h"
#include <cstring>
#include "../profiles/lv_profile_select.h"
#include "../assets/bootlogo_assets.h"

namespace lvgl_ui {

namespace {

// ---------------------------------------------------------------------------
// Boot layout — single source of constants / Раскладка Boot — все константы только здесь
// All values use LV_ACTIVE_PROFILE (width, height, frame_padding).
// Shuttle bar: indeterminate boot indicator only — see bootlogo.md / bootlogo_rus.md.
// Полоска-shuttle: только индикатор «идёт загрузка», не процент выполнения.
// ---------------------------------------------------------------------------

// Center column layout: one root flex container in the middle.
// Центральная колонка: один корневой flex-контейнер по центру.
// Status label: horizontal margin from frame = pad * kStatusPadMul.
// Подпись: отступ по X от pad.
constexpr int32_t kStatusPadMul = 4;
// Vertical gaps between blocks in the column (px). Canonical spacing: bootlogo.md §4.
// Вертикальные зазоры в колонке (px). Эталон — bootlogo.md §4.
constexpr int32_t kGapLogoToStatus = 24;
constexpr int32_t kGapStatusToBar = 38;
// Whole column nudge vs screen center (negative = upward). / Сдвиг всей колонки от центра (минус = вверх).
constexpr int32_t kRootLiftY = -16;

// Indeterminate track + shuttle (decorative only; not bound to boot queue progress).
// Дорожка + бегунок — только визуальный индикатор, не привязка к очереди boot.
constexpr unsigned kBarWidthPercent = 38;
constexpr int32_t kBarHeight = 5;
// Shuttle width + one-way LTR anim duration (ease in-out; playback disabled — see startIndeterminateAnim).
// Ширина бегунка + длительность одного прохода LTR (ease in-out, без обратного хода — startIndeterminateAnim).
constexpr unsigned kShuttleWidthPercent = 55;
constexpr uint32_t kShuttleAnimMs = 1800;

// Use compact boot asset when panel width is at or below this (e.g. JC3248 320×480).
// Компактный ассет при ширине экрана ≤ порога (JC3248 320×480).
constexpr uint16_t kBootLogoSmallAssetMaxScreenW = 320;

// TEMP: always use small boot bitmap (preview). Set false to restore auto by screen width.
// ВРЕМЕННО: всегда малый бутлого для просмотра; false — снова выбор по ширине экрана.
constexpr bool kBootLogoForceSmallAsset = false;

// LVGL 8: lv_img_set_zoom(img, z) uses z=256 for 100% (optional alternative to second asset).
// LVGL 8: zoom 256 = 100%; ~77 ≈ 30% — тяжелее для CPU, возможно мыло; второй битмап обычно чётче.

// Medium asset (170×149) for wide panels; replaces former 211×177 “300” bitmap.
// Средний ассет для широких экранов; вместо прежнего крупного 211×177.
static const lv_img_dsc_t s_boot_logo_dsc_medium = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = YORADIO_BOOTLOGO_MEDIUM_W,
        .h = YORADIO_BOOTLOGO_MEDIUM_H,
    },
    .data_size =
        static_cast<uint32_t>(YORADIO_BOOTLOGO_MEDIUM_W * YORADIO_BOOTLOGO_MEDIUM_H * sizeof(uint16_t)),
    .data = reinterpret_cast<const uint8_t*>(image_data_yoradio_boot_medium),
};

static const lv_img_dsc_t s_boot_logo_dsc_small = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = YORADIO_BOOTLOGO_SMALL_W,
        .h = YORADIO_BOOTLOGO_SMALL_H,
    },
    .data_size = static_cast<uint32_t>(YORADIO_BOOTLOGO_SMALL_W * YORADIO_BOOTLOGO_SMALL_H * sizeof(uint16_t)),
    .data = reinterpret_cast<const uint8_t*>(image_data_yoradio_boot_small),
};

static void boot_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void apply_track_style(lv_obj_t* o) {
    if (!o) return;
    // Zero pad: theme default padding would inset children → shuttle looks centered in the trough.
    // Нулевой pad: иначе тема вдавливает детей → бегунок визуально «с середины» дорожки.
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_make(0x08, 0x08, 0x0d), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(o, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(o, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(o, lv_color_make(0x16, 0x1b, 0x24), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(o, LV_OPA_20, LV_PART_MAIN);
}

static void apply_shuttle_style(lv_obj_t* o) {
    if (!o) return;
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    // Core = “light streak”: one hue, HOR gradient with transition squeezed to mid band (LVGL 8.3 two-stop limit).
    // Ядро — «полоса света»: один оттенок, HOR, переход сжат к середине (только 2 стопа в v8.3).
    // Dark muted edges (same family) → soft bright center; avoids neon “brick” and obvious L/R split.
    // Приглушённые края → мягкий яркий центр; без неоновой «таблетки» и резкого лево/право.
    lv_obj_set_style_bg_color(o, lv_color_make(0x22, 0x42, 0x4c), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(o, lv_color_make(0xa8, 0xe4, 0xef), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    // Full-span L→R: dark at left edge, bright toward +X (motion direction); avoids “blob in the middle”.
    // Градиент на всю ширину: тёмный слева у края, светлее к +X; без пятна только в центре пилюли.
    lv_obj_set_style_bg_main_stop(o, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_stop(o, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    // Light edge bleed only — no heavy “object” shadow.
    // Лёгкое свечение по краю — без тяжёлой тени «объекта».
    lv_obj_set_style_shadow_width(o, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(o, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(o, lv_color_make(0x7a, 0xc8, 0xdc), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(o, 45, LV_PART_MAIN);
}

static void apply_glow_style(lv_obj_t* o) {
    if (!o) return;
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    // Halo-only: body ~ invisible (track-tint + ~0 opa); read as soft bloom, not a second slab.
    // Только ореол: тело почти невидимо; воспринимается как мягкое свечение, не вторая плита.
    lv_obj_set_style_bg_color(o, lv_color_make(0x08, 0x08, 0x0d), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(o, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(o, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(o, lv_color_make(0x5a, 0xa8, 0xc0), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(o, 22, LV_PART_MAIN);
}

// Same math as create() for sizes; anim must not use lv_obj_get_width() before layout (often 0 → no motion).
// Та же геометрия, что при create; до lv_obj_update_layout get_width часто 0 — анимация «стоит».
static inline void boot_bar_geometry(uint16_t screen_w, int32_t* bar_w, int32_t* shuttle_w, int32_t* x_max) {
    *bar_w = static_cast<int32_t>(screen_w * static_cast<int>(kBarWidthPercent) / 100);
    *shuttle_w = LV_MAX(20, *bar_w * static_cast<int>(kShuttleWidthPercent) / 100);
    // End at x == bar_w: glow/shuttle fully leave the track (clipped by parent) before repeat jumps to 0.
    // Конец при x == bar_w: бегунок полностью уезжает вправо (клип родителя), затем цикл с 0.
    *x_max = *bar_w;
}

} // namespace

void LvglBootScreen::shuttleAnimExec(void* var, int32_t x) {
    auto* self = static_cast<LvglBootScreen*>(var);
    if (!self) return;
    // Same x for glow and core: left edges stay aligned; start flush with track’s left (no centered core).
    // Одинаковый x: левые края совпадают; старт у левой границы дорожки (ядро не центрируется в ореоле).
    if (self->_prog_glow && lv_obj_is_valid(self->_prog_glow)) {
        lv_obj_set_x(self->_prog_glow, x);
    }
    if (self->_prog_shuttle && lv_obj_is_valid(self->_prog_shuttle)) {
        lv_obj_set_x(self->_prog_shuttle, x);
    }
}

// Boot-only indeterminate motion — do not reuse as a determinate progress API.
// Только indeterminate для Boot — не использовать как API реального прогресса.
void LvglBootScreen::startIndeterminateAnim() {
    if (!_prog_track || !_prog_shuttle) return;

    lv_anim_del(this, shuttleAnimExec);

    int32_t bar_w = 0, shuttle_w = 0, x_max = 0;
    boot_bar_geometry(LV_ACTIVE_PROFILE.width, &bar_w, &shuttle_w, &x_max);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, shuttleAnimExec);
    lv_anim_set_values(&a, 0, x_max);
    lv_anim_set_time(&a, kShuttleAnimMs);
    // No playback: shuttle always moves LTR; matches HOR gradient (dull→bright along +X). / Без обратного хода: только слева направо, как градиент.
    lv_anim_set_playback_time(&a, 0);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    shuttleAnimExec(this, 0); // explicit x=0 before first tick (ease-in keeps early motion tiny) / явный старт слева
    lv_anim_start(&a);
}

ScreenType LvglBootScreen::screenType() const {
    return ScreenType::Boot;
}

void LvglBootScreen::create() {
    if (_screen) {
        // If screen was auto-deleted by LVGL after handoff, reset stale pointers and recreate.
        // Если LVGL удалил экран через auto_del после handoff — сбрасываем stale-указатели и пересоздаём.
        if (!lv_obj_is_valid(_screen)) {
            _screen = nullptr;
            _root = nullptr;
            _img_logo = _lbl_status = _prog_track = _prog_glow = _prog_shuttle = nullptr;
        } else {
            return;
        }
    }

    const uint16_t W = LV_ACTIVE_PROFILE.width;
    const int32_t pad = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    lv_obj_set_style_bg_color(_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(_screen, LV_SCROLLBAR_MODE_OFF);

    // Root column container: keeps logo+status+bar as one centered block.
    // Корневой контейнер-колонка: лого+статус+бар как единый центрированный блок.
    _root = lv_obj_create(_screen);
    if (_root) {
        lv_obj_set_size(_root, W, LV_SIZE_CONTENT);
        lv_obj_align(_root, LV_ALIGN_CENTER, 0, kRootLiftY);
        lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(_root, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(_root, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(_root, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_root, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(_root, 0, LV_PART_MAIN); // we insert explicit spacers for predictable gaps
        lv_obj_set_layout(_root, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    lv_obj_t* parent = _root ? _root : _screen;

    _img_logo = lv_img_create(parent);
    if (_img_logo) {
        const lv_img_dsc_t* dsc = (kBootLogoForceSmallAsset || W <= kBootLogoSmallAssetMaxScreenW)
                                      ? &s_boot_logo_dsc_small
                                      : &s_boot_logo_dsc_medium;
        lv_img_set_src(_img_logo, dsc);
    }

    // Spacer: logo → status gap.
    // Спейсер: зазор лого → статус.
    lv_obj_t* sp1 = lv_obj_create(parent);
    if (sp1) {
        lv_obj_set_size(sp1, 1, kGapLogoToStatus);
        lv_obj_clear_flag(sp1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(sp1, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(sp1, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(sp1, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(sp1, 0, LV_PART_MAIN);
    }

    _lbl_status = lv_label_create(parent);
    if (_lbl_status) {
        strncpy(_status_text, "Starting...", sizeof(_status_text) - 1);
        _status_text[sizeof(_status_text) - 1] = '\0';
        lv_label_set_text_static(_lbl_status, _status_text);
        lv_obj_set_width(_lbl_status, W - pad * kStatusPadMul);
        // Keep boot status stable (no endless circular scrolling) to avoid heavy relayout churn.
        // Стабильный статус без бесконечной карусели (меньше relayout-нагрузки и артефактов).
        lv_label_set_long_mode(_lbl_status, LV_LABEL_LONG_DOT);
        lv_obj_set_scrollbar_mode(_lbl_status, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_text_align(_lbl_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(_lbl_status, lv_color_make(0xd6, 0xd9, 0xdf), LV_PART_MAIN);
        boot_set_font(_lbl_status, LV_ACTIVE_PROFILE.font_header);
    }

    // Spacer: status → bar gap.
    // Спейсер: зазор статус → бар.
    lv_obj_t* sp2 = lv_obj_create(parent);
    if (sp2) {
        lv_obj_set_size(sp2, 1, kGapStatusToBar);
        lv_obj_clear_flag(sp2, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(sp2, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(sp2, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(sp2, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(sp2, 0, LV_PART_MAIN);
    }

    _prog_track = lv_obj_create(parent);
    _prog_glow = nullptr;
    _prog_shuttle = nullptr;
    if (_prog_track) {
        int32_t bar_w = 0, shuttle_w = 0, x_max_unused = 0;
        boot_bar_geometry(W, &bar_w, &shuttle_w, &x_max_unused);
        lv_obj_set_size(_prog_track, bar_w, kBarHeight);
        lv_obj_clear_flag(_prog_track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(_prog_track, LV_SCROLLBAR_MODE_OFF);
        // Layout id 0 = none — default theme flex would center the shuttle and fight lv_obj_set_x.
        // id 0 — без flex, иначе тема центрирует бегунок и перезаписывает x.
        lv_obj_set_layout(_prog_track, 0);
        apply_track_style(_prog_track);
        // LVGL 8.3 draws children masked to parent coords; x runs to bar_w so the pill exits fully.
        // В v8.3 нет style overflow — маска родителя; x до bar_w, бегунок полностью уезжает вправо.

        _prog_glow = lv_obj_create(_prog_track);
        if (_prog_glow) {
            const int32_t glow_h = kBarHeight + 2;
            lv_obj_set_size(_prog_glow, shuttle_w, glow_h);
            lv_obj_add_flag(_prog_glow, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_align(_prog_glow, LV_ALIGN_DEFAULT);
            lv_obj_set_pos(_prog_glow, 0, (kBarHeight - glow_h) / 2);
            apply_glow_style(_prog_glow);
            lv_obj_clear_flag(_prog_glow, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(_prog_glow, LV_SCROLLBAR_MODE_OFF);
        }

        _prog_shuttle = lv_obj_create(_prog_track);
        if (_prog_shuttle) {
            const int32_t core_w = LV_MAX(12, shuttle_w * 68 / 100);
            const int32_t core_h = LV_MAX(2, kBarHeight - 1);
            lv_obj_set_size(_prog_shuttle, core_w, core_h);
            lv_obj_add_flag(_prog_shuttle, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_align(_prog_shuttle, LV_ALIGN_DEFAULT);
            lv_obj_set_pos(_prog_shuttle, 0, (kBarHeight - core_h) / 2);
            apply_shuttle_style(_prog_shuttle);
            lv_obj_clear_flag(_prog_shuttle, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(_prog_shuttle, LV_SCROLLBAR_MODE_OFF);
        }
    }
}

void LvglBootScreen::enter() {
    if (_screen) {
        lv_obj_update_layout(_screen);
        startIndeterminateAnim();
    }
}

void LvglBootScreen::exit() {
}

void LvglBootScreen::update() {
}

void LvglBootScreen::setStatusUtf8(const char* text) {
    if (!_lbl_status || !text) return;
    if (strcmp(_status_text, text) == 0) return; // avoid redundant invalidation / не дёргать лишний redraw
    strncpy(_status_text, text, sizeof(_status_text) - 1);
    _status_text[sizeof(_status_text) - 1] = '\0';
    lv_label_set_text_static(_lbl_status, _status_text);
}

void LvglBootScreen::onBootSignal() {
    // TEMPORARY: queue still calls this; shuttle ignores signals (indeterminate-only policy).
    // TODO: remove or repurpose once boot queue contract is cleaned up.
    // ВРЕМЕННО: очередь всё ещё вызывает; бегунок не реагирует. TODO: убрать или заменить по контракту очереди.
}

void LvglBootScreen::destroy() {
    lv_anim_del(this, shuttleAnimExec);
    // Lifecycle note: Boot screen object can be auto-deleted by lv_scr_load_anim(..., auto_del=true).
    // Заметка lifecycle: объект Boot-экрана может удаляться самим LVGL через auto_del=true.
    // Therefore destroy() only detaches animation/pointers and never deletes LVGL objects manually.
    // Поэтому destroy() только останавливает анимацию/обнуляет указатели, без ручного lv_obj_del().
    _screen = nullptr;
    _root = nullptr;
    _img_logo = _lbl_status = _prog_track = _prog_glow = _prog_shuttle = nullptr;
}

lv_obj_t* LvglBootScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
