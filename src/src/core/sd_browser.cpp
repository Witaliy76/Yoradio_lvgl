// Binds one WebUI list request to one Directory Service result.
// Network code sends the copied JSON; it does not wait for the scan.
// Связывает один запрос списка WebUI с одним результатом Directory Service.
// Сеть отправляет уже скопированный JSON и не ждёт окончания прохода.
#include "sd_browser.h"
#include "../platform/storage/directory_names.h"
#include "../platform/storage/storage_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#if !defined(YORADIO_DIRECTORY_HOST)
#include "esp_attr.h"
#define S07_EXT_BSS EXT_RAM_BSS_ATTR
#else
#define S07_EXT_BSS
#endif

#if defined(YORADIO_DIRECTORY_HOST)
static bool g_force_media = false;
static bool g_force_ready = false;
static uint64_t g_force_gen = 0;
void sdBrowserHostMedia(bool force, bool ready, uint64_t generation) {
    g_force_media = force;
    g_force_ready = ready;
    g_force_gen = generation;
}
#endif

namespace sd_browser_detail {
constexpr uint32_t kPageItems = 40;
constexpr size_t kJsonCap = 12u * 1024u;
// One row must hold a legal name, its full path, and the worst JSON escape
// (`\uXXXX` is six bytes per source byte). A short scratch is not NoMemory.
// Одна строка вмещает допустимое имя, полный путь и худшее экранирование
// (`\uXXXX` — шесть байт на исходный байт). Короткий scratch — не NoMemory.
constexpr size_t kJsonEscape = 6;
constexpr size_t kItemFixed = 24;
constexpr size_t kItemCap = kItemFixed
    + (yoradio::storage::kFatLfnUtf8Bytes + yoradio::storage::kMaxMediaPathBytes) * kJsonEscape
    + 1u;
constexpr size_t kPathCap = yoradio::storage::kMaxMediaPathBytes + 1u;

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
// The only WebUI identity. Not a client registry.
// Единственная личность WebUI. Это не реестр клиентов.
struct Slot {
    uint32_t client = 0;
    uint32_t request = 0;
    uint64_t generation = 0;
    bool running = false;
    char path[kPathCap]{};
};
S07_EXT_BSS Slot g_slot;

struct Media {
    bool ready = false;
    uint64_t generation = 0;
};

Media currentMedia() {
    Media media;
#if defined(YORADIO_DIRECTORY_HOST)
    if (g_force_media) {
        media.ready = g_force_ready;
        media.generation = g_force_gen;
        return media;
    }
#endif
    const auto status = yoradio::storage::StorageOwner::instance().status();
    media.ready = status.ready;
    media.generation = status.generation;
    return media;
}

Slot loadSlot() {
    portENTER_CRITICAL(&g_lock);
    const Slot copy = g_slot;
    portEXIT_CRITICAL(&g_lock);
    return copy;
}
bool sameSlot(const Slot& slot, uint32_t client, uint32_t request, uint64_t generation, const char* path) {
    return slot.client == client && slot.request == request && slot.generation == generation
        && path && strcmp(slot.path, path) == 0;
}
bool claimStart(uint32_t client, uint32_t request, uint64_t generation, const char* path) {
    const size_t n = strlen(path);
    if (n >= kPathCap) return false;
    portENTER_CRITICAL(&g_lock);
    if (g_slot.running) {
        portEXIT_CRITICAL(&g_lock);
        return false;
    }
    g_slot.client = client;
    g_slot.request = request;
    g_slot.generation = generation;
    memcpy(g_slot.path, path, n + 1u);
    g_slot.running = true;
    portEXIT_CRITICAL(&g_lock);
    return true;
}
void finishRunning(uint32_t client, uint32_t request) {
    portENTER_CRITICAL(&g_lock);
    if (g_slot.running && g_slot.client == client && g_slot.request == request)
        g_slot.running = false;
    portEXIT_CRITICAL(&g_lock);
}

struct Writer {
    char* cur = nullptr;
    char* end = nullptr;
    bool ok = true;
    void add(const char* text) {
        if (!ok || !text) return;
        const size_t n = strlen(text);
        if (cur + n >= end) { ok = false; return; }
        memcpy(cur, text, n);
        cur += n;
    }
    void addChar(char ch) {
        if (!ok) return;
        if (cur + 1 >= end) { ok = false; return; }
        *cur++ = ch;
    }
    void addUnsigned(unsigned value) {
        char number[16];
        snprintf(number, sizeof number, "%u", value);
        add(number);
    }
};

bool appendJsonString(Writer& out, const char* text) {
    out.addChar('"');
    if (!text) text = "";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        char scratch[8];
        if (*p == '"' || *p == '\\') {
            scratch[0] = '\\';
            scratch[1] = static_cast<char>(*p);
            scratch[2] = 0;
            out.add(scratch);
        } else if (*p == '<' || *p == '>' || *p < 0x20u) {
            snprintf(scratch, sizeof scratch, "\\u%04x", *p);
            out.add(scratch);
        } else {
            out.addChar(static_cast<char>(*p));
        }
        if (!out.ok) return false;
    }
    out.addChar('"');
    return out.ok;
}

