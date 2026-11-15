// Exact owned filesystem path and mount generation; no playback command state.
// Полный путь файловой системы и поколение монтирования; без состояния Player.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../utils/utf8.h"

namespace yoradio { namespace storage {
constexpr size_t kMaxMediaPathBytes = 1023; // bytes excluding NUL; never truncated
enum class FileError : uint8_t {
    None, PathTooLong, InvalidPath, StaleMedia, OpenFailed,
    NotRegularFile, FileTooLarge, ReadFailed, SeekFailed, CloseFailed, NoMemory, ReadOnly
};
inline FileError validateMediaPath(const char* path) {
    if (!path) return FileError::InvalidPath;
    size_t length = 0;
    while (length <= kMaxMediaPathBytes && path[length]) ++length;
    if (length > kMaxMediaPathBytes) return FileError::PathTooLong;
    if (length < 2 || path[0] != '/' || path[length-1] == '/' ||
        !yoradio::text::isValidUtf8(path, length, true)) return FileError::InvalidPath;
    size_t start = 1;
    for (size_t i = 1; i <= length; ++i) {
        if (path[i] && strchr("\\:*?\"<>|",path[i])) return FileError::InvalidPath;
        if (path[i] != '/' && path[i] != 0) continue;
        const size_t n = i-start;
        if (!n || (n == 1 && path[start] == '.') ||
            (n == 2 && path[start] == '.' && path[start+1] == '.')) return FileError::InvalidPath;
        // FatFs strips these suffixes. Reject aliases instead of opening a
        // different name from the exact locator the caller supplied.
        if (path[i-1] == '.' || path[i-1] == ' ') return FileError::InvalidPath;
        start = i+1;
    }
    return FileError::None;
}
struct MediaRef {
    uint64_t generation = 0;
    char path[kMaxMediaPathBytes+1]{};
    static FileError copy(const char* path, uint64_t generation, MediaRef& out) {
        const auto error = validateMediaPath(path);
        if (error != FileError::None) return error;
        if (!generation) return FileError::StaleMedia;
        out.generation = generation;
        memcpy(out.path,path,strlen(path)+1);
        return FileError::None;
    }
};
}}
