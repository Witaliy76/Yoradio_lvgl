#ifndef YORADIO_PLATFORM_TOUCH_FT5X06_BACKEND_H
#define YORADIO_PLATFORM_TOUCH_FT5X06_BACKEND_H

#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"

#include "resolved_touch_config.h"
#include "touch_port.h"

namespace yoradio {

// Minimal owner of the official FT5x06 device handles (FocalTech FT5x06 family,
// FT6336U included). Arduino Wire owns the I2C bus; this class owns only the panel
// IO and touch handles attached to it. Same contract as Gt911Backend (S07_bis-8).
// Владелец дескрипторов официального драйвера FT5x06 (семейство FocalTech, включая
// FT6336U). Шиной I2C владеет Arduino Wire; класс освобождает только свои дескрипторы.
// Контракт тот же, что у Gt911Backend (S07_bis-8).
class Ft5x06Backend final {
  public:
    Ft5x06Backend();
    ~Ft5x06Backend();

    bool begin(const ResolvedTouchConfig& config);
    void end();
    bool setOrientation(const YoradioTouchOrientation& orientation);
    TouchSample read();
    bool available() const { return touch_ != nullptr; }

  private:
    static void processCoordinates(esp_lcd_touch_handle_t touch,
                                   uint16_t* x,
                                   uint16_t* y,
                                   uint16_t* strength,
                                   uint8_t* point_count,
                                   uint8_t max_point_count);

    esp_lcd_panel_io_handle_t panel_io_;
    esp_lcd_touch_handle_t touch_;
};

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_TOUCH_FT5X06_BACKEND_H
