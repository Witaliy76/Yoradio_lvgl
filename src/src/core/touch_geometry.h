#ifndef YORADIO_TOUCH_GEOMETRY_H
#define YORADIO_TOUCH_GEOMETRY_H

// Clean S05 touch geometry. The official esp_lcd_touch component owns axis
// orientation; YoRadio only describes the panel space and clips defensively.
// Чистая геометрия S05. Ориентацией осей владеет esp_lcd_touch; YoRadio только
// описывает пространство панели и безопасно ограничивает диапазон.

#include <stdint.h>

struct YoradioTouchGeometry {
    uint16_t panel_width;
    uint16_t panel_height;
    uint16_t logical_width;
    uint16_t logical_height;
};

struct YoradioTouchOrientation {
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
};

// The accepted display mounting is native for Flip OFF. Flip ON is exactly a
// 180-degree rotation, expressed by the official component's two mirror flags.
constexpr YoradioTouchOrientation yoradio_touch_orientation(bool flip_touch) {
    return YoradioTouchOrientation{false, flip_touch, flip_touch};
}

constexpr uint16_t yoradio_touch_axis_max(uint16_t size) {
    return size == 0 ? 0 : static_cast<uint16_t>(size - 1);
}

constexpr uint16_t yoradio_touch_clip_axis(uint16_t value, uint16_t size) {
    return value > yoradio_touch_axis_max(size)
               ? yoradio_touch_axis_max(size)
               : value;
}

inline void yoradio_touch_clip_to_panel(const YoradioTouchGeometry& geometry,
                                        uint16_t* x,
                                        uint16_t* y) {
    if (!x || !y) {
        return;
    }
    *x = yoradio_touch_clip_axis(*x, geometry.panel_width);
    *y = yoradio_touch_clip_axis(*y, geometry.panel_height);
}

// Host-test oracle for the documented esp_lcd_touch order: clamp raw input,
// mirror X/Y, then swap. Production uses the component flags directly.
inline void yoradio_touch_apply_official_orientation_reference(
    const YoradioTouchGeometry& geometry,
    const YoradioTouchOrientation& orientation,
    uint16_t* x,
    uint16_t* y) {
    if (!x || !y) {
        return;
    }
    yoradio_touch_clip_to_panel(geometry, x, y);
    if (orientation.mirror_x) {
        *x = static_cast<uint16_t>(yoradio_touch_axis_max(geometry.panel_width) - *x);
    }
    if (orientation.mirror_y) {
        *y = static_cast<uint16_t>(yoradio_touch_axis_max(geometry.panel_height) - *y);
    }
    if (orientation.swap_xy) {
        const uint16_t tmp = *x;
        *x = *y;
        *y = tmp;
    }
    *x = yoradio_touch_clip_axis(*x, geometry.logical_width);
    *y = yoradio_touch_clip_axis(*y, geometry.logical_height);
}

#endif  // YORADIO_TOUCH_GEOMETRY_H
