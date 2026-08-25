#ifndef YORADIO_TOUCH_GEOMETRY_H
#define YORADIO_TOUCH_GEOMETRY_H

// Current-board touch/display geometry contract for Phase I BASE-TOUCH-GEOMETRY.
// Контракт геометрии тача/дисплея текущей платы (Phase I BASE-TOUCH-GEOMETRY).
// One explicit source of truth for mapping/filter/clip/raw-range/transform.
// Единственный явный источник правды для mapping/filter/clip/raw-range/transform.
// Not a BoardProfile / TouchPort / runtime registry — those belong to Phase II.
// Это не BoardProfile / TouchPort / runtime registry — это Phase II.
// Host-testable: no Arduino, LVGL, or vendor-driver includes.
// Пригодно для host-теста: без Arduino, LVGL и vendor-драйверов.

#include <stdint.h>

struct YoradioTouchGeometry {
    // 1. Raw/native coordinate range used by generic filtering.
    // 1. Сырой/нативный диапазон, по которому фильтрует generic-код.
    // On the current GT911 path this is applied to POST-vendor-rotation samples.
    // На текущем пути GT911 фильтр видит координаты УЖЕ после vendor-rotation.
    uint16_t raw_x_min;
    uint16_t raw_x_max;
    uint16_t raw_y_min;
    uint16_t raw_y_max;

    // 2. Physical panel pixels. Named separately from logical even when equal.
    // 2. Физический размер панели. Отдельно от logical, даже если сейчас равны.
    uint16_t panel_width;
    uint16_t panel_height;

    // 3. LVGL logical resolution (pointer space after mapping).
    // 3. Логическое разрешение LVGL (пространство указателя после mapping).
    uint16_t logical_width;
    uint16_t logical_height;

    // 4. TAMC_GT911 rotation codes selected by config.store.fliptouch.
    // 4. Коды rotation TAMC_GT911, выбираемые config.store.fliptouch.
    // Must match TAMC_GT911.h: LEFT=0, INVERTED=1, RIGHT=2, NORMAL=3.
    // Должны совпадать с TAMC_GT911.h: LEFT=0, INVERTED=1, RIGHT=2, NORMAL=3.
    uint8_t rotation_when_fliptouch_off;
    uint8_t rotation_when_fliptouch_on;

    // 5 / 6. Axis swap and invert applied AFTER vendor rotation, BEFORE LVGL.
    // 5 / 6. Swap и invert ПОСЛЕ vendor-rotation, ДО LVGL.
    bool swap_xy;
    bool invert_x;
    bool invert_y;

    // 7. Current GT911 path is identity in pixel space — no calibration map().
    // 7. Текущий путь GT911 — identity в пикселях, без calibration map().
    bool raw_is_logical_pixel_space;

    // Equal-axis reject: current board used `> 10 && < 470` with 470 = 480-10.
    // Отклонение equal-axis: на текущей плате было `> 10 && < 470`, 470 = 480-10.
    uint16_t equal_axis_reject_margin;

    // 9. Interrupt vs polling. Current 4848S040: TS_INT=255, polled read().
    // 9. Прерывание vs polling. Текущая 4848S040: TS_INT=255, polling read().
    bool uses_interrupt;
};

// TAMC rotation enumerants duplicated so this header stays vendor-free.
// Дублируем enumerants TAMC, чтобы заголовок не тянул vendor-драйвер.
static constexpr uint8_t kYoradioTouchRotLeft = 0;
static constexpr uint8_t kYoradioTouchRotInverted = 1;
static constexpr uint8_t kYoradioTouchRotRight = 2;
static constexpr uint8_t kYoradioTouchRotNormal = 3;

// Current 4848S040 + GT911 values. Literal 480 belongs HERE only.
// Текущие значения 4848S040 + GT911. Литерал 480 живёт ТОЛЬКО здесь.
// 10. Controller↔display association is compile-time TS_MODEL + DSP_MODEL
//     in myoptions.h, not a runtime field.
// 10. Связка контроллер↔дисплей — compile-time TS_MODEL + DSP_MODEL
//     в myoptions.h, не runtime-поле.
static constexpr YoradioTouchGeometry kYoradioTouchGeometryCurrent = {
    0, 480, 0, 480,
    480, 480,
    480, 480,
    kYoradioTouchRotRight,  // fliptouch=false → TAMC rotation 2 (as-built)
    kYoradioTouchRotLeft,   // fliptouch=true  → TAMC rotation 0 (as-built)
    true,                   // swap_xy as-built: touchX=p.y, touchY=p.x
    true,                   // invert_x as-built: indev width-1-x
    false,                  // invert_y as-built: none
    true,                   // no calibration map on GT911
    10,                     // 470 = 480-10
    false                   // polling
};

