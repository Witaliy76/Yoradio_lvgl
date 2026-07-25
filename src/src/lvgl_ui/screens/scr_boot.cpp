/*
 * LvglBootScreen — fixed-dark transitional Boot UI.
 * Переходный Boot UI с фиксированной тёмной темой.
 *
 * Boot image: RGB565 in lvgl_ui/assets (see bootlogo.md §3 — asset, dimensions, descriptor).
 * Битмап boot: RGB565 в lvgl_ui/assets; замена — bootlogo.md §3 / bootlogo_rus.md §3.
 *
 * Column block: LV_ALIGN_CENTER on screen + kRootLiftY; profile = LV_ACTIVE_PROFILE.
 * Колонка: центр экрана + вертикальный сдвиг kRootLiftY; профиль — LV_ACTIVE_PROFILE.
 *
 * Layout policy: bootlogo.md (EN) / bootlogo_rus.md (RU).
 * Политика раскладки: bootlogo.md (EN) / bootlogo_rus.md (RU).
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_boot.h"

#include "lvgl.h"
#include "Arduino.h"
#include <cstring>
#include "../profiles/lv_profile_select.h"
#include "../assets/bootlogo_assets.h"
#include "../../i18n/i18n.h"

namespace lvgl_ui {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Font resources / Ресурсы шрифтов
// Boot uses the profile header font — preserves profile-based semantics.
// Boot использует шрифт заголовка профиля — сохраняет profile-based семантику.
// ─────────────────────────────────────────────────────────────────────────────

static const lv_font_t* boot_status_font() {
    return static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_header);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixed Boot colors / Фиксированные цвета Boot
// Boot is branded lifecycle UI — fixed dark bg/text, not runtime theme.
// Boot — lifecycle UI с фиксированной тёмной темой; yoradio_palette() не используется.
// ─────────────────────────────────────────────────────────────────────────────

static const lv_color_t kBootFixedBackground  = lv_color_hex(0x000000);
static const lv_color_t kBootFixedStatusText  = lv_color_hex(0xCCCCCC);

// ─────────────────────────────────────────────────────────────────────────────
// Image resources / Ресурсы изображений
// ─────────────────────────────────────────────────────────────────────────────

// Medium asset (170×149) for wide panels; replaces former 211×177 "300" bitmap.
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

// Small asset for compact panels (≤ kBootLogoSmallAssetMaxScreenW).
// Компактный ассет для узких экранов (≤ kBootLogoSmallAssetMaxScreenW).
static const lv_img_dsc_t s_boot_logo_dsc_small = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = YORADIO_BOOTLOGO_SMALL_W,
        .h = YORADIO_BOOTLOGO_SMALL_H,
    },
    .data_size =
        static_cast<uint32_t>(YORADIO_BOOTLOGO_SMALL_W * YORADIO_BOOTLOGO_SMALL_H * sizeof(uint16_t)),
    .data = reinterpret_cast<const uint8_t*>(image_data_yoradio_boot_small),
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout constants / Константы раскладки
// All values use LV_ACTIVE_PROFILE (width, height, frame_padding).
// Все значения используют LV_ACTIVE_PROFILE.
// ─────────────────────────────────────────────────────────────────────────────

// Center column layout: one root flex container in the middle.
// Центральная колонка: один корневой flex-контейнер по центру.
constexpr int32_t kRootLiftY     = -16;   // Whole-column nudge vs screen center (negative = upward) / Сдвиг вверх
constexpr lv_coord_t kSpacerWidth = 1;    // Spacer pixel width (flex-managed height carries the gap) / Ширина спейсера

// Status label horizontal margin: frame_padding × kStatusPadMul.
// Отступ по X для строки статуса: frame_padding × kStatusPadMul.
constexpr int32_t kStatusPadMul = 4;

// Vertical gaps between content blocks in the column.
// Вертикальные зазоры между блоками в колонке.
constexpr lv_coord_t kGapLogoToStatus = 24;
constexpr lv_coord_t kGapStatusToBar  = 38;

// Logo asset selection threshold: panels at or below this width use the small asset.
// Порог выбора ассета: экраны ≤ порога используют компактный ассет.
constexpr uint16_t kBootLogoSmallAssetMaxScreenW = 320;

// Set true to force small asset (preview mode). False = auto-select by screen width.
// true — всегда компактный ассет; false — выбор по ширине экрана.
constexpr bool kBootLogoForceSmallAsset = false;

// LVGL 8 note: lv_img_set_zoom(img, z) uses z=256 for 100%.
// A second bitmap is typically sharper than software scaling.

// ─────────────────────────────────────────────────────────────────────────────
// Animation constants / Константы анимации
// Shuttle bar: indeterminate boot indicator only — not bound to boot queue progress.
// Бегунок: только indeterminate-индикатор; не отражает реальный прогресс очереди.
// ─────────────────────────────────────────────────────────────────────────────

constexpr unsigned  kBarWidthPercent    = 38;
constexpr int32_t   kBarHeight          = 5;
// Shuttle width + one-way LTR animation duration (ease in-out; no playback = LTR only).
// Ширина бегунка + длительность одного прохода LTR (ease in-out, без обратного хода).
constexpr unsigned  kShuttleWidthPercent = 55;
constexpr uint32_t  kShuttleAnimMs       = 1800;

// ─────────────────────────────────────────────────────────────────────────────
// Visual constants (track / glow / shuttle geometry)
// Визуальные константы (геометрия дорожки, ореола, бегунка)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int32_t   kGlowExtraHeight         = 2;   // Glow height = kBarHeight + kGlowExtraHeight
static constexpr int32_t   kShuttleMinWidth          = 20;  // Minimum shuttle pixel width
static constexpr int32_t   kShuttleCoreMinWidth      = 12;  // Minimum shuttle core width
static constexpr int32_t   kShuttleCoreMinHeight     = 2;   // Minimum shuttle core height
static constexpr int32_t   kShuttleCoreWidthPercent  = 68;  // Core = 68% of shuttle width

// ─────────────────────────────────────────────────────────────────────────────
// Generic local helpers / Локальные вспомогательные функции
// ─────────────────────────────────────────────────────────────────────────────

// Transparent spacer for column gap control.
// Column pad_row is set to 0; explicit spacers produce predictable pixel gaps.
// Прозрачный спейсер для управления зазорами в колонке.
// pad_row = 0; явные спейсеры дают точные зазоры в пикселях.
static lv_obj_t* create_transparent_spacer(lv_obj_t* parent, lv_coord_t height) {
    lv_obj_t* sp = lv_obj_create(parent);
    if (!sp) return nullptr;
    lv_obj_set_size(sp, kSpacerWidth, height);
    lv_obj_clear_flag(sp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sp, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sp, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sp, 0, LV_PART_MAIN);
    return sp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Boot visual-style helpers / Стилизация визуальных компонентов Boot
// Fixed colors: not derived from yoradio_palette() — Boot stays dark regardless of theme preset.
// Фиксированные цвета: не из palette — Boot остаётся тёмным при любом пресете темы.
// ─────────────────────────────────────────────────────────────────────────────

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
    // Core = "light streak": one hue, HOR gradient with transition squeezed to mid band (LVGL 8.3 two-stop limit).
    // Ядро — «полоса света»: один оттенок, HOR, переход сжат к середине (только 2 стопа в v8.3).
    // Dark muted edges (same family) → soft bright center; avoids neon "brick" and obvious L/R split.
    // Приглушённые края → мягкий яркий центр; без неоновой «таблетки» и резкого лево/право.
    lv_obj_set_style_bg_color(o, lv_color_make(0x22, 0x42, 0x4c), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(o, lv_color_make(0xa8, 0xe4, 0xef), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    // Full-span L→R: dark at left edge, bright toward +X (motion direction); avoids "blob in the middle".
    // Градиент на всю ширину: тёмный слева у края, светлее к +X; без пятна только в центре пилюли.
    lv_obj_set_style_bg_main_stop(o, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_stop(o, 255, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    // Light edge bleed only — no heavy "object" shadow.
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

// ─────────────────────────────────────────────────────────────────────────────
// Geometry helpers / Вспомогательные функции геометрии
// ─────────────────────────────────────────────────────────────────────────────

// Compute bar/shuttle widths from screen width.
// Must match create_indeterminate_bar() — anim must not call lv_obj_get_width()
// before layout update (returns 0 before first lv_obj_update_layout).
// Вычисляет ширины bar/shuttle из ширины экрана.
// Должна соответствовать create_indeterminate_bar() — до update_layout get_width() = 0.
static inline void boot_bar_geometry(uint16_t screen_w, int32_t* bar_w, int32_t* shuttle_w, int32_t* x_max) {
    *bar_w    = static_cast<int32_t>(screen_w * static_cast<int>(kBarWidthPercent) / 100);
    *shuttle_w = LV_MAX(kShuttleMinWidth, *bar_w * static_cast<int>(kShuttleWidthPercent) / 100);
    // End at x == bar_w: glow/shuttle fully leave the track (clipped by parent) before repeat jumps to 0.
    // Конец при x == bar_w: бегунок полностью уезжает вправо (клип родителя), затем цикл с 0.
    *x_max    = *bar_w;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout builders / Билдеры разметки
// ─────────────────────────────────────────────────────────────────────────────

// Root flex column: centers logo+status+bar as one block on _screen.
// Корневой flex-столбец: центрирует лого+статус+бар как единый блок на экране.
lv_obj_t* LvglBootScreen::create_root_column(LvglBootScreen& self, uint16_t screen_width) {
    if (!self._screen) return nullptr;
    lv_obj_t* root = lv_obj_create(self._screen);
    if (!root) return nullptr;
    lv_obj_set_size(root, screen_width, LV_SIZE_CONTENT);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, kRootLiftY);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    // Explicit spacers carry gaps; row gap = 0 avoids double-spacing.
    // Явные спейсеры несут зазоры; pad_row = 0 исключает двойной отступ.
    lv_obj_set_style_pad_row(root, 0, LV_PART_MAIN);
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return root;
}

// Logo: small or medium asset based on screen width and forced-small flag.
// Лого: компактный или средний ассет по ширине экрана и флагу forced-small.
void LvglBootScreen::create_logo(LvglBootScreen& self, lv_obj_t* parent, uint16_t screen_width) {
    self._img_logo = lv_img_create(parent);
    if (!self._img_logo) return;
    const lv_img_dsc_t* dsc =
        (kBootLogoForceSmallAsset || screen_width <= kBootLogoSmallAssetMaxScreenW)
            ? &s_boot_logo_dsc_small
            : &s_boot_logo_dsc_medium;
    lv_img_set_src(self._img_logo, dsc);
}

// Status: spacer(logo→status) + label + spacer(status→bar).
// Статус: спейсер (лого→статус) + label + спейсер (статус→бар).
void LvglBootScreen::create_status(LvglBootScreen& self, lv_obj_t* parent,
                                   uint16_t screen_width, int32_t frame_padding) {
    // Gap: logo → status
    create_transparent_spacer(parent, kGapLogoToStatus);

    self._lbl_status = lv_label_create(parent);
    if (self._lbl_status) {
        strncpy(self._status_text, i18n::text(i18n::TextId::BootStarting),
                sizeof(self._status_text) - 1);
        self._status_text[sizeof(self._status_text) - 1] = '\0';
        // _status_text is owned by LvglBootScreen; LVGL stores only the pointer (no copy).
        // _status_text принадлежит LvglBootScreen; LVGL хранит только указатель без копирования.
        lv_label_set_text_static(self._lbl_status, self._status_text);
        lv_obj_set_width(self._lbl_status,
            static_cast<lv_coord_t>(screen_width - frame_padding * kStatusPadMul));
        // LV_LABEL_LONG_DOT: stable boot status without circular scrolling (avoids relayout churn).
        // LV_LABEL_LONG_DOT: стабильный статус без карусели (меньше relayout нагрузки).
        lv_label_set_long_mode(self._lbl_status, LV_LABEL_LONG_DOT);
        lv_obj_set_scrollbar_mode(self._lbl_status, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_text_align(self._lbl_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(self._lbl_status, kBootFixedStatusText, LV_PART_MAIN);
        const lv_font_t* font = boot_status_font();
        if (font) {
            lv_obj_set_style_text_font(self._lbl_status, font, LV_PART_MAIN);
        }
    }

    // Gap: status → bar
    create_transparent_spacer(parent, kGapStatusToBar);
}

// Indeterminate bar: track + glow + shuttle (decorative; does not reflect boot queue progress).
// Indeterminate бар: дорожка + ореол + бегунок (декоративный; не отражает прогресс очереди).
void LvglBootScreen::create_indeterminate_bar(LvglBootScreen& self, lv_obj_t* parent, uint16_t screen_width) {
    self._prog_glow    = nullptr;
    self._prog_shuttle = nullptr;

    self._prog_track = lv_obj_create(parent);
    if (!self._prog_track) return;

    int32_t bar_w = 0, shuttle_w = 0, x_max_unused = 0;
    boot_bar_geometry(screen_width, &bar_w, &shuttle_w, &x_max_unused);

    lv_obj_set_size(self._prog_track, bar_w, kBarHeight);
    lv_obj_clear_flag(self._prog_track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(self._prog_track, LV_SCROLLBAR_MODE_OFF);
    // Layout id 0 = none — default theme flex would center the shuttle and fight lv_obj_set_x.
    // id 0 — без flex; иначе тема центрирует бегунок и перезаписывает x.
    lv_obj_set_layout(self._prog_track, 0);
    apply_track_style(self._prog_track);
    // LVGL 8.3 draws children masked to parent coords; x runs to bar_w so the pill exits fully.
    // v8.3: маска родителя; x до bar_w — бегунок полностью уезжает вправо.

    self._prog_glow = lv_obj_create(self._prog_track);
    if (self._prog_glow) {
        const int32_t glow_h = kBarHeight + kGlowExtraHeight;
        lv_obj_set_size(self._prog_glow, shuttle_w, glow_h);
        lv_obj_add_flag(self._prog_glow, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_align(self._prog_glow, LV_ALIGN_DEFAULT);
        lv_obj_set_pos(self._prog_glow, 0, (kBarHeight - glow_h) / 2);
        apply_glow_style(self._prog_glow);
        lv_obj_clear_flag(self._prog_glow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(self._prog_glow, LV_SCROLLBAR_MODE_OFF);
    }

    self._prog_shuttle = lv_obj_create(self._prog_track);
    if (self._prog_shuttle) {
        const int32_t core_w = LV_MAX(kShuttleCoreMinWidth, shuttle_w * kShuttleCoreWidthPercent / 100);
        const int32_t core_h = LV_MAX(kShuttleCoreMinHeight, kBarHeight - 1);
        lv_obj_set_size(self._prog_shuttle, core_w, core_h);
        lv_obj_add_flag(self._prog_shuttle, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_align(self._prog_shuttle, LV_ALIGN_DEFAULT);
        lv_obj_set_pos(self._prog_shuttle, 0, (kBarHeight - core_h) / 2);
        apply_shuttle_style(self._prog_shuttle);
        lv_obj_clear_flag(self._prog_shuttle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(self._prog_shuttle, LV_SCROLLBAR_MODE_OFF);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Animation pipeline / Конвейер анимации
// ─────────────────────────────────────────────────────────────────────────────

void LvglBootScreen::shuttleAnimExec(void* var, int32_t x) {
    auto* self = static_cast<LvglBootScreen*>(var);
    if (!self) return;
    // Same x for glow and core: left edges stay aligned; start flush with track's left (no centered core).
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
    // No playback: shuttle always moves LTR; matches HOR gradient (dull→bright along +X).
    // Без обратного хода: только слева направо, как градиент.
    lv_anim_set_playback_time(&a, 0);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    shuttleAnimExec(this, 0); // explicit x=0 before first tick (ease-in keeps early motion tiny) / явный старт слева
    lv_anim_start(&a);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle / Жизненный цикл
// ─────────────────────────────────────────────────────────────────────────────

ScreenType LvglBootScreen::screenType() const {
    return ScreenType::Boot;
}

void LvglBootScreen::create() {
    if (_screen) {
        // If screen was auto-deleted by LVGL after handoff, reset stale handles and recreate.
        // Если LVGL удалил экран через auto_del после handoff — сбрасываем stale handles и пересоздаём.
        if (!lv_obj_is_valid(_screen)) {
            _nullHandles();
        } else {
            return;
        }
    }

    const uint16_t width         = LV_ACTIVE_PROFILE.width;
    const int32_t  frame_padding = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    // Fixed dark boot — ignores Light/Custom runtime preset (logo on dark context).
    // Фиксированный тёмный Boot; игнорирует Light/Custom пресет (лого на тёмном фоне).
    lv_obj_set_style_bg_color(_screen, kBootFixedBackground, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(_screen, LV_SCROLLBAR_MODE_OFF);

    _root = create_root_column(*this, width);

    // If root allocation failed, children are attached directly to _screen.
    // При ошибке выделения root children прикрепляются к _screen.
    lv_obj_t* parent = _root ? _root : _screen;

    create_logo(*this, parent, width);
    create_status(*this, parent, width, frame_padding);
    create_indeterminate_bar(*this, parent, width);
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

// ─────────────────────────────────────────────────────────────────────────────
// Status pipeline / Конвейер статуса
// ─────────────────────────────────────────────────────────────────────────────

void LvglBootScreen::setStatusUtf8(const char* text) {
    if (!_lbl_status || !text) return;
    if (strcmp(_status_text, text) == 0) return; // avoid redundant invalidation / не дёргать лишний redraw
    strncpy(_status_text, text, sizeof(_status_text) - 1);
    _status_text[sizeof(_status_text) - 1] = '\0';
    lv_label_set_text_static(_lbl_status, _status_text);
}

void LvglBootScreen::onBootSignal() {
    // Boot queue still invokes this callback.
    // The current Boot indicator is intentionally indeterminate,
    // so queue signals do not alter the shuttle.
    // Очередь Boot вызывает этот callback.
    // Бегунок намеренно indeterminate — сигналы его не меняют.
}

// ─────────────────────────────────────────────────────────────────────────────
// Teardown / Завершение
// ─────────────────────────────────────────────────────────────────────────────

void LvglBootScreen::_nullHandles() {
    // Null all LVGL handles without deleting objects.
    // Boot screen may be auto-deleted by LVGL (auto_del=true after lv_scr_load_anim handoff).
    // destroy() and recreate path must never call lv_obj_del on these handles.
    // Обнулить все LVGL handles без удаления объектов.
    // Boot-экран может быть auto-deleted LVGL (auto_del=true при lv_scr_load_anim handoff).
    _screen       = nullptr;
    _root         = nullptr;
    _img_logo     = nullptr;
    _lbl_status   = nullptr;
    _prog_track   = nullptr;
    _prog_glow    = nullptr;
    _prog_shuttle = nullptr;
}

void LvglBootScreen::destroy() {
    // Stop animation before nulling handles; shuttleAnimExec dereferences glow/shuttle.
    // Остановить анимацию до обнуления handles; shuttleAnimExec разыменовывает glow/shuttle.
    lv_anim_del(this, shuttleAnimExec);
    // Lifecycle note: Boot screen object can be auto-deleted by lv_scr_load_anim(..., auto_del=true).
    // Therefore destroy() only detaches animation/pointers and never deletes LVGL objects manually.
    // Заметка lifecycle: объект Boot-экрана может удаляться самим LVGL через auto_del=true.
    // Поэтому destroy() только останавливает анимацию и обнуляет указатели, без ручного lv_obj_del().
    _nullHandles();
}

lv_obj_t* LvglBootScreen::screen() {
    return _screen;
}

} // namespace lvgl_ui
