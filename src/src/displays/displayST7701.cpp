/************************************************************************************************
   Display driver for ST7701 (480x480) RGB Panel ESP32-4848S040
   Block 8-E17/E18B: LVGL product — output_display direct; no Canvas; no legacy draw APIs.
************************************************************************************************/

// Adaptation: Witaliy76 - https://github.com/Witaliy76
#include "../core/options.h"
#if DSP_MODEL == DSP_ST7701

#include "displayST7701.h"
#include "display_port.h"
#include <cstring>
#include "../core/spidog.h"
#include "../core/config.h"
#include "../core/network.h"
#include "../core/display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Arduino_GFX_Library.h"

extern const uint8_t st7701_type9_init_operations[];
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32-hal-ledc.h"
#endif

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
#include "../lvgl_ui/lvgl_ui.h"
#endif

#include "driver/adc.h"
#include "esp_adc_cal.h"

#define TAKE_MUTEX() sdog.takeMutex()
#define GIVE_MUTEX() sdog.giveMutex()

static Arduino_DataBus* bus = nullptr;
static Arduino_ESP32RGBPanel* rgbpanel = nullptr;
static Arduino_RGB_Display* output_display = nullptr;

#ifndef BATTERY_OFF

#ifndef ADC_PIN
#define ADC_PIN 5
#endif
#if (ADC_PIN == 5)
#define USER_ADC_CHAN ADC1_CHANNEL_4
#endif

#ifndef R1
#define R1 33
#endif
#ifndef R2
#define R2 100
#endif
#ifndef DELTA_BAT
#define DELTA_BAT 0
#endif

#define BATTERY_SAMPLES 100
#define BATTERY_CHECK_INTERVAL 5000
#define BATTERY_VOLTAGE_THRESHOLD 0.01f
#define BATTERY_STABLE_THRESHOLD 0.002f
#define BATTERY_CHARGE_MIN_VOLTAGE 3.5f
#define BATTERY_SMOOTH_COUNT 10

float ADC_R1 = R1;
float ADC_R2 = R2;
float DELTA = DELTA_BAT;

uint8_t g, t = 1;
bool Charging = false;

float Volt = 0;
float Volt1 = 0, Volt2 = 0, Volt3 = 0, Volt4 = 0, Volt5 = 0;
float smoothBuffer[BATTERY_SMOOTH_COUNT] = {0};
uint8_t smoothIndex = 0;
static esp_adc_cal_characteristics_t adc1_chars;

uint8_t ChargeLevel;
float vs[22] = {2.60, 3.10, 3.20, 3.26, 3.29, 3.33, 3.37, 3.41, 3.46, 3.51, 3.56, 3.61, 3.65, 3.69, 3.72, 3.75, 3.78, 3.82, 3.88, 3.95, 4.03, 4.25};

struct {
    float lastVoltage = 0;
    uint32_t lastCheck = 0;
    uint8_t chargeCount = 0;
    uint8_t dischargeCount = 0;
    uint8_t stableCount = 0;
} chargingState;

#endif

DspCore::DspCore() {
#ifndef BATTERY_OFF
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(USER_ADC_CHAN, ADC_ATTEN_DB_12);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &adc1_chars);
#endif
}

