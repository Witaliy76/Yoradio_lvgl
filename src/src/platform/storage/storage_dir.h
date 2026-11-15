// One synchronous directory scan used by Directory Service. Not a browser.
// Один синхронный проход каталога для Directory Service. Не браузер.
#pragma once
#include "directory_names.h"

namespace yoradio { namespace storage {
// generation is the mount that admitted the scan. The DIR is closed on every
// return after a successful open, including name/path/read errors.
// generation — mount, допустивший проход. DIR закрывается на каждом возврате
// после успешного open, включая ошибки имени, пути и чтения.
DirStatus listDirectory(const char* directory, uint64_t generation,
                        bool (*visit)(const DirEntry& entry, void* user), void* user);
}}
