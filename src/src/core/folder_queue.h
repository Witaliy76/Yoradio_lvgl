// Independent SD folder playback traversal. Not the WebUI/LVGL browse cwd.
// Player owns when this state changes; callers outside the owner only bump epoch.
// Независимый обход папок SD. Это не текущий каталог WebUI/LVGL.
// Состояние меняет только Player-owner; снаружи инвалидация — через epoch.
#pragma once
#include "../platform/storage/media_ref.h"
#include "path_play_request.h"

namespace yoradio { namespace playback {

// Bounded played-history depth for sequential Prev. Not reverse DFS.
// Ограниченная глубина истории для sequential Prev. Не reverse DFS.
constexpr size_t kFolderQueueHistoryDepth = 8;
// Compact folder-shuffle permutation of audio indices. Not a path list.
// Компактная перестановка индексов аудио в папке. Не список путей.
constexpr uint16_t kFolderQueueShuffleMax = 4096;

enum class FolderQueuePoll : uint8_t {
    Idle, Pending, Found, End, Error, Cancelled
};

// Explicit file choice starts a new root-bounded queue session.
// Явный выбор файла начинает новую сессию с границей в корне карты.
void folderQueueOnSelected(const storage::MediaRef& media);
// Queue-owned Next/EOF launch confirmed playing; StartNext cursor enters history.
// Подтверждённый запуск Next/EOF; курсор StartNext попадает в историю.
void folderQueueOnAdvanced(const storage::MediaRef& media);
// History Prev confirmed: pop the peeked entry and adopt the media.
// Подтверждённый Prev: снимает peeped-запись и принимает media.
void folderQueueOnHistoryPlay(const storage::MediaRef& media);

// STOP / Radio / confirmed new selection / media fault cancel in-flight search.
// STOP / Radio / подтверждённый новый выбор / сбой носителя отменяют поиск.
void folderQueueCancel();
void folderQueueReset();

// Search is bound to search_epoch; a later pathEpoch bump makes Found stale.
// Поиск привязан к search_epoch; поздний bump pathEpoch делает Found устаревшим.
bool folderQueueStartNext(const storage::MediaRef& after, uint32_t search_epoch,
                          SdRepeatMode repeat = SdRepeatMode::Off, bool shuffle = false);
bool folderQueueCurrent(storage::MediaRef& out);
uint32_t folderQueueSearchEpoch();
// Drop the current-folder permutation; next search rebuilds if Shuffle is on.
// Сбрасывает перестановку папки; следующий поиск соберёт её заново при Shuffle.
void folderQueueInvalidateShuffle();

// Prev: peek only. Pop happens in OnHistoryPlay after a confirmed start.
// Prev: только peek. Снятие — в OnHistoryPlay после подтверждённого старта.
bool folderQueuePeekHistory(storage::MediaRef& out);

// Owner-only light poll. FatFs work stays in Directory Service Playback slot.
// Лёгкий poll владельца. FatFs остаётся в слоте Playback Directory Service.
FolderQueuePoll folderQueuePoll(storage::MediaRef& out, PathPlayResult& detail);
bool folderQueueIsSearching();
// Claim Found only when search_epoch still matches the caller's expected epoch.
// Found забирается только если search_epoch всё ещё совпадает с ожидаемым.
bool folderQueueClaimFound(uint32_t expected_epoch, storage::MediaRef& out);
bool folderQueueConsumeAdvanceFlag();
bool folderQueueConsumeHistoryFlag();
// Mark a peeked history entry as the pending Prev launch (no pop yet).
// Помечает peeped-запись как ожидающий Prev (пока без pop).
void folderQueueMarkHistoryLaunch();
void folderQueueClearLaunchFlags();

#if defined(YORADIO_DIRECTORY_HOST)
void folderQueueHostReset();
void folderQueueHostSetShuffleSeed(uint32_t seed);
void folderQueueHostFailShuffleAlloc(bool fail);
size_t folderQueueHostShuffleBytes();
#endif

}}
