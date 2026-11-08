// Observe the pinned GT911 driver's own synchronous transactions. No extra I2C
// reads/writes: freshness and ACK failures cannot be recovered from its cache.
// Наблюдает синхронные транзакции закреплённой версии драйвера GT911 без новых
// операций I2C: по кешу точек нельзя определить свежесть отчёта или сбой его подтверждения.
#ifndef YORADIO_PLATFORM_TOUCH_GT911_REPORT_IO_H
#define YORADIO_PLATFORM_TOUCH_GT911_REPORT_IO_H

#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"

namespace yoradio {

// Borrows transport_; Gt911Backend owns its lifetime. Used synchronously on DspTask.
// transport_ заимствован у Gt911Backend, который управляет его временем жизни.
// Обращения выполняются синхронно в DspTask.
class Gt911ReportIo final : public esp_lcd_panel_io_t {
  public:
    Gt911ReportIo() : esp_lcd_panel_io_t{}, transport_(nullptr) {
        rx_param = &receive;
        tx_param = &transmit;
        beginRead();
    }

    void attach(esp_lcd_panel_io_handle_t transport) {
        transport_ = transport;
        beginRead();
    }

    void beginRead() {
        result_ = ESP_OK;
        status_ = 0;
        status_seen_ = false;
        acknowledged_ = false;
    }

    esp_err_t result() const { return result_; }
    bool statusSeen() const { return status_seen_; }
    bool ready() const { return (status_ & 0x80u) != 0; }
    bool keyReport() const { return (status_ & 0x10u) != 0; }
    uint8_t touchCount() const { return status_ & 0x0fu; }
    // The driver's successful write of zero to the status register consumes a report;
    // this is report acknowledgement, not the I2C bus-level ACK bit.
    // Успешная запись нуля в регистр статуса подтверждает обработку отчёта;
    // это не бит ACK шины I2C.
    bool acknowledged() const { return acknowledged_; }

  private:
    static constexpr int kStatusRegister = 0x814e;

    static esp_err_t receive(esp_lcd_panel_io_t* io, int command,
                             void* data, size_t size) {
        auto& self = *static_cast<Gt911ReportIo*>(io);
        const esp_err_t result = esp_lcd_panel_io_rx_param(
            self.transport_, command, data, size);
        self.observeResult(result);
        if (result == ESP_OK && command == kStatusRegister && size == 1) {
            self.status_ = *static_cast<const uint8_t*>(data);
            self.status_seen_ = true;
        }
        return result;
    }

    static esp_err_t transmit(esp_lcd_panel_io_t* io, int command,
                              const void* data, size_t size) {
        auto& self = *static_cast<Gt911ReportIo*>(io);
        const esp_err_t result = esp_lcd_panel_io_tx_param(
            self.transport_, command, data, size);
        self.observeResult(result);
        if (result == ESP_OK && self.status_seen_ &&
            command == kStatusRegister && size == 1 &&
            *static_cast<const uint8_t*>(data) == 0) {
            self.acknowledged_ = true;
        }
        return result;
    }

    void observeResult(esp_err_t result) {
        if (result_ == ESP_OK && result != ESP_OK) {
            result_ = result;
        }
    }

    esp_lcd_panel_io_handle_t transport_;
    esp_err_t result_;
    uint8_t status_;
    bool status_seen_;
    bool acknowledged_;
};

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_TOUCH_GT911_REPORT_IO_H
