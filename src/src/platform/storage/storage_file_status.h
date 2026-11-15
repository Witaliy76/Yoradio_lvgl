#pragma once
#include <atomic>
#include "media_ref.h"

namespace yoradio { namespace storage {
struct StorageFileStatus {
    std::atomic<FileError> error{FileError::None};
    std::atomic<int> native_error{0};
    std::atomic<uint32_t> handles{0}; // includes unconfirmed closes
    std::atomic<bool> eof{false};
    bool failed() const { return error.load() != FileError::None; }
    bool allClosed() const { return handles.load() == 0; }
};
}}
