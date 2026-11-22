// Platform composition: the ONLY place that maps the selected board to backends (D4).
// Композиция платформы: ЕДИНСТВЕННОЕ место сопоставления платы и backend'ов (D4).
//
// Inputs are board facts (BoardProfile) and the user touch switch; outputs are
// compile-time choices: which PanelMode recipe, which display host, which touch
// backend. Backends and UI never compare board ids or controller enums themselves;
// they read these choices. A board whose facts have no backend here fails to build
// with a message naming the missing piece.
// Входы — факты платы (BoardProfile) и пользовательский переключатель тача; выходы —
// выбор при компиляции: рецепт PanelMode, хост дисплея, backend тача. Backend'ы и UI
// сами не сравнивают id платы и перечисления контроллеров; они читают этот выбор.
// Плата, для фактов которой здесь нет backend'а, не собирается, и сообщение называет,
// чего не хватает.
//
// Ownership: platform. Data only: no esp_lcd, no driver headers, no LVGL.
// Владелец: platform. Только данные: без esp_lcd, заголовков драйверов и LVGL.
#ifndef YORADIO_PLATFORM_COMPOSITION_H
#define YORADIO_PLATFORM_COMPOSITION_H

#include <stdint.h>

#include "board/board_select.h"
#include "display/panel_st7701_rgb565_480.h"

#ifndef YORADIO_HAS_TOUCH
#  error "platform/composition.h: include core/options.h first (YORADIO_HAS_TOUCH)."
#endif

namespace yoradio {
namespace composition {

// Display host: who owns the panel bus, the framebuffer and scanout.
// Rgb3Wire = RGB565 parallel scanout via esp_lcd plus a 9-bit 3-wire command bus
// that replays the PanelMode init recipe (displays/rgb_3wire_backend.*).
// Хост дисплея: владелец шины панели, кадрового буфера и scanout.
// Rgb3Wire = RGB565 через esp_lcd плюс 9-битная 3-wire шина команд, исполняющая
// рецепт инициализации PanelMode (displays/rgb_3wire_backend.*).
enum class DisplayHost : uint8_t { None = 0, Rgb3Wire };

enum class TouchBackend : uint8_t { None = 0, Gt911 };

// Panel recipe for a controller. Unknown controllers have no recipe.
// Рецепт панели для контроллера. Для неизвестного контроллера рецепта нет.
inline constexpr panel::PanelMode kNoPanelMode = {};

constexpr const panel::PanelMode& panelModeFor(PanelController controller) {
    switch (controller) {
        case PanelController::St7701: return panel::kSt7701Rgb565_480;
        default:                      return kNoPanelMode;
    }
}

constexpr bool hasCommandBus3Wire(const DisplayWiring& d) {
    return d.cmd_cs_gpio != kPinNotConnected && d.cmd_sck_gpio != kPinNotConnected &&
           d.cmd_sda_gpio != kPinNotConnected;
}

constexpr DisplayHost displayHostFor(const BoardProfile& board) {
    return (board.display.transport == DisplayTransport::Rgb565Parallel16 &&
            hasCommandBus3Wire(board.display) &&
            panelModeFor(board.display.controller).controller != panel::Controller::Unknown)
               ? DisplayHost::Rgb3Wire
               : DisplayHost::None;
}

constexpr TouchBackend touchBackendFor(const BoardProfile& board, bool touch_enabled) {
    if (!touch_enabled) {
        return TouchBackend::None;
    }
    switch (board.touch.controller) {
        case TouchController::Gt911: return TouchBackend::Gt911;
        default:                     return TouchBackend::None;
    }
}

inline constexpr const panel::PanelMode& kPanelMode = panelModeFor(kActiveBoard.display.controller);
inline constexpr DisplayHost kDisplayHost = displayHostFor(kActiveBoard);
inline constexpr TouchBackend kTouchBackend = touchBackendFor(kActiveBoard, YORADIO_HAS_TOUCH != 0);

static_assert(kPanelMode.controller != panel::Controller::Unknown,
              "YoRadio composition: no PanelMode recipe for the selected board's panel controller "
              "(add one in platform/display and map it in panelModeFor()).");
static_assert(kDisplayHost != DisplayHost::None,
              "YoRadio composition: no display host for the selected board "
              "(transport + command bus + panel recipe; see displayHostFor()).");
static_assert(!(YORADIO_HAS_TOUCH) || kTouchBackend != TouchBackend::None,
              "YoRadio composition: touch is enabled but the board's touch controller has no backend "
              "(see touchBackendFor()); set YORADIO_HAS_TOUCH 0 to build without touch.");

}  // namespace composition
}  // namespace yoradio

#endif  // YORADIO_PLATFORM_COMPOSITION_H