SdBrowserReply simple(SdBrowserState state, const char* name, uint32_t request_id,
                      yoradio::storage::DirectoryStatus detail) {
    SdBrowserReply reply;
    reply.state = state;
    reply.detail = detail;
    reply.json = static_cast<char*>(malloc(96));
    if (!reply.json) return reply;
    snprintf(reply.json, 96, "{\"state\":\"%s\",\"req\":%u}", name, static_cast<unsigned>(request_id));
    return reply;
}

const char* errorCode(yoradio::storage::DirectoryStatus status) {
    using yoradio::storage::DirectoryStatus;
    switch (status) {
    case DirectoryStatus::InvalidPath:
    case DirectoryStatus::PathTooLong: return "path";
    case DirectoryStatus::NameTooLong: return "name";
    case DirectoryStatus::StaleMedia: return "stale";
    case DirectoryStatus::ResourceLimit: return "limit";
    case DirectoryStatus::NoMemory: return "memory";
    case DirectoryStatus::OpenFailed: return "open";
    case DirectoryStatus::ReadFailed: return "read";
    case DirectoryStatus::CloseFailed: return "close";
    case DirectoryStatus::Unavailable: return "unavailable";
    default: return "read";
    }
}

SdBrowserReply errorReply(uint32_t request_id, yoradio::storage::DirectoryStatus status) {
    SdBrowserReply reply;
    reply.state = SdBrowserState::Error;
    reply.detail = status;
    reply.json = static_cast<char*>(malloc(128));
    if (!reply.json) return reply;
    snprintf(reply.json, 128, "{\"state\":\"error\",\"req\":%u,\"code\":\"%s\"}",
             static_cast<unsigned>(request_id), errorCode(status));
    return reply;
}

bool parentOf(const char* path, char* out, size_t cap) {
    if (!path || path[0] != '/' || path[1] == 0 || cap < 2) return false;
    const char* slash = strrchr(path, '/');
    if (!slash || slash == path) {
        out[0] = '/';
        out[1] = 0;
        return true;
    }
    const size_t n = static_cast<size_t>(slash - path);
    if (n + 1 > cap) return false;
    memcpy(out, path, n);
    out[n] = 0;
    return true;
}

SdBrowserReply submitNew(uint32_t client, uint32_t request_id, const char* path, uint64_t generation) {
    if (!claimStart(client, request_id, generation, path)) {
        const Slot again = loadSlot();
        if (sameSlot(again, client, request_id, generation, path) && again.running)
            return simple(SdBrowserState::Pending, "pending", request_id, yoradio::storage::DirectoryStatus::Pending);
        return simple(SdBrowserState::Busy, "busy", request_id, yoradio::storage::DirectoryStatus::Pending);
    }
    yoradio::storage::DirectoryQuery query;
    query.consumer = yoradio::storage::DirectoryConsumer::WebUi;
    query.request_id = request_id;
    query.generation = generation;
    query.path = path;
    const auto status = yoradio::storage::directorySubmit(query);
    if (status != yoradio::storage::DirectoryStatus::Ok) {
        finishRunning(client, request_id);
        return errorReply(request_id, status);
    }
    return simple(SdBrowserState::Pending, "pending", request_id, yoradio::storage::DirectoryStatus::Pending);
}

