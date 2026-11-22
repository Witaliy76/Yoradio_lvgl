#include "../../core/options.h"
#include "storage_sdspi.h"

namespace yoradio { namespace storage { namespace sdspi {
static_assert(kResolvedStorageConfig.enabled ==
                  (kStorageOwnerEnabled && kUserSdEnable &&
                   kActiveBoard.storage.onboard_sd_present),
              "Storage mounts only when the Owner build, the user flag and the slot agree.");
static_assert(!kResolvedStorageConfig.allow_autoformat, "Storage never autoformats.");

#if YORADIO_STORAGE_OWNER_ENABLED
#if !defined(CONFIG_FATFS_LFN_HEAP) || !CONFIG_FATFS_LFN_HEAP || \
    !defined(CONFIG_FATFS_API_ENCODING_UTF_8) || !CONFIG_FATFS_API_ENCODING_UTF_8 || \
    CONFIG_FATFS_MAX_LFN != 255 || CONFIG_FATFS_CODEPAGE != 437
#error "Enabled Storage engineering env requires FATFS LFN_HEAP / MAX_LFN=255 / UTF-8 / CP437."
#endif
#endif

const BootConfig& bootConfig() {
    // SD lines come from BoardProfile.storage; the display command CS is the
    // effective one (storage_validate requires it unchanged while SD is on).
    // Pins follow the one permission. Off leaves every SD GPIO disconnected.
    // Линии SD берутся из BoardProfile.storage, CS команд дисплея — итоговый
    // (storage_validate требует его неизменным при включённой SD).
    // Пины следуют одному разрешению. В выключенном состоянии линии SD не задаются.
    // Values only (no reference to the profile object), so nothing extra lands in .rodata.
    // Только значения (без ссылки на объект профиля), чтобы не добавлять .rodata.
    constexpr bool on = kResolvedStorageConfig.enabled;
    constexpr int cs     = on ? static_cast<int>(kActiveBoard.storage.sd_cs_gpio) : -1;
    constexpr int mosi   = on ? static_cast<int>(kActiveBoard.storage.sd_cmd_gpio) : -1;
    constexpr int miso   = on ? static_cast<int>(kActiveBoard.storage.sd_dat0_gpio) : -1;
    constexpr int sck    = on ? static_cast<int>(kActiveBoard.storage.sd_clk_gpio) : -1;
    constexpr int lcd_cs = on ? static_cast<int>(kWiring.display.cmd_cs_gpio) : -1;
    static const BootConfig config{on, cs, mosi, miso, sck, lcd_cs,
                                   kResolvedStorageConfig.mount_path};
    return config;
}
}}}
