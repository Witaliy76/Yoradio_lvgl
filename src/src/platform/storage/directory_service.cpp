// Background per-folder listing. One worker, created on the first request.
// Not the JPEG worker. Snapshots are immutable and reference-counted.
// Фоновое чтение одной папки. Один worker, создаётся с первым запросом.
// Это не JPEG worker. Снимки неизменяемы и живут по счётчику ссылок.
#include "directory_service.h"
#if defined(YORADIO_S07_STORAGE_ENABLED) && YORADIO_S07_STORAGE_ENABLED
#include "directory_names.h"
#include "storage_dir.h"
#include "storage_owner.h"
#include "../../utils/utf8.h"
#include <string.h>
#include <stdlib.h>
#if defined(YORADIO_DIRECTORY_HOST)
#include <malloc.h>
#include "freertos/FreeRTOS.h"
#define S07_EXT_BSS
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
// Large BSS (slots / in-flight path) needs EXT_RAM BSS → PSRAM.
// Крупный BSS (слоты / путь) требует EXT_RAM BSS → PSRAM.
#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "S07 Directory Service: CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY must be y (sdkconfig.defaults)."
#endif
#define S07_EXT_BSS EXT_RAM_BSS_ATTR
#endif

namespace yoradio { namespace storage {

static_assert(kDirectoryConsumerSlots == 3, "WebUI, LVGL and Playback each have one slot");
static_assert(kDirectoryWorkerStackBytes >= 12u * 1024u, "reader frames need 12 KiB");
#if !defined(YORADIO_DIRECTORY_HOST)
// ESP-IDF: ulStackDepth is BYTES; StackType_t is uint8_t on Xtensa.
// ESP-IDF: ulStackDepth — в БАЙТАХ; StackType_t на Xtensa — uint8_t.
static_assert(sizeof(StackType_t) == 1,
              "S07Dir stack depth is passed in bytes; StackType_t must be 1 byte");
#endif

struct Guard {
    Guard() { portENTER_CRITICAL(&lock); }
    ~Guard() { portEXIT_CRITICAL(&lock); }
    static portMUX_TYPE lock;
};
portMUX_TYPE Guard::lock = portMUX_INITIALIZER_UNLOCKED;

struct DirectorySnapshot {
    uint32_t refs = 0;
    uint32_t bytes = 0;
    uint32_t request_id = 0;
    uint32_t count = 0;
    uint64_t generation = 0;
    DirectoryConsumer consumer = DirectoryConsumer::WebUi;
    uint16_t path_len = 0;
};

struct Slot {
    uint32_t ticket = 0;
    uint32_t pending_ticket = 0;
    uint32_t running_ticket = 0;
    uint32_t request_id = 0;
    uint64_t generation = 0;
    char path[kMaxMediaPathBytes + 1]{};
    bool has_pending = false;
    bool running = false;
    bool result_ready = false;
    DirectoryStatus terminal = DirectoryStatus::Pending;
    DirectorySnapshot* published = nullptr;
};

struct Job {
    DirectoryConsumer consumer = DirectoryConsumer::WebUi;
    uint32_t ticket = 0;
    uint32_t request_id = 0;
    uint64_t generation = 0;
};

struct Build {
    uint8_t* data = nullptr;
    size_t used = 0;
    size_t cap = 0;
    uint32_t* offsets = nullptr;
    size_t count = 0;
    size_t offset_cap = 0;
    size_t charged = 0;
    bool budget = false;
    bool oom = false;
    bool cancelled = false;
    bool charge_transferred = false;
    Job job;
    Build() = default;
    Build(const Build&) = delete;
    Build& operator=(const Build&) = delete;
    ~Build();
};

S07_EXT_BSS Slot g_slots[kDirectoryConsumerSlots];
size_t g_cursor = 0;
size_t g_charged = 0;
S07_EXT_BSS char g_inflight[kMaxMediaPathBytes + 1];
bool g_creating = false;
#if defined(YORADIO_DIRECTORY_HOST)
bool g_worker_ready = false;
int g_host_fail_creates = 0;
int g_host_create_attempts = 0;
size_t g_host_live = 0;
size_t g_host_peak = 0;
struct HostAlloc { void* p; size_t n; };
HostAlloc g_host_allocs[64]{};
bool g_host_alloc_overflow = false;
#else
TaskHandle_t g_task = nullptr;
// TCB + task stack stay in internal DRAM (~12 KiB). FatFs/SPI on S07Dir was not
// separately proven safe on a PSRAM stack; large catalog data remains in PSRAM heap/BSS.
// TCB и стек задачи — во внутреннем DRAM (~12 КиБ). FatFs/SPI на S07Dir не доказаны
// отдельно для стека в PSRAM; крупные каталоги остаются в PSRAM heap/BSS.
static StaticTask_t g_worker_tcb;
static StackType_t g_worker_stack[kDirectoryWorkerStackBytes / sizeof(StackType_t)];
static_assert(sizeof(g_worker_stack) == kDirectoryWorkerStackBytes,
              "S07Dir stack buffer must equal kDirectoryWorkerStackBytes");
#endif

size_t align8(size_t n) { return (n + 7u) & ~size_t{7}; }

void* heapAlloc(size_t n) {
    n = align8(n);
    if (!n) return nullptr;
#if defined(YORADIO_DIRECTORY_HOST)
    void* p = _aligned_malloc(n, 8);
    if (!p) return nullptr;
    bool stored = false;
    for (HostAlloc& item : g_host_allocs) {
        if (item.p) continue;
        item.p = p;
        item.n = n;
        stored = true;
        break;
    }
    g_host_alloc_overflow = g_host_alloc_overflow || !stored;
    g_host_live += n;
    if (g_host_live > g_host_peak) g_host_peak = g_host_live;
    return p;
#else
    return heap_caps_aligned_alloc(8, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}
void heapFree(void* p) {
    if (!p) return;
#if defined(YORADIO_DIRECTORY_HOST)
    for (HostAlloc& item : g_host_allocs) {
        if (item.p != p) continue;
        g_host_live -= item.n;
        item.p = nullptr;
        item.n = 0;
        break;
    }
    _aligned_free(p);
#else
    heap_caps_free(p);
#endif
}

size_t slotIndex(DirectoryConsumer consumer) {
    return static_cast<size_t>(consumer);
}
bool knownConsumer(DirectoryConsumer consumer) {
    return slotIndex(consumer) < kDirectoryConsumerSlots;
}

size_t prefixBytes(uint16_t path_len) {
    return (sizeof(DirectorySnapshot) + static_cast<size_t>(path_len) + 1u + 3u) & ~size_t{3};
}
uint8_t* snapshotBytes(DirectorySnapshot* snapshot) {
    return reinterpret_cast<uint8_t*>(snapshot);
}
const uint8_t* snapshotBytes(const DirectorySnapshot* snapshot) {
    return reinterpret_cast<const uint8_t*>(snapshot);
}

bool stillCurrent(const Job& job) {
    Guard guard;
    const Slot& slot = g_slots[slotIndex(job.consumer)];
    return slot.ticket == job.ticket;
}
bool mediaMatches(const Job& job) {
    const StorageStatus status = StorageOwner::instance().status();
    return status.ready && status.generation == job.generation;
}

void uncharge(size_t bytes) {
    if (g_charged >= bytes) g_charged -= bytes;
    else g_charged = 0;
}

Build::~Build() {
    heapFree(data);
    heapFree(offsets);
    data = nullptr;
    offsets = nullptr;
    if (!charge_transferred && charged) {
        Guard guard;
        uncharge(charged);
        charged = 0;
    }
}

size_t blockCharge(size_t bytes, bool live) {
    return live ? align8(bytes) : 0;
}

// Old and new blocks are both charged while the copy exists. The final charge
// drops to the surviving blocks only after the old ones are freed.
// Пока копия существует, в бюджете и старый, и новый блок. После освобождения
// старых остаётся плата только за те, что пережили копирование.
bool reserve(Build& build, size_t data_cap, size_t offset_cap) {
    if (data_cap < build.cap) data_cap = build.cap;
    if (offset_cap < build.offset_cap) offset_cap = build.offset_cap;
    if (data_cap == build.cap && offset_cap == build.offset_cap) return true;
    const bool replace_data = data_cap != build.cap;
    const bool replace_offsets = offset_cap != build.offset_cap;
    const size_t old_data = blockCharge(build.cap, build.data != nullptr);
    const size_t old_offsets = blockCharge(build.offset_cap * sizeof(uint32_t), build.offsets != nullptr);
    const size_t new_data = blockCharge(data_cap, true);
    const size_t new_offsets = blockCharge(offset_cap * sizeof(uint32_t), true);
    const size_t peak = (replace_data && build.data ? old_data + new_data : new_data)
                      + (replace_offsets && build.offsets ? old_offsets + new_offsets : new_offsets);
    const size_t final_charge = new_data + new_offsets;
    size_t previous = 0;
    {
        Guard guard;
        if (g_charged - build.charged + peak > kDirectorySnapshotBudget) {
            build.budget = true;
            return false;
        }
        previous = build.charged;
        g_charged = g_charged - build.charged + peak;
        build.charged = peak;
    }
    uint8_t* data = build.data;
    uint32_t* offsets = build.offsets;
    bool ok = true;
    if (replace_data) {
        data = static_cast<uint8_t*>(heapAlloc(data_cap));
        if (!data) ok = false;
        else if (build.used) memcpy(data, build.data, build.used);
    }
    if (ok && replace_offsets) {
        offsets = static_cast<uint32_t*>(heapAlloc(offset_cap * sizeof(uint32_t)));
        if (!offsets) ok = false;
        else if (build.count) memcpy(offsets, build.offsets, build.count * sizeof(uint32_t));
    }
    if (!ok) {
        if (data != build.data) heapFree(data);
        if (offsets != build.offsets) heapFree(offsets);
        Guard guard;
        g_charged = g_charged - build.charged + previous;
        build.charged = previous;
        build.oom = true;
        return false;
    }
    if (data != build.data) {
        heapFree(build.data);
        build.data = data;
        build.cap = data_cap;
    }
    if (offsets != build.offsets) {
        heapFree(build.offsets);
        build.offsets = offsets;
        build.offset_cap = offset_cap;
    }
    Guard guard;
    g_charged = g_charged - build.charged + final_charge;
    build.charged = final_charge;
    return true;
}

bool ensure(Build& build, size_t data_need, size_t offset_need) {
    if (data_need <= build.cap && offset_need <= build.offset_cap) return true;
    size_t data_cap = build.cap;
    size_t offset_cap = build.offset_cap;
    if (data_need > build.cap) {
        data_cap = build.cap ? build.cap * 2u : 2048u;
        if (data_cap < data_need) data_cap = data_need;
    }
    if (offset_need > build.offset_cap) {
        offset_cap = build.offset_cap ? build.offset_cap * 2u : 16u;
        if (offset_cap < offset_need) offset_cap = offset_need;
    }
    if (reserve(build, data_cap, offset_cap)) return true;
    if (build.oom) return false;
    build.budget = false;
    if (data_need > build.cap) data_cap = data_need;
    else data_cap = build.cap;
    if (offset_need > build.offset_cap) offset_cap = offset_need;
    else offset_cap = build.offset_cap;
    return reserve(build, data_cap, offset_cap);
}

bool appendItem(Build& build, uint8_t kind, const char* name, size_t length) {
    // kind, length lo, length hi, pad, name bytes, NUL.
    // kind, младший и старший байт длины, выравнивание, байты имени, NUL.
    const size_t add = 5u + length;
    if (!ensure(build, build.used + add, build.count + 1u)) return false;
    uint8_t* item = build.data + build.used;
    item[0] = kind;
    item[1] = static_cast<uint8_t>(length & 0xffu);
    item[2] = static_cast<uint8_t>((length >> 8) & 0xffu);
    item[3] = 0;
    memcpy(item + 4, name, length);
    item[4 + length] = 0;
    build.offsets[build.count] = static_cast<uint32_t>(build.used);
    build.used += add;
    ++build.count;
    return true;
}

bool endsWithIcase(const char* name, const char* suffix) {
    const size_t length = strlen(name);
    const size_t suffix_len = strlen(suffix);
    if (suffix_len > length) return false;
    for (size_t i = 0; i < suffix_len; ++i) {
        unsigned char left = static_cast<unsigned char>(name[length - suffix_len + i]);
        unsigned char right = static_cast<unsigned char>(suffix[i]);
        if (left >= 'A' && left <= 'Z') left = static_cast<unsigned char>(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = static_cast<unsigned char>(right - 'A' + 'a');
        if (left != right) return false;
    }
    return true;
}
// Same pair as PathPlayRequest. No extra formats in this slice.
// Та же пара, что у PathPlayRequest. Новых форматов в этом срезе нет.
bool isSupportedAudio(const char* name) {
    return endsWithIcase(name, ".mp3") || endsWithIcase(name, ".flac");
}

bool isDigit(unsigned char ch) { return ch >= '0' && ch <= '9'; }

uint32_t foldWeight(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') cp = cp - 'A' + 'a';
    else if (cp >= 0x0410u && cp <= 0x042Fu) cp += 0x20u;
    else if (cp == 0x0401u) cp = 0x0451u; // Ё -> ё
    // ё sits between е (U+0435) and ж (U+0436). Even weights keep the gap.
    // ё стоит между е (U+0435) и ж (U+0436). Чётные веса оставляют этот промежуток.
    if (cp == 0x0451u) return (0x0435u << 1) + 1u;
    return cp << 1;
}

int compareDigits(const char* a, size_t a_len, size_t& ia,
                  const char* b, size_t b_len, size_t& ib) {
    const size_t a0 = ia;
    const size_t b0 = ib;
    while (ia < a_len && a[ia] == '0') ++ia;
    while (ib < b_len && b[ib] == '0') ++ib;
    const size_t a_sig = ia;
    const size_t b_sig = ib;
    while (ia < a_len && isDigit(static_cast<unsigned char>(a[ia]))) ++ia;
    while (ib < b_len && isDigit(static_cast<unsigned char>(b[ib]))) ++ib;
    const size_t a_digits = ia - a_sig;
    const size_t b_digits = ib - b_sig;
    if (a_digits != b_digits) return a_digits < b_digits ? -1 : 1;
    if (a_digits) {
        const int cmp = memcmp(a + a_sig, b + b_sig, a_digits);
        if (cmp != 0) return cmp < 0 ? -1 : 1;
    }
    const size_t a_total = ia - a0;
    const size_t b_total = ib - b0;
    if (a_total != b_total) return a_total < b_total ? -1 : 1;
    return 0;
}

int compareNatural(const char* a, size_t a_len, const char* b, size_t b_len) {
    size_t ia = 0;
    size_t ib = 0;
    while (ia < a_len && ib < b_len) {
        const bool da = isDigit(static_cast<unsigned char>(a[ia]));
        const bool db = isDigit(static_cast<unsigned char>(b[ib]));
        if (da && db) {
            const int cmp = compareDigits(a, a_len, ia, b, b_len, ib);
            if (cmp != 0) return cmp;
            continue;
        }
        const text::Utf8Unit ua = text::decodeUtf8(a, a_len, ia);
        const text::Utf8Unit ub = text::decodeUtf8(b, b_len, ib);
        if (!ua.valid || !ub.valid) {
            const unsigned char ca = static_cast<unsigned char>(a[ia]);
            const unsigned char cb = static_cast<unsigned char>(b[ib]);
            if (ca != cb) return ca < cb ? -1 : 1;
            ++ia;
            ++ib;
            continue;
        }
        const uint32_t wa = foldWeight(ua.codepoint);
        const uint32_t wb = foldWeight(ub.codepoint);
        if (wa != wb) return wa < wb ? -1 : 1;
        ia += ua.width;
        ib += ub.width;
    }
    if (ia != a_len || ib != b_len) return ia == a_len ? -1 : 1;
    const int cmp = memcmp(a, b, a_len);
    return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
}

const char* nameAt(const Build& build, uint32_t offset, size_t& length, uint8_t& kind) {
    const uint8_t* item = build.data + offset;
    kind = item[0];
    length = static_cast<size_t>(item[1]) | (static_cast<size_t>(item[2]) << 8);
    return reinterpret_cast<const char*>(item + 4);
}

int compareOffsets(const Build& build, uint32_t left, uint32_t right) {
    size_t left_len = 0;
    size_t right_len = 0;
    uint8_t left_kind = 0;
    uint8_t right_kind = 0;
    const char* left_name = nameAt(build, left, left_len, left_kind);
    const char* right_name = nameAt(build, right, right_len, right_kind);
    if (left_kind != right_kind) return left_kind < right_kind ? -1 : 1;
    return compareNatural(left_name, left_len, right_name, right_len);
}

void siftDown(Build& build, size_t count, size_t root) {
    for (;;) {
        const size_t left = root * 2u + 1u;
        if (left >= count) break;
        size_t best = root;
        if (compareOffsets(build, build.offsets[left], build.offsets[best]) > 0) best = left;
        const size_t right = left + 1u;
        if (right < count &&
            compareOffsets(build, build.offsets[right], build.offsets[best]) > 0) best = right;
        if (best == root) break;
        const uint32_t tmp = build.offsets[root];
        build.offsets[root] = build.offsets[best];
        build.offsets[best] = tmp;
        root = best;
    }
}
void sortBuild(Build& build) {
    if (build.count < 2) return;
    size_t start = build.count / 2u;
    while (start > 0) {
        --start;
        siftDown(build, build.count, start);
    }
    size_t end = build.count;
    while (end > 1) {
        --end;
        const uint32_t tmp = build.offsets[0];
        build.offsets[0] = build.offsets[end];
        build.offsets[end] = tmp;
        siftDown(build, end, 0);
    }
}

bool visitEntry(const DirEntry& entry, void* user) {
    Build& build = *static_cast<Build*>(user);
    // Between FatFs calls. The DIR opened by this scan is closed by listDirectory.
    // Между вызовами FatFs. DIR этого прохода закрывает listDirectory.
    if (!stillCurrent(build.job)) {
        build.cancelled = true;
        return false;
    }
    if (!entry.directory && !isSupportedAudio(entry.name)) return true;
    if (!appendItem(build, entry.directory ? 0 : 1, entry.name, strlen(entry.name))) return false;
    return true;
}

const uint8_t* itemPointer(const DirectorySnapshot* snapshot, uint32_t index) {
    if (!snapshot || index >= snapshot->count) return nullptr;
    const size_t prefix = prefixBytes(snapshot->path_len);
    const uint8_t* base = snapshotBytes(snapshot);
    const uint32_t* table = reinterpret_cast<const uint32_t*>(base + prefix);
    const uint32_t offset = table[index];
    if (offset >= snapshot->bytes) return nullptr;
    return base + offset;
}

void discardSnapshot(DirectorySnapshot* snapshot) {
    if (!snapshot) return;
    {
        Guard guard;
        uncharge(snapshot->bytes);
    }
    heapFree(snapshot);
}

void clearRunning(const Job& job) {
    Guard guard;
    Slot& slot = g_slots[slotIndex(job.consumer)];
    if (slot.running_ticket == job.ticket) slot.running = false;
}

void publishTerminal(const Job& job, DirectoryStatus status) {
    DirectorySnapshot* old = nullptr;
    {
        Guard guard;
        Slot& slot = g_slots[slotIndex(job.consumer)];
        if (slot.ticket != job.ticket) {
            if (slot.running_ticket == job.ticket) slot.running = false;
            return;
        }
        old = slot.published;
        slot.published = nullptr;
        slot.terminal = status;
        slot.result_ready = true;
        slot.running = false;
    }
    directoryRelease(old);
}

DirectorySnapshot* seal(Build& build, const Job& job) {
    const size_t path_len = strlen(g_inflight);
    const size_t prefix = prefixBytes(static_cast<uint16_t>(path_len));
    const size_t bytes = align8(prefix + build.count * sizeof(uint32_t) + build.used);
    const size_t old_blocks = blockCharge(build.cap, build.data != nullptr)
                            + blockCharge(build.offset_cap * sizeof(uint32_t), build.offsets != nullptr);
    // The packed snapshot is allocated before the build buffers are freed.
    // Упакованный снимок выделяется до освобождения буферов сборки.
    const size_t peak = old_blocks + bytes;
    size_t previous = 0;
    {
        Guard guard;
        const size_t without = g_charged - build.charged;
        if (without + peak > kDirectorySnapshotBudget) {
            build.budget = true;
            return nullptr;
        }
        previous = build.charged;
        g_charged = without + peak;
        build.charged = peak;
    }
    auto* memory = static_cast<uint8_t*>(heapAlloc(bytes));
    if (!memory) {
        Guard guard;
        g_charged = g_charged - build.charged + previous;
        build.charged = previous;
        build.oom = true;
        return nullptr;
    }
    memset(memory, 0, prefix);
    auto* snapshot = reinterpret_cast<DirectorySnapshot*>(memory);
    snapshot->bytes = static_cast<uint32_t>(bytes);
    snapshot->request_id = job.request_id;
    snapshot->count = static_cast<uint32_t>(build.count);
    snapshot->generation = job.generation;
    snapshot->consumer = job.consumer;
    snapshot->path_len = static_cast<uint16_t>(path_len);
    memcpy(memory + sizeof(DirectorySnapshot), g_inflight, path_len + 1u);
    uint32_t* table = reinterpret_cast<uint32_t*>(memory + prefix);
    uint8_t* packed = memory + prefix + build.count * sizeof(uint32_t);
    if (build.used) memcpy(packed, build.data, build.used);
    for (size_t i = 0; i < build.count; ++i)
        table[i] = static_cast<uint32_t>(prefix + build.count * sizeof(uint32_t) + build.offsets[i]);
    heapFree(build.data);
    heapFree(build.offsets);
    build.data = nullptr;
    build.offsets = nullptr;
    {
        Guard guard;
        g_charged = g_charged - build.charged + bytes;
        build.charged = 0;
    }
    build.charge_transferred = true;
    return snapshot;
}

// Ticket only. Generation was checked by the caller while this lock was not held.
// Только номер запроса. Поколение вызывающий уже проверил без этой блокировки.
void publishSnapshot(const Job& job, DirectorySnapshot* snapshot) {
    DirectorySnapshot* old = nullptr;
    bool keep = false;
    {
        Guard guard;
        Slot& slot = g_slots[slotIndex(job.consumer)];
        if (slot.ticket == job.ticket) {
            old = slot.published;
            slot.published = snapshot;
            snapshot->refs = 1;
            slot.terminal = DirectoryStatus::Ok;
            slot.result_ready = true;
            slot.running = false;
            keep = true;
        } else if (slot.running_ticket == job.ticket) {
            slot.running = false;
        }
    }
    if (!keep) discardSnapshot(snapshot);
    else directoryRelease(old);
}

bool takePending(Job& job) {
    Guard guard;
    for (size_t n = 0; n < kDirectoryConsumerSlots; ++n) {
        const size_t index = (g_cursor + n) % kDirectoryConsumerSlots;
        Slot& slot = g_slots[index];
        if (!slot.has_pending) continue;
        job.consumer = static_cast<DirectoryConsumer>(index);
        job.ticket = slot.pending_ticket;
        job.request_id = slot.request_id;
        job.generation = slot.generation;
        memcpy(g_inflight, slot.path, strlen(slot.path) + 1u);
        slot.has_pending = false;
        slot.running = true;
        slot.running_ticket = slot.pending_ticket;
        g_cursor = index + 1u;
        return true;
    }
    return false;
}
bool hasPending() {
    Guard guard;
    for (size_t i = 0; i < kDirectoryConsumerSlots; ++i)
        if (g_slots[i].has_pending) return true;
    return false;
}

DirectoryStatus mapStatus(DirStatus status) {
    switch (status) {
    case DirStatus::InvalidPath: return DirectoryStatus::InvalidPath;
    case DirStatus::PathTooLong: return DirectoryStatus::PathTooLong;
    case DirStatus::NameTooLong: return DirectoryStatus::NameTooLong;
    case DirStatus::StaleMedia: return DirectoryStatus::StaleMedia;
    case DirStatus::OpenFailed: return DirectoryStatus::OpenFailed;
    case DirStatus::ReadFailed: return DirectoryStatus::ReadFailed;
    case DirStatus::CloseFailed: return DirectoryStatus::CloseFailed;
    case DirStatus::Unavailable: return DirectoryStatus::Unavailable;
    default: return DirectoryStatus::ReadFailed;
    }
}

void runOne() {
    Job job;
    if (!takePending(job)) return;
    if (!stillCurrent(job)) {
        clearRunning(job);
        return;
    }
    if (!mediaMatches(job)) {
        publishTerminal(job, DirectoryStatus::StaleMedia);
        return;
    }
    Build build;
    build.job = job;
    const DirStatus listed = listDirectory(g_inflight, job.generation, visitEntry, &build);
    if (!stillCurrent(job) || build.cancelled) {
        clearRunning(job);
        return;
    }
    if (build.budget) {
        publishTerminal(job, DirectoryStatus::ResourceLimit);
        return;
    }
    if (build.oom) {
        publishTerminal(job, DirectoryStatus::NoMemory);
        return;
    }
    if (listed != DirStatus::End) {
        publishTerminal(job, mapStatus(listed));
        return;
    }
    if (!mediaMatches(job)) {
        publishTerminal(job, DirectoryStatus::StaleMedia);
        return;
    }
    sortBuild(build);
    if (!stillCurrent(job) || !mediaMatches(job)) {
        if (stillCurrent(job)) publishTerminal(job, DirectoryStatus::StaleMedia);
        return;
    }
    DirectorySnapshot* snapshot = seal(build, job);
    if (!snapshot) {
        publishTerminal(job, build.oom ? DirectoryStatus::NoMemory : DirectoryStatus::ResourceLimit);
        return;
    }
    if (!stillCurrent(job) || !mediaMatches(job)) {
        if (stillCurrent(job)) {
            discardSnapshot(snapshot);
            publishTerminal(job, DirectoryStatus::StaleMedia);
        } else {
            discardSnapshot(snapshot);
            clearRunning(job);
        }
        return;
    }
    publishSnapshot(job, snapshot);
}

#if !defined(YORADIO_DIRECTORY_HOST)
void workerTask(void*);
#endif
#if defined(YORADIO_DIRECTORY_HOST)
bool hostTryCreate() {
    ++g_host_create_attempts;
    if (g_host_fail_creates > 0) {
        --g_host_fail_creates;
        return false;
    }
    return true;
}
#endif

// True only when this call can queue work onto a worker that already exists
// or that this call just created. A concurrent caller gets false at once:
// it does not wait and it does not queue work that has no executor yet.
// Host and device use the same decision.
// Истина только если запрос можно поставить уже существующему worker или
// worker, которого создал именно этот вызов. Параллельный вызывающий сразу
// получает false: он не ждёт и не ставит работу без исполнителя.
// Хост и устройство решают одинаково.
bool ensureWorker() {
    {
        Guard guard;
#if defined(YORADIO_DIRECTORY_HOST)
        if (g_worker_ready) return true;
#else
        if (g_task) return true;
#endif
        if (g_creating) return false;
        g_creating = true;
    }
#if defined(YORADIO_DIRECTORY_HOST)
    const bool ok = hostTryCreate();
    {
        Guard guard;
        g_creating = false;
        if (ok) g_worker_ready = true;
    }
    return ok;
#else
    // ESP-IDF FreeRTOS: stack depth is BYTES (not vanilla FreeRTOS words).
    // ESP-IDF FreeRTOS: глубина стека — в БАЙТАХ (не в словах vanilla FreeRTOS).
    TaskHandle_t created = xTaskCreateStaticPinnedToCore(
        workerTask, "S07Dir",
        kDirectoryWorkerStackBytes,
        nullptr, 1,
        g_worker_stack, &g_worker_tcb, 0);
    {
        Guard guard;
        g_creating = false;
        if (created) g_task = created;
    }
    return g_task != nullptr;
#endif
}
#if !defined(YORADIO_DIRECTORY_HOST)
void workerTask(void*) {
    for (;;) {
        if (!hasPending()) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (hasPending()) runOne();
    }
}
void notifyWorker() {
    if (g_task) xTaskNotifyGive(g_task);
}
#else
void notifyWorker() {}
#endif

void directoryRelease(DirectorySnapshot* snapshot) {
    if (!snapshot) return;
    bool dead = false;
    {
        Guard guard;
        if (snapshot->refs == 0) return;
        --snapshot->refs;
        dead = snapshot->refs == 0;
        if (dead) uncharge(snapshot->bytes);
    }
    if (dead) heapFree(snapshot);
}

DirectoryStatus directorySubmit(const DirectoryQuery& query) {
    if (!knownConsumer(query.consumer) || !query.request_id || !query.path)
        return DirectoryStatus::InvalidPath;
    if (!query.generation) return DirectoryStatus::StaleMedia;
    if (validateDirectoryPath(query.path) != DirStatus::Ok) return DirectoryStatus::InvalidPath;
    const size_t length = strlen(query.path);
    if (!ensureWorker()) return DirectoryStatus::NoMemory;
    DirectorySnapshot* dropped = nullptr;
    {
        Guard guard;
        Slot& slot = g_slots[slotIndex(query.consumer)];
        uint32_t ticket = slot.ticket + 1u;
        if (ticket == 0) ticket = 1u;
        slot.ticket = ticket;
        slot.pending_ticket = ticket;
        slot.request_id = query.request_id;
        slot.generation = query.generation;
        memcpy(slot.path, query.path, length + 1u);
        slot.has_pending = true;
        slot.result_ready = false;
        // The service copy is not a cache of the caller's previous folder.
        // Ссылка сервиса — не кэш предыдущей папки вызывающего.
        dropped = slot.published;
        slot.published = nullptr;
    }
    directoryRelease(dropped);
    notifyWorker();
    return DirectoryStatus::Ok;
}

DirectoryStatus directoryPoll(DirectoryConsumer consumer, DirectorySnapshot** out) {
    if (out) *out = nullptr;
    if (!knownConsumer(consumer) || !out) return DirectoryStatus::InvalidPath;
    Guard guard;
    Slot& slot = g_slots[slotIndex(consumer)];
    if (!slot.result_ready) return DirectoryStatus::Pending;
    if (!slot.published) return slot.terminal;
    ++slot.published->refs;
    *out = slot.published;
    return DirectoryStatus::Ok;
}

const char* directorySnapshotPath(const DirectorySnapshot* snapshot) {
    if (!snapshot) return nullptr;
    return reinterpret_cast<const char*>(snapshotBytes(snapshot) + sizeof(DirectorySnapshot));
}
uint32_t directorySnapshotCount(const DirectorySnapshot* snapshot) {
    return snapshot ? snapshot->count : 0;
}
uint32_t directorySnapshotRequestId(const DirectorySnapshot* snapshot) {
    return snapshot ? snapshot->request_id : 0;
}
uint64_t directorySnapshotGeneration(const DirectorySnapshot* snapshot) {
    return snapshot ? snapshot->generation : 0;
}
size_t directorySnapshotBytes(const DirectorySnapshot* snapshot) {
    return snapshot ? snapshot->bytes : 0;
}
size_t directoryBudgetUsed() {
    Guard guard;
    return g_charged;
}
DirectoryItemKind directoryItemKind(const DirectorySnapshot* snapshot, uint32_t index) {
    const uint8_t* item = itemPointer(snapshot, index);
    if (!item || item[0] != 0) return DirectoryItemKind::Audio;
    return DirectoryItemKind::Directory;
}
const char* directoryItemName(const DirectorySnapshot* snapshot, uint32_t index) {
    const uint8_t* item = itemPointer(snapshot, index);
    return item ? reinterpret_cast<const char*>(item + 4) : nullptr;
}
DirectoryStatus directoryItemPath(const DirectorySnapshot* snapshot, uint32_t index,
                                  char* out, size_t out_cap) {
    const uint8_t* item = itemPointer(snapshot, index);
    const char* directory = directorySnapshotPath(snapshot);
    if (!item || !directory || !out || out_cap == 0) return DirectoryStatus::InvalidPath;
    const size_t name_len = static_cast<size_t>(item[1]) | (static_cast<size_t>(item[2]) << 8);
    const char* name = reinterpret_cast<const char*>(item + 4);
    const size_t dir_len = snapshot->path_len;
    const bool root = dir_len == 1 && directory[0] == '/';
    const size_t total = root ? (1u + name_len) : (dir_len + 1u + name_len);
    if (total > kMaxMediaPathBytes || total + 1u > out_cap) return DirectoryStatus::PathTooLong;
    if (root) {
        out[0] = '/';
        memcpy(out + 1, name, name_len + 1u);
    } else {
        memcpy(out, directory, dir_len);
        out[dir_len] = '/';
        memcpy(out + dir_len + 1, name, name_len + 1u);
    }
    return DirectoryStatus::Ok;
}

#if defined(YORADIO_DIRECTORY_HOST)
void directoryWorkerStep() { runOne(); }
size_t directoryHostLiveBytes() { return g_host_live; }
size_t directoryHostPeakBytes() { return g_host_peak; }
bool directoryHostAllocOverflow() { return g_host_alloc_overflow; }
void directoryHostResetPeak() { g_host_peak = g_host_live; }
void directoryHostFailNextCreates(int count) { g_host_fail_creates = count; }
int directoryHostCreateAttempts() { return g_host_create_attempts; }
void directoryHostArmCreating() {
    Guard guard;
    g_worker_ready = false;
    g_creating = true;
}
void directoryHostDisarmCreating(bool ready) {
    Guard guard;
    g_creating = false;
    g_worker_ready = ready;
}
void directoryHostResetWorker() {
    Guard guard;
    g_creating = false;
    g_worker_ready = false;
}
#endif

}}
#else
namespace yoradio { namespace storage {
DirectoryStatus directorySubmit(const DirectoryQuery&) { return DirectoryStatus::Unavailable; }
DirectoryStatus directoryPoll(DirectoryConsumer, DirectorySnapshot** out) {
    if (out) *out = nullptr;
    return DirectoryStatus::Unavailable;
}
void directoryRelease(DirectorySnapshot*) {}
const char* directorySnapshotPath(const DirectorySnapshot*) { return nullptr; }
uint32_t directorySnapshotCount(const DirectorySnapshot*) { return 0; }
uint32_t directorySnapshotRequestId(const DirectorySnapshot*) { return 0; }
uint64_t directorySnapshotGeneration(const DirectorySnapshot*) { return 0; }
size_t directorySnapshotBytes(const DirectorySnapshot*) { return 0; }
size_t directoryBudgetUsed() { return 0; }
DirectoryItemKind directoryItemKind(const DirectorySnapshot*, uint32_t) {
    return DirectoryItemKind::Audio;
}
const char* directoryItemName(const DirectorySnapshot*, uint32_t) { return nullptr; }
DirectoryStatus directoryItemPath(const DirectorySnapshot*, uint32_t, char*, size_t) {
    return DirectoryStatus::Unavailable;
}
}}
#endif
