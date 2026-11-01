// Direct esp_lcd ST7701 RGB backend implementation behind DisplayPort.
// Реализация прямого esp_lcd ST7701 RGB backend за интерфейсом DisplayPort.
// Parity authority: Arduino_GFX @ 00dcd684 (type9 + ESP32RGBPanel config).
// Эталон паритета: Arduino_GFX @ 00dcd684 (type9 + конфиг ESP32RGBPanel).
// Author: Witaliy76 - https://github.com/Witaliy76

#include "../core/options.h"
#if DSP_MODEL == DSP_ST7701

#include "esp_lcd_st7701_backend.h"
#include "st7701_type9_ops.h"
#include "../platform/display/resolved_display_config.h"

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

// The backend adapts one accepted contract to esp_lcd. PanelMode owns physical
// 480x480 RGB565 timing; BoardProfile/Custom Wiring owns real GPIO; DisplayDefaults
// is the tested host recommendation, not a universal ban on other supported choices.
// Backend адаптирует один принятый контракт к esp_lcd. PanelMode владеет физическими
// timing 480x480 RGB565; BoardProfile/Custom Wiring — реальными GPIO; DisplayDefaults
// хранит проверенную рекомендацию хоста, а не универсальный запрет иных настроек.
constexpr const auto& kDisplayConfig = yoradio::kResolvedDisplayConfig;
constexpr size_t kResolvedFramebufferBytes =
    static_cast<size_t>(kDisplayConfig.panel.native_width) *
    kDisplayConfig.panel.native_height * sizeof(uint16_t);

// Keep the public header constants until S04-3B2 checks its remaining consumers.
// They are compatibility names, not a second backend source.
// Публичные константы сохраняются до S04-3B2 как compatibility-имена, а не
// как второй источник параметров backend.
static_assert(kWidth == kDisplayConfig.panel.native_width,
              "ST7701 compatibility kWidth must match ResolvedDisplayConfig.panel");
static_assert(kHeight == kDisplayConfig.panel.native_height,
              "ST7701 compatibility kHeight must match ResolvedDisplayConfig.panel");
static_assert(kFramebufferBytes == kResolvedFramebufferBytes,
              "ST7701 compatibility framebuffer size must match resolved geometry");
static_assert(kPclkNominalHz == kDisplayConfig.recommended.runtime_pclk_hz,
              "ST7701 compatibility nominal PCLK must match resolved runtime PCLK");
static_assert(kPclkFlashGuardHz == kDisplayConfig.recommended.flash_guard_pclk_hz,
              "ST7701 compatibility guard PCLK must match resolved flash-guard PCLK");
static_assert(kDisplayConfig.recommended.startup_pclk_hz ==
                  kDisplayConfig.recommended.flash_guard_pclk_hz,
              "ST7701 accepted startup and flash-guard PCLK must remain equal");
static_assert(kDisplayConfig.effective_bounce_buffer_px <= UINT16_MAX,
              "ST7701 effective bounce must fit the public uint16_t status field");
static_assert(kDisplayConfig.wiring.panel_reset_gpio == yoradio::kPinNotConnected,
              "ST7701 backend expects the 4848S040 panel RESX line to be not connected");

esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_fb = nullptr;
bool s_ready = false;
// Type9 does not write MADCTL; ST7701S reset default is 0x00. Track the full
// value so changing ML never clobbers BGR or another unrelated MADCTL bit.
// Type9 не пишет MADCTL; reset default ST7701S = 0x00. Храним весь байт,
// чтобы изменение ML не затронуло BGR и прочие биты.
uint8_t s_madctl = 0x00;

