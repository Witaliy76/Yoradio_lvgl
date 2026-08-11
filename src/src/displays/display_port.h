// DisplayPort — backend-agnostic display seam (Path L foundation).
// DisplayPort — backend-agnostic шов дисплея (foundation Path L).
// Ownership: YoRadio display layer; concrete backend lives in the active DSP TU.
// Владелец: слой дисплея YoRadio; конкретный backend — в активном DSP TU.
// Public API: no backend / UI library types.
// Публичный API: без типов backend / UI-библиотек.
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_DISPLAY_PORT_H
#define YORADIO_DISPLAY_PORT_H

#include <stdint.h>

// Inclusive pixel rectangle for a flush region / Inclusive-прямоугольник области flush.
struct DisplayArea {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
};

enum class PixelFormat : uint8_t {
    Rgb565 = 0
};

struct DisplayGeometry {
    uint16_t width;
    uint16_t height;
    PixelFormat format;
};

namespace DisplayPort {

// Physical panel bring-up only (no UI registration) / Только физический bring-up (без регистрации UI).
bool begin();

DisplayGeometry geometry();

// Synchronous today; done(ctx) runs after backend work / Сейчас синхронно; done(ctx) после работы backend.
void flush(const DisplayArea& area, const uint16_t* pixels,
           void (*done)(void* ctx), void* ctx);

// percent: existing 0..100 product semantics / percent: текущая продуктовая семантика 0..100.
void setBrightness(uint8_t percent);

void sleep();
void wake();

// Request one RGB scanout resynchronization (no raw panel handle exposed).
// The underlying restart is performed by the driver at the next VSYNC, not at call time;
// multiple requests issued before that VSYNC coalesce into a single restart.
// Safe no-op if the active backend/panel is not ready.
//
// Display policy owns when this is called — see Display::_performRgbResync().
// Other subsystems must use Display::requestRgbResync() instead of calling this directly.
//
// Запрос одного ресинхрона RGB scanout (raw panel handle не раскрывается).
// Реальный restart выполняет драйвер на следующем VSYNC, а не в момент вызова;
// несколько запросов до этого VSYNC схлопываются в один restart.
// Безопасный no-op, если активный backend/panel ещё не готов.
void restartRgbScanout();

}  // namespace DisplayPort

#endif