// BASE-DISP-PORT Slice 2: physical bring-up lives behind DisplayPort::begin();
// DspCore::initDisplay remains the product entry and preserves call-site parity.
// BASE-DISP-PORT Slice 2: физический bring-up — за DisplayPort::begin();
// DspCore::initDisplay остаётся продуктовой точкой входа без смены call-site.
bool DisplayPort::begin() {
    Serial.println("[ST7701] initDisplay start");

    if (!bus) {
        Serial.println("[ST7701] Initializing bus...");
        bus = new Arduino_SWSPI(
            GFX_NOT_DEFINED, ST7701_CS,
            ST7701_SCK, ST7701_SDA, GFX_NOT_DEFINED);
        if (!bus) {
            Serial.println("[ST7701] Failed to initialize bus!");
            return false;
        }
    }

    if (!rgbpanel) {
        Serial.println("[ST7701] Initializing RGB panel...");
        rgbpanel = new Arduino_ESP32RGBPanel(
            ST7701_DE, ST7701_VSYNC, ST7701_HSYNC, ST7701_PCLK,
            ST7701_R0, ST7701_R1, ST7701_R2, ST7701_R3, ST7701_R4,
            ST7701_G0, ST7701_G1, ST7701_G2, ST7701_G3, ST7701_G4, ST7701_G5,
            ST7701_B0, ST7701_B1, ST7701_B2, ST7701_B3, ST7701_B4,
            1, 10, 8, 50,
            1, 10, 8, 20,
            0,
            10000000UL,
            false,
            0, 0, 0);
        if (!rgbpanel) {
            Serial.println("[ST7701] Failed to initialize RGB panel!");
            return false;
        }
    }

    if (!output_display) {
        Serial.println("[ST7701] Initializing RGB display...");
        output_display = new Arduino_RGB_Display(
            480, 480, rgbpanel, 0, false,
            bus, GFX_NOT_DEFINED, st7701_type9_init_operations,
            sizeof(st7701_type9_init_operations));
        if (!output_display) {
            Serial.println("[ST7701] Failed to initialize RGB display!");
            return false;
        }
    }

    static bool s_rgb_output_begun = false;
    if (!output_display) {
        Serial.println("[ST7701] RGB display object missing!");
        return false;
    }
    if (!s_rgb_output_begun) {
        Serial.println("[ST7701] Initializing RGB display output...");
        if (!output_display->begin()) {
            Serial.println("[ST7701] Failed to begin RGB display!");
            return false;
        }
        s_rgb_output_begun = true;
        delay(100);
    }

    pinMode(ST7701_BL, OUTPUT);
    delay(50);
    analogWrite(ST7701_BL, 255);
    delay(100);
    Serial.println("[ST7701] Backlight enabled");

    Serial.println("[ST7701] initDisplay completed successfully");

    if (s_rgb_output_begun && output_display) {
        output_display->fillScreen(0);
        Serial.println("[ST7701] Panel cleared to BLACK (output_display)");
    }

    Serial.println("[ST7701] Display ready for normal operation");
    return true;
}

DisplayGeometry DisplayPort::geometry() {
    // Proven physical facts only; no new geometry architecture.
    // Только доказанные физические факты; без новой geometry-архитектуры.
    return DisplayGeometry{480u, 480u, PixelFormat::Rgb565};
}

void DisplayPort::flush(const DisplayArea& area, const uint16_t* pixels,
                        void (*done)(void* ctx), void* ctx) {
    // Backend-only: no LVGL types, no clip policy, no flush_ready.
    // Только backend: без типов LVGL, без clip-политики, без flush_ready.
    if (!output_display || !pixels) {
        if (done) {
            done(ctx);
        }
        return;
    }

    const int16_t w = static_cast<int16_t>(area.x2 - area.x1 + 1);
    const int16_t h = static_cast<int16_t>(area.y2 - area.y1 + 1);

    TAKE_MUTEX();
    // Arduino_GFX draw API is non-const; pixels are not mutated by the blit path.
    // API Arduino_GFX non-const; blit путь пиксели не меняет.
    output_display->draw16bitRGBBitmap(
        area.x1, area.y1, const_cast<uint16_t*>(pixels), w, h);
    output_display->flush();
    GIVE_MUTEX();

    if (done) {
        done(ctx);
    }
}

void DisplayPort::setBrightness(uint8_t percent) {
    TAKE_MUTEX();
    analogWrite(ST7701_BL, map(percent, 0, 100, 0, 255));
    GIVE_MUTEX();
}

void DisplayPort::sleep() {
    Serial.println("[ST7701] sleep");
    TAKE_MUTEX();
    // displayOff is currently log-only when panel object exists — preserve exactly.
    // displayOff сейчас только лог при наличии панели — сохраняем точно.
    if (output_display) {
        Serial.println("[ST7701] Display OFF");
    }
    analogWrite(ST7701_BL, 0);
    GIVE_MUTEX();
}

void DisplayPort::wake() {
    Serial.println("[ST7701] wake");
    TAKE_MUTEX();
    // displayOn is currently log-only when panel object exists — preserve exactly.
    // displayOn сейчас только лог при наличии панели — сохраняем точно.
    if (output_display) {
        Serial.println("[ST7701] Display ON");
    }
#if defined(ENABLE_BRIGHTNESS_CONTROL)
    analogWrite(ST7701_BL, map(config.store.brightness, 0, 100, 0, 255));
#else
    analogWrite(ST7701_BL, 255);
#endif
    GIVE_MUTEX();
}

bool DspCore::initDisplay() {
    return DisplayPort::begin();
}

void DspCore::displayOn() {
    if (output_display) {
        Serial.println("[ST7701] Display ON");
    }
}

