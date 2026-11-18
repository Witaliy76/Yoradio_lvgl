// Translucent bottom source carousel. The center tile calls Player::activate.
// A side tile or a horizontal swipe only moves focus. Planned tiles do not activate.
// STOP, file probes and audio start stay on the player task.
// Полупрозрачная нижняя карусель источников. Центральная плитка вызывает Player::activate.
// Боковая плитка и горизонтальный свайп только двигают фокус. Planned не активируют.
// STOP, проверка файла и старт звука остаются в задаче плеера.

#include "source_overlay.h"

#include <cstdint>

#include "control_glyph_utf8.h"
#include "font_provider.h"
#include "profiles/resolved_ui_metrics.h"
#include "source_glyph_utf8.h"
#include "theme/lv_theme_yoradio.h"

#include "lvgl.h"

#include "../core/config.h"
#include "../core/player.h"
#include "../i18n/i18n.h"
#include "../platform/storage/storage_owner.h"

namespace lvgl_ui {
namespace {

constexpr uint8_t kMaxTiles = 16;
static_assert(yoradio::source::kCatalogCount <= kMaxTiles, "source carousel tile cap");

lv_obj_t* s_scrim = nullptr;
lv_obj_t* s_panel = nullptr;
lv_obj_t* s_row = nullptr;
lv_obj_t* s_reason = nullptr;
lv_obj_t* s_tiles[kMaxTiles] = {};
lv_obj_t* s_icons[kMaxTiles] = {};
lv_obj_t* s_labels[kMaxTiles] = {};
lv_obj_t* s_speakers[kMaxTiles] = {};
lv_obj_t* s_dots[kMaxTiles] = {};
lv_obj_t* s_arrow_prev = nullptr;
lv_obj_t* s_arrow_next = nullptr;
yoradio::source::SourceId s_tile_ids[kMaxTiles] = {};
uint8_t s_slots[kMaxTiles] = {};
uint8_t s_tile_count = 0;
uint8_t s_focus = 0;

OverlayView s_view;
OverlayCommand s_deferred = OverlayCommand::None;
uint8_t s_painted[kMaxTiles] = {};
uint8_t s_indicator_focus = 0xffu;
uint8_t s_reason_key = 0xffu;
yoradio::source::ActivateOutcome s_reason_painted_outcome = yoradio::source::ActivateOutcome::Cancelled;
bool s_planned_notice = false;

lv_point_t s_press_point{};
bool s_press_valid = false;
lv_coord_t s_press_scroll_x = 0;
bool s_dragged = false;
bool s_programmatic = false;

const char* reasonText(yoradio::source::ActivateOutcome outcome) {
    using yoradio::source::ActivateOutcome;
    switch (outcome) {
    case ActivateOutcome::Busy:
        return i18n::text(i18n::TextId::BrowserPlayBusy);
    case ActivateOutcome::Unavailable:
        return i18n::text(i18n::TextId::PresetUnavailable);
    case ActivateOutcome::StaleMedia:
        return i18n::text(i18n::TextId::BrowserPlayRetry);
    case ActivateOutcome::StopFailed:
        return i18n::text(i18n::TextId::BrowserPlayStopFailed);
    case ActivateOutcome::OpenFailed:
    case ActivateOutcome::Cancelled:
    case ActivateOutcome::Superseded:
        return i18n::text(i18n::TextId::BrowserPlayFailed);
    default:
        return i18n::text(i18n::TextId::BrowserPlayFailed);
    }
}

// One-line tile captions. Technical names stay untranslated; full names stay in i18n
// for WebUI and the media browser.
// Однострочные подписи плиток. Технические имена не переводятся; полные имена
// остаются в i18n для WebUI и медиабраузера.
const char* tileTitle(yoradio::source::SourceId id) {
    using yoradio::source::SourceId;
    switch (id) {
    case SourceId::Radio: return i18n::text(i18n::TextId::SourceShortRadio);
    case SourceId::Sd: return "SD";
    case SourceId::Usb: return "USB";
    case SourceId::Dlna: return "DLNA";
    case SourceId::Nas: return "NAS";
    case SourceId::Yandex: return i18n::text(i18n::TextId::SourceShortYandex);
    case SourceId::Spotify: return "Spotify";
    case SourceId::AirPlay: return "AirPlay";
    case SourceId::Bluetooth: return "BT";
    case SourceId::Aux: return "AUX";
    case SourceId::I2s: return "I2S";
    }
    const yoradio::source::SourceEntry* entry = yoradio::source::find(id);
    return entry ? entry->title : "";
}

bool sourceAvailable(yoradio::source::SourceId id) {
    if (!yoradio::source::isImplemented(id)) return false;
    if (id == yoradio::source::SourceId::Radio) return config.store.countStation > 0;
    if (id == yoradio::source::SourceId::Sd) {
        return yoradio::storage::StorageOwner::instance().status().ready;
    }
    return false;
}

void markDirty() {
    for (uint8_t i = 0; i < kMaxTiles; ++i) s_painted[i] = 0xffu;
    s_indicator_focus = 0xffu;
    s_reason_key = 0xffu;
}

void styleTile(uint8_t index) {
    lv_obj_t* tile = s_tiles[index];
    lv_obj_t* icon = s_icons[index];
    lv_obj_t* label = s_labels[index];
    if (!tile || !icon || !label) return;
    const bool planned = !yoradio::source::isImplemented(s_tile_ids[index]);
    const bool available = sourceAvailable(s_tile_ids[index]);
    yoradio::source::SourceId selected = yoradio::source::SourceId::Radio;
    yoradio::source::SourceId sounding = yoradio::source::SourceId::Radio;
    const bool has_selected = player.selectedSource(selected);
    const bool has_sounding = player.soundingSource(sounding);
    const bool is_selected = has_selected && selected == s_tile_ids[index];
    const bool is_sounding = has_sounding && sounding == s_tile_ids[index];
    const bool focused = index == s_focus;
    const TileChrome chrome = tileChrome(available, is_selected, is_sounding, s_view.awaiting);
    const uint8_t painted = static_cast<uint8_t>((focused ? 1u : 0u) |
                                                 (chrome.selected ? 2u : 0u) |
                                                 (chrome.sounding ? 4u : 0u) |
                                                 (planned ? 8u : 0u) |
                                                 (chrome.available ? 16u : 0u));
    if (s_painted[index] == painted) return;
    s_painted[index] = painted;
    const YoRadioPalette& pal = yoradio_palette();
    // One frame per tile. Focus: accent border 2 px on accent_soft. Selected
    // (also when stopped): accent icon + a soft 1 px accent border off-center.
    // Playing: corner speaker only, from Player::soundingSource, never from a
    // tap or Pending. Planned stays muted and is not "unavailable SD".
    // Одна рамка на плитку. Focus: акцентная рамка 2 px на accent_soft. Selected
    // (и остановленный): акцентная иконка + мягкая рамка 1 px вне центра.
    // Playing: только динамик в углу, по Player::soundingSource, не по нажатию
    // и не по Pending. Planned приглушён и не выглядит как недоступная SD.
    lv_obj_set_style_bg_color(tile, focused ? pal.accent_soft : pal.overlay_card_bg, LV_PART_MAIN);
    lv_opa_t bg_opa = LV_OPA_60;
    if (planned || !chrome.available) bg_opa = focused ? LV_OPA_50 : LV_OPA_30;
    else if (focused) bg_opa = LV_OPA_90;
    lv_obj_set_style_bg_opa(tile, bg_opa, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile, pal.accent_soft,
        static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));
    lv_color_t border = planned ? pal.text_meta : pal.panel_border;
    lv_opa_t border_opa = LV_OPA_50;
    if (focused) {
        border = pal.accent;
        border_opa = LV_OPA_COVER;
    } else if (chrome.selected) {
        border = pal.accent;
        border_opa = LV_OPA_60;
    }
    lv_obj_set_style_border_width(tile, focused ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, border, LV_PART_MAIN);
    lv_obj_set_style_border_opa(tile, border_opa, LV_PART_MAIN);
    lv_obj_set_style_outline_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 14, LV_PART_MAIN);
    const lv_color_t muted = (planned || !chrome.available) ? pal.text_meta : pal.text_primary;
    lv_obj_set_style_text_color(icon, chrome.selected ? pal.accent : muted, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, muted, LV_PART_MAIN);
    if (lv_obj_t* speaker = s_speakers[index]) {
        lv_obj_set_style_text_color(speaker, pal.accent, LV_PART_MAIN);
        if (chrome.sounding) lv_obj_clear_flag(speaker, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(speaker, LV_OBJ_FLAG_HIDDEN);
    }
}

void styleArrow(lv_obj_t* arrow, bool enabled) {
    lv_obj_t* glyph = arrow ? lv_obj_get_child(arrow, 0) : nullptr;
    if (!glyph) return;
    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_text_color(glyph, enabled ? pal.accent : pal.text_meta, LV_PART_MAIN);
    lv_obj_set_style_text_opa(glyph, enabled ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
}

// Dots and arrows follow s_focus. Repainted only when focus changes, no timer.
// Точки и стрелки следуют за s_focus. Перерисовка только при смене фокуса, без таймера.
void refreshIndicators() {
    if (s_indicator_focus == s_focus) return;
    s_indicator_focus = s_focus;
    const YoRadioPalette& pal = yoradio_palette();
    for (uint8_t i = 0; i < s_tile_count; ++i) {
        if (!s_dots[i]) continue;
        const bool on = i == s_focus;
        lv_obj_set_style_bg_color(s_dots[i], on ? pal.accent : pal.text_meta, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_dots[i], on ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
    }
    styleArrow(s_arrow_prev, s_focus > 0u);
    styleArrow(s_arrow_next, s_tile_count > 0u && s_focus + 1u < s_tile_count);
}

void refreshTiles() {
    for (uint8_t i = 0; i < s_tile_count; ++i) styleTile(i);
    refreshIndicators();
    if (!s_reason) return;
    const uint8_t key = static_cast<uint8_t>((s_view.reason ? 1u : 0u) | (s_planned_notice ? 2u : 0u));
    if (s_reason_key == key &&
        (!s_view.reason || s_reason_painted_outcome == s_view.reason_outcome)) return;
    s_reason_key = key;
    s_reason_painted_outcome = s_view.reason_outcome;
    const char* text = "";
    if (s_view.reason) text = reasonText(s_view.reason_outcome);
    else if (s_planned_notice) text = i18n::text(i18n::TextId::SourcePlannedNotice);
    lv_label_set_text(s_reason, text);
}

void destroyWidgets() {
    s_panel = nullptr;
    s_row = nullptr;
    s_reason = nullptr;
    s_arrow_prev = nullptr;
    s_arrow_next = nullptr;
    s_tile_count = 0;
    s_focus = 0;
    for (uint8_t i = 0; i < kMaxTiles; ++i) {
        s_tiles[i] = nullptr;
        s_icons[i] = nullptr;
        s_labels[i] = nullptr;
        s_speakers[i] = nullptr;
        s_dots[i] = nullptr;
    }
    if (s_scrim) {
        lv_obj_del(s_scrim);
        s_scrim = nullptr;
    }
}

void rememberPress(lv_event_t* event) {
    lv_indev_t* indev = lv_indev_get_act();
    if (lv_event_get_code(event) != LV_EVENT_PRESSED || !indev) return;
    lv_indev_get_point(indev, &s_press_point);
    s_press_valid = true;
    s_dragged = false;
    s_programmatic = false;
    s_press_scroll_x = s_row ? lv_obj_get_scroll_x(s_row) : 0;
}

bool downwardRelease(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_PRESSING || !s_press_valid) return false;
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return false;
    lv_point_t now{};
    lv_indev_get_point(indev, &now);
    const int16_t dy = static_cast<int16_t>(now.y - s_press_point.y);
    const int16_t dx = now.x >= s_press_point.x
        ? static_cast<int16_t>(now.x - s_press_point.x)
        : static_cast<int16_t>(s_press_point.x - now.x);
    if (dy < kSourceOverlayMinDelta || dx >= dy) return false;
    s_press_valid = false;
    lv_indev_wait_release(indev);
    return true;
}

// carousel != 0 keeps a horizontal drag alive so the row can snap.
// A vertical gesture still closes. Horizontal motion does not change pages.
// carousel != 0 оставляет горизонтальный жест строке, чтобы она встала в snap.
// Вертикальный жест по-прежнему закрывает. Горизонталь не листает страницы.
void on_pointer(lv_event_t* event) {
    const bool carousel = lv_event_get_user_data(event) != nullptr;
    lv_event_stop_bubbling(event);
    rememberPress(event);
    if (downwardRelease(event)) s_deferred = OverlayCommand::Close;
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) return;
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (carousel && (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)) return;
    lv_indev_wait_release(indev);
    if (dir == LV_DIR_BOTTOM && lv_event_get_target(event) != s_scrim) {
        s_deferred = OverlayCommand::Close;
    }
}

void watchPointer(lv_obj_t* obj, bool carousel) {
    if (!obj) return;
    void* mark = carousel ? reinterpret_cast<void*>(static_cast<uintptr_t>(1u)) : nullptr;
    lv_obj_add_event_cb(obj, on_pointer, LV_EVENT_PRESSED, mark);
    lv_obj_add_event_cb(obj, on_pointer, LV_EVENT_PRESSING, mark);
    lv_obj_add_event_cb(obj, on_pointer, LV_EVENT_GESTURE, mark);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (!carousel) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        return;
    }
    // A tile is not the scroller. B068 cleared SCROLL_CHAIN_HOR, so a finger
    // that started on a tile never reached the row. Only a drag that began
    // on the row's own padding scrolled, which felt like an edge-to-edge swipe.
    // Плитка сама не скроллится. B068 снимал SCROLL_CHAIN_HOR, и палец,
    // начавший на плитке, не доходил до ряда. Крутился только жест,
    // начатый на собственном поле ряда, поэтому он казался свайпом через весь экран.
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
}