// Effective bounce size passed to esp_lcd_new_rgb_panel(). 0 = direct T0.
// Эффективный bounce, переданный в esp_lcd_new_rgb_panel(). 0 = direct T0.
// Startup RGB PCLK. The scanout is brought up at the lower of the two product PCLK values and
// stays there until the flash guard accepts a Boot->Main promotion. The UI handoff does not
// call set_pclk itself. Reason: the scanout begins inside esp_lcd_panel_init() (rgb_panel_init
// programs the divider from panel_config.timings.pclk_hz and calls
// lcd_rgb_panel_start_transmission), so the first frames of a cold power-on are clocked before
// any task exists and the DspTask flash guard cannot give them timing margin afterwards.
// Starting with that margin is what keeps a cold start aligned; no RGB peripheral restart is
// used anywhere in startup, and a startup restart was measured to blank the panel at this PCLK.
//
// This variable is what rgbScanoutStatus().nominalPclkHz reports, which is deliberate: the
// flash guard asks for "nominal" when it raises the clock, so while the startup phase is in
// progress its UP target is the startup PCLK too and a guarded boot write cannot promote the
// clock early. Two guarded episodes really do occur during boot on this product.
// Both values come from the resolved tested host policy; there is no third frequency.
//
// Стартовый PCLK RGB. Scanout поднимается на меньшем из двух продуктовых значений PCLK и
// остаётся на нём, пока flash guard не примет повышение Boot->Main. UI handoff сам set_pclk не вызывает.
// Причина: scanout начинается внутри esp_lcd_panel_init(), т.е. первые кадры холодного старта
// тактуются до появления задач, и guard на DspTask задним числом margin им не даёт. Именно
// старт с этим запасом удерживает выравнивание; restart RGB-периферии в старте не используется
// нигде, а стартовый restart измеренно гасил панель на этом PCLK.
//
// Эту переменную отдаёт rgbScanoutStatus().nominalPclkHz, и это намеренно: guard при повышении
// просит «номинал», поэтому в фазе старта его цель UP — тоже стартовый PCLK, и защищённая
// загрузочная запись не поднимет частоту раньше времени. Оба значения — существующие
// значения из resolved-политики хоста; третьей частоты нет.
uint32_t s_pclk_nominal_hz = kDisplayConfig.recommended.startup_pclk_hz;
bool s_pclk_promoted = false;

uint16_t s_bounce_px = 0;
uint32_t s_restart_exec = 0;
uint32_t s_restart_rejected_bounce = 0;

// VSYNC counter. Written only by s_on_vsync (RGB ISR), read by tasks with acquire ordering.
// The RGB ISR is not IRAM-safe in this build (CONFIG_LCD_RGB_ISR_IRAM_SAFE unset), so the
// counter stops while flash operations disable the cache. A PCLK barrier therefore must be
// complete before a guarded flash write starts, and resumes counting after it returns.
// Счётчик VSYNC. Пишет только s_on_vsync (RGB ISR), задачи читают с acquire-порядком.
// RGB ISR здесь не IRAM-safe, поэтому во время flash-операций счётчик стоит. Барьер PCLK
// должен завершиться до начала защищённой записи и продолжает счёт после её возврата.
volatile uint32_t s_vsync_seq = 0;
bool s_vsync_cb_ok = false;

// Runs in the RGB VSYNC ISR before lcd_rgb_panel_try_update_pclk() and
// lcd_rgb_panel_try_restart_transmission() (stock esp_lcd_panel_rgb.c, IDF 5.5.5).
// One atomic increment only: no logging, no allocation, no task logic.
// Выполняется в RGB VSYNC ISR до try_update_pclk() и try_restart_transmission().
// Только один атомарный инкремент: без логов, аллокаций и логики задач.
bool IRAM_ATTR s_on_vsync(esp_lcd_panel_handle_t panel,
                          const esp_lcd_rgb_panel_event_data_t* edata,
                          void* user_ctx) {
    (void)panel;
    (void)edata;
    (void)user_ctx;
    __atomic_fetch_add(&s_vsync_seq, 1u, __ATOMIC_RELEASE);
    return false;
}