void DspCore::displayOff() {
    if (output_display) {
        Serial.println("[ST7701] Display OFF");
    }
}

void DspCore::loop(bool force) {
#ifndef BATTERY_OFF
    static uint32_t lastBatteryUpdate = 0;
    if (millis() - lastBatteryUpdate >= 1000) {
        readBattery();
        lastBatteryUpdate = millis();
    }
#endif

    (void)force;
}

void DspCore::flip() {
    TAKE_MUTEX();
    if (output_display) {
        output_display->setRotation(config.store.flipscreen ? 2 : 0);
    }
    GIVE_MUTEX();
}

void DspCore::invert() {
    TAKE_MUTEX();
    if (bus) {
        bus->sendCommand(config.store.invertdisplay ? 0x21 : 0x20);
    }
    GIVE_MUTEX();
}

void DspCore::sleep(void) {
    DisplayPort::sleep();
}

void DspCore::wake(void) {
    DisplayPort::wake();
}

void DspCore::setBrightness(uint8_t brightness) {
    DisplayPort::setBrightness(brightness);
}

uint16_t DspCore::width() {
    return DisplayPort::geometry().width;
}

uint16_t DspCore::height() {
    return DisplayPort::geometry().height;
}

#ifndef BATTERY_OFF
void DspCore::readBattery() {
    static uint32_t lastRead = 0;
    if (millis() - lastRead < 100) return;
    lastRead = millis();

    float tempmVolt = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLES; i++) {
        tempmVolt += esp_adc_cal_raw_to_voltage(adc1_get_raw(USER_ADC_CHAN), &adc1_chars);
    }
    float mVolt = (tempmVolt / BATTERY_SAMPLES) / 1000;
    float rawVolt = (mVolt + 0.0028f * mVolt * mVolt + 0.0096f * mVolt - 0.051f) /
                        (ADC_R2 / (ADC_R1 + ADC_R2)) +
                    DELTA;
    if (rawVolt < 0) rawVolt = 0;

    Volt5 = Volt4;
    Volt4 = Volt3;
    Volt3 = Volt2;
    Volt2 = Volt1;
    Volt1 = rawVolt;
    float firstSmooth = (Volt1 + Volt2 + Volt3 + Volt4 + Volt5) / 5;

    smoothBuffer[smoothIndex] = firstSmooth;
    smoothIndex = (smoothIndex + 1) % BATTERY_SMOOTH_COUNT;

    float sum = 0;
    for (uint8_t i = 0; i < BATTERY_SMOOTH_COUNT; i++) {
        sum += smoothBuffer[i];
    }
    Volt = sum / BATTERY_SMOOTH_COUNT;

    if (millis() - chargingState.lastCheck >= BATTERY_CHECK_INTERVAL) {
        chargingState.lastCheck = millis();
        float voltDelta = Volt - chargingState.lastVoltage;

        if (abs(voltDelta) < BATTERY_STABLE_THRESHOLD) {
            chargingState.stableCount++;
            if (chargingState.stableCount >= 3) {
                chargingState.chargeCount = 0;
                chargingState.dischargeCount = 0;
                Charging = false;
            }
        } else {
            chargingState.stableCount = 0;
            if (voltDelta > BATTERY_VOLTAGE_THRESHOLD && Volt >= BATTERY_CHARGE_MIN_VOLTAGE) {
                chargingState.chargeCount++;
                chargingState.dischargeCount = 0;
                if (chargingState.chargeCount >= 2) {
                    Charging = true;
                }
            } else if (voltDelta < -BATTERY_VOLTAGE_THRESHOLD) {
                chargingState.dischargeCount++;
                if (chargingState.dischargeCount >= 2) {
                    chargingState.chargeCount = 0;
                    chargingState.dischargeCount = 0;
                    Charging = false;
                }
            } else {
                chargingState.chargeCount = 0;
                chargingState.dischargeCount = 0;
            }
        }
        chargingState.lastVoltage = Volt;
    }

    uint8_t idx = 0;
    while (true) {
        if (Volt < vs[idx]) {
            ChargeLevel = 0;
            break;
        }
        if (Volt < vs[idx + 1]) {
            mVolt = Volt - vs[idx];
            ChargeLevel = idx * 5 + round(mVolt / ((vs[idx + 1] - vs[idx]) / 5));
            break;
        } else {
            idx++;
        }
    }
    if (ChargeLevel < 0) ChargeLevel = 0;
    if (ChargeLevel > 100) ChargeLevel = 100;
}
#endif

#endif
