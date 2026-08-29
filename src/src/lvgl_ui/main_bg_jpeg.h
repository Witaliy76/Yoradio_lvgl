/*
 * Main background: LittleFS JPEG → TJPGD → FIT/ScaleOnce → RGB565 PSRAM cache.
 * Фон Main: JPEG из LittleFS → TJPGD → FIT/ScaleOnce → кэш RGB565 в PSRAM.
 *
 * Boot prepare: DspTask, synchronous (audio not started yet).
 * Runtime theme change: one bounded worker; LVGL apply only on DspTask.
 * Boot — синхронно на DspTask. Смена темы — worker; в LVGL только на DspTask.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef MAIN_BG_JPEG_H
#define MAIN_BG_JPEG_H

#include <stdint.h>
#include "lvgl.h"

namespace lvgl_ui {

const char* mainBgJpegPathForSlot(uint8_t slot);

void mainBgCacheInvalidate();

// Cache hit only — never decodes. out_dsc.data points into the cache — do not free.
// Только попадание, без decode. out_dsc.data в кэше — не освобождать.
bool mainBgCacheGet(const char* fs_path, lv_img_dsc_t& out_dsc);

// Boot: decode active theme on DspTask. / Boot: декод активной темы на DspTask.
bool mainBgCachePreloadActive();

// Runtime: one outstanding JPEG job; a newer request supersedes a stale one.
// Runtime: один job; новый запрос вытесняет незавершённый.
void mainBgCacheRequestAsync(const char* fs_path);

// DspTask: install a finished worker buffer into the cache. Returns true if Main should refresh.
// DspTask: поставить готовый буфер в кэш. true — Main должен обновить картинку.
bool mainBgCachePollApply();

// Runtime JPEG job not yet installed on DspTask (WebUI /bg_status). Boot preload is not included.
// Runtime JPEG ещё не установлен на DspTask. Boot preload сюда не входит.
bool mainBgCacheRuntimeBusy();

}  // namespace lvgl_ui

#endif /* MAIN_BG_JPEG_H */