constexpr uint8_t kCmdCommand2BankSelect = 0xFF;
constexpr uint8_t kCmdSourceDirection = 0xC7;
constexpr uint8_t kCmdMadctl = 0x36;
constexpr uint8_t kSourceDirectionReverse = 0x04;  // SDIR.SS, bit 2
constexpr uint8_t kMadctlMl = 0x10;                // MADCTL.ML, bit 4
constexpr uint8_t kCommand2Bank0[] = {0x77, 0x01, 0x00, 0x00, 0x10};
constexpr uint8_t kCommand2Disable[] = {0x77, 0x01, 0x00, 0x00, 0x00};

// --- 9-bit software SPI (Arduino_SWSPI semantics, DC undefined) ---
// --- Программный 9-bit SPI (семантика Arduino_SWSPI, DC нет) ---

inline void spiMosiHigh() { digitalWrite(kDisplayConfig.wiring.cmd_sda_gpio, HIGH); }
inline void spiMosiLow() { digitalWrite(kDisplayConfig.wiring.cmd_sda_gpio, LOW); }
inline void spiSckHigh() { digitalWrite(kDisplayConfig.wiring.cmd_sck_gpio, HIGH); }
inline void spiSckLow() { digitalWrite(kDisplayConfig.wiring.cmd_sck_gpio, LOW); }
inline void csLow() { digitalWrite(kDisplayConfig.wiring.cmd_cs_gpio, LOW); }
inline void csHigh() { digitalWrite(kDisplayConfig.wiring.cmd_cs_gpio, HIGH); }

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
    // GPIO live in BoardProfile because they describe board routing, not ST7701
    // physics. Change supported pin overrides under Custom Wiring in src/myoptions.h.
    // GPIO находятся в BoardProfile как разводка платы, а не физика ST7701.
    // Поддерживаемые overrides меняются в Custom Wiring файла src/myoptions.h.
    pinMode(kDisplayConfig.wiring.cmd_cs_gpio, OUTPUT);
    pinMode(kDisplayConfig.wiring.cmd_sck_gpio, OUTPUT);
    pinMode(kDisplayConfig.wiring.cmd_sda_gpio, OUTPUT);
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
    // The backend reads resolved metadata, while the accepted Type9 byte stream and
    // its interpreter remain local here. These checks bind identity, not ownership.
    // Backend читает resolved metadata, а Type9-байты и интерпретатор
    // остаются локальными; проверки связывают идентичность, а не владение.
    static_assert(yoradio::kResolvedDisplayConfig.panel.init.encoded_bytes ==
                      kType9GfxEncodedBytes,
                  "ResolvedDisplayConfig: PanelMode Type9 byte count does not match st7701_type9_ops.h; check both metadata owners.");
    static_assert(yoradio::kResolvedDisplayConfig.panel.init.high_level_ops ==
                      kType9GfxHighLevelOps,
                  "ResolvedDisplayConfig: PanelMode Type9 operation count does not match st7701_type9_ops.h; check both metadata owners.");

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
    write9BitCommand(kDisplayConfig.panel.init.soft_reset_command);
    csHigh();
    delay(kDisplayConfig.panel.init.soft_reset_delay_ms);
    return true;
}

