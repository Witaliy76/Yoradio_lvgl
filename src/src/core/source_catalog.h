// Static source catalog and the pure activation decisions.
// Player owns the selected source, the RAM bookmark and playback.
// Open returns a navigation intent and does not stop or start audio.
// Planned entries are visible to the source carousel only. They are not a
// temporarily unavailable Radio/SD, and they do not enter Player::activate.
// Статический каталог источников и чистые решения активации.
// Player владеет выбранным источником, RAM-закладкой и воспроизведением.
// Open возвращает намерение навигации и не останавливает и не запускает звук.
// Planned видны только карусели. Это не временно недоступные Radio/SD,
// и они не входят в Player::activate.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace yoradio { namespace source {

// Ids already shipped stay 0 and 1. Later ids are reserved names, not backends.
// Уже выпущенные id остаются 0 и 1. Следующие id — имена, не backend'ы.
enum class SourceId : uint8_t {
    Radio = 0,
    Sd = 1,
    Usb = 2,
    Dlna = 3,
    Nas = 4,
    Yandex = 5,
    Spotify = 6,
    AirPlay = 7,
    Bluetooth = 8,
    Aux = 9,
    I2s = 10
};

// ControlPanel has no file path and no folder queue. A Planned ControlPanel
// is still not a playback stub.
// ControlPanel без пути файла и без folder queue. Planned ControlPanel
// по-прежнему не заглушка воспроизведения.
enum class OpenKind : uint8_t { StationList = 0, Directory = 1, ControlPanel = 2 };

// Planned is not Implemented with availability false. WebUI and the media
// browser publish Implemented only.
// Planned — не Implemented с availability false. WebUI и медиабраузер
// публикуют только Implemented.
enum class SourceStatus : uint8_t { Implemented = 0, Planned = 1 };

struct SourceEntry {
    SourceId id;
    const char* key;
    const char* title;
    const char* icon_key;
    OpenKind open_kind;
    SourceStatus status;
};

// Order is the carousel order. NAS is SMB2, not SMB1 and not WebDAV.
// Radio and SD stay the only Implemented entries.
// Порядок — порядок карусели. NAS — это SMB2, не SMB1 и не WebDAV.
// Radio и SD остаются единственными Implemented.
inline constexpr SourceEntry kCatalog[] = {
    { SourceId::Radio, "radio", "Radio", "radio", OpenKind::StationList, SourceStatus::Implemented },
    { SourceId::Sd, "sd", "SD", "sd", OpenKind::Directory, SourceStatus::Implemented },
    { SourceId::Usb, "usb", "USB Flash", "usb", OpenKind::Directory, SourceStatus::Planned },
    { SourceId::Dlna, "dlna", "DLNA / UPnP", "cast", OpenKind::Directory, SourceStatus::Planned },
    { SourceId::Nas, "nas", "NAS / SMB2", "folder-share", OpenKind::Directory, SourceStatus::Planned },
    { SourceId::Yandex, "yandex", "Yandex Music", "brand-yandex", OpenKind::ControlPanel, SourceStatus::Planned },
    { SourceId::Spotify, "spotify", "Spotify", "brand-spotify", OpenKind::ControlPanel, SourceStatus::Planned },
    { SourceId::AirPlay, "airplay", "AirPlay", "brand-apple", OpenKind::ControlPanel, SourceStatus::Planned },
    { SourceId::Bluetooth, "bluetooth", "Bluetooth Audio", "bluetooth", OpenKind::ControlPanel, SourceStatus::Planned },
    { SourceId::Aux, "aux", "AUX / Line In", "plug-connected", OpenKind::ControlPanel, SourceStatus::Planned },
    { SourceId::I2s, "i2s", "I2S Input", "wave-sine", OpenKind::ControlPanel, SourceStatus::Planned },
};
inline constexpr size_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

static_assert(kCatalogCount == 11, "carousel order is part of the shared catalog");
static_assert(kCatalog[0].id == SourceId::Radio && kCatalog[0].status == SourceStatus::Implemented, "Radio stays implemented");
static_assert(kCatalog[1].id == SourceId::Sd && kCatalog[1].status == SourceStatus::Implemented, "SD stays implemented");
static_assert(kCatalog[2].id == SourceId::Usb && kCatalog[3].id == SourceId::Dlna && kCatalog[4].id == SourceId::Nas, "USB, DLNA, NAS/SMB2 stay in that order");
static_assert(kCatalog[10].id == SourceId::I2s && kCatalog[10].status == SourceStatus::Planned, "I2S stays planned");

inline const SourceEntry* find(SourceId id) {
    for (size_t i = 0; i < kCatalogCount; ++i) if (kCatalog[i].id == id) return &kCatalog[i];
    return nullptr;
}

