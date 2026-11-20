// Lists one directory through FatFs f_readdir, not POSIX dirent.
// Перечисляет один каталог через FatFs f_readdir, не через POSIX dirent.
#include "storage_dir.h"
#if defined(YORADIO_STORAGE_OWNER_ENABLED) && YORADIO_STORAGE_OWNER_ENABLED
#include "storage_owner.h"
#include "storage_sdspi.h"
#include "storage_engineering_log.h"
#include "fatfs_lfn_override/fatfs_dir_read.h"
#include <string.h>

namespace yoradio { namespace storage {
namespace {
struct FatReader {
    HandleLease handle;
    void* dir = nullptr;
    bool reserved = false;
    uint64_t generation = 0;
    char fat_path[kMaxMediaPathBytes + 4]{};

    void noteFatResult(int result, const char* where) {
        if (result == 0 || result == -1 || result == -3 || result == -4) return;
        YORADIO_STORAGE_NOTE("[Storage] dir %s error native=%d\n", where, result);
        // Same physical-access policy as the file bridge. Path/OOM stay local.
        // Та же политика физического доступа, что у файлового bridge. Путь/OOM локальны.
        if (yoradio_fat_is_media_error(result))
            StorageOwner::instance().reportMediaFault(generation, result);
    }

    int open() {
        const char* volume = sdspi::fatVolumePrefix();
        if (!volume || strlen(volume) != 2) return -3;
        OperationLease operation;
        auto& owner = StorageOwner::instance();
        if (!owner.acquireOperation(generation, operation)) return -3;
        if (!owner.reserveHandle(operation, handle)) return -3;
        reserved = true;
        const int result = yoradio_fat_dir_open(fat_path, &dir);
        if (result != 0) {
            noteFatResult(result, "open");
            handle.releaseAfterClose();
            reserved = false;
            dir = nullptr;
        }
        return result;
    }
    int read(char* name, size_t cap, int* is_dir) {
        OperationLease operation;
        if (!StorageOwner::instance().acquireOperation(handle, operation)) return -3;
        const int result = yoradio_fat_dir_read(dir, name, cap, is_dir);
        noteFatResult(result, "read");
        return result;
    }
    int close() {
        if (!reserved) return 0;
        CloseLease closing;
        if (!StorageOwner::instance().acquireClose(handle, closing)) return -1;
        reserved = false;
        const int result = dir ? yoradio_fat_dir_close(dir) : 0;
        dir = nullptr;
        // confirmClosed only after a proven close. Unknown close keeps the count.
        // confirmClosed только после доказанного close. Неизвестный close сохраняет счётчик.
        if (result != 0) {
            YORADIO_STORAGE_NOTE("[Storage] dir close error native=%d\n", result);
            return -1;
        }
        if (!closing.confirmClosed()) {
            YORADIO_STORAGE_NOTE("[Storage] dir close error native=%d\n", result);
            return -1;
        }
        return 0;
    }
};
}

DirStatus listDirectory(const char* directory, uint64_t generation,
                        bool (*visit)(const DirEntry& entry, void* user), void* user) {
    if (validateDirectoryPath(directory) != DirStatus::Ok || !visit)
        return DirStatus::InvalidPath;
    const char* volume = sdspi::fatVolumePrefix();
    if (!volume) return DirStatus::StaleMedia;
    FatReader reader;
    reader.generation = generation;
    const size_t dir_len = strlen(directory);
    if (strlen(volume) + dir_len > sizeof(reader.fat_path) - 1)
        return DirStatus::PathTooLong;
    memcpy(reader.fat_path, volume, strlen(volume));
    memcpy(reader.fat_path + strlen(volume), directory, dir_len + 1);
    return scanDirectoryNames(reader, directory, visit, user);
}
}}
#else
namespace yoradio { namespace storage {
DirStatus listDirectory(const char*, uint64_t,
                        bool (*)(const DirEntry&, void*), void*) {
    return DirStatus::Unavailable;
}
}}
#endif
