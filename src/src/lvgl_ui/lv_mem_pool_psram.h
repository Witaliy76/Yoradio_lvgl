/**
 * @file lv_mem_pool_psram.h
 * C-compatible one-shot allocator for LVGL fixed TLSF pool in PSRAM.
 * C-совместимый one-shot аллокатор фиксированного TLSF pool LVGL в PSRAM.
 *
 * Included from lv_mem.c via LV_MEM_POOL_INCLUDE when YORADIO_LVGL_POOL_IN_PSRAM=1.
 * Подключается из lv_mem.c через LV_MEM_POOL_INCLUDE при YORADIO_LVGL_POOL_IN_PSRAM=1.
 * LVGL 8.3 has no LV_MEM_POOL_FREE; backing block has process lifetime.
 * В LVGL 8.3 нет LV_MEM_POOL_FREE; backing block живёт весь процесс.
 */
#ifndef YORADIO_LV_MEM_POOL_PSRAM_H
#define YORADIO_LV_MEM_POOL_PSRAM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Allocate (or reuse) the single LVGL pool backing buffer. / Выделить (или reuse) единственный backing buffer pool LVGL. */
void *yoradio_lvgl_pool_alloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* YORADIO_LV_MEM_POOL_PSRAM_H */
