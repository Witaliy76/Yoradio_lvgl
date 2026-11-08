#include "../../core/options.h"
#include "gt911_backend.h"

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

Gt911Backend::Gt911Backend()
    : panel_io_(nullptr),
      touch_(nullptr),
      driver_config_{} {}

Gt911Backend::~Gt911Backend() {
    end();
}

bool Gt911Backend::begin(const ResolvedTouchConfig& config) {
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

    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = config.wiring.i2c_address;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 16;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = config.i2c_clock_hz;

    if (esp_lcd_new_panel_io_i2c(bus, &io_config, &panel_io_) != ESP_OK) {
        panel_io_ = nullptr;
        return false;
    }

    driver_config_.dev_addr = config.wiring.i2c_address;

    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = yoradio_touch_axis_max(config.geometry.panel_width);
    touch_config.y_max = yoradio_touch_axis_max(config.geometry.panel_height);
    touch_config.rst_gpio_num = toGpio(config.wiring.reset_gpio);
    touch_config.int_gpio_num = toGpio(config.wiring.int_gpio);
    touch_config.flags.swap_xy = config.orientation.swap_xy;
    touch_config.flags.mirror_x = config.orientation.mirror_x;
    touch_config.flags.mirror_y = config.orientation.mirror_y;
    touch_config.process_coordinates = &Gt911Backend::processCoordinates;
    touch_config.driver_data = &driver_config_;

    report_io_.attach(panel_io_);
    if (esp_lcd_touch_new_i2c_gt911(&report_io_, &touch_config, &touch_) != ESP_OK) {
        touch_ = nullptr;
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
        return false;
    }

    return true;
}

void Gt911Backend::end() {
    if (touch_) {
        esp_lcd_touch_del(touch_);
        touch_ = nullptr;
    }
    if (panel_io_) {
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
    }
    report_io_.attach(nullptr);
    // Arduino Wire remains the I2C bus owner; never call Wire.end() here.
    // Шиной I2C продолжает владеть Arduino Wire; вызывать Wire.end() здесь нельзя.
}

bool Gt911Backend::setOrientation(
    const YoradioTouchOrientation& orientation) {
    if (!touch_) {
        return false;
    }
    return esp_lcd_touch_set_swap_xy(touch_, orientation.swap_xy) == ESP_OK &&
           esp_lcd_touch_set_mirror_x(touch_, orientation.mirror_x) == ESP_OK &&
           esp_lcd_touch_set_mirror_y(touch_, orientation.mirror_y) == ESP_OK;
}

// esp_lcd_touch invokes this after copying its cached point outside the driver
// lock and before software mirror/swap. Keep only the production range-safety
// clamp here; no logging or secondary controller read belongs in this callback.
// esp_lcd_touch вызывает обработчик после копирования точки из кеша, вне блокировки
// драйвера и до mirror/swap. Здесь только ограничение диапазона, без логов и чтений I2C.
void Gt911Backend::processCoordinates(esp_lcd_touch_handle_t touch,
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

TouchSample Gt911Backend::read() {
    if (!touch_) {
        return makeTouchUnavailable();
    }

    report_io_.beginRead();
    const esp_err_t read_result = esp_lcd_touch_read_data(touch_);
    if (read_result != ESP_OK || report_io_.result() != ESP_OK ||
        !report_io_.statusSeen()) {
        // Do not query the component cache after a failed hardware transaction.
        // После неудачной аппаратной транзакции не запрашиваем точку из кеша.
        return makeTouchError();
    }

    // Readiness belongs to this exact vendor transaction, never to its point
    // cache. In 1.2.1, no-ready leaves the cache unchanged; bad counts clear it.
    // Готовность определяется текущей транзакцией, а не кешем точек. В 1.2.1
    // отсутствие ready не меняет кеш; некорректное число касаний его очищает.
    if (!report_io_.ready()) {
        return makeTouchNoData();
    }
    if (!report_io_.acknowledged() || report_io_.touchCount() > 5) {
        return makeTouchError();
    }
    // With buttons enabled the driver handles key reports before touch points
    // and leaves the point cache unchanged. They cannot prove finger release.
    // При включённых кнопках драйвер сначала обрабатывает их отчёты, не меняя
    // кеш точек. Такие отчёты не подтверждают отпускание пальца.
    if (report_io_.keyReport()) {
        return makeTouchNoData();
    }
    if (report_io_.touchCount() == 0) {
        return makeTouchReleased();
    }

    esp_lcd_touch_point_data_t point{};
    uint8_t point_count = 0;
    if (esp_lcd_touch_get_data(touch_, &point, &point_count, 1) != ESP_OK) {
        return makeTouchError();
    }
    if (point_count == 0) {
        return makeTouchError();
    }
    return makeTouchPressed(point.x, point.y);
}

}  // namespace yoradio
