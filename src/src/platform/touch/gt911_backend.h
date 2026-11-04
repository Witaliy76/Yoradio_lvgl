#ifndef YORADIO_PLATFORM_TOUCH_GT911_BACKEND_H
#define YORADIO_PLATFORM_TOUCH_GT911_BACKEND_H

#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

#include "resolved_touch_config.h"
#include "touch_port.h"

namespace yoradio {

// Minimal owner of the official GT911 device handles. Arduino Wire owns the
// I2C bus; this class owns only panel IO and touch handles attached to it.
class Gt911Backend final {
  public:
    Gt911Backend();
    ~Gt911Backend();

    bool begin(const ResolvedTouchConfig& config);
    void end();
    bool setOrientation(const YoradioTouchOrientation& orientation);
    TouchSample read();
    bool available() const { return touch_ != nullptr; }

  private:
    // The official component applies swap/mirror after this range-safety clamp.
    static void processCoordinates(esp_lcd_touch_handle_t touch,
                                   uint16_t* x,
                                   uint16_t* y,
                                   uint16_t* strength,
                                   uint8_t* point_count,
                                   uint8_t max_point_count);

    esp_lcd_panel_io_handle_t panel_io_;
    esp_lcd_touch_handle_t touch_;
    esp_lcd_touch_io_gt911_config_t driver_config_;
};

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_TOUCH_GT911_BACKEND_H
