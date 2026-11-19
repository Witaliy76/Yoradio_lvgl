#include "../../core/options.h"
#include "storage_sdspi.h"

namespace yoradio { namespace storage { namespace sdspi {
static_assert(kResolvedStorageConfig.enabled ==
                  (kStorageEngineeringEnabled && kUserSdEnable &&
                   kActiveBoard.storage.onboard_sd_present),
              "Storage mounts only when the Owner build, the user flag and the slot agree.");
static_assert(!kResolvedStorageConfig.allow_autoformat, "Storage never autoformats.");

#if YORADIO_S07_STORAGE_ENABLED
#if !defined(CONFIG_FATFS_LFN_HEAP) || !CONFIG_FATFS_LFN_HEAP || \
    !defined(CONFIG_FATFS_API_ENCODING_UTF_8) || !CONFIG_FATFS_API_ENCODING_UTF_8 || \
    CONFIG_FATFS_MAX_LFN != 255 || CONFIG_FATFS_CODEPAGE != 437
#error "Enabled Storage engineering env requires FATFS LFN_HEAP / MAX_LFN=255 / UTF-8 / CP437."
#endif
#endif

const BootConfig& bootConfig() {
    // One candidate map for the tested board. The Owner build uses it; macro 0
    // leaves the pins disconnected. No BoardProfile evidence upgrade.
    // Единая карта кандидата проверенной платы. Сборка Owner её использует;
    // при макросе 0 пины отключены. BoardProfile не повышается.
    using namespace storage_schematic_4848s040;
    // Pins follow the one permission. Off leaves every SD GPIO disconnected.
    // Пины следуют одному разрешению. В выключенном состоянии линии SD не задаются.
    static const BootConfig config{kResolvedStorageConfig.enabled,
        kResolvedStorageConfig.enabled ? kTfDat3CsGpio : -1,
        kResolvedStorageConfig.enabled ? kTfCmdGpio : -1,
        kResolvedStorageConfig.enabled ? kTfDat0Gpio : -1,
        kResolvedStorageConfig.enabled ? kTfClkGpio : -1,
        kResolvedStorageConfig.enabled ? kLcdCmdCsGpio : -1,
        kResolvedStorageConfig.mount_path};
    return config;
}
}}}
