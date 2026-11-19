// Concrete IDF SDSPI/FAT/VFS ownership with explicit, checked teardown stages.
// The convenience sdspi_mount/unmount helpers hide destructive partial cleanup;
// keep the small per-stage ownership record here instead. No format/index IO.
#include "storage_sdspi.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "sd_ssr_policy.h"
// Plain include so the compiler dependency lists this header. __has_include
// left B034 linked against the previous policy.
// Обычный include, чтобы зависимость компилятора содержала этот заголовок.
// __has_include оставил B034 со старой политикой.
#include "../../../myoptions.h"
#ifndef YORADIO_SD_SSR_TIMEOUT_FALLBACK
#define YORADIO_SD_SSR_TIMEOUT_FALLBACK 0
#endif
#if !defined(YORADIO_DIRECTORY_HOST)
#include "storage_engineering_log.h"
#endif

// Set only around our sdmmc_card_init. The linker wrap is process-wide;
// the fallback is not.
// Флаг только вокруг нашего sdmmc_card_init. Подмена линкера на весь процесс;
// fallback — нет.
static bool g_our_sdspi_mount = false;
static bool g_ssr_step_timed_out = false;

namespace yoradio { namespace storage { namespace sdspi {
namespace {
constexpr spi_host_device_t kHost = SPI2_HOST;
struct Session {
    bool pins = false, bus = false, device = false;
    bool disk = false, vfs = false, fat = false, mounted = false;
    int32_t poisoned = 0; // SDK destructive cleanup failure: do not retry freed objects
    sdspi_dev_handle_t handle = 0;
    sdmmc_card_t card{};
    FATFS* filesystem = nullptr;
    BYTE drive = FF_DRV_NOT_USED;
    char drive_name[3] = {};
} session;

bool held() {
    return session.pins || session.bus || session.device || session.disk ||
           session.vfs || session.fat || session.poisoned;
}
Result facts(int32_t error = 0) {
    Result r;
    r.error = error;
    r.resources_held = held();
    r.mounted = session.mounted;
    return r;
}
bool validConfig() {
    const auto& c = bootConfig();
    if (!c.enabled || !c.path || c.path[0] != '/') return false;
    const int pins[] = {c.cs, c.mosi, c.miso, c.sck, c.lcd_cs};
    for (unsigned i = 0; i < 5; ++i) {
        if (pins[i] < 0 || pins[i] > 48 || (pins[i] >= 22 && pins[i] <= 37)) return false;
        for (unsigned j = 0; j < i; ++j) if (pins[i] == pins[j]) return false;
    }
    return true;
}
esp_err_t lcdDeselected() {
    const auto pin = static_cast<gpio_num_t>(bootConfig().lcd_cs);
    // Set output latch before enabling output; never pulse LCD_CS low.
    auto err = gpio_set_level(pin, 1);
    if (err != ESP_OK) return err;
    return gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}
esp_err_t resetSdPins() {
    const auto& c = bootConfig();
    const int pins[] = {c.cs, c.mosi, c.miso, c.sck};
    for (int pin : pins) {
        const auto err = gpio_reset_pin(static_cast<gpio_num_t>(pin));
        if (err != ESP_OK) return err;
    }
    // RN1 holds TF CS high when released; no RGB scanout pins are reset.
    return lcdDeselected();
}
Result rollback(esp_err_t error, bool no_media = false) {
    auto r = cleanup();
    r.cleanup_error = r.error;
    r.error = error;
    r.no_media = no_media;
    return r;
}
}

Result cleanup() {
    if (session.poisoned) return facts(session.poisoned);
    if (session.fat) {
        const auto fr = f_mount(nullptr, session.drive_name, 0);
        if (fr != FR_OK) return facts(static_cast<int32_t>(fr));
        session.fat = false;
        session.mounted = false;
    }
    if (session.vfs) {
        const auto err = esp_vfs_fat_unregister_path(bootConfig().path);
        // Installed IDF frees its FATFS context even when unregister returns an
        // error. Never reuse that pointer or call unregister again on failure.
        session.vfs = false;
        session.filesystem = nullptr;
        if (err != ESP_OK) { session.poisoned = err; return facts(err); }
    }
    if (session.disk) {
        ff_diskio_unregister(session.drive); // IDF void API
        session.disk = false;
        session.drive = FF_DRV_NOT_USED;
    }
    if (session.device) {
        const auto err = sdspi_host_remove_device(session.handle);
        if (err != ESP_OK) return facts(err);
        session.device = false;
    }
    if (session.bus) {
        const auto err = spi_bus_free(kHost);
        if (err != ESP_OK) return facts(err);
        session.bus = false;
    }
    if (session.pins) {
        const auto err = resetSdPins();
        if (err != ESP_OK) return facts(err);
        session.pins = false;
    }
    return facts();
}

int32_t space(uint64_t& free_bytes, uint64_t& total_bytes) {
    if (!session.mounted) return ESP_ERR_INVALID_STATE;
    uint64_t total = 0, free = 0;
    const auto err = esp_vfs_fat_info(bootConfig().path, &total, &free);
    if (err == ESP_OK) { free_bytes = free; total_bytes = total; }
    return err;
}

int compiledSsrTimeoutFallback() {
    return YORADIO_SD_SSR_TIMEOUT_FALLBACK ? 1 : 0;
}

Result mount(uint32_t frequency_khz) {
    // Early entry uses 400 kHz. Runtime requests 20 MHz or retries at 10 MHz.
    // Ранний вход использует 400 кГц. Runtime запрашивает 20 МГц или повторяет на 10 МГц.
    if (!validConfig() || (frequency_khz != 400 && frequency_khz != 1000 &&
                          frequency_khz != 10000 && frequency_khz != 20000))
        return facts(ESP_ERR_NOT_SUPPORTED); // disabled path performs no SDK IO
    if (held()) return facts(ESP_ERR_INVALID_STATE);
    const auto& c = bootConfig();
    session.pins = true;
    auto err = lcdDeselected();
    if (err != ESP_OK) return rollback(err);
    err = resetSdPins();
    if (err != ESP_OK) return rollback(err);
    spi_bus_config_t bus{};
    bus.mosi_io_num = c.mosi;
    bus.miso_io_num = c.miso;
    bus.sclk_io_num = c.sck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4000;
    err = spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return rollback(err); // never free someone else's bus
    session.bus = true;
    err = sdspi_host_init();
    if (err != ESP_OK) return rollback(err);
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = kHost;
    device.gpio_cs = static_cast<gpio_num_t>(c.cs);
    err = sdspi_host_init_device(&device, &session.handle);
    if (err != ESP_OK) return rollback(err);
    session.device = true;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = session.handle;
    host.max_freq_khz = static_cast<int>(frequency_khz);
    g_ssr_step_timed_out = false;
    g_our_sdspi_mount = true;
    err = sdmmc_card_init(&host, &session.card);
    g_our_sdspi_mount = false;
    const bool ssr_timed_out = g_ssr_step_timed_out;
    g_ssr_step_timed_out = false;
    if (err != ESP_OK) {
        // A timeout of the status register means that step did not answer.
        // It is not evidence that the card is absent or damaged.
        // Таймаут регистра состояния значит, что этот шаг не ответил.
        // Это не доказательство отсутствия или повреждения карты.
        if (ssr_timed_out) {
#if !defined(YORADIO_DIRECTORY_HOST)
            S07_STORAGE_LOG("[S07-ENG] S07 SSR timeout kept err=0x%lx; not evidence the card is absent or damaged\n",
                static_cast<unsigned long>(err));
#endif
            return rollback(err, false);
        }
        return rollback(err, err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND);
    }
    err = ff_diskio_get_drive(&session.drive);
    if (err != ESP_OK) return rollback(err);
    ff_diskio_register_sdmmc(session.drive, &session.card); // IDF void API
    session.disk = true;
    session.drive_name[0] = static_cast<char>('0' + session.drive);
    session.drive_name[1] = ':';
    session.drive_name[2] = '\0';
    // Boot-only media: match IDF's default; real I/O errors remain reported.
    ff_sdmmc_set_disk_status_check(session.drive, false);
    esp_vfs_fat_conf_t vfs{};
    vfs.base_path = c.path;
    vfs.fat_drive = session.drive_name;
    vfs.max_files = 4;
    err = esp_vfs_fat_register_cfg(&vfs, &session.filesystem);
    // An existing path is NOT our registration. Never unregister another owner.
    if (err != ESP_OK) return rollback(err);
    session.vfs = true;
    session.fat = true;             // f_mount may attach before reporting failure
    const auto fr = f_mount(session.filesystem, session.drive_name, 1);
    if (fr != FR_OK) return rollback(static_cast<int32_t>(fr));
    session.mounted = true;
    Result result = facts();
    result.requested_khz = static_cast<int>(frequency_khz);
    result.card_max_khz = static_cast<int>(session.card.max_freq_khz);
    result.real_khz = session.card.real_freq_khz;
    err = space(result.free_bytes, result.total_bytes);
    if (err != ESP_OK) return rollback(err);
    return result;
}
const char* fatVolumePrefix() {
    return session.mounted && session.drive_name[1] == ':' ? session.drive_name : nullptr;
}
}}} // namespace yoradio::storage::sdspi

