// Direct esp_lcd ST7701 RGB backend implementation (BASE-DISP-ESPLCD-PARITY Slice 2).
// Реализация прямого esp_lcd ST7701 RGB backend (BASE-DISP-ESPLCD-PARITY Slice 2).
// Parity authority: Arduino_GFX @ 00dcd684 (type9 + ESP32RGBPanel config).
// Эталон паритета: Arduino_GFX @ 00dcd684 (type9 + конфиг ESP32RGBPanel).

#include "../core/options.h"
#if DSP_MODEL == DSP_ST7701

#include "esp_lcd_st7701_backend.h"
#include "st7701_type9_ops.h"

#include <Arduino.h>
#include <string.h>

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include "esp_err.h"

namespace yoradio_esp_lcd_st7701 {
namespace {

esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_fb = nullptr;
bool s_ready = false;

// --- 9-bit software SPI (Arduino_SWSPI semantics, DC undefined) ---
// --- Программный 9-bit SPI (семантика Arduino_SWSPI, DC нет) ---

inline void spiMosiHigh() { digitalWrite(ST7701_SDA, HIGH); }
inline void spiMosiLow() { digitalWrite(ST7701_SDA, LOW); }
inline void spiSckHigh() { digitalWrite(ST7701_SCK, HIGH); }
inline void spiSckLow() { digitalWrite(ST7701_SCK, LOW); }
inline void csLow() { digitalWrite(ST7701_CS, LOW); }
inline void csHigh() { digitalWrite(ST7701_CS, HIGH); }

void write9BitCommand(uint8_t c) {
    // D/C bit = 0 (command) / бит D/C = 0 (команда)
    spiMosiLow();
    spiSckHigh();
    spiSckLow();
    uint8_t bit = 0x80;
    while (bit) {
        if (c & bit) {
            spiMosiHigh();
        } else {
            spiMosiLow();
        }
        spiSckHigh();
        bit >>= 1;
        spiSckLow();
    }
}

void write9BitData(uint8_t d) {
    // D/C bit = 1 (data) / бит D/C = 1 (данные)
    spiMosiHigh();
    spiSckHigh();
    spiSckLow();
    uint8_t bit = 0x80;
    while (bit) {
        if (d & bit) {
            spiMosiHigh();
        } else {
            spiMosiLow();
        }
        spiSckHigh();
        bit >>= 1;
        spiSckLow();
    }
}

bool initControlBusPins() {
    pinMode(ST7701_CS, OUTPUT);
    pinMode(ST7701_SCK, OUTPUT);
    pinMode(ST7701_SDA, OUTPUT);
    csHigh();
    spiSckLow();
    spiMosiLow();
    return true;
}

// Mirror Arduino_DataBus::batchOperation for the Type9 opcode subset used here.
// Зеркало Arduino_DataBus::batchOperation для подмножества опкодов Type9.
bool replayType9Ops() {
    static_assert(kType9GfxEncodedBytes == 271u, "Type9 encoded size must stay 271");
    static_assert(kType9GfxHighLevelOps == 55u, "Type9 high-level op count must stay 55");

    const uint8_t* operations = kType9GfxEncoded;
    const size_t len = kType9GfxEncodedBytes;
    for (size_t i = 0; i < len; ++i) {
        uint8_t l = 0;
        switch (operations[i]) {
            case kBeginWrite:
                csLow();
                break;
            case kWriteC8D16:
                l++;
                /* fall through */
            case kWriteC8D8:
                l++;
                /* fall through */
            case kWriteCommand8:
                write9BitCommand(operations[++i]);
                break;
            case kWriteBytes:
                l = operations[++i];
                break;
            case kEndWrite:
                csHigh();
                break;
            case kDelay:
                delay(operations[++i]);
                break;
            default:
                Serial.printf("[esp_lcd_st7701] Unknown Type9 opcode at %u: %u\n",
                              static_cast<unsigned>(i), static_cast<unsigned>(operations[i]));
                return false;
        }
        while (l--) {
            write9BitData(operations[++i]);
        }
    }
    return true;
}

bool softResetPanel() {
    // Arduino_RGB_Display::begin: no RST GPIO → sendCommand(0x01) + delay(120).
    // Нет GPIO RST → команда 0x01 + delay(120). Separate from in-table 120 ms after 0x11.
    csLow();
    write9BitCommand(0x01);
    csHigh();
    delay(120);
    return true;
}

bool createRgbPanel() {
    // Copied/mapped from Arduino_ESP32RGBPanel::getFrameBuffer @ 00dcd684.
    // Скопировано/смаплено из Arduino_ESP32RGBPanel::getFrameBuffer @ 00dcd684.
    // Product ctor (displayST7701.cpp): prefer_speed=10e6, useBigEndian=false, bounce=0,
    // H pol=1 FP=10 PW=8 BP=50; V pol=1 FP=10 PW=8 BP=20; pclk_active_neg=0;
    // de_idle_high=0; pclk_idle_high=0.
    constexpr uint16_t kHsyncPolarity = 1;
    constexpr uint16_t kVsyncPolarity = 1;

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings.pclk_hz = 10000000UL;
    panel_config.timings.h_res = kWidth;
    panel_config.timings.v_res = kHeight;
    panel_config.timings.hsync_pulse_width = 8;
    panel_config.timings.hsync_back_porch = 50;
    panel_config.timings.hsync_front_porch = 10;
    panel_config.timings.vsync_pulse_width = 8;
    panel_config.timings.vsync_back_porch = 20;
    panel_config.timings.vsync_front_porch = 10;
    panel_config.timings.flags.hsync_idle_low = (kHsyncPolarity == 0) ? 1 : 0;
    panel_config.timings.flags.vsync_idle_low = (kVsyncPolarity == 0) ? 1 : 0;
    panel_config.timings.flags.de_idle_high = 0;
    panel_config.timings.flags.pclk_active_neg = 0;
    panel_config.timings.flags.pclk_idle_high = 0;

    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    panel_config.num_fbs = 1;
    panel_config.bounce_buffer_size_px = 0;
    // GFX sets deprecated sram_trans_align=8 / psram_trans_align=64 (union → dma_burst_size).
    panel_config.sram_trans_align = 8;
    panel_config.psram_trans_align = 64;

    panel_config.hsync_gpio_num = ST7701_HSYNC;
    panel_config.vsync_gpio_num = ST7701_VSYNC;
    panel_config.de_gpio_num = ST7701_DE;
    panel_config.pclk_gpio_num = ST7701_PCLK;
    panel_config.disp_gpio_num = GPIO_NUM_NC;

    // Little-endian / useBigEndian=false pin map (B0..B4, G0..G5, R0..R4).
    panel_config.data_gpio_nums[0] = ST7701_B0;
    panel_config.data_gpio_nums[1] = ST7701_B1;
    panel_config.data_gpio_nums[2] = ST7701_B2;
    panel_config.data_gpio_nums[3] = ST7701_B3;
    panel_config.data_gpio_nums[4] = ST7701_B4;
    panel_config.data_gpio_nums[5] = ST7701_G0;
    panel_config.data_gpio_nums[6] = ST7701_G1;
    panel_config.data_gpio_nums[7] = ST7701_G2;
    panel_config.data_gpio_nums[8] = ST7701_G3;
    panel_config.data_gpio_nums[9] = ST7701_G4;
    panel_config.data_gpio_nums[10] = ST7701_G5;
    panel_config.data_gpio_nums[11] = ST7701_R0;
    panel_config.data_gpio_nums[12] = ST7701_R1;
    panel_config.data_gpio_nums[13] = ST7701_R2;
    panel_config.data_gpio_nums[14] = ST7701_R3;
    panel_config.data_gpio_nums[15] = ST7701_R4;

    panel_config.flags.disp_active_low = true;
    panel_config.flags.refresh_on_demand = false;
    panel_config.flags.fb_in_psram = true;
    panel_config.flags.double_fb = false;
    panel_config.flags.no_fb = false;
    panel_config.flags.bb_invalidate_cache = false;

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &s_panel);
    if (err != ESP_OK || !s_panel) {
        Serial.printf("[esp_lcd_st7701] esp_lcd_new_rgb_panel failed: %s\n", esp_err_to_name(err));
        s_panel = nullptr;
        return false;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        Serial.printf("[esp_lcd_st7701] esp_lcd_panel_reset failed: %s\n", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        Serial.printf("[esp_lcd_st7701] esp_lcd_panel_init failed: %s\n", esp_err_to_name(err));
        return false;
    }

    void* frame_buffer = nullptr;
    err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &frame_buffer);
    if (err != ESP_OK || frame_buffer == nullptr) {
        Serial.printf("[esp_lcd_st7701] get_frame_buffer failed: %s\n", esp_err_to_name(err));
        return false;
    }
    s_fb = static_cast<uint16_t*>(frame_buffer);

