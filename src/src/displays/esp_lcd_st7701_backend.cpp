// Direct esp_lcd ST7701 RGB backend implementation behind DisplayPort.
// Реализация прямого esp_lcd ST7701 RGB backend за интерфейсом DisplayPort.
// Parity authority: Arduino_GFX @ 00dcd684 (type9 + ESP32RGBPanel config).
// Эталон паритета: Arduino_GFX @ 00dcd684 (type9 + конфиг ESP32RGBPanel).
// Author: Witaliy76 - https://github.com/Witaliy76

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
// Type9 does not write MADCTL; ST7701S reset default is 0x00. Track the full
// value so changing ML never clobbers BGR or another unrelated MADCTL bit.
// Type9 не пишет MADCTL; reset default ST7701S = 0x00. Храним весь байт,
// чтобы изменение ML не затронуло BGR и прочие биты.
uint8_t s_madctl = 0x00;

constexpr uint8_t kCmdCommand2BankSelect = 0xFF;
constexpr uint8_t kCmdSourceDirection = 0xC7;
constexpr uint8_t kCmdMadctl = 0x36;
constexpr uint8_t kSourceDirectionReverse = 0x04;  // SDIR.SS, bit 2
constexpr uint8_t kMadctlMl = 0x10;                // MADCTL.ML, bit 4
constexpr uint8_t kCommand2Bank0[] = {0x77, 0x01, 0x00, 0x00, 0x10};
constexpr uint8_t kCommand2Disable[] = {0x77, 0x01, 0x00, 0x00, 0x00};

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

bool writeControlCommand(uint8_t command, const uint8_t* data = nullptr, size_t data_size = 0) {
    csLow();
    write9BitCommand(command);
    for (size_t i = 0; i < data_size; ++i) {
        write9BitData(data[i]);
    }
    csHigh();
    return true;
}

bool applyPanelOrientation180(bool flipped) {
    const uint8_t sdir = flipped ? kSourceDirectionReverse : 0x00;
    if (!writeControlCommand(kCmdCommand2BankSelect, kCommand2Bank0,
                             sizeof(kCommand2Bank0)) ||
        !writeControlCommand(kCmdSourceDirection, &sdir, 1) ||
        !writeControlCommand(kCmdCommand2BankSelect, kCommand2Disable,
                             sizeof(kCommand2Disable))) {
        return false;
    }

    if (flipped) {
        s_madctl |= kMadctlMl;
    } else {
        s_madctl &= static_cast<uint8_t>(~kMadctlMl);
    }
    return writeControlCommand(kCmdMadctl, &s_madctl, 1);
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

    // Some 4848S040 panels accept orientation commands only before the RGB
    // pixel stream starts. Keep this a fixed board option; touch is separate.
    if (ST7701_BOOT_ORIENTATION_180) {
        Serial.println("[esp_lcd_st7701] applying fixed boot orientation 180");
        if (!applyPanelOrientation180(true)) {
            Serial.println("[esp_lcd_st7701] fixed boot orientation FAIL");
            end();
            return false;
        }
    }

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
    Serial.println("[esp_lcd_st7701] begin PASS (active DisplayPort backend)");
    return true;
}

bool isReady() {
    return s_ready && s_panel && s_fb;
}

bool setInverted(bool inverted) {
    if (!isReady()) {
        return false;
    }

    // The documented ST7701 INVOFF/INVON path was accepted by the write-only
    // 9-bit transport but produced no physical change on ESP32-4848S040.
    // ESP-IDF's native RGB panel operation performs the same visible bitwise
    // inversion at the RGB output GPIO matrix, without touching framebuffer,
    // flush/cache topology, or adding a second command transport.
    // На 4848S040 контроллер проигнорировал документированные 0x20/0x21.
    // Штатная операция esp_lcd инвертирует RGB-линии в GPIO matrix, не меняя
    // framebuffer и принятый flush/cache path.
    const esp_err_t err = esp_lcd_panel_invert_color(s_panel, inverted);
    if (err != ESP_OK) {
        Serial.printf("[esp_lcd_st7701] RGB output inversion failed: %s\n",
                      esp_err_to_name(err));
        return false;
    }
    return true;
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

bool blitRgb565(int16_t x, int16_t y, const uint16_t* src, int16_t w, int16_t h) {
    if (!s_fb || !src || w <= 0 || h <= 0) {
        return false;
    }

    // Clip exactly like gfx_draw_bitmap_to_framebuffer (Arduino_G.cpp @ 00dcd684):
    // same reject test, same source-pointer rewind, same x_skip source stride.
    // Отсечение точно как в gfx_draw_bitmap_to_framebuffer: тот же reject,
    // та же перемотка указателя источника, тот же x_skip (stride источника).
    constexpr int16_t kMaxX = static_cast<int16_t>(kWidth) - 1;
    constexpr int16_t kMaxY = static_cast<int16_t>(kHeight) - 1;
    if (((x + w - 1) < 0) || ((y + h - 1) < 0) || (x > kMaxX) || (y > kMaxY)) {
        return false;
    }

    int16_t x_skip = 0;
    if ((y + h - 1) > kMaxY) {
        h -= (y + h - 1) - kMaxY;
    }
    if (y < 0) {
        src -= static_cast<int32_t>(y) * w;
        h += y;
        y = 0;
    }
    if ((x + w - 1) > kMaxX) {
        x_skip = (x + w - 1) - kMaxX;
        w -= x_skip;
    }
    if (x < 0) {
        src -= x;
        x_skip -= x;
        w += x;
        x = 0;
    }

    // Destination stride is the physical framebuffer width; RGB565 copied verbatim
    // (no byte swap, no RGB/BGR conversion) — parity with the GFX row loop.
    // Stride приёмника = физическая ширина FB; RGB565 копируется как есть.
    uint16_t* row = s_fb + (static_cast<size_t>(y) * kWidth) + x;
    const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
    const int16_t src_stride = static_cast<int16_t>(w + x_skip);
    for (int16_t j = 0; j < h; ++j) {
        memcpy(row, src, row_bytes);
        src += src_stride;
        row += kWidth;
    }
    return true;
}

void setBrightnessPercent(uint8_t percent) {
    pinMode(ST7701_BL, OUTPUT);
    analogWrite(ST7701_BL, map(percent, 0, 100, 0, 255));
}

void end() {
    s_ready = false;
    s_fb = nullptr;
    s_madctl = 0x00;
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
    }
}

bool restartRgbScanout() {
    // Official esp_lcd recovery for a DMA/LCD desync caused by insufficient bandwidth.
    // esp_lcd_rgb_panel_restart() only raises an internal flag; the driver performs the
    // GDMA restart in its next VSYNC handler, so requests issued within the same frame
    // coalesce into a single restart.
    // Официальное восстановление esp_lcd при рассинхроне DMA/LCD из-за нехватки bandwidth.
    // esp_lcd_rgb_panel_restart() лишь поднимает внутренний флаг; сам restart драйвер
    // выполняет в следующем обработчике VSYNC, поэтому запросы внутри одного кадра
    // схлопываются в один restart.
    if (!s_ready || !s_panel) {
        return false;
    }
    const esp_err_t err = esp_lcd_rgb_panel_restart(s_panel);
    if (err != ESP_OK) {
        Serial.printf("[esp_lcd_st7701] rgb_panel_restart failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace yoradio_esp_lcd_st7701

#endif  // DSP_ST7701