bool createRgbPanel() {
    // Panel geometry/timing belong to PanelMode: the same ST7701 glass may be routed
    // by another board without changing its 480x480 recipe. esp_lcd-only flags remain
    // adapter details here. Геометрия/timing принадлежат PanelMode; разводка — плате.
    const auto& panel = kDisplayConfig.panel;
    const auto& wiring = kDisplayConfig.wiring;
    const auto& policy = kDisplayConfig.recommended;

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings.pclk_hz = s_pclk_nominal_hz;  // startup PCLK for the first scanout
    panel_config.timings.h_res = panel.native_width;
    panel_config.timings.v_res = panel.native_height;
    panel_config.timings.hsync_pulse_width = panel.timing.hsync_pulse_width;
    panel_config.timings.hsync_back_porch = panel.timing.hsync_back_porch;
    panel_config.timings.hsync_front_porch = panel.timing.hsync_front_porch;
    panel_config.timings.vsync_pulse_width = panel.timing.vsync_pulse_width;
    panel_config.timings.vsync_back_porch = panel.timing.vsync_back_porch;
    panel_config.timings.vsync_front_porch = panel.timing.vsync_front_porch;
    panel_config.timings.flags.hsync_idle_low =
        yoradio::panel::espLcdIdleLowFromSourcePolarity(panel.timing.hsync.source_polarity);
    panel_config.timings.flags.vsync_idle_low =
        yoradio::panel::espLcdIdleLowFromSourcePolarity(panel.timing.vsync.source_polarity);
    panel_config.timings.flags.de_idle_high = panel.timing.de_idle_high;
    panel_config.timings.flags.pclk_active_neg = panel.timing.pclk_active_neg;
    panel_config.timings.flags.pclk_idle_high = panel.timing.pclk_idle_high;

    panel_config.data_width = panel.rgb_bus_width_bits;
    panel_config.bits_per_pixel = panel.rgb_bus_width_bits;
    panel_config.num_fbs = policy.rgb_framebuffer_count;
    // Default is the TEST1-proven direct path (0). B10 remains a supported user
    // choice and resolves to native_width * 10 = 4800 px; TEST1 did not validate B10.
    // Default — проверенный TEST1 direct path (0). B10 разрешён и даёт 4800 px,
    // но аппаратная проверка TEST1 не является проверкой B10.
    panel_config.bounce_buffer_size_px = kDisplayConfig.effective_bounce_buffer_px;
    s_bounce_px = static_cast<uint16_t>(panel_config.bounce_buffer_size_px);
    // GFX sets deprecated sram_trans_align=8 / psram_trans_align=64 (union → dma_burst_size).
    panel_config.sram_trans_align = 8;
    panel_config.psram_trans_align = 64;

    panel_config.hsync_gpio_num = wiring.hsync_gpio;
    panel_config.vsync_gpio_num = wiring.vsync_gpio;
    panel_config.de_gpio_num = wiring.de_gpio;
    panel_config.pclk_gpio_num = wiring.pclk_gpio;
    // This board has no software-controlled display-enable/RESX line. Its physical
    // panel_reset_gpio is kPinNotConnected; inventing a GPIO or restarting RGB DMA
    // would not create a missing wire. У платы нет управляемой линии RESX/enable.
    panel_config.disp_gpio_num = GPIO_NUM_NC;

    // Copy effective Custom Wiring directly, preserving the accepted esp_lcd slot
    // order. No second GPIO array. Копируем effective-разводку без второй таблицы.
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B0)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B0)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B1)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B1)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B2)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B2)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B3)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B3)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B4)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::B4)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G0)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G0)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G1)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G1)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G2)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G2)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G3)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G3)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G4)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G4)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G5)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::G5)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R0)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R0)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R1)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R1)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R2)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R2)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R3)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R3)];
    panel_config.data_gpio_nums[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R4)] =
        wiring.data_gpio[yoradio::rgb565DataSlotIndex(yoradio::Rgb565DataSlot::R4)];

    panel_config.flags.disp_active_low = true;
    panel_config.flags.refresh_on_demand = false;
    panel_config.flags.fb_in_psram =
        policy.rgb_framebuffer_memory == yoradio::DisplayBufferMemory::Psram;
    panel_config.flags.double_fb = policy.double_framebuffer;
    panel_config.flags.no_fb =
        policy.rgb_framebuffer_owner != yoradio::RgbFramebufferOwner::PanelDriver;
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
    err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, policy.rgb_framebuffer_count,
                                             &frame_buffer);
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
                  static_cast<void*>(s_fb),
                  static_cast<unsigned>(kResolvedFramebufferBytes));

    // VSYNC counter for the PCLK barrier (stock esp_lcd API, no IDF patch). On a ready
    // direct-RGB panel, a failed registration makes guarded writes Denied; it no longer
    // fails open. The panel can scan, but persistence waits for safe protection.
    // Счётчик VSYNC для барьера PCLK. На готовой direct-RGB панели сбой регистрации
    // даёт Denied для защищённых записей: scanout возможен, persistence ждёт защиты.
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
    callbacks.on_vsync = s_on_vsync;
    err = esp_lcd_rgb_panel_register_event_callbacks(s_panel, &callbacks, nullptr);
    s_vsync_cb_ok = (err == ESP_OK);
    if (!s_vsync_cb_ok) {
        Serial.printf("[esp_lcd_st7701] vsync callback register FAIL: %s\n", esp_err_to_name(err));
    }
    Serial.printf("[esp_lcd_st7701] pclk=%lu bounce_px=%u vsync_cb=%s\n",
                  static_cast<unsigned long>(panel_config.timings.pclk_hz),
                  static_cast<unsigned>(s_bounce_px), s_vsync_cb_ok ? "ok" : "FAIL");
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

    // Some 4848S040 panels accept orientation commands only before the RGB pixel
    // stream starts. The resolver carries the existing user choice; touch is separate.
    // Некоторые панели принимают orientation только до RGB stream. Resolver передаёт
    // существующий выбор пользователя; настройка тача остаётся отдельной.
    if (kDisplayConfig.boot_orientation_180) {
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
    return kResolvedFramebufferBytes;
}

