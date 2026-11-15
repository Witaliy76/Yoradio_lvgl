// Static Radio/SD catalog and the pure activation decisions.
// Player owns the selected source, the RAM bookmark and playback.
// Open returns a navigation intent and does not stop or start audio.
// Статический каталог Radio/SD и чистые решения активации.
// Player владеет выбранным источником, RAM-закладкой и воспроизведением.
// Open возвращает намерение навигации и не останавливает и не запускает звук.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace yoradio { namespace source {

enum class SourceId : uint8_t { Radio = 0, Sd = 1 };

// ControlPanel is reserved for a future Bluetooth-style source.
// It is not registered and has no playback stub.
// ControlPanel зарезервирован для будущего источника вроде Bluetooth.
// Он не зарегистрирован и не имеет заглушки воспроизведения.
enum class OpenKind : uint8_t { StationList = 0, Directory = 1, ControlPanel = 2 };

struct SourceEntry {
    SourceId id;
    const char* key;
    const char* title;
    const char* icon_key;
    OpenKind open_kind;
};

// USB would be another Directory entry with its own mount generation.
// Bluetooth would be ControlPanel: no file path and no folder queue.
// USB был бы ещё одной записью Directory со своим поколением mount.
// Bluetooth был бы ControlPanel: без пути файла и без folder queue.
inline constexpr SourceEntry kCatalog[] = {
    { SourceId::Radio, "radio", "Radio", "radio", OpenKind::StationList },
    { SourceId::Sd, "sd", "SD", "sd", OpenKind::Directory },
};
inline constexpr size_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

inline const SourceEntry* find(SourceId id) {
    for (size_t i = 0; i < kCatalogCount; ++i) if (kCatalog[i].id == id) return &kCatalog[i];
    return nullptr;
}

inline const SourceEntry* findKey(const char* key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < kCatalogCount; ++i) if (strcmp(kCatalog[i].key, key) == 0) return &kCatalog[i];
    return nullptr;
}

// Facts come from the existing owners. This catalog stores no availability.
// Факты приходят от существующих владельцев. Каталог не хранит доступность.
struct OpenFacts {
    bool radio_has_stations = false;
    bool storage_ready = false;
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
    intent.available = id == SourceId::Radio ? facts.radio_has_stations : facts.storage_ready;
    return intent;
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
    if (facts.stop_failed) return { ActivateStep::Done, ActivateOutcome::StopFailed };
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
// Подтверждённый выбор заменяет только остановленную ветку. Без выбора toggle прежний.
inline ToggleWhenStopped toggleWhenStopped(bool armed, SourceId id, bool bookmark,
                                           bool storage_ready, bool storage_mounted,
                                           uint64_t storage_generation, uint64_t bookmark_generation) {
    if (!armed) return ToggleWhenStopped::Legacy;
    if (id == SourceId::Radio) return ToggleWhenStopped::StartRadio;
    if (!bookmark) return ToggleWhenStopped::NeedsSelection;
    if (!storage_mounted && !storage_ready) return ToggleWhenStopped::Unavailable;
    if (!storage_ready || !storage_generation || bookmark_generation != storage_generation) {
        return ToggleWhenStopped::StaleBookmark;
    }
    return ToggleWhenStopped::StartSd;
}

}}
