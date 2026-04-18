/**
 * SaveManager v2 — M1/M2 compile-time section map + dark backend API over `config_t`.
 * Physical spans tile `config_t` with no gaps; logical SectionId tags each span.
 * Include only in TUs after `config_t` is defined (e.g. save_manager.cpp after config.h).
 *
 * M1: metadata only. `SM_V2_ENABLED` defaults to 0.
 * M2: Preferences-backed per-section storage + legacy→v2 migration. Dark by default:
 *     while `SM_V2_ENABLED=0`, no auto-calls are made, active write path stays v1.
 *     Callers can still invoke the v2 primitives directly (tests / future cutover).
 */
#ifndef SAVE_MANAGER_SECTIONS_H
#define SAVE_MANAGER_SECTIONS_H

#include <cstddef>
#include <cstdint>

#ifndef SM_V2_ENABLED
#define SM_V2_ENABLED 0
#endif

/* Requires complete `config_t` (include this header only after config.h in the TU). */

namespace sm {

enum class SectionId : uint8_t {
  Meta = 0,
  Hot,
  Time,
  Weather,
  Tuning,
  Controls,
  Screensaver,
  Network,
  Ai,
  Count
};

constexpr size_t kSectionIdCount = static_cast<size_t>(SectionId::Count);

/** One contiguous byte range inside `config_t` (relative to start of struct). */
struct ConfigSectionSpan {
  SectionId id;
  uint16_t byte_offset;
  uint16_t byte_size;
};

/** Tile `config_t` from low address to high; each row is one physical span. */
inline constexpr ConfigSectionSpan kConfigSectionSpans[] = {
    // 0: magic + schema version
    {SectionId::Meta, static_cast<uint16_t>(offsetof(config_t, config_set)),
     static_cast<uint16_t>(offsetof(config_t, volume) - offsetof(config_t, config_set))},
    // 1: interactive playback / tone / station selection (volume .. smartstart)
    {SectionId::Hot, static_cast<uint16_t>(offsetof(config_t, volume)),
     static_cast<uint16_t>(offsetof(config_t, tzHour) - offsetof(config_t, volume))},
    // 2: time + SNTP strings
    {SectionId::Time, static_cast<uint16_t>(offsetof(config_t, tzHour)),
     static_cast<uint16_t>(offsetof(config_t, showweather) - offsetof(config_t, tzHour))},
    // 3: weather block
    {SectionId::Weather, static_cast<uint16_t>(offsetof(config_t, showweather)),
     static_cast<uint16_t>(offsetof(config_t, _reserved) - offsetof(config_t, showweather))},
    // 4: SD / encoder / play mode (reserved .. play_mode)
    {SectionId::Tuning, static_cast<uint16_t>(offsetof(config_t, _reserved)),
     static_cast<uint16_t>(offsetof(config_t, irtlp) - offsetof(config_t, _reserved))},
    // 5: GPIO / encoder options (irtlp .. rotate90)
    {SectionId::Controls, static_cast<uint16_t>(offsetof(config_t, irtlp)),
     static_cast<uint16_t>(offsetof(config_t, screensaverEnabled) - offsetof(config_t, irtlp))},
    // 6: screensaver block
    {SectionId::Screensaver, static_cast<uint16_t>(offsetof(config_t, screensaverEnabled)),
     static_cast<uint16_t>(offsetof(config_t, mdnsname) - offsetof(config_t, screensaverEnabled))},
    // 7: mdns + playlist flags
    {SectionId::Network, static_cast<uint16_t>(offsetof(config_t, mdnsname)),
     static_cast<uint16_t>(offsetof(config_t, ai_enabled) - offsetof(config_t, mdnsname))},
    // 8: AI fields (tail of struct)
    {SectionId::Ai, static_cast<uint16_t>(offsetof(config_t, ai_enabled)),
     static_cast<uint16_t>(sizeof(config_t) - offsetof(config_t, ai_enabled))},
};

constexpr size_t kConfigSectionSpanCount = sizeof(kConfigSectionSpans) / sizeof(kConfigSectionSpans[0]);

// --- Continuity: spans must partition config_t exactly (no gaps / overlaps).
static_assert(kConfigSectionSpanCount == kSectionIdCount, "section span table must match SectionId::Count");

static_assert(kConfigSectionSpans[0].byte_offset == 0, "first span must start at 0");

#define SM_SPAN_END_IDX_(i) ((kConfigSectionSpans[(i)].byte_offset) + (kConfigSectionSpans[(i)].byte_size))

static_assert(SM_SPAN_END_IDX_(0) == kConfigSectionSpans[1].byte_offset, "span gap/overlap at 0-1");
static_assert(SM_SPAN_END_IDX_(1) == kConfigSectionSpans[2].byte_offset, "span gap/overlap at 1-2");
static_assert(SM_SPAN_END_IDX_(2) == kConfigSectionSpans[3].byte_offset, "span gap/overlap at 2-3");
static_assert(SM_SPAN_END_IDX_(3) == kConfigSectionSpans[4].byte_offset, "span gap/overlap at 3-4");
static_assert(SM_SPAN_END_IDX_(4) == kConfigSectionSpans[5].byte_offset, "span gap/overlap at 4-5");
static_assert(SM_SPAN_END_IDX_(5) == kConfigSectionSpans[6].byte_offset, "span gap/overlap at 5-6");
static_assert(SM_SPAN_END_IDX_(6) == kConfigSectionSpans[7].byte_offset, "span gap/overlap at 6-7");
static_assert(SM_SPAN_END_IDX_(7) == kConfigSectionSpans[8].byte_offset, "span gap/overlap at 7-8");
static_assert(SM_SPAN_END_IDX_(8) == sizeof(config_t), "last span must end at sizeof(config_t)");

#undef SM_SPAN_END_IDX_

// Every span non-empty (also catches wrong offsetof math).
static_assert(kConfigSectionSpans[0].byte_size > 0, "empty span 0");
static_assert(kConfigSectionSpans[1].byte_size > 0, "empty span 1");
static_assert(kConfigSectionSpans[2].byte_size > 0, "empty span 2");
static_assert(kConfigSectionSpans[3].byte_size > 0, "empty span 3");
static_assert(kConfigSectionSpans[4].byte_size > 0, "empty span 4");
static_assert(kConfigSectionSpans[5].byte_size > 0, "empty span 5");
static_assert(kConfigSectionSpans[6].byte_size > 0, "empty span 6");
static_assert(kConfigSectionSpans[7].byte_size > 0, "empty span 7");
static_assert(kConfigSectionSpans[8].byte_size > 0, "empty span 8");

/**
 * True if `sid` is runtime-authoritative in the current v2 milestone.
 *
 * M4d scope: the full `config_t` partition (every row in `kConfigSectionSpans`).
 * IR codes live outside `config_t`
 * (`syncIrBlobNow` / EEPROM_START_IR) and stay on the legacy IR path.
 *
 * When widening this set on a device that already carries a v2 marker, bump
 * `kV2SchemaVersion` in save_manager.cpp and extend the boot-upgrade branch
 * in `runBootMigrationIfNeeded()` so the newly-promoted section's v2 blob is
 * re-seeded from the current legacy-backed snapshot before overlay. Otherwise
 * stale migration-time blobs would clobber post-migration cold-path writes.
 */
constexpr bool isV2ManagedSection(SectionId sid) {
  return sid == SectionId::Meta ||
         sid == SectionId::Hot ||
         sid == SectionId::Time ||
         sid == SectionId::Weather ||
         sid == SectionId::Tuning ||
         sid == SectionId::Controls ||
         sid == SectionId::Screensaver ||
         sid == SectionId::Network ||
         sid == SectionId::Ai;
}

/** Map a byte offset inside `config_t` to a section; returns Count if none (should not happen when offset valid). */
inline SectionId section_id_for_store_offset(size_t offset) {
  for (size_t i = 0; i < kConfigSectionSpanCount; ++i) {
    const uint32_t base = kConfigSectionSpans[i].byte_offset;
    const uint32_t end = base + kConfigSectionSpans[i].byte_size;
    if (offset >= base && offset < end) {
      return kConfigSectionSpans[i].id;
    }
  }
  return SectionId::Count;
}

/**
 * v2 dark backend (M2). Preferences-backed per-section persistence.
 *
 * Layout:
 *  - One NVS namespace (see kV2Namespace in save_manager.cpp), distinct from legacy EEPROM blob.
 *  - One blob key per section (see kV2SectionKeys in save_manager.cpp).
 *  - One marker key (see kV2MarkerKey) stores the active v2 schema version.
 *
 * Semantics:
 *  - All primitives are safe to call regardless of `SM_V2_ENABLED`. They operate on a
 *    separate NVS namespace and do NOT touch the legacy EEPROM blob / v1 writer.
 *  - `SM_V2_ENABLED=0` (default) keeps v1 as the active runtime write path. These
 *    primitives are not auto-invoked; they are "dark" until M3 wiring.
 */
namespace v2 {

/** Short stable key for a section blob (≤15 chars, NVS-safe). nullptr if sid is out of range. */
const char* sectionKey(SectionId sid);

/** True if the v2 marker key is present in the v2 namespace. */
bool hasMarker();

/** Write the v2 marker with the current schema version. Returns true on success. */
bool writeMarker();

/** Read one section blob into `dst`. Returns true if the stored blob size matches `expected_size`. */
bool loadSection(SectionId sid, void* dst, size_t expected_size);

/** Write one section blob from `src`. Returns true on full write. */
bool saveSection(SectionId sid, const void* src, size_t size);

/**
 * Fan out current `config.store` into per-section v2 blobs and set the marker.
 * Caller must guarantee `config.store` holds the desired source (typically the
 * legacy-EEPROM-loaded snapshot). Atomicity: marker is written only if every
 * section blob save succeeded.
 */
bool migrateFromLegacyStore();

/**
 * Read every v2 section blob into `config.store`. Fails (and leaves `config.store`
 * partially updated) if any section is missing / wrong size. Intended to be gated
 * by a successful `hasMarker()` check.
 */
bool loadStoreFromSections();

/**
 * Boot-time orchestrator. Intended to be called once, after `config.store` has been
 * populated from the legacy EEPROM blob.
 *  - If marker present: load v2 blobs over `config.store` (v2 wins).
 *  - If marker absent and legacy magic is valid: fan out to v2 + write marker.
 *  - Otherwise: no-op (treated as fresh device; defaults handled by Config).
 *
 * Currently invoked from `sm::init()` ONLY under `#if SM_V2_ENABLED` — that is
 * the intentional cutover point for M3.
 */
void runBootMigrationIfNeeded();

}  // namespace v2

}  // namespace sm

#endif  // SAVE_MANAGER_SECTIONS_H