bool cacheWritebackFull() {
    if (!s_fb) {
        return false;
    }
    // Conservative full-FB C2M — parity with Arduino_RGB_Display::flush(auto_flush=false).
    // Консервативный полный C2M FB — паритет с Arduino_RGB_Display::flush(auto_flush=false).
    const esp_err_t err = esp_cache_msync(
        s_fb, kResolvedFramebufferBytes,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
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
    const size_t pixels = static_cast<size_t>(kDisplayConfig.panel.native_width) *
                          kDisplayConfig.panel.native_height;
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
    constexpr int16_t kMaxX = static_cast<int16_t>(kDisplayConfig.panel.native_width) - 1;
    constexpr int16_t kMaxY = static_cast<int16_t>(kDisplayConfig.panel.native_height) - 1;
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
    uint16_t* row = s_fb +
                    (static_cast<size_t>(y) * kDisplayConfig.panel.native_width) + x;
    const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
    const int16_t src_stride = static_cast<int16_t>(w + x_skip);
    for (int16_t j = 0; j < h; ++j) {
        memcpy(row, src, row_bytes);
        src += src_stride;
        row += kDisplayConfig.panel.native_width;
    }
    return true;
}

void setBrightnessPercent(uint8_t percent) {
    pinMode(kDisplayConfig.wiring.backlight_gpio, OUTPUT);
    analogWrite(kDisplayConfig.wiring.backlight_gpio, map(percent, 0, 100, 0, 255));
}

void end() {
    s_ready = false;
    s_fb = nullptr;
    s_madctl = 0x00;
    s_vsync_cb_ok = false;
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
    }
}

