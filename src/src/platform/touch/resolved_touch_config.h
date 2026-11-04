// ResolvedTouchConfig — effective clean touch policy without a second hardware table.
#ifndef YORADIO_PLATFORM_TOUCH_RESOLVED_TOUCH_CONFIG_H
#define YORADIO_PLATFORM_TOUCH_RESOLVED_TOUCH_CONFIG_H

#include <stdint.h>

#include "../../core/touch_geometry.h"
#include "../board/board_select.h"
#include "../display/resolved_display_config.h"

namespace yoradio {

struct ResolvedTouchConfig {
    const TouchWiring& wiring;
    TouchServiceMode service_mode;
    uint32_t i2c_clock_hz;
    YoradioTouchGeometry geometry;
    YoradioTouchOrientation orientation;
    bool flip_touch;
    bool available;
};

constexpr YoradioTouchGeometry resolveTouchGeometry(uint16_t physical_width,
                                                    uint16_t physical_height,
                                                    uint16_t logical_width,
                                                    uint16_t logical_height) {
    return YoradioTouchGeometry{
        physical_width,
        physical_height,
        logical_width,
        logical_height,
    };
}

constexpr ResolvedTouchConfig resolveTouchConfig(bool flip_touch) {
    return ResolvedTouchConfig{
        kWiring.touch,
        kActiveBoardDefaults.touch.service_mode,
        kActiveBoardDefaults.touch.i2c_clock_hz,
        resolveTouchGeometry(
            kResolvedDisplayConfig.panel.native_width,
            kResolvedDisplayConfig.panel.native_height,
            kResolvedDisplayConfig.panel.native_width,
            kResolvedDisplayConfig.panel.native_height),
        yoradio_touch_orientation(flip_touch),
        flip_touch,
        kWiring.touch.controller == TouchController::Gt911,
    };
}

static_assert(resolveTouchConfig(false).geometry.panel_width == 480 &&
                  resolveTouchConfig(false).geometry.panel_height == 480,
              "ResolvedTouchConfig: 4848S040 touch geometry must follow the display contract.");
static_assert(resolveTouchConfig(false).service_mode == TouchServiceMode::Polling,
              "ResolvedTouchConfig: 4848S040 touch service remains polling.");
static_assert(!resolveTouchConfig(false).orientation.swap_xy &&
                  !resolveTouchConfig(false).orientation.mirror_x &&
                  !resolveTouchConfig(false).orientation.mirror_y,
              "ResolvedTouchConfig: Flip OFF is native official orientation.");
static_assert(!resolveTouchConfig(true).orientation.swap_xy &&
                  resolveTouchConfig(true).orientation.mirror_x &&
                  resolveTouchConfig(true).orientation.mirror_y,
              "ResolvedTouchConfig: Flip ON is official 180-degree mirroring.");
static_assert(resolveTouchConfig(false).wiring.controller == TouchController::Gt911 &&
                  resolveTouchConfig(false).available,
              "ResolvedTouchConfig: selected 4848S040 touch controller must be GT911.");

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_TOUCH_RESOLVED_TOUCH_CONFIG_H
