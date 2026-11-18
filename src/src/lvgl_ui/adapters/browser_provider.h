// Compact source-browser contract for the Stations page.
// Компактный контракт браузера источников для страницы Stations.
//
// The catalog in core/source_catalog.h stays the only source registry.
// This table binds a SourceId to row operations. It stores no titles and no availability.
// Каталог в core/source_catalog.h остаётся единственным реестром.
// Здесь только привязка SourceId к операциям строк, без имён и доступности.
#pragma once

#include <stdint.h>

#include "../../core/source_catalog.h"
#include "../../core/path_play_request.h"

namespace lvgl_ui {

enum class BrowserStatus : uint8_t {
    Ready = 0,
    Loading,
    Empty,
    Unavailable,
    Error,
    ResourceLimit
};

enum class BrowserEntry : uint8_t {
    Sources = 0,
    Radio,
    Sd
};

// Content rows of one opened source. The page owns ".." and the Sources list.
// Строки содержимого открытого источника. «..» и список Sources принадлежат странице.
// Radio ignores request_id. SD uses it to drop a late Directory Service result.
// Radio не смотрит на request_id. SD по нему отбрасывает поздний ответ Directory Service.
struct BrowserProvider {
    yoradio::source::SourceId id;
    bool implemented;
    BrowserStatus (*prepare)(uint32_t request_id, uint64_t generation);
    void (*release)(uint32_t request_id);
    uint16_t (*count)();
    // stable_id is provider-local: SD uses an index valid only for this snapshot
    // revision. It is not a persistent file identity; gestures must bind revision.
    bool (*line)(uint16_t index, char* name, size_t name_cap, char* stable_id, size_t id_cap);
    void (*play)(const char* stable_id);
    bool (*is_sounding)(const char* stable_id);
    uint32_t (*revision)();
};

const BrowserProvider* browserProvider(yoradio::source::SourceId id);

// Confirmed playback only. A selected-but-stopped source and SmartStart do not count.
// Только подтверждённое воспроизведение. Выбранный, но остановленный источник и SmartStart не считаются.
constexpr BrowserEntry browserEntry(bool radio_playing, bool radio_has_stations,
                                 bool sd_playing, bool sd_implemented) {
    if (radio_playing && radio_has_stations) return BrowserEntry::Radio;
    // A playing file opens SD only when that provider exists. It is not "card missing".
    // Играющий файл открывает SD только при живом поставщике. Это не «карта отсутствует».
    if (sd_playing && sd_implemented) return BrowserEntry::Sd;
    return BrowserEntry::Sources;
}

static_assert(browserEntry(true, true, false, false) == BrowserEntry::Radio, "playing radio opens radio");
static_assert(browserEntry(true, true, true, true) == BrowserEntry::Radio, "playing radio stays ahead of sd");
static_assert(browserEntry(true, false, false, false) == BrowserEntry::Sources, "radio without stations opens sources");
static_assert(browserEntry(false, true, false, false) == BrowserEntry::Sources, "silence opens sources");
static_assert(browserEntry(false, true, true, false) == BrowserEntry::Sources, "sd without a provider opens sources");
static_assert(browserEntry(false, false, true, true) == BrowserEntry::Sd, "playing sd opens sd");

BrowserEntry browserEntryFromPlayer();

// 0-based station index while Radio is confirmed and the playlist URL still matches.
// Индекс станции с нуля, пока Radio подтверждён и URL в плейлисте ещё совпадает.
int32_t browserRadioContentIndex();

// Catalog row of the confirmed playing source, or -1. SD may be marked before its provider exists.
// Строка каталога подтверждённого источника или -1. SD можно отметить до появления его поставщика.
int32_t browserSoundingCatalogRow();

// SD folder requests. The page keeps navigation; these calls do not start playback.
// One Directory Service snapshot is held for the visible folder and released on replace.
// Запросы папок SD. Страница ведёт навигацию; эти вызовы не запускают воспроизведение.
// Для видимой папки держится один снимок Directory Service и отпускается при замене.
void browserSdOpen(uint32_t request_id, const char* path);
void browserSdOpenPlaying(uint32_t request_id);
bool browserSdOpenParent(uint32_t request_id);
bool browserSdOpenIndex(uint32_t request_id, uint16_t index);
bool browserSdAtRoot();
BrowserStatus browserSdPoll(uint32_t request_id);
BrowserStatus browserSdStatus();
bool browserSdShouldOpenRoot();
uint16_t browserSdCount();
bool browserSdName(uint16_t index, char* name, size_t name_cap, bool* is_directory);
bool browserSdIsDirectory(uint16_t index);
bool browserSdPlay(uint16_t index);
yoradio::playback::PathPlayResult browserSdPlayResult();
int32_t browserSdSoundingIndex();
uint32_t browserSdRevision();

} // namespace lvgl_ui
