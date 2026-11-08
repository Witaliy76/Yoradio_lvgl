// TouchPort is the LVGL-neutral primary-pointer boundary.
// TouchPort — независимый от LVGL контракт основного указателя.
#ifndef YORADIO_PLATFORM_TOUCH_TOUCH_PORT_H
#define YORADIO_PLATFORM_TOUCH_TOUCH_PORT_H

#include <stdint.h>

namespace yoradio {

enum class TouchSampleStatus : uint8_t {
    // Fresh, trustworthy pressed point in logical display coordinates.
    // Свежее достоверное касание в логических координатах экрана.
    Valid = 0,
    // No fresh touch report; does not confirm release or clear fault uncertainty.
    // Свежего отчёта о касании нет; отпускание и выход из ошибки не подтверждены.
    NoData,
    // Failed or untrustworthy read; a cached point must not become a new press.
    // Ошибка или недостоверное чтение; сохранённая точка не означает новое нажатие.
    Error,
    // Backend is not ready; this is not evidence of physical release.
    // Драйвер не готов; это не подтверждение физического отпускания.
    Unavailable,
    // Fresh, successfully consumed report confirming zero contacts.
    // Свежий успешно обработанный отчёт подтверждает отсутствие касаний.
    Released,
};

// Coordinates are meaningful only for a pressed Valid sample. Other helpers set
// pressed=false and zero coordinates; that alone does not confirm physical release.
// Координаты значимы только для нажатого Valid. Для остальных статусов функции
// ниже задают pressed=false и нулевые координаты; это ещё не подтверждает отпускание.
struct TouchSample {
    TouchSampleStatus status;
    bool pressed;
    uint16_t x;
    uint16_t y;
};

constexpr TouchSample makeTouchPressed(uint16_t x, uint16_t y) {
    return TouchSample{TouchSampleStatus::Valid, true, x, y};
}

constexpr TouchSample makeTouchNoData() {
    return TouchSample{TouchSampleStatus::NoData, false, 0, 0};
}

constexpr TouchSample makeTouchReleased() {
    return TouchSample{TouchSampleStatus::Released, false, 0, 0};
}

constexpr TouchSample makeTouchError() {
    return TouchSample{TouchSampleStatus::Error, false, 0, 0};
}

constexpr TouchSample makeTouchUnavailable() {
    return TouchSample{TouchSampleStatus::Unavailable, false, 0, 0};
}

constexpr bool hasPressedPoint(const TouchSample& sample) {
    return sample.status == TouchSampleStatus::Valid && sample.pressed;
}

// TouchScreen owns the production backend; DspTask polls this interface. The LVGL
// adapter owns fault rearm and wake-only policy. Samples transfer no device ownership.
// Рабочим драйвером владеет TouchScreen, опрос выполняет DspTask. Восстановлением
// после ошибки и пробуждением без нажатия управляет адаптер LVGL. Отчёт — только данные.
class TouchPort {
  public:
    virtual ~TouchPort() = default;
    virtual TouchSample read() = 0;
    virtual bool available() const = 0;
};

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_TOUCH_TOUCH_PORT_H
