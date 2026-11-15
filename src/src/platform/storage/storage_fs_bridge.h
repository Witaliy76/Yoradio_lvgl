// A read-only Arduino FS adapter for the owner's existing IDF FAT/VFS mount.
// Адаптер только для чтения существующим Audio; использует mount владельца Storage.
#pragma once
#include <FS.h>
#include "storage_file_status.h"

namespace yoradio { namespace storage {
class StorageFsBridge : public fs::FS {
public:
    StorageFsBridge();
    bool bind(uint64_t generation); // only after previous handles are confirmed closed
    std::shared_ptr<StorageFileStatus> fileStatus() const;
};
// Presence of one file through an owner lease. Not the playback binding and
// not a media-fault report. Call it from the player loop, not a UI callback.
// Наличие одного файла через lease владельца. Не playback binding и не media fault.
// Вызывать из player loop, не из UI callback.
enum class MediaProbe : uint8_t { Present, Missing, Unavailable, Stale, Io };
MediaProbe probeMediaFile(uint64_t generation, const char* path);
}}