inline const SourceEntry* findKey(const char* key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < kCatalogCount; ++i) if (strcmp(kCatalog[i].key, key) == 0) return &kCatalog[i];
    return nullptr;
}

inline bool isImplemented(SourceId id) {
    const SourceEntry* entry = find(id);
    return entry && entry->status == SourceStatus::Implemented;
}

// Visible media-browser rows. Planned entries are not rows.
// Строки медиабраузера. Planned в них не входят.
inline size_t implementedCount() {
    size_t n = 0;
    for (size_t i = 0; i < kCatalogCount; ++i) {
        if (kCatalog[i].status == SourceStatus::Implemented) ++n;
    }
    return n;
}

inline const SourceEntry* implementedAt(size_t index) {
    size_t n = 0;
    for (size_t i = 0; i < kCatalogCount; ++i) {
        if (kCatalog[i].status != SourceStatus::Implemented) continue;
        if (n == index) return &kCatalog[i];
        ++n;
    }
    return nullptr;
}

inline int32_t implementedIndex(SourceId id) {
    int32_t n = 0;
    for (size_t i = 0; i < kCatalogCount; ++i) {
        if (kCatalog[i].status != SourceStatus::Implemented) continue;
        if (kCatalog[i].id == id) return n;
        ++n;
    }
    return -1;
}

// Facts come from the existing owners. This catalog stores no availability.
// Факты приходят от существующих владельцев. Каталог не хранит доступность.
struct OpenFacts {
    bool radio_has_stations = false;
    bool storage_ready = false;
    // Final SD permission from kResolvedStorageConfig. Not card readiness.
    // Итоговое разрешение SD из kResolvedStorageConfig. Не готовность карты.
    bool storage_permitted = false;
};

struct OpenIntent {
    bool known = false;
    SourceId id = SourceId::Radio;
    OpenKind kind = OpenKind::StationList;
    bool available = false;
};

inline OpenIntent openSource(SourceId id, OpenFacts facts) {
    const SourceEntry* entry = find(id);
    OpenIntent intent;
    intent.id = id;
    if (!entry) return intent;
    intent.known = true;
    intent.kind = entry->open_kind;
    if (entry->status != SourceStatus::Implemented) return intent;
    if (id == SourceId::Radio) intent.available = facts.radio_has_stations;
    else intent.available = facts.storage_permitted && facts.storage_ready;
    return intent;
}

// Disabled by the user flag, or permitted but the card is not ready.
// Запрещено флагом пользователя либо разрешено, но карта не готова.
inline const char* sdBlockedReason(bool permitted, bool mounted, bool ready) {
    if (!permitted) return "SD disabled";
    if (!mounted && !ready) return "Card unavailable";
    return "Card not ready";
}

// Pending is admission. It is not Playing.
// Pending — приём запроса. Это не Playing.
enum class ActivateOutcome : uint8_t {
    Pending = 0,
    SelectedStopped,
    Playing,
    AlreadyActive,
    NeedsSelection,
    Unavailable,
    StaleMedia,
    StopFailed,
    OpenFailed,
    Cancelled,
    Superseded,
    Busy
};

struct ActivateSnapshot {
    uint32_t request = 0;
    SourceId source = SourceId::Radio;
    ActivateOutcome outcome = ActivateOutcome::Cancelled;
    uint32_t epoch = 0;
    bool complete = false;
};

// A completion counts only for its own request. Any other id is stale.
// Завершение относится только к своему запросу. Любой другой id устарел.
inline ActivateOutcome outcomeFor(const ActivateSnapshot& snap, uint32_t request) {
    if (!request || request != snap.request) return ActivateOutcome::Superseded;
    return snap.outcome;
}

inline bool completionMatches(const ActivateSnapshot& snap, uint32_t request) {
    return snap.complete && request && request == snap.request &&
           snap.outcome != ActivateOutcome::Pending;
}

enum class FileCheck : uint8_t { Present, Missing, Unavailable, Stale, Io };

struct ActivateFacts {
    bool stop_failed = false;
    bool radio_playing = false;
    bool sd_playing = false;
    bool radio_has_stations = false;
    bool storage_ready = false;
    bool storage_mounted = false;
    bool sd_permitted = false;
    uint64_t storage_generation = 0;
    bool bookmark_valid = false;
    uint64_t bookmark_generation = 0;
    uint8_t smartstart = 2;
};

enum class ActivateStep : uint8_t { Done, Probe, StopThenFinish };

struct ActivateDecision {
    ActivateStep step = ActivateStep::Done;
    ActivateOutcome outcome = ActivateOutcome::Cancelled;
};

