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

// MANUAL / SERVICE ONLY (S01-5D). One RGB scanout restart, no raw panel handle exposed. The
// driver performs it at the next VSYNC, not at call time, and requests issued before that VSYNC
// coalesce. Safe no-op when the panel is not ready; rejected when bounce buffers are active
// (bounce > 0).
//
// This is NOT a recovery mechanism and no product code may call it automatically. A one-shot
// restart during PLAY is nondeterministic (NORMAL / BLACK / SHIFT, S01-4X-ISO-D), which is why
// the whole automatic resync layer was removed in S01-5D. Its only permitted caller is the
// diagnostic CLI, so that a human can force a restart deliberately while investigating; every
// such call is counted in restartExec, which must therefore read 0 in an acceptance session.
// Runtime scanout integrity is maintained by prevention (the PCLK flash guard), and cold-start
// alignment by the separate boot-only path.
//
// ТОЛЬКО РУЧНОЙ / СЕРВИСНЫЙ ВЫЗОВ (S01-5D). Один restart RGB scanout; raw panel handle не
// раскрывается. Драйвер выполняет его на следующем VSYNC; запросы до этого VSYNC схлопываются.
// Безопасный no-op, если панель не готова; отклоняется при bounce > 0.
//
// Это НЕ механизм восстановления, и продуктовый код не вправе вызывать его автоматически.
// Одиночный restart во время PLAY недетерминирован (NORMAL / BLACK / SHIFT), поэтому весь
// автоматический слой resync удалён в S01-5D. Единственный допустимый вызывающий —
// диагностический CLI, чтобы человек мог форсировать restart осознанно при разборе; каждый такой
// вызов учитывается в restartExec, который в приёмочной сессии обязан быть 0. Целостность
// scanout в runtime обеспечивает профилактика (PCLK flash guard), а выравнивание холодного
// старта — отдельный boot-only путь.
void restartRgbScanout();

// RGB scanout facts for the flash guard and diagnostics. No backend types.
// Факты RGB scanout для flash guard и диагностики. Без типов backend.
struct RgbScanoutStatus {
    bool panelReady;
    bool vsyncCounterReady;
    uint16_t bounceBufferPx;      // 0 = direct T0 / 0 = direct T0
    uint32_t vsyncSeq;
    uint32_t nominalPclkHz;
    uint32_t flashGuardPclkHz;
    uint32_t restartExec;          // esp_lcd_rgb_panel_restart() calls issued
    uint32_t restartRejectedBounce;
};
RgbScanoutStatus rgbScanoutStatus();

// Monotonic VSYNC count; the driver applies a pending PCLK after this count advances.
// Монотонный счётчик VSYNC; драйвер применяет отложенный PCLK после его инкремента.
uint32_t rgbVsyncSeq();

// Record a new RGB PCLK. Applied by the driver in a later VSYNC ISR; true = recorded only.
// Caller: the RGB flash guard on DspTask. Never a scanout restart.
// Записать новый PCLK. Драйвер применит его в одном из следующих VSYNC ISR; true = только
// записано. Вызывающий — RGB flash guard на DspTask. Это не restart scanout.
bool requestRgbPclkHz(uint32_t hz);

// One-shot promotion of the RGB PCLK from the startup frequency to the runtime nominal, issued at
// the Boot->Main handoff. Ordinary set-PCLK transition; never a scanout restart.
// Одноразовое повышение PCLK от стартовой частоты к runtime-номиналу на переходе Boot->Main.
// Обычный переход set-PCLK; не restart scanout.
bool promoteRgbPclkToRuntimeNominal();

}  // namespace DisplayPort

#endif
