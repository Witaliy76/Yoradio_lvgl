// FatFs directory read compiled with the same FILINFO as the fatfs library.
// Чтение каталога FatFs, собранное с тем же FILINFO, что и библиотека fatfs.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// 0 = one entry, -1 = end, -4 = name does not fit or bad argument, other = FRESULT.
// name is copied exactly, or not at all when it does not fit.
// 0 = одна запись, -1 = конец, -4 = имя не помещается или плохой аргумент, иначе FRESULT.
// Имя копируется целиком либо не копируется, если не помещается.
int yoradio_fat_dir_open(const char* fat_path, void** out_dir);
int yoradio_fat_dir_read(void* dir, char* name, size_t name_cap, int* is_dir);
int yoradio_fat_dir_close(void* dir);
// 1 for FR_DISK_ERR / FR_NOT_READY. Path and OOM codes stay 0.
// 1 для FR_DISK_ERR / FR_NOT_READY. Коды пути и нехватки памяти дают 0.
int yoradio_fat_is_media_error(int fresult);
#ifdef __cplusplus
}
#endif
