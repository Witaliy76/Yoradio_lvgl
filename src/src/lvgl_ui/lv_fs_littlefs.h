/**
 * LVGL v8.3 filesystem bridge to Arduino LittleFS (single mount, drive letter L:).
 * Мост ФС LVGL к Arduino LittleFS — один mount, буква диска L:.
 *
 * Stage 6.1F-b: minimal open/read/seek/tell/close for file-backed lv_img (.bin).
 * Регистрация один раз после lv_init(); пути вида "L:/bg/main_dark.bin".
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_FS_LITTLEFS_H
#define LV_FS_LITTLEFS_H

#ifdef __cplusplus
extern "C" {
#endif

void lv_fs_littlefs_register(void);

#ifdef __cplusplus
}
#endif

#endif