    if (!esp_ptr_external_ram(s_fb)) {
        Serial.println("[esp_lcd_st7701] framebuffer is NOT in external/PSRAM");
        return false;
    }
    Serial.printf("[esp_lcd_st7701] FB ok addr=%p bytes=%u PSRAM=yes\n",
                  static_cast<void*>(s_fb), static_cast<unsigned>(kFramebufferBytes));
    return true;
}

}  // namespace

bool backendPresent() {
    return true;
}

bool begin() {
    Serial.println("[esp_lcd_st7701] begin enter");
    if (s_ready) {
        return true;
    }
    end();  // clear any half state / очистить полусостояние

    if (!initControlBusPins()) {
        Serial.println("[esp_lcd_st7701] control bus pin init FAIL");
        return false;
    }
    Serial.println("[esp_lcd_st7701] soft reset 0x01 + 120ms");
    if (!softResetPanel()) {
        end();
        return false;
    }
    Serial.println("[esp_lcd_st7701] Type9 replay");
    if (!replayType9Ops()) {
        Serial.println("[esp_lcd_st7701] Type9 replay FAIL");
        end();
        return false;
    }
    Serial.println("[esp_lcd_st7701] Type9 replay PASS");

    if (!createRgbPanel()) {
        end();
        return false;
    }

    if (!fillRgb565(0x0000)) {
        Serial.println("[esp_lcd_st7701] initial clear FAIL");
        end();
        return false;
    }

    s_ready = true;
    Serial.println("[esp_lcd_st7701] begin PASS (not selected by DisplayPort)");
    return true;
}

