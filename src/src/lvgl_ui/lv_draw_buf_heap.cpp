/**
 * @file lv_draw_buf_heap.cpp
 * PSRAM-heap allocator for LVGL default draw buffers (render layers). See header for rationale.
 * PSRAM-куча как аллокатор default draw buffers LVGL (слои рендера). Обоснование — в заголовке.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#include "lv_draw_buf_heap.h"

#include "lvgl.h"
// lv_draw_buf_handlers_t is an opaque struct in the public API; the aggregate private header is
// LVGL's sanctioned way to reach its fields. We only touch the two allocator callbacks.
// lv_draw_buf_handlers_t непрозрачен в публичном API; агрегатный private-заголовок — штатный
// способ LVGL добраться до полей. Трогаем только два callback'а аллокатора.
#include "lvgl_private.h"

#include "esp_heap_caps.h"

namespace {

void* heapBufMalloc(size_t size_bytes, lv_color_format_t color_format) {
    LV_UNUSED(color_format);
    // Mirror LVGL's default buf_malloc: over-allocate so the default align_pointer_cb can round the
    // data pointer up to LV_DRAW_BUF_ALIGN. lv_draw_buf_t keeps unaligned_data for the free path.
    // Как в штатном buf_malloc LVGL: берём с запасом, чтобы default align_pointer_cb мог выровнять
    // указатель до LV_DRAW_BUF_ALIGN. Для free LVGL хранит unaligned_data.
    size_bytes += LV_DRAW_BUF_ALIGN - 1;
    return heap_caps_malloc(size_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void heapBufFree(void* buf) {
    heap_caps_free(buf);
}

} // namespace

namespace lvgl_ui {

bool installPsramDrawBufHandlers() {
    lv_draw_buf_handlers_t* handlers = lv_draw_buf_get_handlers();
    if (!handlers) return false;
    // Replace the allocator pair only. Buffers record their handlers pointer at creation and free
    // through it, so swapping before the first lv_draw_buf exists keeps malloc/free paired.
    // Меняем только пару аллокатора. Буфер запоминает указатель на handlers при создании и
    // освобождается через него — замена до первого lv_draw_buf сохраняет парность malloc/free.
    handlers->buf_malloc_cb = heapBufMalloc;
    handlers->buf_free_cb = heapBufFree;
    return true;
}

} // namespace lvgl_ui
