// Private concrete SDSPI implementation seam. Host tests link doubles here;
// production has no runtime override/config injection and exposes no raw FS.
#pragma once
#include <stdint.h>

namespace yoradio { namespace storage { namespace sdspi {
struct BootConfig {
    bool enabled = false;
    // Stock is disconnected; engineering binding reuses the accepted candidate.
    // Штатные пины отключены; инженерная привязка использует принятый кандидат.
    int cs = -1, mosi = -1, miso = -1, sck = -1, lcd_cs = -1;
    const char* path = "/sdcard";
};
const BootConfig& bootConfig();
// Value compiled into this translation unit. Boot logs this, not a second copy.
// Значение, собранное в этом файле. Boot печатает его, а не вторую копию.
int compiledSsrTimeoutFallback();
// "0:" while this mount owns the volume. Null after release. Not a DIR handle.
// "0:" пока этот mount владеет томом. Null после освобождения. Не дескриптор DIR.
const char* fatVolumePrefix();
struct Result {
    int32_t error = 0;
    int32_t cleanup_error = 0;
    bool no_media = false;          // no card response, not card-detect evidence
    bool resources_held = false;
    bool mounted = false;
    uint64_t free_bytes = 0;
    uint64_t total_bytes = 0;
    // Requested host.max_freq_khz and the frequency the driver stored on the card.
    // Запрошенный host.max_freq_khz и частота, которую драйвер записал в карту.
    int requested_khz = 0;
    int card_max_khz = 0;
    int real_khz = 0;
};
Result mount(uint32_t frequency_khz);
Result cleanup();
int32_t space(uint64_t& free_bytes, uint64_t& total_bytes);
}}}
