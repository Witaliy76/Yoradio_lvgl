/**
 * @file lv_mem_pool_psram.cpp
 * One-shot PSRAM backing for LVGL built-in TLSF pool (LV_MEM_CUSTOM=0 path).
 * One-shot PSRAM backing для встроенного TLSF pool LVGL (путь LV_MEM_CUSTOM=0).
 *
 * LVGL 8.3 has no LV_MEM_POOL_FREE; backing block has process lifetime.
 * В LVGL 8.3 нет LV_MEM_POOL_FREE; backing block живёт весь процесс.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#include "lv_mem_pool_psram.h"

#include <stdint.h>

#include "Arduino.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_system.h"

namespace {

struct PoolState {
    void*  ptr;
    size_t requested;
};

static PoolState s_pool = {};

static void pool_fatal(const char* reason) {
    Serial.printf("[LVGL_POOL] FATAL %s\n", reason);
    Serial.flush();
    esp_system_abort(reason);
}

}  // namespace

extern "C" void *yoradio_lvgl_pool_alloc(size_t size) {
    // Reuse same process-lifetime block (LVGL has no LV_MEM_POOL_FREE).
    // Повторный вызов: тот же block на весь процесс (у LVGL нет LV_MEM_POOL_FREE).
    if (s_pool.ptr != nullptr) {
        if (size != s_pool.requested) {
            pool_fatal("reuse size mismatch");
        }
        return s_pool.ptr;
    }

    if (size == 0) {
        pool_fatal("requested size=0");
    }

    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        pool_fatal("heap_caps_malloc returned NULL");
    }
    if (!esp_ptr_external_ram(p)) {
        pool_fatal("pointer not in external RAM");
    }
    if (((uintptr_t)p % 4u) != 0u) {
        pool_fatal("pointer not 4-byte aligned");
    }
    if (heap_caps_get_allocated_size(p) < size) {
        pool_fatal("allocated size < requested");
    }

    s_pool.ptr       = p;
    s_pool.requested = size;
    return p;
}
