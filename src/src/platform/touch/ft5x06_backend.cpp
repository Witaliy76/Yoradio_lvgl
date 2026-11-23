#include "../../core/options.h"
#include "ft5x06_backend.h"

#include <Wire.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp32-hal-i2c.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"

namespace {

gpio_num_t toGpio(uint8_t pin) {
    return pin == yoradio::kPinNotConnected
               ? GPIO_NUM_NC
               : static_cast<gpio_num_t>(pin);
}

}  // namespace

namespace yoradio {

Ft5x06Backend::Ft5x06Backend()
    : panel_io_(nullptr),
      touch_(nullptr) {}

Ft5x06Backend::~Ft5x06Backend() {
    end();
}

bool Ft5x06Backend::begin(const ResolvedTouchConfig& config) {
    end();
    if (!config.available || config.service_mode != TouchServiceMode::Polling) {
        return false;
    }

    if (!Wire.begin(config.wiring.i2c_sda_gpio,
                    config.wiring.i2c_scl_gpio,
                    config.i2c_clock_hz)) {
        return false;
    }

    i2c_master_bus_handle_t bus = static_cast<i2c_master_bus_handle_t>(
        i2cBusHandle(Wire.getBusNum()));
    if (!bus) {
        return false;
    }

    // Same values as ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG(): 8-bit register address,
    // no control phase. Address and clock come from the board profile / defaults.
    // Те же значения, что в ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG(): 8-битный адрес
    // регистра, без control phase. Адрес и частота — из профиля платы и умолчаний.
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = config.wiring.i2c_address;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = config.i2c_clock_hz;

    if (esp_lcd_new_panel_io_i2c(bus, &io_config, &panel_io_) != ESP_OK) {
        panel_io_ = nullptr;
        return false;
    }

    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = yoradio_touch_axis_max(config.geometry.panel_width);
    touch_config.y_max = yoradio_touch_axis_max(config.geometry.panel_height);
    touch_config.rst_gpio_num = toGpio(config.wiring.reset_gpio);
    touch_config.int_gpio_num = toGpio(config.wiring.int_gpio);
    touch_config.flags.swap_xy = config.orientation.swap_xy;
    touch_config.flags.mirror_x = config.orientation.mirror_x;
    touch_config.flags.mirror_y = config.orientation.mirror_y;
    touch_config.process_coordinates = &Ft5x06Backend::processCoordinates;

    if (esp_lcd_touch_new_i2c_ft5x06(panel_io_, &touch_config, &touch_) != ESP_OK) {
        touch_ = nullptr;
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
        return false;
    }

    return true;
}

void Ft5x06Backend::end() {
    if (touch_) {
        esp_lcd_touch_del(touch_);
        touch_ = nullptr;
    }
    if (panel_io_) {
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
    }
    // Arduino Wire remains the I2C bus owner; never call Wire.end() here.
    // Шиной I2C продолжает владеть Arduino Wire; вызывать Wire.end() здесь нельзя.
}

bool Ft5x06Backend::setOrientation(
    const YoradioTouchOrientation& orientation) {
    if (!touch_) {
        return false;
    }
    return esp_lcd_touch_set_swap_xy(touch_, orientation.swap_xy) == ESP_OK &&
           esp_lcd_touch_set_mirror_x(touch_, orientation.mirror_x) == ESP_OK &&
           esp_lcd_touch_set_mirror_y(touch_, orientation.mirror_y) == ESP_OK;
}

// Range-safety clamp only, as in Gt911Backend: no logging, no I2C here.
// Только ограничение диапазона, как в Gt911Backend: без логов и чтений I2C.
void Ft5x06Backend::processCoordinates(esp_lcd_touch_handle_t touch,
                                       uint16_t* x,
                                       uint16_t* y,
                                       uint16_t*,
                                       uint8_t* point_count,
                                       uint8_t max_point_count) {
    if (!touch || !x || !y || !point_count) {
        return;
    }
    const uint8_t count = *point_count < max_point_count
                              ? *point_count
                              : max_point_count;
    for (uint8_t index = 0; index < count; ++index) {
        x[index] = yoradio_touch_clip_axis(
            x[index], static_cast<uint16_t>(touch->config.x_max + 1u));
        y[index] = yoradio_touch_clip_axis(
            y[index], static_cast<uint16_t>(touch->config.y_max + 1u));
    }
}

// esp_lcd_touch_ft5x06 1.1.1 semantics: read_data() reads TD_STATUS; with 0 points
// (or an invalid count > 5) it leaves the cache untouched, and get_data()
// invalidates the cache after every call. So a successful read followed by
// zero points means the finger is up; an I2C failure is an error.
// Семантика 1.1.1: при 0 точек (или неверном числе > 5) read_data() не трогает кеш,
// а get_data() сбрасывает его после каждого вызова. Поэтому успешное чтение
// с нулём точек означает «палец поднят»; ошибка I2C — ошибка.
TouchSample Ft5x06Backend::read() {
    if (!touch_) {
        return makeTouchUnavailable();
    }
    if (esp_lcd_touch_read_data(touch_) != ESP_OK) {
        return makeTouchError();
    }

    esp_lcd_touch_point_data_t point{};
    uint8_t point_count = 0;
    if (esp_lcd_touch_get_data(touch_, &point, &point_count, 1) != ESP_OK) {
        return makeTouchError();
    }
    if (point_count == 0) {
        return makeTouchReleased();
    }
    return makeTouchPressed(point.x, point.y);
}

}  // namespace yoradio
