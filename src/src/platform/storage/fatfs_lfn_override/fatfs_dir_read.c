// Built into the fatfs library so FILINFO matches ff.c. The app must not
// allocate FILINFO from the unmodified SDK header.
// Собирается в библиотеку fatfs, чтобы FILINFO совпал с ff.c.
// Приложение не выделяет FILINFO из немодифицированного заголовка SDK.
#include "fatfs_dir_read.h"
#include "ff.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(FF_LFN_BUF == 768, "FatFs LFN byte buffer was not overridden");
_Static_assert(FF_FS_EXFAT == 1 && sizeof(((FILINFO*)0)->fsize) == 8,
    "FatFs exFAT requires 64-bit FILINFO sizes");
_Static_assert(sizeof(((FILINFO*)0)->fname) == FF_LFN_BUF + 1,
    "FILINFO.fname must be the overridden byte buffer plus NUL");

int yoradio_fat_dir_open(const char* fat_path, void** out_dir) {
    if (!fat_path || !out_dir) return -3;
    *out_dir = NULL;
    FF_DIR* dir = (FF_DIR*)malloc(sizeof(FF_DIR));
    if (!dir) return FR_NOT_ENOUGH_CORE;
    memset(dir, 0, sizeof(*dir));
    const FRESULT result = f_opendir(dir, fat_path);
    if (result != FR_OK) {
        free(dir);
        return (int)result;
    }
    *out_dir = dir;
    return 0;
}

int yoradio_fat_dir_read(void* dir, char* name, size_t name_cap, int* is_dir) {
    if (!dir || !name || !is_dir || name_cap < (FF_LFN_BUF + 1)) return -4;
    name[0] = 0;
    *is_dir = 0;
    for (;;) {
        FILINFO info;
        memset(&info, 0, sizeof(info));
        const FRESULT result = f_readdir((FF_DIR*)dir, &info);
        if (result != FR_OK) return (int)result;
        if (info.fname[0] == 0) return -1;
        // FAT volume label. This IDF ff.h does not publish AM_VOL (0x08).
        // Метка тома FAT. В ff.h этого IDF нет AM_VOL (0x08).
        if (info.fattrib & 0x08) continue;
        const size_t length = strlen(info.fname);
        // Exact copy of fname when it fits. Length alone does not detect an SFN
        // that FatFs already substituted for a lost LFN; the shared FF_LFN_BUF=768
        // prevents that substitution for legal UTF-8 LFNs. altname is never copied.
        // Точная копия fname, если помещается. Длина сама по себе не обнаруживает
        // SFN, которым FatFs уже заменил потерянный LFN; общий FF_LFN_BUF=768
        // не даёт этой подмены для допустимых UTF-8 LFN. altname никогда не копируется.
        if (length == 0 || length > FF_LFN_BUF || length >= name_cap) return -4;
        memcpy(name, info.fname, length + 1);
        *is_dir = (info.fattrib & AM_DIR) ? 1 : 0;
        return 0;
    }
}

int yoradio_fat_dir_close(void* dir) {
    if (!dir) return -3;
    const FRESULT result = f_closedir((FF_DIR*)dir);
    free(dir);
    return result == FR_OK ? 0 : (int)result;
}

int yoradio_fat_is_media_error(int fresult) {
    return fresult == FR_DISK_ERR || fresult == FR_NOT_READY;
}