bool restartRgbScanout() {
    // MANUAL / SERVICE ONLY (S01-5D): the diagnostic CLI is the sole caller. Official esp_lcd
    // recovery for a DMA/LCD desync caused by insufficient bandwidth, kept as a human tool only.
    // ТОЛЬКО РУЧНОЙ / СЕРВИСНЫЙ ВЫЗОВ (S01-5D): единственный вызывающий — диагностический CLI.
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
    // Accepted invariant: with bounce buffers the driver owns scanout recovery; a direct
    // manual/event restart is rejected here, the single place that can call esp_lcd.
    // Принятый инвариант: при bounce восстановлением владеет драйвер; прямой ручной или
    // событийный restart отклоняется здесь — в единственном месте, где вызывается esp_lcd.
    if (s_bounce_px > 0) {
        ++s_restart_rejected_bounce;
        return false;
    }
    ++s_restart_exec;
    const esp_err_t err = esp_lcd_rgb_panel_restart(s_panel);
    if (err != ESP_OK) {
        Serial.printf("[esp_lcd_st7701] rgb_panel_restart failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

uint32_t restartExecCount() {
    return s_restart_exec;
}

uint32_t restartRejectedBounceCount() {
    return s_restart_rejected_bounce;
}

uint16_t bounceBufferPx() {
    return s_bounce_px;
}

uint32_t nominalPclkHz() {
    // Startup phase reports the flash-guard frequency; after the one-shot promotion it
    // reports the runtime nominal. The flash guard reads this as its UP target.
    // В фазе старта отдаёт частоту flash guard; после одноразового повышения — runtime.
    return s_pclk_nominal_hz;
}

bool promotePclkToRuntimeNominal() {
    // Driver acceptance only. The divider is programmed later in the VSYNC ISR.
    // A rejected set_pclk does not latch the one-shot. No scanout restart.
    // Только принятие драйвером. Делитель программируется позже в VSYNC ISR.
    // Отклонённый set_pclk не защёлкивает one-shot. Без restart scanout.
    if (s_pclk_promoted) {
        return true;
    }
    if (!s_ready || !s_panel) {
        return false;
    }
    const esp_err_t err = esp_lcd_rgb_panel_set_pclk(
        s_panel, kDisplayConfig.recommended.runtime_pclk_hz);
    if (err != ESP_OK) {
        // Backoff lives in the guard. This log is the 1st rejection and every 10th.
        // Пауза между попытками — в guard. Этот лог — первый отказ и каждый десятый.
        static uint32_t s_promote_reject = 0;
        ++s_promote_reject;
        if (s_promote_reject == 1u || (s_promote_reject % 10u) == 0u) {
            Serial.printf("[esp_lcd_st7701] pclk promote not_accepted err=%s count=%lu\n",
                          esp_err_to_name(err), (unsigned long)s_promote_reject);
        }
        return false;
    }
    s_pclk_nominal_hz = kDisplayConfig.recommended.runtime_pclk_hz;
    s_pclk_promoted = true;
    Serial.printf("[esp_lcd_st7701] pclk promote driver_accepted startup=%lu runtime=%lu "
                  "divider=vsync_later hardware_hz=not_confirmed vsyncSeq=%lu\n",
                  (unsigned long)kDisplayConfig.recommended.startup_pclk_hz,
                  (unsigned long)kDisplayConfig.recommended.runtime_pclk_hz,
                  (unsigned long)vsyncSeq());
    return true;
}

uint32_t vsyncSeq() {
    return __atomic_load_n(&s_vsync_seq, __ATOMIC_ACQUIRE);
}

bool vsyncCounterReady() {
    return s_ready && s_vsync_cb_ok;
}

bool requestRgbPclkHz(uint32_t hz) {
    // esp_lcd_rgb_panel_set_pclk() stores the frequency and raises need_update_pclk under the
    // driver spinlock. The divider is programmed later in the VSYNC ISR, after s_on_vsync().
    // It never sets need_restart.
    // set_pclk() запоминает частоту и поднимает need_update_pclk под spinlock драйвера.
    // Делитель программируется позже в VSYNC ISR, после s_on_vsync(). need_restart не ставится.
    if (!s_ready || !s_panel) {
        return false;
    }
    const esp_err_t err = esp_lcd_rgb_panel_set_pclk(s_panel, hz);
    if (err != ESP_OK) {
        Serial.printf("[esp_lcd_st7701] set_pclk failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace yoradio_esp_lcd_st7701

#endif  // DSP_ST7701
