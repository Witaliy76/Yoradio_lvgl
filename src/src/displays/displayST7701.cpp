/************************************************************************************************
   Display driver for ST7701 (480x480) RGB Panel ESP32-4848S040
   Block 8-E17/E18B: LVGL product — direct DisplayPort; no Canvas; no legacy draw APIs.
************************************************************************************************/

// Adaptation: Witaliy76 - https://github.com/Witaliy76
#include "../core/options.h"
#if DSP_MODEL == DSP_ST7701

#include "displayST7701.h"
#include "display_port.h"
#include "esp_lcd_st7701_backend.h"
#include <cstring>
#include "../core/spidog.h"
#include "../core/config.h"
#include "../core/network.h"
#include "../core/display.h"

#if defined(ARDUINO_ARCH_ESP32)
#include "esp32-hal-ledc.h"
#endif

#include "driver/adc.h"
#include "esp_adc_cal.h"

#define TAKE_MUTEX() sdog.takeMutex()
#define GIVE_MUTEX() sdog.giveMutex()

// Physical panel state for the log-only lifecycle paths below.
// Состояние физической панели для log-only путей ниже.
static inline bool physicalBackendReady() {
    return yoradio_esp_lcd_st7701::isReady();
}

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
    // Direct esp_lcd is the sole RGB peripheral + framebuffer owner.
    // Direct esp_lcd — единственный владелец RGB-периферии и FB.
    Serial.println("[ST7701] backend: direct esp_lcd");
    // Direct init failure must surface through the existing DisplayPort/init path;
    // never continue with a null/uninitialized framebuffer.
    // Сбой прямой инициализации возвращается через существующий путь init;
    // работа с null/неинициализированным FB запрещена.
    if (!yoradio_esp_lcd_st7701::begin()) {
        Serial.println("[ST7701] Direct esp_lcd backend begin FAILED");
        return false;
    }
    if (!yoradio_esp_lcd_st7701::isReady() || !yoradio_esp_lcd_st7701::framebuffer()) {
        Serial.println("[ST7701] Direct esp_lcd backend not ready / no framebuffer!");
        return false;
    }

    // Apply persisted inversion after panel init and before backlight. Direct
    // orientation is a fixed board option applied before the RGB stream starts.
    // Инверсия применяется до подсветки; orientation direct-backend фиксируется
    // board-опцией до запуска RGB stream.
    if (!yoradio_esp_lcd_st7701::setInverted(config.store.invertdisplay)) {
        Serial.println("[ST7701] Failed to apply saved display inversion");
        return false;
    }

    pinMode(ST7701_BL, OUTPUT);
    delay(50);
    analogWrite(ST7701_BL, 255);
    delay(100);
    Serial.println("[ST7701] Backlight enabled");

    Serial.println("[ST7701] initDisplay completed successfully");

    // Direct backend already cleared the FB to black inside begin() (before the
    // backlight came up); keep the accepted log line for boot-evidence parity.
    // Прямой backend уже очистил FB в begin() (до включения подсветки).
    Serial.println("[ST7701] Panel cleared to BLACK (esp_lcd framebuffer)");

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
    if (!physicalBackendReady() || !pixels) {
        if (done) {
            done(ctx);
        }
        return;
    }

    // Rectangle geometry is the accepted contract: inclusive area, source treated
    // as packed with stride = w — identical to what draw16bitRGBBitmap consumed.
    // Геометрия по принятому контракту: inclusive-область, stride источника = w.
    const int16_t w = static_cast<int16_t>(area.x2 - area.x1 + 1);
    const int16_t h = static_cast<int16_t>(area.y2 - area.y1 + 1);

    TAKE_MUTEX();
    // CPU blit of the invalidated rectangle only into the esp_lcd-owned FB, then
    // the accepted conservative full-FB C2M writeback.
    // CPU-блит только изменённого прямоугольника в FB esp_lcd, затем принятый
    // консервативный полный C2M writeback.
    yoradio_esp_lcd_st7701::blitRgb565(area.x1, area.y1, pixels, w, h);
    yoradio_esp_lcd_st7701::cacheWritebackFull();
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
    // displayOff is currently log-only when the panel is up — preserve exactly.
    // displayOff сейчас только лог при поднятой панели — сохраняем точно.
    if (physicalBackendReady()) {
        Serial.println("[ST7701] Display OFF");
    }
    analogWrite(ST7701_BL, 0);
    GIVE_MUTEX();
}

void DisplayPort::wake() {
    Serial.println("[ST7701] wake");
    TAKE_MUTEX();
    // displayOn is currently log-only when the panel is up — preserve exactly.
    // displayOn сейчас только лог при поднятой панели — сохраняем точно.
    if (physicalBackendReady()) {
        Serial.println("[ST7701] Display ON");
    }
#if defined(ENABLE_BRIGHTNESS_CONTROL)
    analogWrite(ST7701_BL, map(config.store.brightness, 0, 100, 0, 255));
#else
    analogWrite(ST7701_BL, 255);
#endif
    GIVE_MUTEX();
}

void DisplayPort::restartRgbScanout() {
    // Backend owns the panel handle; upper layers only request a scanout resync.
    // Backend владеет panel handle; верхние слои только запрашивают ресинхрон.
    if (!yoradio_esp_lcd_st7701::isReady()) {
        return;  // panel not up yet / панель ещё не поднята
    }
    TAKE_MUTEX();
    (void)yoradio_esp_lcd_st7701::restartRgbScanout();
    GIVE_MUTEX();
}

bool DspCore::initDisplay() {
    return DisplayPort::begin();
}

void DspCore::displayOn() {
    if (physicalBackendReady()) {
        Serial.println("[ST7701] Display ON");
    }
}

void DspCore::displayOff() {
    if (physicalBackendReady()) {
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

// Inversion is live on the direct backend. This panel
// accepts its 180-degree scan commands only before the RGB stream starts, so
// direct orientation is the fixed ST7701_BOOT_ORIENTATION_180 board option.
// Touch orientation remains an independent runtime option.
void DspCore::flip() {
    TAKE_MUTEX();
    (void)config.store.flipscreen;
    Serial.println("[ST7701] Runtime flip unavailable; use ST7701_BOOT_ORIENTATION_180");
    GIVE_MUTEX();
}

void DspCore::invert() {
    TAKE_MUTEX();
    if (!yoradio_esp_lcd_st7701::setInverted(config.store.invertdisplay)) {
        Serial.println("[ST7701] Runtime inversion command FAILED");
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
