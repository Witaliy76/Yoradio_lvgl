/**
 * @file lv_draw_buf_heap.h
 * Route LVGL default draw-buffer allocations (render layers) to the PSRAM heap.
 * Перенаправление default draw-buffer аллокаций LVGL (слои рендера) в кучу PSRAM.
 *
 * Why / Зачем:
 *   lv_bar / lv_obj render layers (ARGB8888 chunks, ~18 KB for the Main volume bar) were served
 *   from the TLSF widget pool. With LV_USE_OS == LV_OS_NONE a failed layer allocation makes
 *   lv_refr spin in "Allocating layer buffer failed. Try later" forever → task watchdog reset.
 *   Layer buffers are transient (per frame); the PSRAM heap holds them with megabytes of
 *   contiguous headroom, so the widget pool stops being a single point of failure for rendering.
 *   Слои рендера брались из TLSF-пула виджетов; при отказе выделения lv_refr крутится в
 *   «Try later» без выхода → watchdog. Слои живут один кадр — PSRAM-куча держит их с запасом
 *   в мегабайты, и пул перестаёт быть единой точкой отказа рендера.
 *
 * Scope / Границы:
 *   Only lv_draw_buf_get_handlers() (default handlers). Font-glyph and image-cache handlers stay
 *   on the LVGL pool untouched. Only the malloc/free pair is replaced; copy/align/stride/cache
 *   callbacks keep LVGL defaults.
 *   Только default handlers. Шрифтовые и image-cache handlers не трогаем. Меняется только пара
 *   malloc/free; copy/align/stride/cache остаются штатными.
 *
 * Lifecycle / Жизненный цикл:
 *   Install once right after lv_init(), before any lv_draw_buf exists. Process lifetime.
 *   Ставится один раз сразу после lv_init(), до создания первого lv_draw_buf. Живёт весь процесс.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_DRAW_BUF_HEAP_H
#define LV_DRAW_BUF_HEAP_H

namespace lvgl_ui {

// Returns false if LVGL did not expose default handlers (nothing changed in that case).
// Возвращает false, если LVGL не отдал default handlers (тогда ничего не меняется).
bool installPsramDrawBufHandlers();

} // namespace lvgl_ui

#endif // LV_DRAW_BUF_HEAP_H