uint8_t centeredIndex() {
    if (!s_row || s_tile_count == 0u) return 0u;
    lv_area_t area;
    lv_obj_get_coords(s_row, &area);
    const lv_coord_t mid = static_cast<lv_coord_t>((area.x1 + area.x2) / 2);
    uint8_t best = s_focus < s_tile_count ? s_focus : 0u;
    lv_coord_t best_dist = 32767;
    for (uint8_t i = 0; i < s_tile_count; ++i) {
        if (!s_tiles[i]) continue;
        lv_area_t tile;
        lv_obj_get_coords(s_tiles[i], &tile);
        const lv_coord_t center = static_cast<lv_coord_t>((tile.x1 + tile.x2) / 2);
        const lv_coord_t dist = center >= mid ? static_cast<lv_coord_t>(center - mid)
                                              : static_cast<lv_coord_t>(mid - center);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

void syncFocusToCenter() {
    const uint8_t next = centeredIndex();
    if (next == s_focus) return;
    s_focus = next;
    s_planned_notice = false;
    markDirty();
    refreshTiles();
}

void on_row_scroll(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_SCROLL && code != LV_EVENT_SCROLL_END) return;
    if (s_programmatic) {
        if (code == LV_EVENT_SCROLL_END) s_programmatic = false;
        return;
    }
    if (s_press_valid && s_row) {
        const lv_coord_t x = lv_obj_get_scroll_x(s_row);
        const lv_coord_t delta = x >= s_press_scroll_x ? static_cast<lv_coord_t>(x - s_press_scroll_x)
                                                       : static_cast<lv_coord_t>(s_press_scroll_x - x);
        if (delta > 8) s_dragged = true;
    }
    if (s_dragged || code == LV_EVENT_SCROLL_END) syncFocusToCenter();
}

void focusIndex(uint8_t index, bool animate) {
    if (index >= s_tile_count || !s_tiles[index]) return;
    const bool moved = index != s_focus;
    s_focus = index;
    s_programmatic = true;
    lv_obj_scroll_to_view(s_tiles[index], animate ? LV_ANIM_ON : LV_ANIM_OFF);
    if (!animate) s_programmatic = false;
    if (moved) s_planned_notice = false;
    markDirty();
    refreshTiles();
}

void applyTile(uint8_t index) {
    if (index >= s_tile_count) return;
    const bool activatable = yoradio::source::isImplemented(s_tile_ids[index]);
    const CarouselPress action = carouselPress(index, s_focus, activatable, s_view.awaiting, s_dragged);
    if (action == CarouselPress::Ignored) return;
    if (action == CarouselPress::MoveFocus) {
        focusIndex(index, true);
        return;
    }
    if (action == CarouselPress::Planned) {
        s_planned_notice = true;
        s_view.reason = false;
        markDirty();
        refreshTiles();
        return;
    }
    s_planned_notice = false;
    const auto admission = player.activate(s_tile_ids[index]);
    const OverlayCommand command = applyAdmission(s_view, admission.request, admission.outcome, s_tile_ids[index]);
    if (command != OverlayCommand::None) s_deferred = command;
    markDirty();
    refreshTiles();
}

void on_scrim(lv_event_t* event) {
    lv_event_stop_bubbling(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (lv_event_get_target(event) != s_scrim) return;
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) lv_indev_wait_release(indev);
    s_deferred = OverlayCommand::Close;
}

void on_tile(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    lv_indev_t* indev = lv_indev_get_act();
    if (indev && lv_indev_get_scroll_obj(indev) != nullptr) s_dragged = true;
    const auto* slot = static_cast<const uint8_t*>(lv_event_get_user_data(event));
    if (!slot) return;
    applyTile(*slot);
}

// Arrow tap = the same stepping as sourceOverlayFocusPrevious/Next. Stops at the ends.
// Нажатие стрелки — тот же шаг, что sourceOverlayFocusPrevious/Next. На краях стоит.
void on_arrow(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(event);
    if (s_tile_count == 0u) return;
    const int8_t delta = lv_event_get_user_data(event) != nullptr ? 1 : -1;
    const uint8_t next = carouselStep(s_focus, s_tile_count, delta);
    if (next != s_focus) focusIndex(next, true);
}

uint8_t initialFocus() {
    yoradio::source::SourceId selected = yoradio::source::SourceId::Radio;
    if (!player.selectedSource(selected)) return 0u;
    for (uint8_t i = 0; i < s_tile_count; ++i) {
        if (s_tile_ids[i] == selected) return i;
    }
    return 0u;
}

void buildPanel() {
    lv_disp_t* disp = lv_disp_get_default();
    lv_obj_t* top = disp ? lv_layer_top() : nullptr;
    if (!disp || !top) return;
    const lv_coord_t hor = lv_disp_get_hor_res(disp);
    const lv_coord_t ver = lv_disp_get_ver_res(disp);
    const YoRadioPalette& pal = yoradio_palette();
    const auto& metrics = kResolvedUiMetrics.factory;
    const lv_coord_t pad = static_cast<lv_coord_t>(metrics.frame_padding);
    const lv_coord_t icon_px = static_cast<lv_coord_t>(metrics.font_large_px + metrics.font_header_px);
    const lv_font_t* title_font = FontProvider::text(metrics.font_header_px);
    const lv_font_t* caption_font = FontProvider::text(metrics.font_header_px);
    const lv_font_t* reason_font = FontProvider::text(metrics.font_small_px);
    const lv_coord_t title_lh = title_font ? lv_font_get_line_height(title_font)
                                           : static_cast<lv_coord_t>(metrics.font_header_px + 4u);
    const lv_coord_t reason_lh = reason_font ? lv_font_get_line_height(reason_font)
                                             : static_cast<lv_coord_t>(metrics.font_small_px + 4u);

    // B070 composition. Geometry stays local, like the Main control band.
    // The panel is tall enough to cover Main's control band, so the two button
    // sets never show through each other.
    // Композиция B070. Геометрия локальная, как у полки управления Main.
    // Панель выше полки Main, поэтому два набора кнопок не просвечивают друг через друга.
    constexpr lv_coord_t kPanelH = 204;
    constexpr lv_coord_t kInset = 4;
    constexpr lv_coord_t kRadius = 22;
    constexpr lv_coord_t kGripW = 40;
    constexpr lv_coord_t kGripH = 4;
    constexpr lv_coord_t kGripTop = 5;
    constexpr lv_coord_t kTitleTop = 12;
    constexpr lv_coord_t kRuleW = 96;
    constexpr lv_coord_t kRuleGap = 14;
    constexpr lv_coord_t kArrowW = 36;
    constexpr lv_coord_t kArrowEdge = 4;
    constexpr lv_coord_t kTileGap = 12;
    // Vertical breathing room inside the row; keeps the B070 tile position.
    // Вертикальный запас внутри ряда; сохраняет положение плиток B070.
    constexpr lv_coord_t kRowPadV = 4;
    // Speaker sits in the tile pad + border corner: 5–6 px from the top/right edge.
    // Динамик в углу pad + рамки плитки: 5–6 px от верхнего и правого края.
    constexpr uint16_t kSpeakerPx = 16;
    constexpr lv_coord_t kDot = 6;
    constexpr lv_coord_t kDotGap = 7;
    constexpr lv_coord_t kDotsBottom = 14;

    const lv_coord_t panel_w = hor - kInset * 2;
    const lv_coord_t panel_h = kPanelH < ver / 2 ? kPanelH : static_cast<lv_coord_t>(ver / 2);
    const lv_coord_t inner_w = panel_w - 2;
    const lv_coord_t inner_h = panel_h - 2;
    const lv_coord_t view_w = inner_w - (kArrowEdge + kArrowW) * 2;
    const lv_coord_t tile_w = carouselTileWidth(view_w);
    const lv_coord_t tile_h = tile_w;
    const lv_coord_t row_h = tile_h + kRowPadV * 2;
    const lv_coord_t side = (view_w > tile_w) ? static_cast<lv_coord_t>((view_w - tile_w) / 2) : pad;
    const lv_coord_t title_bottom = kTitleTop + title_lh;
    const lv_coord_t dots_top = inner_h - kDotsBottom - kDot;
    const lv_coord_t row_y = static_cast<lv_coord_t>((title_bottom + dots_top) / 2 - row_h / 2);
    const lv_coord_t reason_y = row_y + row_h;

    s_scrim = lv_obj_create(top);
    if (!s_scrim) return;
    lv_obj_set_size(s_scrim, hor, ver);
    lv_obj_align(s_scrim, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_scrim, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_scrim, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_scrim, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_scrim, on_scrim, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_scrim, on_pointer, LV_EVENT_GESTURE, nullptr);

    s_panel = lv_obj_create(s_scrim);
    if (!s_panel) {
        destroyWidgets();
        return;
    }
    // Plain bg_opa over the live screen: no blur, no framebuffer copy, no shadow.
    // Обычный bg_opa поверх живого экрана: без blur, без копии кадра, без тени.
    lv_obj_set_size(s_panel, panel_w, panel_h);
    lv_obj_align(s_panel, LV_ALIGN_BOTTOM_MID, 0, -kInset);
    lv_obj_set_style_bg_color(s_panel, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_panel, pal.accent, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, kRadius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
    watchPointer(s_panel, false);

    lv_obj_t* grip = lv_obj_create(s_panel);
    if (grip) {
        lv_obj_remove_style_all(grip);
        lv_obj_set_size(grip, kGripW, kGripH);
        lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, kGripTop);
        lv_obj_set_style_radius(grip, kGripH / 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(grip, pal.text_meta, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(grip, LV_OPA_60, LV_PART_MAIN);
        lv_obj_clear_flag(grip, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t* title = lv_label_create(s_panel);
    if (title) {
        lv_label_set_text(title, i18n::text(i18n::TextId::BrowserSourcesTitle));
        lv_obj_set_style_text_font(title, title_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, pal.overlay_title_text, LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kTitleTop);
        lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);
        // Rules fade from the panel color into the accent toward the title.
        // Линии переходят от цвета панели к акценту в сторону заголовка.
        for (int side_index = 0; side_index < 2; ++side_index) {
            lv_obj_t* rule = lv_obj_create(s_panel);
            if (!rule) continue;
            const bool left = side_index == 0;
            lv_obj_remove_style_all(rule);
            lv_obj_set_size(rule, kRuleW, 1);
            lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(rule, left ? pal.panel_background : pal.accent, LV_PART_MAIN);
            lv_obj_set_style_bg_grad_color(rule, left ? pal.accent : pal.panel_background, LV_PART_MAIN);
            lv_obj_set_style_bg_grad_dir(rule, LV_GRAD_DIR_HOR, LV_PART_MAIN);
            lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align_to(rule, title, left ? LV_ALIGN_OUT_LEFT_MID : LV_ALIGN_OUT_RIGHT_MID,
                            left ? -kRuleGap : kRuleGap, 0);
        }
    }

    for (int side_index = 0; side_index < 2; ++side_index) {
        const bool next = side_index == 1;
        lv_obj_t* arrow = lv_obj_create(s_panel);
        if (!arrow) continue;
        lv_obj_remove_style_all(arrow);
        lv_obj_set_size(arrow, kArrowW, tile_h);
        lv_obj_align(arrow, next ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT,
                     next ? -kArrowEdge : kArrowEdge, row_y + kRowPadV);
        lv_obj_set_style_radius(arrow, 10, LV_PART_MAIN);
        lv_obj_set_style_bg_color(arrow, pal.accent_soft,
            static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));
        lv_obj_set_style_bg_opa(arrow, LV_OPA_40,
            static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));
        lv_obj_add_flag(arrow, LV_OBJ_FLAG_CLICKABLE);
        watchPointer(arrow, false);
        lv_obj_add_event_cb(arrow, on_arrow, LV_EVENT_CLICKED,
            next ? reinterpret_cast<void*>(static_cast<uintptr_t>(1u)) : nullptr);
        lv_obj_t* glyph = lv_label_create(arrow);
        if (glyph) {
            lv_label_set_text(glyph, next ? control_glyph_utf8_chevron_right()
                                          : control_glyph_utf8_chevron_left());
            lv_obj_set_style_text_font(glyph, FontProvider::icon(28), LV_PART_MAIN);
            lv_obj_center(glyph);
            lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE);
        }
        (next ? s_arrow_next : s_arrow_prev) = arrow;
    }

    s_row = lv_obj_create(s_panel);
    if (!s_row) {
        destroyWidgets();
        return;
    }
    lv_obj_set_size(s_row, view_w, row_h);
    lv_obj_align(s_row, LV_ALIGN_TOP_MID, 0, row_y);
    lv_obj_set_style_bg_opa(s_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_row, kRowPadV, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_row, kRowPadV, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_row, side, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_row, side, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_row, kTileGap, LV_PART_MAIN);
    lv_obj_set_style_radius(s_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_row, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_row, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_row, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_add_flag(s_row, LV_OBJ_FLAG_SCROLLABLE);
    watchPointer(s_row, true);
    lv_obj_add_event_cb(s_row, on_row_scroll, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(s_row, on_row_scroll, LV_EVENT_SCROLL_END, nullptr);

    s_tile_count = 0;
    for (size_t i = 0; i < yoradio::source::kCatalogCount && s_tile_count < kMaxTiles; ++i) {
        const uint8_t slot = s_tile_count;
        s_tile_ids[slot] = yoradio::source::kCatalog[i].id;
        s_slots[slot] = slot;
        lv_obj_t* tile = lv_obj_create(s_row);
        s_tiles[slot] = tile;
        if (!tile) continue;
        lv_obj_set_size(tile, tile_w, tile_h);
        lv_obj_set_style_pad_all(tile, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_row(tile, 6, LV_PART_MAIN);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        watchPointer(tile, true);
        lv_obj_add_event_cb(tile, on_tile, LV_EVENT_CLICKED, &s_slots[slot]);

        lv_obj_t* icon = lv_label_create(tile);
        s_icons[slot] = icon;
        if (icon) {
            lv_label_set_text(icon, source_glyph_utf8_for_key(yoradio::source::kCatalog[i].icon_key));
            lv_obj_set_style_text_font(icon, FontProvider::icon(static_cast<uint16_t>(icon_px)), LV_PART_MAIN);
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_t* label = lv_label_create(tile);
        s_labels[slot] = label;
        if (label) {
            lv_label_set_text(label, tileTitle(s_tile_ids[slot]));
            lv_obj_set_width(label, tile_w - 8);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_font(label, caption_font, LV_PART_MAIN);
            lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        }
        // Same speaker glyph as the Stations current-station marker. Outside the
        // flex column, so it never moves the icon or caption.
        // Тот же динамик, что у текущей станции в Stations. Вне flex-колонки,
        // поэтому не сдвигает иконку и подпись.
        lv_obj_t* speaker = lv_label_create(tile);
        s_speakers[slot] = speaker;
        if (speaker) {
            lv_label_set_text(speaker, station_glyph_utf8_volume_2());
            lv_obj_set_style_text_font(speaker, FontProvider::icon(kSpeakerPx), LV_PART_MAIN);
            lv_obj_add_flag(speaker, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_add_flag(speaker, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(speaker, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align(speaker, LV_ALIGN_TOP_RIGHT, 0, 0);
        }
        ++s_tile_count;
    }

    s_reason = lv_label_create(s_panel);
    if (s_reason) {
        lv_label_set_text(s_reason, "");
        lv_obj_set_size(s_reason, view_w, reason_lh);
        lv_obj_align(s_reason, LV_ALIGN_TOP_MID, 0, reason_y);
        lv_label_set_long_mode(s_reason, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_reason, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_reason, reason_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_reason, pal.overlay_body_text, LV_PART_MAIN);
        lv_obj_clear_flag(s_reason, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t* dots = lv_obj_create(s_panel);
    if (dots) {
        lv_obj_remove_style_all(dots);
        lv_obj_set_size(dots, LV_SIZE_CONTENT, kDot);
        lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, dots_top);
        lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(dots, kDotGap, LV_PART_MAIN);
        lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
        for (uint8_t i = 0; i < s_tile_count; ++i) {
            lv_obj_t* dot = lv_obj_create(dots);
            s_dots[i] = dot;
            if (!dot) continue;
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, kDot, kDot);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    lv_obj_update_layout(s_panel);
    s_focus = initialFocus();
    s_dragged = false;
    focusIndex(s_focus, false);
}

OverlayCommand takeDeferred() {
    const OverlayCommand command = s_deferred;
    s_deferred = OverlayCommand::None;
    if (command == OverlayCommand::None || command == OverlayCommand::ShowReason) return command;
    noteClosed(s_view);
    destroyWidgets();
    return command;
}

}  // namespace

bool sourceOverlayIsOpen() {
    return s_view.open && s_scrim != nullptr;
}

bool sourceOverlayOpen(OverlayPlace place) {
    if (!mayOpenFromPlace(place, s_view.open)) return false;
    if (!noteOpened(s_view)) return false;
    s_deferred = OverlayCommand::None;
    s_press_valid = false;
    s_dragged = false;
    s_programmatic = false;
    s_planned_notice = false;
    markDirty();
    buildPanel();
    if (!s_scrim) {
        noteClosed(s_view);
        return false;
    }
    return true;
}

void sourceOverlayDismiss() {
    if (!s_view.open && !s_scrim) return;
    noteClosed(s_view);
    s_deferred = OverlayCommand::None;
    s_planned_notice = false;
    destroyWidgets();
}

OverlayCommand sourceOverlayService(OverlayPlace place) {
    if (s_view.open && (place == OverlayPlace::Modal || place == OverlayPlace::Wake ||
                        place == OverlayPlace::StationsOrBrowser)) {
        sourceOverlayDismiss();
        return OverlayCommand::None;
    }
    if (s_view.open && s_view.awaiting) {
        const auto snap = player.activation();
        const OverlayCommand polled = onSnapshot(s_view, s_view.watched_generation, snap);
        if (polled != OverlayCommand::None) s_deferred = polled;
    }
    if (s_view.open && s_scrim) refreshTiles();
    const OverlayCommand command = takeDeferred();
    if (command == OverlayCommand::ShowReason && s_view.open) refreshTiles();
    return command;
}

void sourceOverlayFocusPrevious() {
    if (!s_view.open || s_tile_count == 0u) return;
    focusIndex(carouselStep(s_focus, s_tile_count, -1), true);
}

void sourceOverlayFocusNext() {
    if (!s_view.open || s_tile_count == 0u) return;
    focusIndex(carouselStep(s_focus, s_tile_count, 1), true);
}

void sourceOverlayFocusSelect() {
    if (!s_view.open) return;
    s_dragged = false;
    applyTile(s_focus);
}

}  // namespace lvgl_ui
