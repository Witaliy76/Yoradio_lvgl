// Project override for the installed IDF 5.5.5 ffconf.h. Do not edit that SDK copy.
// FF_LFN_BUF is a byte capacity. 255 cannot hold a 255-unit LFN in UTF-8 (up to 765 bytes).
// Проектная подмена установленного ffconf.h IDF 5.5.5. Сам SDK не правится.
// FF_LFN_BUF — байты. 255 не вмещает LFN из 255 единиц UTF-16 в UTF-8 (до 765 байт).
#ifndef YORADIO_FFCONF_LFN_OVERRIDE
#define YORADIO_FFCONF_LFN_OVERRIDE
#include_next "ffconf.h"
#undef FF_LFN_BUF
#define FF_LFN_BUF 768
#endif
