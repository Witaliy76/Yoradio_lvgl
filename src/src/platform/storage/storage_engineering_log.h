// Short event diagnostics only in the explicit enabled engineering environment.
// Краткая диагностика событий только в явно включённой инженерной конфигурации.
#pragma once
#if defined(YORADIO_STORAGE_OWNER_ENABLED) && YORADIO_STORAGE_OWNER_ENABLED
#include <Arduino.h>
#define S07_STORAGE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define S07_STORAGE_LOG(...) ((void)0)
#endif

// GPIO48 matrix read stays on the device compiler. Host tests compile this header
// with the engineering flag set and must not pull in ESP-IDF register headers.
// Чтение матрицы GPIO48 только на устройстве. Хостовые тесты не тянут регистры ESP-IDF.
#if defined(YORADIO_STORAGE_OWNER_ENABLED) && YORADIO_STORAGE_OWNER_ENABLED && defined(__XTENSA__)
#include "../../core/options.h"
#include "storage_owner.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#endif

namespace yoradio {
namespace storage {
// Read-only GPIO48 output-matrix snapshot. It does not reconfigure the pin or read the card.
// Снимок выходной матрицы GPIO48. Пин не перенастраивается, карта не читается.
inline void g48(const char* tag) {
#if defined(YORADIO_STORAGE_OWNER_ENABLED) && YORADIO_STORAGE_OWNER_ENABLED && defined(__XTENSA__)
    const uint32_t out_sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 48 * 4) & GPIO_FUNC0_OUT_SEL_V;
    const char* kind = "other";
    int rmt_ch = -1;
    if (out_sel == FSPICLK_OUT_IDX) kind = "SPI2-FSPI-CLK";       // SPI2_HOST clock
    else if (out_sel == SPI3_CLK_OUT_IDX) kind = "SPI3-CLK";
    else if (out_sel == SPICLK_OUT_IDX) kind = "SPI1-CLK";
    else if (out_sel >= RMT_SIG_OUT0_IDX && out_sel <= RMT_SIG_OUT3_IDX) {
        kind = "RMT";
        rmt_ch = static_cast<int>(out_sel - RMT_SIG_OUT0_IDX);
    } else if (out_sel == SIG_GPIO_OUT_IDX) kind = "GPIO";
    const StorageStatus storage = StorageOwner::instance().status();
    Serial.printf("[S07-ENG] g48 tag=%s out_sel=%u kind=%s rmt_ch=%d led=%u state=%u ready=%u gen=%llu err=%u handles=%u\n",
        tag ? tag : "-", static_cast<unsigned>(out_sel), kind, rmt_ch,
        static_cast<unsigned>(REAL_LEDBUILTIN), static_cast<unsigned>(storage.state),
        static_cast<unsigned>(storage.ready), static_cast<unsigned long long>(storage.generation),
        static_cast<unsigned>(storage.error), static_cast<unsigned>(storage.open_handles));
#else
    (void)tag;
#endif
}
}
}