bool isReady() {
    return s_ready && s_panel && s_fb;
}

uint16_t* framebuffer() {
    return s_fb;
}

size_t framebufferBytes() {
    return kFramebufferBytes;
}

bool cacheWritebackFull() {
    if (!s_fb) {
        return false;
    }
    // Conservative full-FB C2M — parity with Arduino_RGB_Display::flush(auto_flush=false).
    // Консервативный полный C2M FB — паритет с Arduino_RGB_Display::flush(auto_flush=false).
    const esp_err_t err = esp_cache_msync(
        s_fb, kFramebufferBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (err != ESP_OK) {
        Serial.printf("[esp_lcd_st7701] esp_cache_msync FAIL: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool fillRgb565(uint16_t color) {
    if (!s_fb) {
        return false;
    }
    const size_t pixels = static_cast<size_t>(kWidth) * kHeight;
    for (size_t i = 0; i < pixels; ++i) {
        s_fb[i] = color;
    }
    return cacheWritebackFull();
}

bool drawBringupPattern() {
    if (!s_fb) {
        return false;
    }
    // Simple 480×480 diagnostic: quadrants + RGB bars + W/B strip.
    // Простая диагностика 480×480: квадранты + RGB полосы + W/B.
    // RGB565: R=0xF800, G=0x07E0, B=0x001F, W=0xFFFF, K=0x0000
    for (uint16_t y = 0; y < kHeight; ++y) {
        for (uint16_t x = 0; x < kWidth; ++x) {
            uint16_t c;
            if (y < 40) {
                // Top orientation bar: white→black left-to-right marker at y=0 edge.
                // Верхняя полоса ориентации.
                c = (x < 40) ? 0xFFFF : ((x >= (kWidth - 40)) ? 0x0000 : 0x8430);
            } else if (y >= (kHeight - 40)) {
                // Bottom bar: black→white (opposite of top corners).
                c = (x < 40) ? 0x0000 : ((x >= (kWidth - 40)) ? 0xFFFF : 0x8430);
            } else if (y < 160) {
                c = 0xF800;  // red band
            } else if (y < 280) {
                c = 0x07E0;  // green band
            } else if (y < 400) {
                c = 0x001F;  // blue band
            } else {
                // Lower field: left black / right white
                c = (x < (kWidth / 2)) ? 0x0000 : 0xFFFF;
            }
            // Left/right edge ticks for horizontal orientation.
            if (x < 4) {
                c = 0xFFFF;
            } else if (x >= (kWidth - 4)) {
                c = 0x0000;
            }
            s_fb[static_cast<size_t>(y) * kWidth + x] = c;
        }
    }
    return cacheWritebackFull();
}

void setBrightnessPercent(uint8_t percent) {
    pinMode(ST7701_BL, OUTPUT);
    analogWrite(ST7701_BL, map(percent, 0, 100, 0, 255));
}

void end() {
    s_ready = false;
    s_fb = nullptr;
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
    }
}

}  // namespace yoradio_esp_lcd_st7701

#endif  // DSP_ST7701
