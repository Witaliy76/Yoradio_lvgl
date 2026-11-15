// WebUI folder list. One call accepts a request or copies one ready page.
// It does not walk the card, start playback, or keep a shared current folder.
// Список папки для WebUI. Один вызов принимает запрос или копирует готовую страницу.
// Карту не обходит, воспроизведение не запускает и общую текущую папку не хранит.
#pragma once
#include "../platform/storage/directory_service.h"
#include <stdint.h>

// Pending: this request is still running. Busy: another WebUI request holds the
// only slot. Cancelled: the ready result belongs to a different request.
// Unavailable: the card is not ready; a retained snapshot is not proof that it is.
// Pending — этот запрос ещё выполняется. Busy — слот занят другим запросом WebUI.
// Cancelled — готовый результат принадлежит другому запросу.
// Unavailable — карта не готова; удержанный снимок этого не доказывает.
enum class SdBrowserState : uint8_t {
    Pending, Busy, Cancelled, Unavailable, Empty, Ready, Error
};

struct SdBrowserReply {
    SdBrowserState state = SdBrowserState::Error;
    yoradio::storage::DirectoryStatus detail = yoradio::storage::DirectoryStatus::Unavailable;
    char* json = nullptr; // copied page or a short status; caller frees it
};

// One stored WebUI identity: client, request, path and generation. `start`
// accepts a scan. A poll or a later page passes false and never starts one.
// `offset` is the first snapshot row, not a file identity. The poll reference
// is released before return; bytes in `json` no longer alias the snapshot.
// Хранится одна личность WebUI: клиент, запрос, путь и поколение. `start`
// принимает проход. Poll и следующая страница передают false и проход не начинают.
// `offset` — первая строка снимка, не идентификатор файла. Ссылка poll
// снимается до возврата; байты `json` больше не указывают в снимок.
SdBrowserReply sdBrowserList(uint32_t client, uint32_t request_id, uint32_t offset,
                             const char* path, bool start);
void sdBrowserRelease(SdBrowserReply& reply);

#if defined(YORADIO_DIRECTORY_HOST)
void sdBrowserHostMedia(bool force, bool ready, uint64_t generation);
#endif