enum class RowResult : uint8_t { Ok, Overflow, Rejected };

RowResult writeItem(char* item, char* joined, size_t joined_cap,
                    const yoradio::storage::DirectorySnapshot* snapshot, uint32_t index) {
    using namespace yoradio::storage;
    const char* name = directoryItemName(snapshot, index);
    if (!name || directoryItemPath(snapshot, index, joined, joined_cap) != DirectoryStatus::Ok)
        return RowResult::Rejected;
    Writer out;
    out.cur = item;
    out.end = item + kItemCap;
    out.add(directoryItemKind(snapshot, index) == DirectoryItemKind::Directory
                ? "{\"k\":\"d\",\"n\":" : "{\"k\":\"f\",\"n\":");
    appendJsonString(out, name);
    out.add(",\"p\":");
    appendJsonString(out, joined);
    out.addChar('}');
    if (!out.ok || out.cur >= item + kItemCap) return RowResult::Overflow;
    *out.cur = 0;
    return RowResult::Ok;
}

SdBrowserReply copyPage(yoradio::storage::DirectorySnapshot* snapshot, uint32_t request_id, uint32_t offset) {
    using namespace yoradio::storage;
    char* buffer = static_cast<char*>(malloc(kJsonCap));
    if (!buffer) {
        directoryRelease(snapshot);
        return errorReply(request_id, DirectoryStatus::NoMemory);
    }
    Writer out;
    out.cur = buffer;
    out.end = buffer + kJsonCap;
    const char* path = directorySnapshotPath(snapshot);
    const uint32_t total = directorySnapshotCount(snapshot);
    const unsigned long long generation = directorySnapshotGeneration(snapshot);
    out.add("{\"state\":\"");
    out.add(total == 0 ? "empty" : "ready");
    out.add("\",\"req\":");
    out.addUnsigned(request_id);
    out.add(",\"gen\":");
    char gen[32];
    snprintf(gen, sizeof gen, "%llu", generation);
    out.add(gen);
    out.add(",\"path\":");
    appendJsonString(out, path ? path : "");
    // One heap scratch: the network task must not hold a path plus a JSON item.
    // Один временный блок в куче: задаче сети не нужен путь и запись JSON на стеке.
    char* scratch = static_cast<char*>(malloc(kItemCap + kPathCap));
    if (!scratch) {
        free(buffer);
        directoryRelease(snapshot);
        return errorReply(request_id, DirectoryStatus::NoMemory);
    }
    char* item = scratch;
    char* pathbuf = scratch + kItemCap;
    if (parentOf(path, pathbuf, kPathCap)) {
        out.add(",\"up\":");
        appendJsonString(out, pathbuf);
    } else {
        out.add(",\"up\":null");
    }
    out.add(",\"total\":");
    out.addUnsigned(total);
    out.add(",\"offset\":");
    out.addUnsigned(offset);
    out.add(",\"items\":[");
    uint32_t produced = 0;
    uint32_t index = offset;
    bool rejected = false;
    bool overflow = false;
    while (out.ok && index < total && produced < kPageItems) {
        const RowResult row = writeItem(item, pathbuf, kPathCap, snapshot, index);
        if (row == RowResult::Rejected) { rejected = true; break; }
        if (row == RowResult::Overflow) { overflow = true; break; }
        const size_t n = strlen(item);
        const size_t tail = 48;
        if (out.cur + n + 1 + tail >= out.end) {
            if (produced == 0) overflow = true;
            break;
        }
        if (produced) out.addChar(',');
        out.add(item);
        ++produced;
        ++index;
    }
    if (out.ok && !rejected && !(overflow && produced == 0)) {
        out.add("],\"count\":");
        out.addUnsigned(produced);
        out.add(",\"more\":");
        out.add(index < total ? "true" : "false");
        out.add(",\"next\":");
        out.addUnsigned(index);
        out.addChar('}');
    }
    free(scratch);
    directoryRelease(snapshot);
    if (rejected) {
        free(buffer);
        return errorReply(request_id, DirectoryStatus::PathTooLong);
    }
    // A row that does not fit the fixed page is a limit, not a failed malloc.
    // Строка, не влезшая в фиксированную страницу, — это предел, не ошибка malloc.
    if ((overflow && produced == 0) || !out.ok || out.cur >= buffer + kJsonCap) {
        free(buffer);
        return errorReply(request_id, DirectoryStatus::ResourceLimit);
    }
    *out.cur = 0;
    SdBrowserReply reply;
    reply.state = total == 0 ? SdBrowserState::Empty : SdBrowserState::Ready;
    reply.detail = DirectoryStatus::Ok;
    reply.json = buffer;
    return reply;
}