// Boot still plays only when smartstart == 1. Explicit activation plays for
// every value except 2, which selects the source and leaves it stopped.
// Boot по-прежнему играет только при smartstart == 1. Явная активация играет
// при любом значении, кроме 2: 2 выбирает источник и оставляет его остановленным.
inline bool smartStartBootsRadio(uint8_t smartstart) { return smartstart == 1; }
inline bool smartStartStartsSource(uint8_t smartstart) { return smartstart != 2; }

inline ActivateDecision classify(SourceId id, const ActivateFacts& facts) {
    // Planned and unknown ids finish immediately. They must not fall through
    // into the SD probe and stop the sounding source.
    // Planned и неизвестные id завершаются сразу. Они не попадают в проверку SD
    // и не останавливают звучащий источник.
    if (!isImplemented(id)) return { ActivateStep::Done, ActivateOutcome::Cancelled };
    if (facts.stop_failed) return { ActivateStep::Done, ActivateOutcome::StopFailed };
    // Config-off never probes the card and never asks the browser to open.
    // Выключенная настройка не проверяет карту и не открывает браузер.
    if (id == SourceId::Sd && !facts.sd_permitted) {
        return { ActivateStep::Done, ActivateOutcome::Unavailable };
    }
    if (id == SourceId::Radio) {
        if (facts.radio_playing) return { ActivateStep::Done, ActivateOutcome::AlreadyActive };
        if (!facts.radio_has_stations) return { ActivateStep::Done, ActivateOutcome::NeedsSelection };
        return { ActivateStep::StopThenFinish, ActivateOutcome::Pending };
    }
    if (facts.sd_playing) return { ActivateStep::Done, ActivateOutcome::AlreadyActive };
    if (!facts.bookmark_valid) return { ActivateStep::Done, ActivateOutcome::NeedsSelection };
    if (!facts.storage_mounted && !facts.storage_ready) return { ActivateStep::Done, ActivateOutcome::Unavailable };
    if (!facts.storage_ready || !facts.storage_generation ||
        facts.bookmark_generation != facts.storage_generation) {
        return { ActivateStep::Done, ActivateOutcome::StaleMedia };
    }
    return { ActivateStep::Probe, ActivateOutcome::Pending };
}

// A failed probe finishes before STOP. The sounding source stays as it is.
// Missing means the caller should open the browser and choose again.
// A real open or IO error stays OpenFailed.
// Неудачная проверка завершается до STOP. Звучащий источник не меняется.
// Missing — повод снова открыть браузер и выбрать файл.
// Настоящая ошибка открытия или IO остаётся OpenFailed.
inline ActivateDecision afterProbe(FileCheck check) {
    if (check == FileCheck::Present) return { ActivateStep::StopThenFinish, ActivateOutcome::Pending };
    if (check == FileCheck::Missing) return { ActivateStep::Done, ActivateOutcome::NeedsSelection };
    if (check == FileCheck::Unavailable) return { ActivateStep::Done, ActivateOutcome::Unavailable };
    if (check == FileCheck::Stale) return { ActivateStep::Done, ActivateOutcome::StaleMedia };
    return { ActivateStep::Done, ActivateOutcome::OpenFailed };
}

inline ActivateDecision afterStop(bool stop_ok, bool epoch_ok, uint8_t smartstart) {
    if (!stop_ok) return { ActivateStep::Done, ActivateOutcome::StopFailed };
    if (!epoch_ok) return { ActivateStep::Done, ActivateOutcome::Superseded };
    if (!smartStartStartsSource(smartstart)) return { ActivateStep::Done, ActivateOutcome::SelectedStopped };
    return { ActivateStep::StopThenFinish, ActivateOutcome::Pending };
}

enum class ToggleWhenStopped : uint8_t {
    Legacy, StartRadio, StartSd, StaleBookmark, NeedsSelection, Unavailable
};

// Armed selection replaces only the stopped branch. Unarmed toggle stays legacy.
// A planned id does not borrow the SD branch.
// Подтверждённый выбор заменяет только остановленную ветку. Без выбора toggle прежний.
// Planned id не занимает ветку SD.
inline ToggleWhenStopped toggleWhenStopped(bool armed, SourceId id, bool bookmark,
                                           bool storage_ready, bool storage_mounted,
                                           uint64_t storage_generation, uint64_t bookmark_generation) {
    if (!armed) return ToggleWhenStopped::Legacy;
    if (id == SourceId::Radio) return ToggleWhenStopped::StartRadio;
    if (id != SourceId::Sd) return ToggleWhenStopped::Legacy;
    if (!bookmark) return ToggleWhenStopped::NeedsSelection;
    if (!storage_mounted && !storage_ready) return ToggleWhenStopped::Unavailable;
    if (!storage_ready || !storage_generation || bookmark_generation != storage_generation) {
        return ToggleWhenStopped::StaleBookmark;
    }
    return ToggleWhenStopped::StartSd;
}

}}
