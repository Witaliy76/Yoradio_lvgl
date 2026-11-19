// Project override for the installed IDF 5.5.5 ffconf.h. Do not edit that SDK copy.
// FF_LFN_BUF is a byte capacity. 255 cannot hold a 255-unit LFN in UTF-8 (up to 765 bytes).
// Проектная подмена установленного ffconf.h IDF 5.5.5. Сам SDK не правится.
// FF_LFN_BUF — байты. 255 не вмещает LFN из 255 единиц UTF-16 в UTF-8 (до 765 байт).
#ifndef YORADIO_FFCONF_LFN_OVERRIDE
#define YORADIO_FFCONF_LFN_OVERRIDE
#include "ffconf_sdk.h"
#undef FF_LFN_BUF
#define FF_LFN_BUF 768
#undef FF_FS_EXFAT
#define FF_FS_EXFAT 1

// exFAT dir_read uses FF_USE_LABEL in a C expression. Kconfig omits disabled options.
// В exFAT dir_read использует FF_USE_LABEL в выражении C. Kconfig не определяет выключенные опции.
#ifndef CONFIG_FATFS_USE_LABEL
#undef FF_USE_LABEL
#define FF_USE_LABEL 0
#endif

#if !FF_USE_LFN || FF_LFN_UNICODE != 2 || FF_MAX_LFN != 255
#error "YoRadio FAT/exFAT needs 255 UTF-16 units and a UTF-8 path API"
#endif
#endif
