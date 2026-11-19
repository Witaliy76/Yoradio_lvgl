// Radio provider over station_list_adapter, plus the SourceId binding table.
// Поставщик Radio поверх station_list_adapter и таблица привязки по SourceId.
//
// SD lists one folder through DirectoryConsumer::Lvgl. Radio does not use that slot.
// SD читает одну папку через DirectoryConsumer::Lvgl. Radio этот слот не занимает.

#include "browser_provider.h"

#include <cstdio>
#include <cstring>
#include "esp_attr.h"

#include "../../core/config.h"
#include "../../core/path_play_request.h"
#include "../../core/player.h"
#include "../../platform/storage/directory_names.h"
#include "../../platform/storage/directory_service.h"
#include "../../platform/storage/storage_owner.h"
#include "station_list_adapter.h"

namespace lvgl_ui {
namespace {

bool parseStationId(const char* id, uint16_t* out) {
    if (!id || !out || id[0] == '\0') return false;
    unsigned value = 0;
    for (const char* p = id; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10u + static_cast<unsigned>(*p - '0');
        if (value > 65535u) return false;
    }
    if (value == 0u) return false;
    *out = static_cast<uint16_t>(value);
    return true;
}

bool radioUrlMatches(uint16_t station) {
    static uint32_t cached_revision = 0xffffffffu;
    static uint16_t cached_station = 0;
    static EXT_RAM_BSS_ATTR char url[BUFLEN];
    static bool loaded = false;
    static uint32_t last_attempt = 0;
    const uint32_t revision = config.playlistViewRevision();
    const bool changed = revision != cached_revision || station != cached_station;
    if (changed || (!loaded && static_cast<uint32_t>(millis() - last_attempt) >= 1000u)) {
        cached_revision = revision;
        cached_station = station;
        last_attempt = millis();
        url[0] = '\0';
        // Cache catalog data, not the comparison with the mutable Player URL.
        loaded = station_list_adapter::station_url(station, url, sizeof(url));
    }
    return config.playlistReady() && url[0] != '\0' && strcmp(url, config.station.url) == 0;
}

BrowserStatus radioPrepare(uint32_t, uint64_t) {
    return station_list_adapter::station_count() == 0u
               ? BrowserStatus::Empty
               : BrowserStatus::Ready;
}

void radioRelease(uint32_t) {}

uint16_t radioCount() {
    return station_list_adapter::station_count();
}

bool radioLine(uint16_t index, char* name, size_t name_cap, char* stable_id, size_t id_cap) {
    if (!name || name_cap == 0u || !stable_id || id_cap == 0u) return false;
    const uint16_t station = static_cast<uint16_t>(index + 1u);
    if (!station_list_adapter::is_valid_station_num(station)) return false;
    if (!station_list_adapter::station_name(station, name, name_cap)) {
        name[0] = '\0';
    }
    const int wrote = snprintf(stable_id, id_cap, "%u", static_cast<unsigned>(station));
    return wrote > 0 && static_cast<size_t>(wrote) < id_cap;
}

void radioPlay(const char* stable_id) {
    uint16_t station = 0;
    if (!parseStationId(stable_id, &station)) return;
    station_list_adapter::play_station(station);
}

bool radioSounding(const char* stable_id) {
    uint16_t station = 0;
    if (!parseStationId(stable_id, &station)) return false;
    const auto context = player.playbackContext();
    if (context.state != yoradio::playback::PlaybackState::Playing) return false;
    if (context.source != yoradio::playback::PlaybackSource::Radio) return false;
    if (context.radioStation != station) return false;
    return radioUrlMatches(station);
}

uint32_t radioRevision() {
    return config.playlistViewRevision();
}

constexpr BrowserProvider kRadio{
    yoradio::source::SourceId::Radio,
    true,
    radioPrepare,
    radioRelease,
    radioCount,
    radioLine,
    radioPlay,
    radioSounding,
    radioRevision,
};

// One folder path plus one scratch. The catalog itself stays in the Directory snapshot.
// Один путь папки и один scratch. Сам каталог остаётся в снимке Directory Service.
struct SdView {
    yoradio::storage::DirectorySnapshot* held = nullptr;
    uint32_t request_id = 0;
    uint64_t generation = 0;
    BrowserStatus status = BrowserStatus::Unavailable;
    uint32_t revision = 1;
    int32_t sounding_index = -1;
    uint32_t sounding_revision = 0;
    yoradio::playback::PlaybackContext sounding_context{};
    yoradio::playback::PathPlayResult play_result = yoradio::playback::PathPlayResult::None;
    yoradio::playback::PathPlayRequest request{}; // DspTask scratch, reused for currentMedia
    bool live = false;
    bool settled = true;
    bool sounding_valid = false;
    char path[yoradio::storage::kMaxMediaPathBytes + 1]{};
    char scratch[yoradio::storage::kMaxMediaPathBytes + 1]{};
};

EXT_RAM_BSS_ATTR SdView g_sd;

void sdBump() {
    g_sd.revision = (g_sd.revision == UINT32_MAX) ? 1u : g_sd.revision + 1u;
    g_sd.sounding_valid = false;
}

void sdDropHold() {
    yoradio::storage::directoryRelease(g_sd.held);
    g_sd.held = nullptr;
}

void sdFail(BrowserStatus status) {
    sdDropHold();
    yoradio::storage::directoryCancel(yoradio::storage::DirectoryConsumer::Lvgl);
    g_sd.settled = true;
    g_sd.status = status;
    g_sd.sounding_index = -1;
    sdBump();
}

BrowserStatus sdMapTerminal(yoradio::storage::DirectoryStatus status) {
    using yoradio::storage::DirectoryStatus;
    switch (status) {
    case DirectoryStatus::ResourceLimit:
        return BrowserStatus::ResourceLimit;
    case DirectoryStatus::Unavailable:
    case DirectoryStatus::StaleMedia:
        return BrowserStatus::Unavailable;
    case DirectoryStatus::Pending:
        return BrowserStatus::Loading;
    case DirectoryStatus::Ok:
        return BrowserStatus::Ready;
    default:
        return BrowserStatus::Error;
    }
}

bool sdSnapshotCurrent() {
    if (!g_sd.live || !g_sd.held || !g_sd.settled) return false;
    if (g_sd.status != BrowserStatus::Ready && g_sd.status != BrowserStatus::Empty) return false;
    if (yoradio::storage::directorySnapshotRequestId(g_sd.held) != g_sd.request_id) return false;
    if (yoradio::storage::directorySnapshotGeneration(g_sd.held) != g_sd.generation) return false;
    const char* path = yoradio::storage::directorySnapshotPath(g_sd.held);
    return path && strcmp(path, g_sd.path) == 0;
}

void sdWatchMedia() {
    if (!g_sd.live || g_sd.generation == 0u) return;
    const auto storage = yoradio::storage::StorageOwner::instance().status();
    if (storage.ready && storage.generation == g_sd.generation) return;
    if (g_sd.status == BrowserStatus::Unavailable && !g_sd.held) return;
    sdFail(BrowserStatus::Unavailable);
}

void sdAcceptSnapshot(yoradio::storage::DirectorySnapshot* snapshot) {
    sdDropHold();
    g_sd.held = snapshot;
    g_sd.settled = true;
    const uint32_t count = yoradio::storage::directorySnapshotCount(snapshot);
    if (count > 65534u) {
        sdFail(BrowserStatus::ResourceLimit);
        return;
    }
    g_sd.status = (count == 0u) ? BrowserStatus::Empty : BrowserStatus::Ready;
    g_sd.sounding_index = -1;
    sdBump();
}

BrowserStatus sdPrepare(uint32_t, uint64_t) {
    return g_sd.live ? g_sd.status : BrowserStatus::Unavailable;
}

void sdRelease(uint32_t) {
    // The page has one view. Leaving it drops that view even if its id already moved.
    // У страницы один просмотр. Уход снимает его, даже если номер запроса уже сменился.
    sdDropHold();
    yoradio::storage::directoryCancel(yoradio::storage::DirectoryConsumer::Lvgl);
    g_sd.live = false;
    g_sd.settled = true;
    g_sd.request_id = 0;
    g_sd.generation = 0;
    g_sd.status = BrowserStatus::Unavailable;
    g_sd.path[0] = '\0';
    g_sd.scratch[0] = '\0';
    g_sd.sounding_index = -1;
    g_sd.sounding_valid = false;
}

uint16_t sdCount() {
    if (!sdSnapshotCurrent() || g_sd.status != BrowserStatus::Ready) return 0u;
    return static_cast<uint16_t>(yoradio::storage::directorySnapshotCount(g_sd.held));
}

bool sdCopyName(uint16_t index, char* name, size_t name_cap, bool* is_directory) {
    if (!name || name_cap == 0u || !sdSnapshotCurrent() || g_sd.status != BrowserStatus::Ready) return false;
    if (index >= yoradio::storage::directorySnapshotCount(g_sd.held)) return false;
    const bool directory =
        yoradio::storage::directoryItemKind(g_sd.held, index) == yoradio::storage::DirectoryItemKind::Directory;
    if (is_directory) *is_directory = directory;
    const char* item = yoradio::storage::directoryItemName(g_sd.held, index);
    if (!item) return false;
    size_t length = strlen(item);
    if (length >= name_cap) {
        length = name_cap - 1u;
        while (length > 0u && (static_cast<unsigned char>(item[length]) & 0xC0u) == 0x80u) --length;
    }
    memcpy(name, item, length);
    name[length] = '\0';
    return true;
}

bool sdPlayIndex(uint16_t index) {
    if (!sdSnapshotCurrent() || g_sd.status != BrowserStatus::Ready) return false;
    if (yoradio::storage::directoryItemKind(g_sd.held, index) != yoradio::storage::DirectoryItemKind::Audio) {
        return false;
    }
    if (yoradio::storage::directoryItemPath(g_sd.held, index, g_sd.scratch, sizeof(g_sd.scratch))
        != yoradio::storage::DirectoryStatus::Ok) {
        return false;
    }
    const auto storage = yoradio::storage::StorageOwner::instance().status();
    if (!storage.ready || storage.generation == 0u || storage.generation != g_sd.generation) {
        g_sd.play_result = yoradio::playback::PathPlayResult::StaleMedia;
        return false;
    }
    // Accepted only queues the path. Playing still belongs to the player.
    // Accepted только ставит путь в очередь. Playing по-прежнему подтверждает Player.
    auto& request = g_sd.request;
    const auto copied = yoradio::playback::PathPlayRequest::copy(
        g_sd.scratch, storage.generation, player.pathEpoch(), request);
    g_sd.play_result = copied == yoradio::playback::PathPlayResult::None
        ? player.requestPlay(request) : copied;
    return g_sd.play_result == yoradio::playback::PathPlayResult::Accepted;
}

bool sdLine(uint16_t index, char* name, size_t name_cap, char* stable_id, size_t id_cap) {
    if (!name || name_cap == 0u || !stable_id || id_cap == 0u) return false;
    if (!sdCopyName(index, name, name_cap, nullptr)) return false;
    const int wrote = snprintf(stable_id, id_cap, "%u", static_cast<unsigned>(index));
    return wrote > 0 && static_cast<size_t>(wrote) < id_cap;
}

void sdPlayStable(const char* stable_id) {
    if (!stable_id || stable_id[0] == '\0') return;
    unsigned value = 0;
    for (const char* p = stable_id; *p; ++p) {
        if (*p < '0' || *p > '9') return;
        value = value * 10u + static_cast<unsigned>(*p - '0');
        if (value > 65534u) return;
    }
    (void)browserSdPlay(static_cast<uint16_t>(value));
}

bool sdSounding(const char* stable_id) {
    if (!stable_id || stable_id[0] == '\0') return false;
    unsigned value = 0;
    for (const char* p = stable_id; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10u + static_cast<unsigned>(*p - '0');
        if (value > 65534u) return false;
    }
    const int32_t sounding = browserSdSoundingIndex();
    return sounding >= 0 && static_cast<uint32_t>(sounding) == value;
}

uint32_t sdRevision() {
    return g_sd.revision;
}

const BrowserProvider kSd{
    yoradio::source::SourceId::Sd,
    true,
    sdPrepare,
    sdRelease,
    sdCount,
    sdLine,
    sdPlayStable,
    sdSounding,
    sdRevision,
};

const BrowserProvider* kProviders[] = { &kRadio, &kSd };

} // namespace

namespace {

bool parentDirectory(const char* path, char* out, size_t cap) {
    if (!path || path[0] != '/' || cap < 2u) return false;
    const char* slash = strrchr(path, '/');
    if (!slash) return false;
    if (slash == path) {
        out[0] = '/';
        out[1] = '\0';
    } else {
        const size_t n = static_cast<size_t>(slash - path);
        if (n + 1u > cap) return false;
        memcpy(out, path, n);
        out[n] = '\0';
    }
    return yoradio::storage::validateDirectoryPath(out) == yoradio::storage::DirStatus::Ok;
}

bool fileInDirectory(const char* file, const char* directory, const char** base) {
    if (!file || !directory || file[0] != '/') return false;
    if (directory[0] == '/' && directory[1] == '\0') {
        if (strchr(file + 1, '/') != nullptr || file[1] == '\0') return false;
        if (base) *base = file + 1;
        return true;
    }
    const size_t n = strlen(directory);
    if (n == 0u || strncmp(file, directory, n) != 0 || file[n] != '/') return false;
    if (strchr(file + n + 1, '/') != nullptr || file[n + 1] == '\0') return false;
    if (base) *base = file + n + 1;
    return true;
}

} // namespace

void browserSdOpen(uint32_t request_id, const char* path) {
    if (request_id == 0u) return;
    if (!yoradio::kResolvedStorageConfig.enabled) {
        g_sd.live = true;
        g_sd.request_id = request_id;
        g_sd.generation = 0;
        g_sd.settled = true;
        g_sd.status = BrowserStatus::Unavailable;
        sdBump();
        return;
    }
    g_sd.play_result = yoradio::playback::PathPlayResult::None;
    if (!path || path[0] == '\0') path = "/";
    if (path != g_sd.scratch) {
        const size_t length = strlen(path);
        if (length >= sizeof(g_sd.scratch)) {
            g_sd.live = true;
            g_sd.request_id = request_id;
            sdFail(BrowserStatus::Error);
            return;
        }
        memcpy(g_sd.scratch, path, length + 1u);
    }
    sdDropHold();
    g_sd.live = true;
    yoradio::storage::directoryCancel(yoradio::storage::DirectoryConsumer::Lvgl);
    g_sd.request_id = request_id;
    g_sd.sounding_index = -1;
    memcpy(g_sd.path, g_sd.scratch, strlen(g_sd.scratch) + 1u);

    const auto storage = yoradio::storage::StorageOwner::instance().status();
    if (!storage.ready || storage.generation == 0u) {
        g_sd.generation = 0;
        g_sd.settled = true;
        g_sd.status = BrowserStatus::Unavailable;
        sdBump();
        return;
    }
    if (yoradio::storage::validateDirectoryPath(g_sd.path) != yoradio::storage::DirStatus::Ok) {
        if (strcmp(g_sd.path, "/") != 0) {
            browserSdOpen(request_id, "/");
            return;
        }
        g_sd.generation = storage.generation;
        g_sd.settled = true;
        g_sd.status = BrowserStatus::Error;
        sdBump();
        return;
    }

    g_sd.generation = storage.generation;
    yoradio::storage::DirectoryQuery query;
    query.consumer = yoradio::storage::DirectoryConsumer::Lvgl;
    query.request_id = request_id;
    query.generation = g_sd.generation;
    query.path = g_sd.path;
    const auto submitted = yoradio::storage::directorySubmit(query);
    if (submitted == yoradio::storage::DirectoryStatus::Ok) {
        g_sd.settled = false;
        g_sd.status = BrowserStatus::Loading;
        sdBump();
        return;
    }
    g_sd.settled = true;
    g_sd.status = sdMapTerminal(submitted);
    sdBump();
}

void browserSdOpenPlaying(uint32_t request_id) {
    // DspTask only. Avoids a 1 KiB MediaRef on the UI stack.
    // Только DspTask. MediaRef ~1 КиБ не кладётся на стек UI.
    auto& media = g_sd.request.media;
    const auto storage = yoradio::storage::StorageOwner::instance().status();
    const auto context = player.playbackContext();
    const bool playing = context.state == yoradio::playback::PlaybackState::Playing
                         && context.source == yoradio::playback::PlaybackSource::SingleFile;
    if (playing && storage.ready && player.currentMedia(media)
        && media.generation == storage.generation
        && parentDirectory(media.path, g_sd.scratch, sizeof(g_sd.scratch))) {
        browserSdOpen(request_id, g_sd.scratch);
        return;
    }
    browserSdOpen(request_id, "/");
}

bool browserSdOpenParent(uint32_t request_id) {
    if (!g_sd.live || browserSdAtRoot()) return false;
    if (!parentDirectory(g_sd.path, g_sd.scratch, sizeof(g_sd.scratch))) {
        browserSdOpen(request_id, "/");
        return true;
    }
    browserSdOpen(request_id, g_sd.scratch);
    return true;
}

bool browserSdOpenIndex(uint32_t request_id, uint16_t index) {
    if (!sdSnapshotCurrent() || g_sd.status != BrowserStatus::Ready) return false;
    if (yoradio::storage::directoryItemKind(g_sd.held, index)
        != yoradio::storage::DirectoryItemKind::Directory) {
        return false;
    }
    const auto storage = yoradio::storage::StorageOwner::instance().status();
    if (!storage.ready || storage.generation != g_sd.generation) return false;
    if (yoradio::storage::directoryItemPath(g_sd.held, index, g_sd.scratch, sizeof(g_sd.scratch))
        != yoradio::storage::DirectoryStatus::Ok) {
        return false;
    }
    const auto again = yoradio::storage::StorageOwner::instance().status();
    if (!again.ready || again.generation != g_sd.generation) return false;
    browserSdOpen(request_id, g_sd.scratch);
    return true;
}

bool browserSdAtRoot() {
    return g_sd.path[0] == '/' && g_sd.path[1] == '\0';
}

BrowserStatus browserSdPoll(uint32_t request_id) {
    if (!g_sd.live || request_id != g_sd.request_id) {
        return g_sd.live ? g_sd.status : BrowserStatus::Unavailable;
    }
    sdWatchMedia();
    if (!g_sd.live || request_id != g_sd.request_id) return g_sd.status;
    if (g_sd.settled) return g_sd.status;

    yoradio::storage::DirectorySnapshot* snapshot = nullptr;
    const auto polled = yoradio::storage::directoryPoll(
        yoradio::storage::DirectoryConsumer::Lvgl, &snapshot);
    if (!g_sd.live || request_id != g_sd.request_id) {
        yoradio::storage::directoryRelease(snapshot);
        return g_sd.status;
    }
    if (polled == yoradio::storage::DirectoryStatus::Pending) return BrowserStatus::Loading;
    if (polled == yoradio::storage::DirectoryStatus::Ok) {
        const char* path = snapshot ? yoradio::storage::directorySnapshotPath(snapshot) : nullptr;
        const bool mine = snapshot
            && yoradio::storage::directorySnapshotRequestId(snapshot) == g_sd.request_id
            && yoradio::storage::directorySnapshotGeneration(snapshot) == g_sd.generation
            && path && strcmp(path, g_sd.path) == 0;
        if (!mine) {
            yoradio::storage::directoryRelease(snapshot);
            sdFail(BrowserStatus::Error);
            return g_sd.status;
        }
        sdAcceptSnapshot(snapshot);
        sdWatchMedia();
        return g_sd.status;
    }
    yoradio::storage::directoryRelease(snapshot);
    sdFail(sdMapTerminal(polled));
    return g_sd.status;
}

BrowserStatus browserSdStatus() {
    return g_sd.live ? g_sd.status : BrowserStatus::Unavailable;
}

bool browserSdShouldOpenRoot() {
    if (!g_sd.live || g_sd.generation == 0u) return false;
    const auto storage = yoradio::storage::StorageOwner::instance().status();
    return storage.ready && storage.generation != 0u && storage.generation != g_sd.generation;
}

uint16_t browserSdCount() {
    return sdCount();
}

bool browserSdName(uint16_t index, char* name, size_t name_cap, bool* is_directory) {
    return sdCopyName(index, name, name_cap, is_directory);
}

bool browserSdIsDirectory(uint16_t index) {
    if (!sdSnapshotCurrent() || g_sd.status != BrowserStatus::Ready) return false;
    if (index >= yoradio::storage::directorySnapshotCount(g_sd.held)) return false;
    return yoradio::storage::directoryItemKind(g_sd.held, index)
           == yoradio::storage::DirectoryItemKind::Directory;
}

bool browserSdPlay(uint16_t index) {
    g_sd.play_result = yoradio::playback::PathPlayResult::NotReady;
    return sdPlayIndex(index);
}

yoradio::playback::PathPlayResult browserSdPlayResult() { return g_sd.play_result; }

int32_t browserSdSoundingIndex() {
    if (!sdSnapshotCurrent() || g_sd.status != BrowserStatus::Ready) return -1;
    const auto context = player.playbackContext();
    const auto& cached = g_sd.sounding_context;
    if (g_sd.sounding_valid && g_sd.sounding_revision == g_sd.revision
        && cached.epoch == context.epoch && cached.state == context.state
        && cached.source == context.source && cached.generation == context.generation) {
        return g_sd.sounding_index;
    }
    g_sd.sounding_valid = true;
    g_sd.sounding_revision = g_sd.revision;
    g_sd.sounding_context = context;
    g_sd.sounding_index = -1;

    if (context.state != yoradio::playback::PlaybackState::Playing
        || context.source != yoradio::playback::PlaybackSource::SingleFile) return -1;
    auto& media = g_sd.request.media;
    if (!player.currentMedia(media) || media.generation != g_sd.generation) return -1;
    const char* base = nullptr;
    if (!fileInDirectory(media.path, g_sd.path, &base)) return -1;
    const uint32_t count = yoradio::storage::directorySnapshotCount(g_sd.held);
    for (uint32_t index = 0; index < count; ++index) {
        if (yoradio::storage::directoryItemKind(g_sd.held, index)
            != yoradio::storage::DirectoryItemKind::Audio) {
            continue;
        }
        const char* name = yoradio::storage::directoryItemName(g_sd.held, index);
        if (!name || strcmp(name, base) != 0) continue;
        if (yoradio::storage::directoryItemPath(g_sd.held, index, g_sd.scratch, sizeof(g_sd.scratch))
            != yoradio::storage::DirectoryStatus::Ok) {
            continue;
        }
        if (strcmp(g_sd.scratch, media.path) != 0) continue;
        g_sd.sounding_index = static_cast<int32_t>(index);
        break;
    }
    return g_sd.sounding_index;
}

uint32_t browserSdRevision() {
    return g_sd.revision;
}

const BrowserProvider* browserProvider(yoradio::source::SourceId id) {
    for (const BrowserProvider* provider : kProviders) {
        if (provider && provider->id == id) return provider;
    }
    return nullptr;
}

int32_t browserRadioContentIndex() {
    const auto context = player.playbackContext();
    if (context.state != yoradio::playback::PlaybackState::Playing) return -1;
    if (context.source != yoradio::playback::PlaybackSource::Radio) return -1;
    if (context.radioStation == 0u) return -1;
    char id[8];
    const int wrote = snprintf(id, sizeof(id), "%u", static_cast<unsigned>(context.radioStation));
    if (wrote <= 0 || static_cast<size_t>(wrote) >= sizeof(id)) return -1;
    if (!radioSounding(id)) return -1;
    return static_cast<int32_t>(context.radioStation) - 1;
}

int32_t browserSoundingCatalogRow() {
    const auto context = player.playbackContext();
    if (context.state != yoradio::playback::PlaybackState::Playing) return -1;
    yoradio::source::SourceId id = yoradio::source::SourceId::Radio;
    if (context.source == yoradio::playback::PlaybackSource::Radio) {
        if (browserRadioContentIndex() < 0) return -1;
        id = yoradio::source::SourceId::Radio;
    } else if (context.source == yoradio::playback::PlaybackSource::SingleFile) {
        id = yoradio::source::SourceId::Sd;
    } else {
        return -1;
    }
    return yoradio::source::implementedIndex(id);
}

BrowserEntry browserEntryFromPlayer() {
    const auto context = player.playbackContext();
    const bool playing = context.state == yoradio::playback::PlaybackState::Playing;
    const bool radio_playing =
        playing && context.source == yoradio::playback::PlaybackSource::Radio;
    const bool sd_playing =
        playing && context.source == yoradio::playback::PlaybackSource::SingleFile;
    const BrowserProvider* sd = browserProvider(yoradio::source::SourceId::Sd);
    return browserEntry(radio_playing, config.store.countStation > 0u, sd_playing,
                        sd && sd->implemented);
}

} // namespace lvgl_ui
