// Root-bounded depth-first next-track search for SD path playback.
// Depth-first поиск следующего трека SD в границах корня карты.
#include "folder_queue.h"
#include "../platform/storage/directory_service.h"
#include "../platform/storage/directory_names.h"
#include <string.h>
#include <stdlib.h>
#if !defined(YORADIO_DIRECTORY_HOST)
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#endif

namespace yoradio { namespace playback {
namespace {

using storage::DirectoryConsumer;
using storage::DirectoryItemKind;
using storage::DirectoryQuery;
using storage::DirectorySnapshot;
using storage::DirectoryStatus;
using storage::MediaRef;
using storage::kFatLfnUtf8Bytes;
using storage::kMaxMediaPathBytes;

enum class Phase : uint8_t {
    Idle,
    WaitList,
    Ready,
    End,
    Error,
    Cancelled
};

enum class Step : uint8_t {
    NextAfterFile,   // list parent of cursor; next audio, else first child dir
    FirstInSubtree,  // list folder; first audio, else first child dir
    NextSibling      // list parent of folder; next child dir after basename
};

struct State {
    bool session = false;
    Phase phase = Phase::Idle;
    Step step = Step::NextAfterFile;
    uint32_t request_id = 1;
    uint32_t active_request = 0;
    uint32_t search_epoch = 0; // epoch captured when StartNext began
    uint64_t generation = 0;
    PathPlayResult detail = PathPlayResult::None;
    MediaRef current{};
    MediaRef pending_previous{};
    MediaRef found{};
    bool has_found = false;
    bool launch_ready = false;
    bool advance_launch = false;
    bool history_launch = false;
    char list_path[kMaxMediaPathBytes + 1]{};
    char after_name[kFatLfnUtf8Bytes + 1]{};
    MediaRef history[kFolderQueueHistoryDepth]{};
    uint8_t history_count = 0;
    DirectorySnapshot* held = nullptr;
    SdRepeatMode repeat = SdRepeatMode::Off;
    bool shuffle = false;
    bool wrapped_all = false;
    bool shuffle_valid = false;
    uint16_t shuffle_n = 0;
    uint16_t shuffle_cap = 0;
    // Permutation lives in PSRAM (host: malloc) — keep internal DRAM free for TLS.
    // Перестановка в PSRAM (на хосте — malloc), чтобы не заедать DRAM под TLS.
    uint16_t* shuffle_perm = nullptr;
    char shuffle_folder[kMaxMediaPathBytes + 1]{};
    uint32_t rng = 1;
};

// ~14 KiB MediaRef/history — EXT_RAM_BSS needs ALLOW_BSS_SEG_EXTERNAL_MEMORY.
// ~14 КиБ MediaRef/истории — EXT_RAM_BSS требует ALLOW_BSS_SEG_EXTERNAL_MEMORY.
#if defined(YORADIO_DIRECTORY_HOST)
State g;
bool g_host_fail_shuffle_alloc = false;
#else
EXT_RAM_BSS_ATTR State g;
#endif

void releaseHeld() {
    if (!g.held) return;
    storage::directoryRelease(g.held);
    g.held = nullptr;
}

void clearShuffle();
void cancelSearch();

bool copyPath(char* out, size_t cap, const char* path) {
    if (!out || !path || !cap) return false;
    const size_t n = strlen(path);
    if (n + 1u > cap) return false;
    memcpy(out, path, n + 1u);
    return true;
}

bool parentOf(const char* path, char* out, size_t cap) {
    if (!path || path[0] != '/' || !cap) return false;
    if (path[1] == 0) {
        if (cap < 2) return false;
        out[0] = '/';
        out[1] = 0;
        return true;
    }
    const char* slash = strrchr(path, '/');
    if (!slash) return false;
    if (slash == path) {
        if (cap < 2) return false;
        out[0] = '/';
        out[1] = 0;
        return true;
    }
    const size_t n = static_cast<size_t>(slash - path);
    if (n + 1u > cap) return false;
    memcpy(out, path, n);
    out[n] = 0;
    return true;
}

bool baseName(const char* path, char* out, size_t cap) {
    if (!path || path[0] != '/' || path[1] == 0 || !out || !cap) return false;
    const char* slash = strrchr(path, '/');
    if (!slash || !slash[1]) return false;
    return copyPath(out, cap, slash + 1);
}

bool isRoot(const char* path) {
    return path && path[0] == '/' && path[1] == 0;
}

void clearSearch() {
    releaseHeld();
    g.phase = Phase::Idle;
    g.active_request = 0;
    g.search_epoch = 0;
    g.has_found = false;
    g.launch_ready = false;
    g.found = {};
    g.detail = PathPlayResult::None;
    g.list_path[0] = 0;
    g.after_name[0] = 0;
}

void fail(PathPlayResult detail) {
    releaseHeld();
    storage::directoryCancel(DirectoryConsumer::Playback);
    clearShuffle();
    g.phase = Phase::Error;
    g.active_request = 0;
    g.has_found = false;
    g.launch_ready = false;
    g.detail = detail;
}

void finishEnd() {
    releaseHeld();
    storage::directoryCancel(DirectoryConsumer::Playback);
    clearShuffle();
    g.phase = Phase::End;
    g.active_request = 0;
    g.has_found = false;
    g.launch_ready = false;
    g.detail = PathPlayResult::EndOfFile;
}

void publishFound(const char* path) {
    releaseHeld();
    if (MediaRef::copy(path, g.generation, g.found) != storage::FileError::None) {
        fail(PathPlayResult::InvalidPath);
        return;
    }
    g.has_found = true;
    g.launch_ready = true;
    g.phase = Phase::Ready;
    g.active_request = 0;
    g.detail = PathPlayResult::None;
}

bool submitList(const char* path) {
    if (!copyPath(g.list_path, sizeof g.list_path, path)) {
        fail(PathPlayResult::PathTooLong);
        return false;
    }
    uint32_t id = g.request_id + 1u;
    if (id == 0) id = 1u;
    g.request_id = id;
    g.active_request = id;
    DirectoryQuery query;
    query.consumer = DirectoryConsumer::Playback;
    query.request_id = id;
    query.generation = g.generation;
    query.path = g.list_path;
    const DirectoryStatus status = storage::directorySubmit(query);
    if (status != DirectoryStatus::Ok) {
        fail(status == DirectoryStatus::NoMemory ? PathPlayResult::NoMemory :
             status == DirectoryStatus::StaleMedia ? PathPlayResult::StaleMedia :
             status == DirectoryStatus::Unavailable ? PathPlayResult::NoMedia :
             PathPlayResult::IoError);
        return false;
    }
    g.phase = Phase::WaitList;
    g.detail = PathPlayResult::SearchingNext;
    return true;
}

bool beginNextAfterFile(const char* file_path) {
    char parent[kMaxMediaPathBytes + 1];
    char name[kFatLfnUtf8Bytes + 1];
    if (!parentOf(file_path, parent, sizeof parent) || !baseName(file_path, name, sizeof name)) {
        fail(PathPlayResult::InvalidPath);
        return false;
    }
    g.step = Step::NextAfterFile;
    if (!copyPath(g.after_name, sizeof g.after_name, name)) {
        fail(PathPlayResult::PathTooLong);
        return false;
    }
    return submitList(parent);
}

bool beginFirstInSubtree(const char* folder) {
    g.step = Step::FirstInSubtree;
    g.after_name[0] = 0;
    return submitList(folder);
}

uint32_t shuffleRand() {
#if defined(YORADIO_DIRECTORY_HOST)
    g.rng = g.rng * 1664525u + 1013904223u;
    return g.rng;
#else
    return esp_random();
#endif
}

void freeShuffleStorage() {
    if (g.shuffle_perm) {
        free(g.shuffle_perm);
        g.shuffle_perm = nullptr;
    }
    g.shuffle_cap = 0;
}

bool ensureShuffleStorage(uint16_t n) {
    if (!n || n > kFolderQueueShuffleMax) return false;
    if (g.shuffle_perm && g.shuffle_cap == n) return true;
    freeShuffleStorage();
#if defined(YORADIO_DIRECTORY_HOST)
    g.shuffle_perm = g_host_fail_shuffle_alloc ? nullptr
        : static_cast<uint16_t*>(malloc(sizeof(uint16_t) * n));
#else
    g.shuffle_perm = static_cast<uint16_t*>(heap_caps_malloc(
        sizeof(uint16_t) * n,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    if (!g.shuffle_perm) return false;
    g.shuffle_cap = n;
    return true;
}

bool rebuildShuffle(uint16_t n) {
    if (!ensureShuffleStorage(n)) return false;
    for (uint16_t i = 0; i < n; ++i) g.shuffle_perm[i] = i;
    for (uint16_t i = n; i > 1; --i) {
        const uint16_t j = static_cast<uint16_t>(shuffleRand() % i);
        const uint16_t tmp = g.shuffle_perm[i - 1];
        g.shuffle_perm[i - 1] = g.shuffle_perm[j];
        g.shuffle_perm[j] = tmp;
    }
    g.shuffle_n = n;
    g.shuffle_valid = true;
    return true;
}

void clearShuffle() {
    g.shuffle_valid = false;
    g.shuffle_n = 0;
    g.shuffle_folder[0] = 0;
    freeShuffleStorage();
}

uint16_t countAudio(const DirectorySnapshot* snapshot) {
    uint16_t n = 0;
    if (!snapshot) return 0;
    const uint32_t count = storage::directorySnapshotCount(snapshot);
    for (uint32_t i = 0; i < count; ++i) {
        if (storage::directoryItemKind(snapshot, i) == DirectoryItemKind::Audio) {
            if (n == UINT16_MAX) return n;
            ++n;
        }
    }
    return n;
}

bool audioIndexByName(const DirectorySnapshot* snapshot, const char* name, uint16_t& out) {
    if (!snapshot || !name) return false;
    uint16_t audio = 0;
    const uint32_t count = storage::directorySnapshotCount(snapshot);
    for (uint32_t i = 0; i < count; ++i) {
        if (storage::directoryItemKind(snapshot, i) != DirectoryItemKind::Audio) continue;
        const char* item = storage::directoryItemName(snapshot, i);
        if (item && strcmp(item, name) == 0) {
            out = audio;
            return true;
        }
        ++audio;
    }
    return false;
}

bool audioPathAt(const DirectorySnapshot* snapshot, uint16_t audio_index, char* out, size_t cap) {
    if (!snapshot || !out) return false;
    uint16_t audio = 0;
    const uint32_t count = storage::directorySnapshotCount(snapshot);
    for (uint32_t i = 0; i < count; ++i) {
        if (storage::directoryItemKind(snapshot, i) != DirectoryItemKind::Audio) continue;
        if (audio == audio_index)
            return storage::directoryItemPath(snapshot, i, out, cap) == DirectoryStatus::Ok;
        ++audio;
    }
    return false;
}

bool beginNextSibling(const char* folder) {
    if (isRoot(folder)) {
        // Repeat All wraps once to the first playable from the card root.
        // Repeat All один раз возвращается к первому playable от корня карты.
        if (g.repeat == SdRepeatMode::All && !g.wrapped_all) {
            g.wrapped_all = true;
            return beginFirstInSubtree("/");
        }
        finishEnd();
        return true;
    }
    char parent[kMaxMediaPathBytes + 1];
    char name[kFatLfnUtf8Bytes + 1];
    if (!parentOf(folder, parent, sizeof parent) || !baseName(folder, name, sizeof name)) {
        fail(PathPlayResult::InvalidPath);
        return false;
    }
    g.step = Step::NextSibling;
    if (!copyPath(g.after_name, sizeof g.after_name, name)) {
        fail(PathPlayResult::PathTooLong);
        return false;
    }
    return submitList(parent);
}

bool itemPath(const DirectorySnapshot* snapshot, uint32_t index, char* out, size_t cap) {
    return storage::directoryItemPath(snapshot, index, out, cap) == DirectoryStatus::Ok;
}

void leaveFolderFromSnapshot(const DirectorySnapshot* snapshot) {
    clearShuffle();
    const uint32_t count = storage::directorySnapshotCount(snapshot);
    char path[kMaxMediaPathBytes + 1];
    for (uint32_t i = 0; i < count; ++i) {
        if (storage::directoryItemKind(snapshot, i) != DirectoryItemKind::Directory) continue;
        if (!itemPath(snapshot, i, path, sizeof path)) {
            fail(PathPlayResult::PathTooLong);
            return;
        }
        releaseHeld();
        beginFirstInSubtree(path);
        return;
    }
    releaseHeld();
    beginNextSibling(g.list_path);
}

void publishFirstAudio(const DirectorySnapshot* snapshot) {
    const uint32_t count = storage::directorySnapshotCount(snapshot);
    char path[kMaxMediaPathBytes + 1];
    for (uint32_t i = 0; i < count; ++i) {
        if (storage::directoryItemKind(snapshot, i) != DirectoryItemKind::Audio) continue;
        if (!itemPath(snapshot, i, path, sizeof path)) {
            fail(PathPlayResult::PathTooLong);
            return;
        }
        publishFound(path);
        return;
    }
    finishEnd();
}

void handleShuffle(const DirectorySnapshot* snapshot, bool entering) {
    const uint16_t n = countAudio(snapshot);
    if (n > kFolderQueueShuffleMax) {
        fail(PathPlayResult::ShuffleLimit);
        return;
    }
    if (n == 0) {
        if (g.repeat == SdRepeatMode::Folder) {
            finishEnd();
            return;
        }
        leaveFolderFromSnapshot(snapshot);
        return;
    }
    const bool rebuilt = !g.shuffle_valid || !g.shuffle_perm || g.shuffle_n != n ||
                         strcmp(g.shuffle_folder, g.list_path) != 0;
    if (rebuilt) {
        if (!rebuildShuffle(n)) {
            fail(PathPlayResult::NoMemory);
            return;
        }
        if (!copyPath(g.shuffle_folder, sizeof g.shuffle_folder, g.list_path)) {
            fail(PathPlayResult::PathTooLong);
            return;
        }
        // Current track is already chosen; put it at slot 0 so the rest of the
        // pass cannot wrap back to it until every other file has played.
        // Текущий трек уже выбран; на слот 0, чтобы проход не вернулся к нему
        // пока не сыграют остальные файлы папки.
        if (!entering && g.after_name[0]) {
            uint16_t cur = 0;
            if (audioIndexByName(snapshot, g.after_name, cur)) {
                for (uint16_t i = 0; i < n; ++i) {
                    if (g.shuffle_perm[i] == cur) {
                        g.shuffle_perm[i] = g.shuffle_perm[0];
                        g.shuffle_perm[0] = cur;
                        break;
                    }
                }
            }
        }
    }
    uint16_t next_slot = 0;
    if (!entering && g.after_name[0]) {
        uint16_t cur = 0;
        if (audioIndexByName(snapshot, g.after_name, cur)) {
            for (uint16_t i = 0; i < n; ++i) {
                if (g.shuffle_perm[i] == cur) {
                    next_slot = static_cast<uint16_t>(i + 1u);
                    break;
                }
            }
        }
    }
    if (next_slot >= n) {
        if (g.repeat == SdRepeatMode::Folder) {
            if (!rebuildShuffle(n)) {
                fail(PathPlayResult::NoMemory);
                return;
            }
            next_slot = 0;
        } else {
            leaveFolderFromSnapshot(snapshot);
            return;
        }
    }
    char path[kMaxMediaPathBytes + 1];
    if (!audioPathAt(snapshot, g.shuffle_perm[next_slot], path, sizeof path)) {
        fail(PathPlayResult::PathTooLong);
        return;
    }
    publishFound(path);
}

void processSnapshot(DirectorySnapshot* snapshot) {
    if (!snapshot || storage::directorySnapshotRequestId(snapshot) != g.active_request) {
        storage::directoryRelease(snapshot);
        return;
    }
    if (storage::directorySnapshotGeneration(snapshot) != g.generation) {
        storage::directoryRelease(snapshot);
        fail(PathPlayResult::StaleMedia);
        return;
    }
    releaseHeld();
    g.held = snapshot;

    const uint32_t count = storage::directorySnapshotCount(snapshot);
    char path[kMaxMediaPathBytes + 1];

    if (g.step == Step::NextAfterFile) {
        if (g.shuffle) {
            handleShuffle(snapshot, false);
            return;
        }
        bool seen = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (storage::directoryItemKind(snapshot, i) != DirectoryItemKind::Audio) continue;
            const char* name = storage::directoryItemName(snapshot, i);
            if (!name) continue;
            if (!seen) {
                if (strcmp(name, g.after_name) == 0) seen = true;
                continue;
            }
            if (!itemPath(snapshot, i, path, sizeof path)) {
                fail(PathPlayResult::PathTooLong);
                return;
            }
            publishFound(path);
            return;
        }
        // Repeat Folder wraps to the first audio of this folder; no child dirs.
        // Repeat Folder замыкается на первом аудио этой папки; без подпапок.
        if (g.repeat == SdRepeatMode::Folder) {
            publishFirstAudio(snapshot);
            return;
        }
        // Files of this folder are done: enter child directories in sort order.
        // Файлы папки закончены: вход в подкаталоги в порядке сортировки.
        leaveFolderFromSnapshot(snapshot);
        return;
    }

    if (g.step == Step::FirstInSubtree) {
        if (g.shuffle) {
            handleShuffle(snapshot, true);
            return;
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (storage::directoryItemKind(snapshot, i) != DirectoryItemKind::Audio) continue;
            if (!itemPath(snapshot, i, path, sizeof path)) {
                fail(PathPlayResult::PathTooLong);
                return;
            }
            publishFound(path);
            return;
        }
        leaveFolderFromSnapshot(snapshot);
        return;
    }

    // NextSibling: first directory after after_name, then climb.
    bool seen = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (storage::directoryItemKind(snapshot, i) != DirectoryItemKind::Directory) continue;
        const char* name = storage::directoryItemName(snapshot, i);
        if (!name) continue;
        if (!seen) {
            if (strcmp(name, g.after_name) == 0) seen = true;
            continue;
        }
        if (!itemPath(snapshot, i, path, sizeof path)) {
            fail(PathPlayResult::PathTooLong);
            return;
        }
        releaseHeld();
        beginFirstInSubtree(path);
        return;
    }
    releaseHeld();
    if (!seen && g.after_name[0]) {
        // Named sibling vanished: continue after its sorted position is unavailable —
        // climb rather than replaying earlier branches.
        // Сосед исчез: не повторяем ранние ветки, поднимаемся выше.
    }
    beginNextSibling(g.list_path);
}

void pushHistory(const MediaRef& media) {
    if (!media.path[0] || !media.generation) return;
    if (g.history_count < kFolderQueueHistoryDepth) {
        g.history[g.history_count++] = media;
        return;
    }
    memmove(&g.history[0], &g.history[1],
            sizeof(MediaRef) * (kFolderQueueHistoryDepth - 1u));
    g.history[kFolderQueueHistoryDepth - 1u] = media;
}

bool popHistoryIfTop(const MediaRef& media) {
    if (!g.history_count) return false;
    MediaRef& top = g.history[g.history_count - 1u];
    if (top.generation != media.generation || strcmp(top.path, media.path) != 0)
        return false;
    --g.history_count;
    top = {};
    return true;
}

} // namespace

void folderQueueOnSelected(const MediaRef& media) {
    folderQueueCancel();
    g.session = media.path[0] != 0 && media.generation != 0;
    g.current = media;
    g.pending_previous = {};
    g.generation = media.generation;
    g.history_count = 0;
    g.advance_launch = false;
    g.history_launch = false;
    clearShuffle();
    clearSearch();
}

void folderQueueOnAdvanced(const MediaRef& media) {
    if (g.pending_previous.path[0] && g.pending_previous.generation)
        pushHistory(g.pending_previous);
    g.pending_previous = {};
    g.session = media.path[0] != 0 && media.generation != 0;
    g.current = media;
    g.generation = media.generation;
    g.advance_launch = false;
    g.history_launch = false;
    clearSearch();
}

void folderQueueOnHistoryPlay(const MediaRef& media) {
    popHistoryIfTop(media);
    g.pending_previous = {};
    g.session = media.path[0] != 0 && media.generation != 0;
    g.current = media;
    g.generation = media.generation;
    g.advance_launch = false;
    g.history_launch = false;
    clearSearch();
}

namespace {
void cancelSearch() {
    // Bump id so a late Playback snapshot cannot match an active search.
    // Номер запроса растёт, чтобы поздний снимок не совпал с активным поиском.
    uint32_t id = g.request_id + 1u;
    if (id == 0) id = 1u;
    g.request_id = id;
    releaseHeld();
    storage::directoryCancel(DirectoryConsumer::Playback);
    g.phase = g.session ? Phase::Cancelled : Phase::Idle;
    g.active_request = 0;
    g.search_epoch = 0;
    g.has_found = false;
    g.launch_ready = false;
    g.found = {};
    g.detail = PathPlayResult::None;
    g.advance_launch = false;
    g.history_launch = false;
    g.pending_previous = {};
}
} // namespace

void folderQueueCancel() {
    cancelSearch();
    clearShuffle();
}

void folderQueueReset() {
    folderQueueCancel();
    g.session = false;
    g.current = {};
    g.generation = 0;
    g.history_count = 0;
    g.phase = Phase::Idle;
    clearShuffle();
    freeShuffleStorage();
}

bool folderQueueStartNext(const MediaRef& after, uint32_t search_epoch,
                          SdRepeatMode repeat, bool shuffle) {
    if (!after.path[0] || !after.generation || !search_epoch) return false;
    // A new search within the session preserves the permutation. External
    // cancellation releases it, including STOP, Radio and a new selection.
    cancelSearch();
    if (!shuffle || after.generation != g.generation) clearShuffle();
    g.session = true;
    g.current = after;
    g.pending_previous = after;
    g.generation = after.generation;
    g.search_epoch = search_epoch;
    g.repeat = repeat;
    g.shuffle = shuffle;
    g.wrapped_all = false;
    g.advance_launch = true;
    g.history_launch = false;
    g.phase = Phase::Idle;
    return beginNextAfterFile(after.path);
}

void folderQueueInvalidateShuffle() {
    clearShuffle();
}

bool folderQueueCurrent(MediaRef& out) {
    if (!g.session || !g.current.path[0] || !g.current.generation) return false;
    out = g.current;
    return true;
}

uint32_t folderQueueSearchEpoch() {
    return g.search_epoch;
}

bool folderQueuePeekHistory(MediaRef& out) {
    if (!g.history_count) return false;
    out = g.history[g.history_count - 1u];
    return out.path[0] != 0;
}

FolderQueuePoll folderQueuePoll(MediaRef& out, PathPlayResult& detail) {
    out = {};
    detail = g.detail;
    if (g.phase == Phase::Idle) return FolderQueuePoll::Idle;
    if (g.phase == Phase::Cancelled) {
        g.phase = Phase::Idle;
        return FolderQueuePoll::Cancelled;
    }
    if (g.phase == Phase::End) {
        detail = PathPlayResult::EndOfFile;
        g.phase = Phase::Idle;
        return FolderQueuePoll::End;
    }
    if (g.phase == Phase::Error) {
        const PathPlayResult err = g.detail;
        g.phase = Phase::Idle;
        detail = err;
        return FolderQueuePoll::Error;
    }
    if (g.phase == Phase::Ready && g.has_found) {
        out = g.found;
        detail = PathPlayResult::None;
        return FolderQueuePoll::Found;
    }
    if (g.phase != Phase::WaitList) return FolderQueuePoll::Idle;

    DirectorySnapshot* snapshot = nullptr;
    const DirectoryStatus status = storage::directoryPoll(DirectoryConsumer::Playback, &snapshot);
    if (status == DirectoryStatus::Pending) {
        detail = PathPlayResult::SearchingNext;
        return FolderQueuePoll::Pending;
    }
    if (status != DirectoryStatus::Ok || !snapshot) {
        fail(status == DirectoryStatus::StaleMedia ? PathPlayResult::StaleMedia :
             status == DirectoryStatus::NoMemory ? PathPlayResult::NoMemory :
             status == DirectoryStatus::Unavailable ? PathPlayResult::NoMedia :
             PathPlayResult::ReadFailed);
        detail = g.detail;
        g.phase = Phase::Idle;
        return FolderQueuePoll::Error;
    }
    processSnapshot(snapshot);
    detail = g.detail;
    if (g.phase == Phase::Ready && g.has_found) {
        out = g.found;
        return FolderQueuePoll::Found;
    }
    if (g.phase == Phase::WaitList) return FolderQueuePoll::Pending;
    if (g.phase == Phase::End) {
        g.phase = Phase::Idle;
        detail = PathPlayResult::EndOfFile;
        return FolderQueuePoll::End;
    }
    if (g.phase == Phase::Error) {
        const PathPlayResult err = g.detail;
        g.phase = Phase::Idle;
        detail = err;
        return FolderQueuePoll::Error;
    }
    return FolderQueuePoll::Pending;
}

bool folderQueueIsSearching() {
    return g.phase == Phase::WaitList ||
           (g.phase == Phase::Ready && g.has_found);
}

bool folderQueueClaimFound(uint32_t expected_epoch, MediaRef& out) {
    if (!g.launch_ready || !g.has_found || !g.search_epoch) return false;
    if (expected_epoch != g.search_epoch) return false;
    out = g.found;
    g.launch_ready = false;
    g.has_found = false;
    g.phase = Phase::Idle;
    g.advance_launch = true;
    g.history_launch = false;
    // Keep search_epoch until OnAdvanced/Cancel so admit can re-check it.
    // search_epoch сохраняется до OnAdvanced/Cancel для повторной проверки admit.
    return true;
}

bool folderQueueConsumeAdvanceFlag() {
    const bool advance = g.advance_launch;
    g.advance_launch = false;
    return advance;
}

bool folderQueueConsumeHistoryFlag() {
    const bool history = g.history_launch;
    g.history_launch = false;
    return history;
}

void folderQueueMarkHistoryLaunch() {
    g.history_launch = true;
    g.advance_launch = false;
}

void folderQueueClearLaunchFlags() {
    g.advance_launch = false;
    g.history_launch = false;
}

#if defined(YORADIO_DIRECTORY_HOST)
void folderQueueHostReset() {
    folderQueueCancel();
    freeShuffleStorage();
    g = State{};
    g_host_fail_shuffle_alloc = false;
}

void folderQueueHostFailShuffleAlloc(bool fail) { g_host_fail_shuffle_alloc = fail; }
size_t folderQueueHostShuffleBytes() { return g.shuffle_cap * sizeof(uint16_t); }

void folderQueueHostSetShuffleSeed(uint32_t seed) {
    g.rng = seed ? seed : 1u;
}
#endif

}}
