// Pure Source Selector decisions. LVGL draws them; Player::activate owns sound.
// Чистые решения выбора источника. LVGL их рисует; звуком владеет Player::activate.
// No Source Manager, no audio command, no file API.
// Без Source Manager, без аудиокоманды и без файлового API.
#pragma once

#include <stdint.h>

#include "../core/source_catalog.h"

namespace lvgl_ui {

inline constexpr int16_t kSourceOverlayEdgePx = 64;
inline constexpr int16_t kSourceOverlayMinDelta = 22;

// Where a bottom-edge swipe is allowed to open the panel.
// Где свайп от нижнего края может открыть панель.
enum class OverlayPlace : uint8_t {
    PlayerSurface = 0,
    StationsOrBrowser,
    Modal,
    Wake
};

enum class OverlayCommand : uint8_t {
    None = 0,
    Close,
    CloseAndRefresh,
    OpenSdBrowser,
    OpenRadioBrowser,
    ShowReason
};

enum class TilePress : uint8_t { CallActivate, Suppressed };

enum class OverlayHit : uint8_t { Outside, Panel, Underlying };
enum class HitDisposition : uint8_t { Consume, ConsumeAndClose };

struct OverlayView {
    uint32_t generation = 0;
    uint32_t watched_generation = 0;
    uint32_t request = 0;
    bool open = false;
    bool awaiting = false;
    bool reason = false;
    yoradio::source::ActivateOutcome reason_outcome = yoradio::source::ActivateOutcome::Cancelled;
};

struct TileChrome {
    bool available = false;
    bool selected = false;
    bool sounding = false;
};

struct BottomStroke {
    bool tracking = false;
    bool down = false;
    int16_t start_x = 0;
    int16_t start_y = 0;
    int16_t x = 0;
    int16_t y = 0;
    int16_t height = 0;
    OverlayPlace place = OverlayPlace::PlayerSurface;
    bool overlay_open = false;
};

inline bool mayOpenFromPlace(OverlayPlace place, bool overlay_open) {
    return !overlay_open && place == OverlayPlace::PlayerSurface;
}

// Upward stroke that began in the bottom edge. Stations, browser, modal and wake do not open.
// Свайп вверх из нижней полосы. Stations, браузер, модалка и пробуждение не открывают.
inline bool bottomSwipeOpens(const BottomStroke& stroke) {
    if (!stroke.tracking || !stroke.down || stroke.overlay_open) return false;
    if (stroke.place != OverlayPlace::PlayerSurface) return false;
    if (stroke.height <= kSourceOverlayEdgePx) return false;
    if (stroke.start_y < static_cast<int16_t>(stroke.height - kSourceOverlayEdgePx)) return false;
    const int16_t upward = static_cast<int16_t>(stroke.start_y - stroke.y);
    if (upward < kSourceOverlayMinDelta) return false;
    const int16_t dx = stroke.x >= stroke.start_x
        ? static_cast<int16_t>(stroke.x - stroke.start_x)
        : static_cast<int16_t>(stroke.start_x - stroke.x);
    if (dx >= upward) return false;
    return true;
}

inline bool noteOpened(OverlayView& view) {
    if (view.open) return false;
    view.open = true;
    view.generation = (view.generation == UINT32_MAX) ? 1u : (view.generation + 1u);
    view.watched_generation = view.generation;
    view.request = 0;
    view.awaiting = false;
    view.reason = false;
    return true;
}

// Closing the panel does not cancel an activation Player already accepted.
// Закрытие панели не отменяет активацию, которую Player уже принял.
inline void noteClosed(OverlayView& view) {
    view.open = false;
    view.awaiting = false;
    view.reason = false;
}

inline bool closeCancelsBackend() { return false; }

inline TilePress tilePress(bool awaiting) {
    return awaiting ? TilePress::Suppressed : TilePress::CallActivate;
}

// Pending is not a successful switch. A local pending flag does not paint selection.
// Pending — не успешное переключение. Локальный флаг не рисует выбор.
inline TileChrome tileChrome(bool available, bool selected, bool sounding, bool pending_target) {
    (void)pending_target;
    return TileChrome{available, selected, sounding};
}

inline OverlayCommand applyTerminal(OverlayView& view, yoradio::source::ActivateOutcome outcome,
                                    yoradio::source::SourceId id) {
    using yoradio::source::ActivateOutcome;
    using yoradio::source::SourceId;
    if (outcome == ActivateOutcome::Pending) {
        view.awaiting = true;
        view.reason = false;
        return OverlayCommand::None;
    }
    view.awaiting = false;
    view.request = 0;
    switch (outcome) {
    case ActivateOutcome::AlreadyActive:
    case ActivateOutcome::Playing:
        view.reason = false;
        return OverlayCommand::Close;
    case ActivateOutcome::SelectedStopped:
        view.reason = false;
        return OverlayCommand::CloseAndRefresh;
    case ActivateOutcome::NeedsSelection:
        view.reason = false;
        return id == SourceId::Sd ? OverlayCommand::OpenSdBrowser : OverlayCommand::OpenRadioBrowser;
    default:
        view.reason = true;
        view.reason_outcome = outcome;
        return OverlayCommand::ShowReason;
    }
}

inline OverlayCommand applyAdmission(OverlayView& view, uint32_t request,
                                     yoradio::source::ActivateOutcome outcome,
                                     yoradio::source::SourceId id) {
    using yoradio::source::ActivateOutcome;
    if (!view.open || view.awaiting) return OverlayCommand::None;
    if (outcome == ActivateOutcome::Busy || request == 0u) {
        view.reason = true;
        view.reason_outcome = (outcome == ActivateOutcome::Busy) ? ActivateOutcome::Busy : outcome;
        return OverlayCommand::ShowReason;
    }
    view.request = request;
    view.watched_generation = view.generation;
    if (outcome == ActivateOutcome::Pending) {
        view.awaiting = true;
        view.reason = false;
        return OverlayCommand::None;
    }
    return applyTerminal(view, outcome, id);
}

// A completion applies only to this open generation and this request id.
// Завершение применяется только к этому открытию и к этому request id.
inline OverlayCommand onSnapshot(OverlayView& view, uint32_t event_generation,
                                 const yoradio::source::ActivateSnapshot& snap) {
    if (!view.open || !view.awaiting) return OverlayCommand::None;
    if (!event_generation || event_generation != view.generation) return OverlayCommand::None;
    if (!yoradio::source::completionMatches(snap, view.request)) return OverlayCommand::None;
    return applyTerminal(view, snap.outcome, snap.source);
}

inline HitDisposition hitDisposition(OverlayHit hit, bool downward_on_panel) {
    if (hit == OverlayHit::Outside) return HitDisposition::ConsumeAndClose;
    if (hit == OverlayHit::Panel && downward_on_panel) return HitDisposition::ConsumeAndClose;
    return HitDisposition::Consume;
}

inline bool reachesUnderlying() { return false; }

inline bool issuesAudio() { return false; }

// Encoder / IR model. Previous and Next stop on the ends. Select is separate.
// Модель энкодера и ИК. Previous и Next останавливаются на краях. Select отдельно.
// B068 used (view_w * 5) / 9, which is 257 px on a 464 px row. Snap center
// only advances after the drag passes the midpoint to the next tile, so that
// width asked for about 130 px of travel. 92 px is the 480×480 target (88–96).
// B068 брал (view_w * 5) / 9 — 257 px на ряду 464 px. Snap center переходит
// на следующую плитку только после середины шага, то есть примерно 130 px.
// 92 px — цель для 480×480 (диапазон 88–96).
inline int16_t carouselTileWidth(int16_t view_w) {
    constexpr int16_t kWidth = 92;
    if (view_w < kWidth) return view_w > 0 ? view_w : kWidth;
    return kWidth;
}

inline uint8_t carouselStep(uint8_t index, uint8_t count, int8_t delta) {
    if (count == 0u) return 0u;
    if (index >= count) index = static_cast<uint8_t>(count - 1u);
    if (delta < 0) return index == 0u ? index : static_cast<uint8_t>(index - 1u);
    if (delta > 0) {
        const uint8_t last = static_cast<uint8_t>(count - 1u);
        return index >= last ? index : static_cast<uint8_t>(index + 1u);
    }
    return index;
}

// Drag never activates. A side tile only becomes the center. Planned is not activate.
// Свайп не активирует. Боковая плитка только становится центром. Planned — не activate.
enum class CarouselPress : uint8_t { Activate, MoveFocus, Planned, Ignored };

inline CarouselPress carouselPress(uint8_t pressed, uint8_t focus, bool activatable,
                                   bool awaiting, bool dragged) {
    if (dragged) return CarouselPress::Ignored;
    if (pressed != focus) return CarouselPress::MoveFocus;
    if (awaiting) return CarouselPress::Ignored;
    if (!activatable) return CarouselPress::Planned;
    return CarouselPress::Activate;
}

}  // namespace lvgl_ui
