// Exact directory-entry names for a future Directory Service. Not a filesystem.
// Точные имена записей каталога для будущего Directory Service. Не файловая система.
#pragma once
#include "media_ref.h"

namespace yoradio { namespace storage {
// Bytes of UTF-8, excluding NUL. Must match the fatfs FF_LFN_BUF override.
// Байты UTF-8 без NUL. Должно совпадать с подменой FF_LFN_BUF в fatfs.
constexpr size_t kFatLfnUtf8Bytes = 768;

enum class DirStatus : uint8_t {
    Ok, End, Skip,
    PathTooLong, NameTooLong, InvalidPath,
    StaleMedia, OpenFailed, ReadFailed, CloseFailed, Unavailable
};

struct DirEntry {
    bool directory = false;
    char name[kFatLfnUtf8Bytes + 1]{};
    char path[kMaxMediaPathBytes + 1]{};
};

// Root "/" is a directory. File locators still reject it in validateMediaPath.
// Корень "/" — каталог. Для файла validateMediaPath по-прежнему его отвергает.
inline DirStatus validateDirectoryPath(const char* path) {
    if (!path || path[0] != '/') return DirStatus::InvalidPath;
    if (path[1] == 0) return DirStatus::Ok;
    return validateMediaPath(path) == FileError::None
        ? DirStatus::Ok : DirStatus::InvalidPath;
}

// Copies one component exactly. "." / ".." are Skip, not published names.
// Копирует один компонент целиком. "." / ".." — Skip, не опубликованные имена.
inline DirStatus acceptDirectoryEntry(const char* directory, const char* name,
                                      bool is_directory, DirEntry& out) {
    if (!directory || !name || validateDirectoryPath(directory) != DirStatus::Ok)
        return DirStatus::InvalidPath;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
        return DirStatus::Skip;
    size_t length = 0;
    while (name[length]) {
        if (length >= kFatLfnUtf8Bytes) return DirStatus::NameTooLong;
        ++length;
    }
    if (length == 0) return DirStatus::InvalidPath;
    if (name[length - 1] == '.' || name[length - 1] == ' ') return DirStatus::InvalidPath;
    for (size_t i = 0; i < length; ++i) {
        if (name[i] == '/' || name[i] == '\\' || strchr(":*?\"<>|", name[i]))
            return DirStatus::InvalidPath;
    }
    if (!yoradio::text::isValidUtf8(name, length, true)) return DirStatus::InvalidPath;
    const size_t dir_len = strlen(directory);
    const bool root = dir_len == 1;
    const size_t path_len = root ? (1 + length) : (dir_len + 1 + length);
    if (path_len > kMaxMediaPathBytes) return DirStatus::PathTooLong;
    memset(&out, 0, sizeof(out));
    out.directory = is_directory;
    memcpy(out.name, name, length);
    if (root) {
        out.path[0] = '/';
        memcpy(out.path + 1, name, length);
    } else {
        memcpy(out.path, directory, dir_len);
        out.path[dir_len] = '/';
        memcpy(out.path + dir_len + 1, name, length);
    }
    return DirStatus::Ok;
}

// reader.open(): 0 ready, -3 stale generation, anything else open failure.
// reader.read(name, cap, &is_dir): 0 entry, -1 end, -3 stale, -4 name does not fit,
// anything else read failure.
// reader.close(): 0 closed. A failed close is reported even after another error.
// reader.open(): 0 готов, -3 чужое поколение, иначе ошибка открытия.
// reader.read: 0 запись, -1 конец, -3 чужое поколение, -4 имя не помещается,
// иначе ошибка чтения.
// reader.close(): 0 закрыт. Ошибка закрытия важнее предыдущей ошибки чтения.
// Nested locals: name[769] here plus DirEntry (1794) and FatReader::fat_path (1027).
// Directory Service sizes StorageDir to 12 KiB (bytes, internal DRAM); see directory_service.h.
// Вложенные локальные буферы: name[769], DirEntry (1794) и FatReader::fat_path (1027).
// Directory Service задаёт StorageDir 12 КиБ (байты, внутренний DRAM); см. directory_service.h.
template <typename Reader>
DirStatus scanDirectoryNames(Reader& reader, const char* directory,
                             bool (*visit)(const DirEntry&, void*), void* user) {
    if (!visit || validateDirectoryPath(directory) != DirStatus::Ok)
        return DirStatus::InvalidPath;
    const int opened = reader.open();
    if (opened == -3) return DirStatus::StaleMedia;
    if (opened != 0) return DirStatus::OpenFailed;
    bool live = true;
    auto finish = [&](DirStatus status) -> DirStatus {
        if (!live) return status;
        live = false;
        return reader.close() == 0 ? status : DirStatus::CloseFailed;
    };
    for (;;) {
        char name[kFatLfnUtf8Bytes + 1];
        int is_dir = 0;
        const int rc = reader.read(name, sizeof name, &is_dir);
        if (rc == -1) return finish(DirStatus::End);
        if (rc == -3) return finish(DirStatus::StaleMedia);
        if (rc == -4) return finish(DirStatus::NameTooLong);
        if (rc != 0) return finish(DirStatus::ReadFailed);
        DirEntry entry;
        const DirStatus accepted = acceptDirectoryEntry(directory, name, is_dir != 0, entry);
        if (accepted == DirStatus::Skip) continue;
        if (accepted != DirStatus::Ok) return finish(accepted);
        if (!visit(entry, user)) return finish(DirStatus::Ok);
    }
}
}}
