// PanelMode — panel-intrinsic recipe types (S04-1). Data only.
// Типы PanelMode — собственные параметры панели (S04-1). Только данные.
// Ownership: platform/display. No GPIO, BoardId, LVGL, touch, backlight,
// framebuffer allocation, PCLK policy, or flash guard.
// Владелец: platform/display. Без GPIO, BoardId, LVGL, тача, подсветки,
// выделения кадрового буфера, политики PCLK и защиты записи flash.
// Depends only on <stdint.h>, so a later LVGL include stays free of esp_lcd.
// Зависит только от <stdint.h>, поэтому включение из кода LVGL не требует esp_lcd.
//
// These names live in yoradio::panel so they do not redefine yoradio::PanelController
// and yoradio::DisplayTransport from board_contract.h. The numeric values match
// those enums (Unknown = 0, St7701 = 1, Rgb565Parallel16 = 1), but
// ResolvedDisplayConfig compares their meanings explicitly, not their numbers.
// Имена находятся в yoradio::panel и не переопределяют перечисления board_contract.h.
// Числа совпадают, но ResolvedDisplayConfig явно сопоставляет смысл значений.
#ifndef YORADIO_PLATFORM_DISPLAY_PANEL_MODE_H
#define YORADIO_PLATFORM_DISPLAY_PANEL_MODE_H

#include <stdint.h>

namespace yoradio {
namespace panel {

enum class Controller : uint8_t {
    Unknown = 0,
    St7701  = 1,
};

enum class Interface : uint8_t {
    Unknown           = 0,
    Rgb565Parallel16  = 1,
};

// Rgb565 = 0 matches the meaning of ::PixelFormat::Rgb565 used by DisplayPort.
// The types are still distinct; do not cast between them.
// Rgb565 = 0 по смыслу совпадает с ::PixelFormat::Rgb565, используемым DisplayPort.
// Типы разные; преобразовывать один в другой нельзя.
enum class PixelFormat : uint8_t {
    Rgb565 = 0,
};

// Source polarity is the product / Arduino_GFX value (1 = active high).
// esp_lcd stores the complementary idle-low flag. They are not the same number:
//   source polarity 1 -> esp_lcd idle_low 0
//   source polarity 0 -> esp_lcd idle_low 1
// This is the mapping in esp_lcd_st7701_backend.cpp createRgbPanel().
// Исходная полярность — значение продукта / Arduino_GFX (1 = активный высокий).
// esp_lcd хранит противоположный флаг idle_low. Это не одно и то же число.
constexpr uint8_t espLcdIdleLowFromSourcePolarity(uint8_t source_polarity) {
    return static_cast<uint8_t>(source_polarity == 0 ? 1 : 0);
}

// One sync edge: the source polarity and the esp_lcd flag it maps to.
// Полярность сигнала синхронизации и соответствующий ей флаг esp_lcd.
struct SyncPolarity {
    uint8_t source_polarity;
    uint8_t esp_lcd_idle_low;
};

struct Timing {
    uint16_t hsync_pulse_width;
    uint16_t hsync_back_porch;
    uint16_t hsync_front_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_back_porch;
    uint16_t vsync_front_porch;
    SyncPolarity hsync;
    SyncPolarity vsync;
    uint8_t de_idle_high;
    uint8_t pclk_active_neg;
    uint8_t pclk_idle_high;
};

// Identity of a checked vendor init stream. Not the stream, and not a pointer to it.
// The encoded bytes stay in the backend translation unit that already replays them.
// Описание проверенной последовательности команд производителя, без самих байтов.
// Байты остаются в единице трансляции драйвера, которая выполняет эти команды.
struct InitRecipeId {
    const char* name;
    const char* provenance;
    uint16_t encoded_bytes;
    uint16_t high_level_ops;
    uint16_t in_stream_delay_ms;
    uint8_t soft_reset_command;
    uint16_t soft_reset_delay_ms;
};

// Quirks of the panel itself. Host clocks and buffer counts do not belong here.
// Особенности самой панели. Частоты хоста и число буферов сюда не входят.
struct Quirks {
    bool orientation_commands_only_before_rgb_scanout;
};

struct PanelMode {
    Controller controller;
    Interface interface_kind;
    uint16_t native_width;
    uint16_t native_height;
    PixelFormat pixel_format;
    uint8_t rgb_bus_width_bits;
    Timing timing;
    InitRecipeId init;
    Quirks quirks;
};

}  // namespace panel
}  // namespace yoradio

#endif  // YORADIO_PLATFORM_DISPLAY_PANEL_MODE_H
