// One folder in the background: immutable snapshot for WebUI and LVGL.
// The service does not own navigation and does not start playback.
// Один каталог в фоне: неизменяемый снимок для WebUI и LVGL.
// Сервис не хранит навигацию и не запускает воспроизведение.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace yoradio { namespace storage {

// WebUI, LVGL and Playback each keep their own request. One does not cancel another.
// У WebUI, LVGL и Playback отдельные запросы. Один не отменяет другой.
enum class DirectoryConsumer : uint8_t { WebUi = 0, Lvgl = 1, Playback = 2 };

enum class DirectoryItemKind : uint8_t { Directory = 0, Audio = 1 };

// Ok is a finished catalog, including an empty one. ResourceLimit is not a
// short catalog. Pending means the latest request for that consumer is still
// running. There is no single "current folder" here.
// Ok — законченный каталог, в том числе пустой. ResourceLimit — не укороченный
// список. Pending — последний запрос этого потребителя ещё выполняется.
// Единой «текущей папки» у сервиса нет.
enum class DirectoryStatus : uint8_t {
    Ok, Pending, InvalidPath, StaleMedia, ResourceLimit, NoMemory,
    OpenFailed, ReadFailed, CloseFailed, NameTooLong, PathTooLong, Unavailable
};

// B061: bounded 2 MiB PSRAM pool for up to 4096-entry playback folders.
// Includes every block allocated at the same time:
// snapshots callers still hold, the catalog being built, and the previous
// block that is still alive while it is copied. A temporary copy is not free
// budget. 100/500/2000 measurements stay with the WebUI consumer.
// B061: потолок PSRAM 2 МиБ для всех одновременно выделенных блоков: снимки,
// которые ещё держат вызывающие, собираемый каталог и обе копии при росте.
// Длинные имена и дополнительные старые снимки могут дать ResourceLimit.
// Замеры 100/500/2000 остаются потребителю WebUI.
constexpr size_t kDirectorySnapshotBudget = 2u * 1024u * 1024u;
// One outstanding request per consumer. A newer request replaces that slot.
// Один ожидающий запрос на потребителя. Более новый запрос заменяет этот слот.
constexpr size_t kDirectoryConsumerSlots = 3;
// listDirectory keeps FatReader::fat_path (1027), name[769] and DirEntry (1794)
// live together. yoradio_fat_dir_read adds FILINFO (~0.8 KiB) only while
// f_readdir runs, before DirEntry exists. The LFN workspace is heap
// (FF_USE_LFN 3), not stack. The in-flight path lives in one static buffer.
// Peak of those frames is about 5 KiB; 12 KiB (bytes) leaves FatFs/FreeRTOS margin.
// S07Dir stack is internal DRAM (~12 KiB cost); snapshots/slots stay in PSRAM.
// Heapsort does not recurse. Priority 1 on core 0; Audio stays priority 4 on core 1.
// Passed to xTaskCreateStaticPinnedToCore as BYTES (ESP-IDF), not vanilla words.
// listDirectory одновременно держит FatReader::fat_path (1027), name[769] и
// DirEntry (1794). FILINFO (~0.8 КиБ) живёт только внутри f_readdir, до DirEntry.
// Рабочий буфер LFN — куча (FF_USE_LFN 3), не стек. Путь запроса — один статический
// буфер. Пик кадров ~5 КиБ; 12 КиБ (байты) — запас FatFs/FreeRTOS.
// Стек S07Dir — внутренний DRAM (~12 КиБ); снимки/слоты остаются в PSRAM.
// Heapsort не рекурсивный. Приоритет 1 на ядре 0; Audio — приоритет 4 на ядре 1.
// В xTaskCreateStaticPinnedToCore передаётся в БАЙТАХ (ESP-IDF), не в словах.
constexpr uint32_t kDirectoryWorkerStackBytes = 12u * 1024u;

struct DirectoryQuery {
    DirectoryConsumer consumer = DirectoryConsumer::WebUi;
    uint32_t request_id = 0;     // nonzero; identifies this request only
    uint64_t generation = 0;     // mount incarnation the caller observed
    const char* path = nullptr;  // one directory; copied into the slot
};

// Catalog bytes do not change after publication. refs is the only mutable field
// and is not visible here. Release every successful poll exactly once.
// Байты каталога после публикации не меняются. Меняется только скрытый счётчик
// ссылок. Каждый успешный poll освобождается ровно одним release.
struct DirectorySnapshot;

// Copies the query and returns. Does not switch source or touch the player queue.
// A new query from the same consumer supersedes that consumer's unpublished work
// and drops the service's own reference to its previous snapshot. A reference
// already returned by directoryPoll stays valid. Ok means a worker will run the
// request. NoMemory means it was not queued: the worker could not be created,
// or another caller is still creating it. The caller does not wait.
// Копирует запрос и возвращается. Не переключает источник и не трогает очередь Player.
// Новый запрос того же потребителя заменяет его ещё не опубликованную работу
// и снимает собственную ссылку сервиса на прежний снимок. Ссылка, уже выданная
// directoryPoll, остаётся действительной. Ok значит, что запрос выполнит worker.
// NoMemory значит, что запрос не поставлен: worker создать не удалось
// или его ещё создаёт другой вызывающий. Вызывающий не ждёт.
DirectoryStatus directorySubmit(const DirectoryQuery& query);
// Pending, a terminal error with *out == nullptr, or Ok and a retained snapshot.
// Pending, конечная ошибка с *out == nullptr, либо Ok и удержанный снимок.
DirectoryStatus directoryPoll(DirectoryConsumer consumer, DirectorySnapshot** out);
// Logical cancellation of this consumer only; the worker closes its own DIR.
// Drops the service reference; snapshots retained by callers remain valid.
void directoryCancel(DirectoryConsumer consumer);
void directoryRelease(DirectorySnapshot* snapshot);

const char* directorySnapshotPath(const DirectorySnapshot* snapshot);
uint32_t directorySnapshotCount(const DirectorySnapshot* snapshot);
uint32_t directorySnapshotRequestId(const DirectorySnapshot* snapshot);
uint64_t directorySnapshotGeneration(const DirectorySnapshot* snapshot);
size_t directorySnapshotBytes(const DirectorySnapshot* snapshot);
size_t directoryBudgetUsed();
DirectoryItemKind directoryItemKind(const DirectorySnapshot* snapshot, uint32_t index);
// Exact entry name, not a filesystem path and not the synthetic parent row.
// Точное имя записи, не путь файловой системы и не синтетическая строка вверх.
const char* directoryItemName(const DirectorySnapshot* snapshot, uint32_t index);
// Writes directory + name into the caller buffer. Does not retain that buffer.
// Записывает каталог + имя в буфер вызывающего. Этот буфер сервис не хранит.
DirectoryStatus directoryItemPath(const DirectorySnapshot* snapshot, uint32_t index,
                                  char* out, size_t out_cap);

// Host tests run one queued job on the calling thread. The device task is the
// only production caller; UI, network and Player must not call it.
// Host-тест выполняет одну работу в вызывающем потоке. На устройстве её вызывает
// только задача сервиса; UI, сеть и Player вызывать её не должны.
#if defined(YORADIO_DIRECTORY_HOST)
void directoryWorkerStep();
#endif

}}