bool snapshotMine(const yoradio::storage::DirectorySnapshot* snapshot, uint32_t request_id,
                  uint64_t generation, const char* path) {
    using namespace yoradio::storage;
    return snapshot
        && directorySnapshotRequestId(snapshot) == request_id
        && directorySnapshotGeneration(snapshot) == generation
        && directorySnapshotPath(snapshot)
        && strcmp(directorySnapshotPath(snapshot), path) == 0;
}

SdBrowserReply list(uint32_t client, uint32_t request_id, uint32_t offset, const char* path, bool start) {
    using namespace yoradio::storage;
    if (!client || !request_id || !path || strlen(path) >= kPathCap)
        return errorReply(request_id, DirectoryStatus::InvalidPath);
    const Media media = currentMedia();
    if (!media.ready || media.generation == 0)
        return simple(SdBrowserState::Unavailable, "unavailable", request_id, DirectoryStatus::Unavailable);

    Slot slot = loadSlot();
    DirectorySnapshot* snapshot = nullptr;
    const DirectoryStatus polled = directoryPoll(DirectoryConsumer::WebUi, &snapshot);
    if (slot.running && polled != DirectoryStatus::Pending) {
        finishRunning(slot.client, slot.request);
        slot.running = false;
    }
    const bool mine = sameSlot(slot, client, request_id, media.generation, path);
    const bool snap_mine = snapshotMine(snapshot, request_id, media.generation, path);

    if (mine && polled == DirectoryStatus::Pending) {
        directoryRelease(snapshot);
        return simple(SdBrowserState::Pending, "pending", request_id, DirectoryStatus::Pending);
    }
    if (mine && polled == DirectoryStatus::Ok && snap_mine && !slot.running) {
        const Media again = currentMedia();
        if (!again.ready || again.generation == 0 || again.generation != media.generation) {
            directoryRelease(snapshot);
            return simple(SdBrowserState::Unavailable, "unavailable", request_id, DirectoryStatus::Unavailable);
        }
        return copyPage(snapshot, request_id, offset);
    }
    // The owner of a terminal error still receives it. A poll does not scan again.
    // Владелец конечной ошибки получает именно её. Poll не начинает новый проход.
    if (mine && polled != DirectoryStatus::Ok && polled != DirectoryStatus::Pending) {
        directoryRelease(snapshot);
        finishRunning(client, request_id);
        return errorReply(request_id, polled);
    }
    directoryRelease(snapshot);
    if (!start)
        return simple(SdBrowserState::Cancelled, "cancelled", request_id, DirectoryStatus::Pending);
    if (slot.running)
        return simple(mine ? SdBrowserState::Pending : SdBrowserState::Busy,
                      mine ? "pending" : "busy", request_id, DirectoryStatus::Pending);
    return submitNew(client, request_id, path, media.generation);
}
}

SdBrowserReply sdBrowserList(uint32_t client, uint32_t request_id, uint32_t offset,
                             const char* path, bool start) {
    return sd_browser_detail::list(client, request_id, offset, path, start);
}

void sdBrowserRelease(SdBrowserReply& reply) {
    free(reply.json);
    reply.json = nullptr;
}