#if defined(YORADIO_S07_STORAGE_ENABLED) && !defined(YORADIO_DIRECTORY_HOST)
#include "esp_log.h"
// --wrap=sdmmc_init_sd_ssr. The real step always runs first. ESP_OK continues
// card init into host frequency and SCR check; FAT mount stays in mount().
// Only our mount, the compiled policy, and ESP_ERR_TIMEOUT may continue.
// --wrap=sdmmc_init_sd_ssr. Настоящий шаг всегда выполняется первым. ESP_OK
// продолжает init частоты и проверку SCR; FAT mount остаётся в mount().
// Продолжить можно только нашему mount, собранной политике и ESP_ERR_TIMEOUT.
extern "C" esp_err_t __real_sdmmc_init_sd_ssr(sdmmc_card_t* card);
extern "C" esp_err_t __wrap_sdmmc_init_sd_ssr(sdmmc_card_t* card) {
    const esp_err_t err = __real_sdmmc_init_sd_ssr(card);
    if (err == ESP_ERR_TIMEOUT) g_ssr_step_timed_out = true;
    const yoradio::storage::SdSsrStepDecision decision = yoradio::storage::sdSsrStepDecision(
        static_cast<int32_t>(err), YORADIO_SD_SSR_TIMEOUT_FALLBACK != 0,
        g_our_sdspi_mount, static_cast<int32_t>(ESP_ERR_TIMEOUT));
    if (decision.clear_ssr && card) card->ssr = {};
    if (decision.fallback_used) {
        ESP_LOGW("S07-ENG",
            "S07 SSR timeout fallback: sdmmc_init_sd_ssr err=0x%x; erase-timing fields cleared",
            err);
    }
    return static_cast<esp_err_t>(decision.error);
}
#endif
