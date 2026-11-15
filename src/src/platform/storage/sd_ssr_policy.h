// Decision for one finished call of sdmmc_init_sd_ssr. No IDF or card I/O.
// Решение по одному завершённому вызову sdmmc_init_sd_ssr. Без IDF и без чтения карты.
#pragma once
#include <stdint.h>

namespace yoradio { namespace storage {

struct SdSsrStepDecision {
    int32_t error = 0;          // value returned to sdmmc_card_init
    bool clear_ssr = false;     // zero erase-timing fields after an allowed timeout
    bool fallback_used = false; // this timeout was continued
};

// Continue only when this step timed out, the compiled policy allows it, and the
// call is inside our SDSPI mount. Every other code, including ESP_ERR_NO_MEM, is kept.
// Продолжение только при таймауте этого шага, включённой политике и нашем SDSPI mount.
// Любой другой код, включая ESP_ERR_NO_MEM, сохраняется.
inline SdSsrStepDecision sdSsrStepDecision(int32_t real_error, bool fallback_enabled,
                                           bool our_sdspi_mount, int32_t timeout_code) {
    SdSsrStepDecision decision;
    decision.error = real_error;
    if (real_error != timeout_code || !fallback_enabled || !our_sdspi_mount) return decision;
    decision.error = 0;
    decision.clear_ssr = true;
    decision.fallback_used = true;
    return decision;
}

}}
