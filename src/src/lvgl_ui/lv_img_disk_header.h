/**
 * @file lv_img_disk_header.h
 * BASE-LVGL9-MIGRATION C5: translate the YoRadio 4-byte on-disk image header (unchanged since
 * LVGL 8.3, still written by netserver.cpp WebUI uploads) into an LVGL 9 in-memory
 * lv_image_header_t (12 bytes). The on-disk format is NOT changed by this migration.
 *
 * Перевод 4-байтного заголовка YoRadio (формат не менялся с LVGL 8.3, тот же пишет
 * netserver.cpp при загрузке через WebUI) в 12-байтный lv_image_header_t LVGL 9.
 * Формат на диске этой миграцией не меняется.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_IMG_DISK_HEADER_H
#define LV_IMG_DISK_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

namespace lvgl_ui {

// Decoded fields of the 4-byte on-disk header (v8 lv_img_header_t packed layout, LE):
// bits 0-4 cf, 5-7 always_zero, 8-9 reserved, 10-20 w, 21-31 h.
struct ImgDiskHeader {
    uint8_t  cf;
    uint16_t w;
    uint16_t h;
};

// Parse the raw 4 on-disk header bytes. Always succeeds (all 32 bit patterns are decodable);
// callers validate resulting fields (cf, w, h) against expectations for the loader in question.
void imgDiskHeaderParse(const uint8_t raw[4], ImgDiskHeader& out);

// Map a parsed on-disk header to an LVGL 9 lv_image_header_t for the two product on-disk color
// formats: v8 cf=4 (LV_IMG_CF_TRUE_COLOR) -> LV_COLOR_FORMAT_RGB565, v8 cf=5
// (LV_IMG_CF_TRUE_COLOR_ALPHA) -> LV_COLOR_FORMAT_RGB565A8. Returns false for any other cf.
// stride is always w*2 (the RGB565 plane row stride) per LVGL 9's img_width_to_stride() —
// for RGB565A8 the A8 plane immediately follows the full color plane (w*h*2 bytes in).
bool imgDiskHeaderToLvHeader(const ImgDiskHeader& disk, lv_image_header_t& out);

// STATION-ART-LVGL9-REPAIR: the v8 TRUE_COLOR_ALPHA payload on disk is INTERLEAVED — 3 bytes per
// pixel (RGB565 LE, then the alpha byte). LVGL 9's RGB565A8 is PLANAR — a full color plane of
// h rows × (w*2) bytes, immediately followed by an A8 plane of h rows × w bytes. Any loader of a
// cf=5 on-disk asset must therefore split the payload; imgDiskHeaderToLvHeader() only fixes the
// header. Converts one row: color_row receives w*2 bytes, alpha_row receives w bytes.
//
// В v8 TRUE_COLOR_ALPHA пиксели чередуются (3 Б/пиксель), в RGB565A8 LVGL 9 — две отдельные
// плоскости. Функция разбирает одну строку: color_row — w*2 байт, alpha_row — w байт.
void imgDiskRgb565AlphaRowToPlanar(const uint8_t* src_row, uint16_t w,
                                   uint8_t* color_row, uint8_t* alpha_row);

}  // namespace lvgl_ui

#endif /* LV_IMG_DISK_HEADER_H */