inline uint8_t yoradio_touch_rotation(const YoradioTouchGeometry& g, bool fliptouch) {
    return fliptouch ? g.rotation_when_fliptouch_on : g.rotation_when_fliptouch_off;
}

// Generic raw filter. Returns false to drop the sample.
// Generic-фильтр сырых координат. false = отбросить выборку.
inline bool yoradio_touch_filter_raw(const YoradioTouchGeometry& g, uint16_t raw_x, uint16_t raw_y) {
    if (raw_x > g.raw_x_max || raw_y > g.raw_y_max) {
        return false;
    }
    if (raw_x < g.raw_x_min || raw_y < g.raw_y_min) {
        return false;
    }
    if (raw_x == 0 && raw_y == 0) {
        return false;
    }
    const uint16_t margin = g.equal_axis_reject_margin;
    if (raw_x == raw_y && margin > 0 && g.raw_x_max > margin) {
        const uint16_t hi_cut = static_cast<uint16_t>(g.raw_x_max - margin);
        if (raw_x > margin && raw_x < hi_cut) {
            return false;
        }
    }
    return true;
}

inline uint16_t yoradio_touch_clip_axis(uint16_t value, uint16_t size) {
    if (size == 0) {
        return 0;
    }
    if (value >= size) {
        return static_cast<uint16_t>(size - 1);
    }
    return value;
}

// As-built order after a passing filter: swap, then clip to logical size.
// Как в as-built после прошедшего фильтра: swap, затем clip в logical size.
inline void yoradio_touch_swap_and_clip(const YoradioTouchGeometry& g,
                                        uint16_t in_x, uint16_t in_y,
                                        uint16_t* out_x, uint16_t* out_y) {
    uint16_t x = in_x;
    uint16_t y = in_y;
    if (g.swap_xy) {
        const uint16_t tmp = x;
        x = y;
        y = tmp;
    }
    x = yoradio_touch_clip_axis(x, g.logical_width);
    y = yoradio_touch_clip_axis(y, g.logical_height);
    if (out_x) {
        *out_x = x;
    }
    if (out_y) {
        *out_y = y;
    }
}

// As-built invert happens AFTER clip (LVGL indev used width-1-x, not width-x).
// Invert в as-built — ПОСЛЕ clip (indev: width-1-x, не width-x).
inline void yoradio_touch_apply_invert(const YoradioTouchGeometry& g, uint16_t* x, uint16_t* y) {
    if (!x || !y) {
        return;
    }
    if (g.invert_x && g.logical_width > 0) {
        *x = static_cast<uint16_t>(g.logical_width - 1 - *x);
    }
    if (g.invert_y && g.logical_height > 0) {
        *y = static_cast<uint16_t>(g.logical_height - 1 - *y);
    }
}

// Full generic map for the LVGL pointer path: swap → clip → invert.
// Полный generic-map пути LVGL: swap → clip → invert.
inline void yoradio_touch_map_for_lvgl(const YoradioTouchGeometry& g,
                                       uint16_t in_x, uint16_t in_y,
                                       uint16_t* out_x, uint16_t* out_y) {
    yoradio_touch_swap_and_clip(g, in_x, in_y, out_x, out_y);
    yoradio_touch_apply_invert(g, out_x, out_y);
}

// Replica of TAMC_GT911::readPoint rotation. Synthetic / documentation only.
// Replica rotation из TAMC_GT911::readPoint. Только synthetic / документация.
// Production still uses the vendor driver; do not double-apply this at runtime.
// Production по-прежнему вызывает vendor; не применять это второй раз в runtime.
inline void yoradio_touch_apply_vendor_rotation(uint16_t width, uint16_t height, uint8_t rotation,
                                                uint16_t* x, uint16_t* y) {
    if (!x || !y) {
        return;
    }
    uint16_t vx = *x;
    uint16_t vy = *y;
    uint16_t temp;
    switch (rotation) {
        case kYoradioTouchRotNormal:
            vx = static_cast<uint16_t>(width - vx);
            vy = static_cast<uint16_t>(height - vy);
            break;
        case kYoradioTouchRotLeft:
            temp = vx;
            vx = static_cast<uint16_t>(width - vy);
            vy = temp;
            break;
        case kYoradioTouchRotInverted:
            break;
        case kYoradioTouchRotRight:
            temp = vx;
            vx = vy;
            vy = static_cast<uint16_t>(height - temp);
            break;
        default:
            break;
    }
    *x = vx;
    *y = vy;
}

#endif
