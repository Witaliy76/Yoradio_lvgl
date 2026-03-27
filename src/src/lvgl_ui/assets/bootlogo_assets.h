/*
 * Declarations for lcd-image-converter RGB565 boot assets (linked .c in this folder).
 * Объявления RGB565-ассетов boot из lcd-image-converter (.c в этой папке).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Medium boot bitmap — yoradio_boot_medium.c (e.g. 480×480-class panels). / Средний для крупных экранов. */
extern const uint16_t image_data_yoradio_boot_medium[];

/** Compact boot bitmap — yoradio_boot_small.c (e.g. 320-wide panels). */
extern const uint16_t image_data_yoradio_boot_small[];

#ifdef __cplusplus
}
#endif

/** yoradio_boot_medium.c — 170×149 (source: bootlogo/yoradio_240.c). */
#define YORADIO_BOOTLOGO_MEDIUM_W 170
#define YORADIO_BOOTLOGO_MEDIUM_H 149

/** yoradio_boot_small.c — 114×97. */
#define YORADIO_BOOTLOGO_SMALL_W 114
#define YORADIO_BOOTLOGO_SMALL_H 97
