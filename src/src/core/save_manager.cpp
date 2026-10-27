/**
 * @file save_manager.cpp
 * @brief YoRadio SaveManager implementation: debounced worker, v1/v2 NVS persistence, boot migration,
 *        OTA suspend/resume, flush/restart. `Config` is the public facade; `sm::*` is internal.
 *
 * @author https://github.com/Witaliy76
 * @license MIT License v1.0, dated 18/04/2026
 *
 * Design summary:
 * - FreeRTOS one-shot debounce (SM_DEBOUNCE_MS) + worker queue serializes commits.
 * - SM_V2_ENABLED=0: worker writes full `config_t` via EEPROM.put+commit (legacy v1).
 * - SM_V2_ENABLED=1 (M5): debounced path writes **v2 Preferences blobs only** for `config_t`;
 *   legacy EEPROM is still **read** at boot for migration/downgrade; **written** on explicit
 *   `syncFullStoreNow()` and rare OOB `s_dirty` hatch (see in-file M5 comments).
 * - RAM is source of truth between commits; no per-field EEPROM.put before flush.
 */
#include "config.h"
#include "rgb_flash_guard.h"
#include "save_manager_sections.h"
#include <EEPROM.h>
#include <Preferences.h>
#include <cstddef>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/queue.h>

/* Optional: set SM_DIAG_PERSIST=1 (e.g. build flag) for brief `[sm]` Serial traces. Default 0. */
#ifndef SM_DIAG_PERSIST
#define SM_DIAG_PERSIST 0
#endif
#if SM_DIAG_PERSIST
#define SM_LOG(fmt, ...) Serial.printf("[sm] " fmt "\n", ##__VA_ARGS__)
static void smLogReadback(const char* tag) {
  const int base = EEPROM_START;
  const size_t offVol = offsetof(config_t, volume);
  const size_t offLs = offsetof(config_t, lastStation);
  const uint8_t ev = EEPROM.read(base + static_cast<int>(offVol));
  const uint8_t lo = EEPROM.read(base + static_cast<int>(offLs));
  const uint8_t hi = EEPROM.read(base + static_cast<int>(offLs + 1));
  const uint16_t els = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
  SM_LOG("%s after commit EEPROM: vol=%u lastSt=%u | RAM: vol=%u lastSt=%u", tag,
         static_cast<unsigned>(ev), static_cast<unsigned>(els),
         static_cast<unsigned>(config.store.volume),
         static_cast<unsigned>(config.store.lastStation));
}
#else
#define SM_LOG(...) ((void)0)
static inline void smLogReadback(const char*) {}
#endif

#ifndef SM_DEBOUNCE_MS
#define SM_DEBOUNCE_MS 300u
#endif

#ifndef SM_WORKER_STACK
#define SM_WORKER_STACK 3072u
#endif

#ifndef SM_QUEUE_LENGTH
#define SM_QUEUE_LENGTH 4u
#endif

static constexpr uint32_t kQueueMsgCommit = 1u;

static SemaphoreHandle_t s_eeprom_mutex;
static TimerHandle_t s_debounce_timer;
static QueueHandle_t s_commit_queue;
static TaskHandle_t s_worker_handle;
static bool s_inited;
// M5+SM_V2: set only for out-of-store / unresolved writes (no v2 section). Debounced worker
// does not consume this flag; flushSync + ofv2 pre-init may run a one-shot EEPROM hatch.
static volatile bool s_dirty;
static volatile bool s_ota_suspended;
static int s_batch_depth;
#if SM_V2_ENABLED
// v2 per-section dirty bitmap (bit i = SectionId(i)). Any `config_t` field
// write routed by `isV2ManagedSection` sets a bit here; OOB / IR-only paths
// use `s_dirty` (v1 full-store) instead. Volatile because the debounce timer
// callback reads it after arming from another task.
static volatile uint32_t s_dirty_v2_mask;
#endif

static void debounceTimerCallback(TimerHandle_t t);
static void workerTask(void* arg);
static void runDebouncedCommitFromWorker();

// ---------------------------------------------------------------------------
// RGB flash guard episodes (S01-5B)
//
// DEVICE-PROVEN: a physical NVS write can leave the direct-T0 ST7701 scanout permanently
// shifted at RGB PCLK 10 MHz; the same writes at a confirmed 6 MHz stay clean (S01-4X-P).
// A post-write RGB restart is not a reliable repair (S01-4X-ISO/Q/S/S2), so it is gone.
// Every physical write in this TU now runs inside one guard episode, in this fixed order:
//
//   1. rgb_flash_guard::acquire()  - returns after 6 MHz is confirmed (+2 VSYNC), or Denied.
//                                    s_eeprom_mutex is NOT held while waiting.
//   2. s_eeprom_mutex taken
//   3. Preferences / EEPROM writes - synchronous: when they return, flash work is finished
//   4. s_eeprom_mutex given
//   5. rgb_flash_guard::release()  - the guard returns to 10 MHz after its linger.
//
// The guard is acquired first everywhere, and no mutex holder ever waits for the guard, so
// the two cannot deadlock. A synchronous flush on DspTask (sleep shutdown) is safe because
// acquire() on DspTask drives the PCLK transition itself instead of waiting for Display::loop.
// Denied: nothing is written, the dirty bits stay set and the 300 ms debounce timer is
// re-armed, so the same RAM state is retried later. No write at 10 MHz, no restart fallback.
// The episode boundary stays per commit, never per key/field/section; the debounce cadence
// is unchanged.
//
// DEVICE-PROVEN: физическая запись NVS при PCLK 10 МГц может навсегда сдвинуть direct-T0
// scanout ST7701; те же записи при подтверждённых 6 МГц чисты (S01-4X-P). RGB restart после
// записи ненадёжен и удалён. Каждая физическая запись здесь идёт внутри эпизода guard в
// фиксированном порядке: acquire (без мьютекса) → мьютекс → синхронные записи → мьютекс
// отпущен → release. Guard всегда берётся первым, держатель мьютекса никогда не ждёт guard,
// поэтому deadlock невозможен. Синхронный flush на DspTask безопасен: acquire() на DspTask
// сам ведёт смену PCLK. Denied: ничего не пишется, dirty-биты остаются, debounce 300 мс
// перевзводится — повтор позже. Записи на 10 МГц и restart нет.

#if YORADIO_RGB_FLASH_GUARD
#define SM_EPISODE_LOG(fmt, ...) Serial.printf("[SAVE] " fmt "\n", ##__VA_ARGS__)
#else
#define SM_EPISODE_LOG(...) ((void)0)
#endif

// Consecutive Denied episodes; reset by the next granted episode. Log is rate-limited by it.
// Подряд идущие Denied; сбрасывается следующим разрешённым эпизодом. Ограничивает лог.
static uint32_t s_guard_denied_streak;

// Nothing was written: re-arm the debounced writer with the same gates as endBatch().
// Ничего не записано: перевзвести отложенную запись с теми же условиями, что у endBatch().
static void sm_retryAfterGuardDenied(const char* source) {
  ++s_guard_denied_streak;
  if (s_guard_denied_streak == 1u || (s_guard_denied_streak % 10u) == 0u) {
    Serial.printf("[SAVE] deferred source=%s reason=rgb_guard_not_confirmed streak=%lu retry_ms=%u\n",
                  source, (unsigned long)s_guard_denied_streak, (unsigned)SM_DEBOUNCE_MS);
  }
  if (s_inited && s_debounce_timer && !s_ota_suspended && s_batch_depth == 0) {
    (void)xTimerReset(s_debounce_timer, pdMS_TO_TICKS(25));
  }
}

static inline bool sm_anyDirty() {
  if (s_dirty) return true;
#if SM_V2_ENABLED
  if (s_dirty_v2_mask != 0u) return true;
#endif
  return false;
}

#if SM_V2_ENABLED && SM_DIAG_PERSIST
// M3.1: single-word commit classification for logs. Makes it trivial to tell
// whether a commit window is v2-only (ideal on HOT bursts), v1-only (cold
// settings writes), MIXED (cold touched while HOT was also queued), or a
// spurious clean wakeup.
static const char* sm_commitClass(uint32_t v2_mask, bool v1_dirty) {
  if (v2_mask != 0u && v1_dirty) return "MIXED";
  if (v2_mask != 0u)             return "v2-only";
  if (v1_dirty)                  return "v1-only";
  return "clean";
}
#endif

// Full-store EEPROM mirror for `config_t`. Under SM_V2_ENABLED (M5), **not** invoked from the
// debounced worker — only from explicit syncFullStoreNow(), legacy SM_V2=0 builds, and OOB hatch
// paths when `s_dirty` is set.
static void commitV1FullStoreUnderMutex() {
  if (!s_dirty) {
    return;
  }
  SM_LOG("EEPROM.put+commit begin sizeof(store)=%u eeprom_len=%u", (unsigned)sizeof(config.store),
         (unsigned)EEPROM.length());
  EEPROM.put(EEPROM_START, config.store);
  if (!EEPROM.commit()) {
    SM_LOG("EEPROM.commit FAILED");
  }
  s_dirty = false;
  SM_LOG("EEPROM.put+commit end");
  smLogReadback("commit");
}

#if SM_V2_ENABLED
// Forward-declare v2 entry points used from the static helpers / sm::* methods
// below; real implementations live at the bottom of this TU next to the rest
// of sm::v2. These are file-scope forward decls — they MUST sit outside the
// main `namespace sm { ... }` block further down, otherwise they resolve as
// `sm::sm::v2::...` at link time.
namespace sm {
namespace v2 {
bool saveSection(SectionId sid, const void* src, size_t size);
void refreshManagedSectionsFromStore();
}  // namespace v2
}  // namespace sm

static void commitV2DirtySectionsUnderMutex() {
  if (s_dirty_v2_mask == 0u) {
    return;
  }
  // Snapshot + clear up front so any new onFieldWrittenV2 arriving mid-flush
  // can cleanly dirty the mask again for the next commit cycle. If a write
  // fails we re-set the bit so the next flush retries it.
  const uint32_t mask = s_dirty_v2_mask;
  s_dirty_v2_mask = 0u;
#if SM_DIAG_PERSIST
  SM_LOG("v2 commit: mask=0x%x", (unsigned)mask);
#endif
  for (size_t i = 0; i < sm::kConfigSectionSpanCount; ++i) {
    const sm::ConfigSectionSpan& s = sm::kConfigSectionSpans[i];
    const uint32_t bit = 1u << static_cast<uint8_t>(s.id);
    if ((mask & bit) == 0u) {
      continue;
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(&config.store) + s.byte_offset;
    const char* key = sm::v2::sectionKey(s.id);
    if (!sm::v2::saveSection(s.id, src, s.byte_size)) {
      SM_LOG("v2 commit: sec=%s(%u) sz=%u FAILED — re-dirtying bit",
             key ? key : "?", (unsigned)static_cast<uint8_t>(s.id), (unsigned)s.byte_size);
      s_dirty_v2_mask |= bit;
    } else {
      SM_LOG("v2 commit: sec=%s(%u) sz=%u OK",
             key ? key : "?", (unsigned)static_cast<uint8_t>(s.id), (unsigned)s.byte_size);
    }
  }
}
#endif  // SM_V2_ENABLED

static inline uint32_t sm_dirtyV2Mask() {
#if SM_V2_ENABLED
  return s_dirty_v2_mask;
#else
  return 0u;
#endif
}

#if SM_V2_ENABLED
// Every runtime-managed v2 section. Used to keep the whole store dirty after a denied full sync.
// Все v2-секции, управляемые в runtime. Нужно, чтобы после отказа full sync весь store остался dirty.
static uint32_t sm_allManagedSectionsMask() {
  uint32_t m = 0u;
  for (size_t i = 0; i < sm::kConfigSectionSpanCount; ++i) {
    const sm::SectionId id = sm::kConfigSectionSpans[i].id;
    if (sm::isV2ManagedSection(id)) {
      m |= 1u << static_cast<uint8_t>(id);
    }
  }
  return m;
}
#endif

// Writes everything that is dirty now. Caller holds s_eeprom_mutex (when it exists) and a
// non-Denied guard grant.
// Пишет всё, что сейчас dirty. Вызывающий держит s_eeprom_mutex (если он есть) и grant guard.
static void sm_commitDirtyLocked() {
#if SM_V2_ENABLED
  commitV2DirtySectionsUnderMutex();
  if (s_dirty) {
    SM_LOG("M5: s_dirty — OOB hatch EEPROM.put+commit");
    commitV1FullStoreUnderMutex();
  }
#else
  commitV1FullStoreUnderMutex();
#endif
}

// One physical persistence episode in the fixed guard order (see the block comment above).
// Returns false only when the guard denied it: then nothing was written and the dirty state
// was kept and re-armed. respectOtaSuspend: the worker skips writes during OTA, explicit
// synchronous flushes keep their historical "always flush" contract.
// Один эпизод физической записи в фиксированном порядке guard (см. блок выше). false — только
// при отказе guard: ничего не записано, dirty сохранён и перевзведён.
static bool sm_runGuardedCommitEpisode(const char* source, bool respectOtaSuspend) {
  const rgb_flash_guard::Grant grant = rgb_flash_guard::acquire(source);
  if (grant == rgb_flash_guard::Grant::Denied) {
    sm_retryAfterGuardDenied(source);
    return false;
  }
  s_guard_denied_streak = 0;

  const uint32_t t0 = millis();
  bool wrote = false;
  uint32_t mask = 0u;
  bool v1 = false;
  // Mutex only after the guard: never hold it while waiting for the PCLK transition.
  // Мьютекс только после guard: не держать его, пока ждём смену PCLK.
  if (s_eeprom_mutex && xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) != pdTRUE) {
    rgb_flash_guard::release(grant, source);
    return true;
  }
  if (!(respectOtaSuspend && s_ota_suspended)) {
#if SM_V2_ENABLED && SM_DIAG_PERSIST
    SM_LOG("episode %s class=%s mask=0x%x v1=%d", source,
           sm_commitClass(s_dirty_v2_mask, s_dirty),
           (unsigned)s_dirty_v2_mask, (int)s_dirty);
#endif
    mask = sm_dirtyV2Mask();
    v1 = s_dirty;
    if (mask != 0u || v1) {
      SM_EPISODE_LOG("persistence begin source=%s mask=0x%lx v1=%d guard=%s", source,
                     (unsigned long)mask, v1 ? 1 : 0, rgb_flash_guard::grantName(grant));
      sm_commitDirtyLocked();
      wrote = true;
    }
  }
  if (s_eeprom_mutex) {
    xSemaphoreGive(s_eeprom_mutex);
  }
  // All Preferences/EEPROM calls of this episode have returned: flash work is finished.
  // Все вызовы Preferences/EEPROM эпизода вернулись: работа с flash завершена.
  if (wrote) {
    SM_EPISODE_LOG("persistence complete source=%s ms=%lu redirty_mask=0x%lx stack_free=%u", source,
                   (unsigned long)(millis() - t0), (unsigned long)sm_dirtyV2Mask(),
                   (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  }
  rgb_flash_guard::release(grant, source);
  return true;
}

static void runDebouncedCommitFromWorker() {
  // Clean wakeup or OTA window: nothing will be written, so no guard episode either.
  // Pending bits survive an OTA window and are written by resumeAfterOta() -> flushSync().
  // «Чистое» пробуждение или окно OTA: записи не будет, эпизода guard тоже.
  if (s_ota_suspended || !sm_anyDirty()) {
    return;
  }
  (void)sm_runGuardedCommitEpisode("NVS", /*respectOtaSuspend=*/true);
}

static void debounceTimerCallback(TimerHandle_t t) {
  (void)t;
  SM_LOG("debounce timer fired -> queue commit");
  const uint32_t msg = kQueueMsgCommit;
  if (s_commit_queue) {
    const BaseType_t q = xQueueSend(s_commit_queue, &msg, pdMS_TO_TICKS(25));
    if (q != pdTRUE) {
      SM_LOG("queue send FAIL (worker busy/full)");
    }
  }
}

static void workerTask(void* arg) {
  (void)arg;
  uint32_t msg;
  for (;;) {
    if (xQueueReceive(s_commit_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (msg == kQueueMsgCommit) {
      SM_LOG("worker: commit msg, ota_susp=%d", (int)s_ota_suspended);
      runDebouncedCommitFromWorker();
    }
  }
}

namespace sm {

void init() {
  if (s_inited) {
    SM_LOG("init: already inited");
    return;
  }
  s_dirty = false;
  s_ota_suspended = false;
  s_batch_depth = 0;
#if SM_V2_ENABLED
  s_dirty_v2_mask = 0u;
#endif

  s_eeprom_mutex = xSemaphoreCreateMutex();
  if (!s_eeprom_mutex) {
    SM_LOG("init FAIL: mutex");
    return;
  }

  s_commit_queue = xQueueCreate(SM_QUEUE_LENGTH, sizeof(uint32_t));
  if (!s_commit_queue) {
    SM_LOG("init FAIL: queue");
    vSemaphoreDelete(s_eeprom_mutex);
    s_eeprom_mutex = nullptr;
    return;
  }

  s_debounce_timer = xTimerCreate(
      "sm_db",
      pdMS_TO_TICKS(SM_DEBOUNCE_MS),
      pdFALSE,
      nullptr,
      debounceTimerCallback);
  if (!s_debounce_timer) {
    SM_LOG("init FAIL: timer");
    vQueueDelete(s_commit_queue);
    s_commit_queue = nullptr;
    vSemaphoreDelete(s_eeprom_mutex);
    s_eeprom_mutex = nullptr;
    return;
  }

  const BaseType_t ok = xTaskCreatePinnedToCore(
      workerTask,
      "savemgr",
      SM_WORKER_STACK,
      nullptr,
      tskIDLE_PRIORITY + 1,
      &s_worker_handle,
      ARDUINO_RUNNING_CORE);
  if (ok != pdPASS) {
    SM_LOG("init FAIL: task ok=%d", (int)ok);
    xTimerDelete(s_debounce_timer, portMAX_DELAY);
    s_debounce_timer = nullptr;
    vQueueDelete(s_commit_queue);
    s_commit_queue = nullptr;
    vSemaphoreDelete(s_eeprom_mutex);
    s_eeprom_mutex = nullptr;
    return;
  }

  s_inited = true;
  SM_LOG("init OK: core=%d debounce=%ums eeprom_len=%u sizeof(store)=%u need>=%u",
         (int)ARDUINO_RUNNING_CORE, (unsigned)SM_DEBOUNCE_MS, (unsigned)EEPROM.length(),
         (unsigned)sizeof(config.store),
         (unsigned)(EEPROM_START + sizeof(config.store)));

  // Boot-time v2 overlay / migration runs from `Config::init()` AFTER the legacy
  // EEPROM blob has been loaded into `config.store` (and the magic-check /
  // `setDefaults()` branch completed). Invoking it here would read/overwrite an
  // empty `config.store`, and the subsequent `eepromRead()` in Config::init would
  // clobber any overlay. See Config::init for the call site.
}

void onStoreWriteCompleted(bool commitRequested) {
#if SM_DIAG_PERSIST
  SM_LOG("owe commit=%d inited=%d v2_mask=0x%x v1_dirty=%d",
         (int)commitRequested, (int)s_inited,
#if SM_V2_ENABLED
         (unsigned)s_dirty_v2_mask,
#else
         0u,
#endif
         (int)s_dirty);
#endif

#if SM_V2_ENABLED
  // M3.1: under v2, this entry point is reached ONLY from the commit-only nudge
  // in `Config::saveValue` (field unchanged, commit=true). The field-aware path
  // (`onFieldWrittenV2`) has already dirtied the correct lane — v2 mask for
  // in-store managed fields, or `s_dirty` as legacy-fallback for OOB writes.
  //
  // Unconditionally setting `s_dirty = true` here (legacy v1 footgun fix) would
  // promote every pure-HOT/META sequence into a MIXED commit — the trailing
  // `saveValue(same_value, commit=true)` idiom would wake the v1 full-store
  // writer on each and every such burst, regardless of whether any cold field
  // was touched. That is the dominant source of "EEPROM.put+commit" noise seen
  // alongside `ofv2 ...` on device.
  //
  // Correct v2 semantic: arm the debouncer for whatever is already dirty.
  // If nothing is dirty, arming is still safe — both commit helpers early-return
  // on a clean lane.
  if (!commitRequested) {
    return;
  }
  if (!s_inited || !s_debounce_timer) {
    // Pre-init window: the value-changed path already handled its own fallback
    // write; a bare nudge here has nothing to add. No EEPROM writes from a
    // nudge pre-init (was a v1-only paranoia; not needed under v2).
    return;
  }
  if (s_ota_suspended) {
    SM_LOG("owe(v2 nudge): deferred (OTA suspend)");
    return;
  }
  if (s_batch_depth > 0) {
    SM_LOG("owe(v2 nudge): deferred (batch depth=%d)", s_batch_depth);
    return;
  }
  if (!sm_anyDirty()) {
    SM_LOG("owe(v2 nudge): clean — no arm");
    return;
  }
  if (xTimerReset(s_debounce_timer, portMAX_DELAY) != pdPASS) {
    SM_LOG("owe(v2 nudge): xTimerReset FAIL");
  }
  return;
#else
  // valueChanged path sets s_dirty when RAM was updated; commit-only (unchanged field) may still
  // need to flush prior commit=false updates — treat commitRequested as "ensure NVS will catch up".
  s_dirty = true;
  if (!commitRequested) {
    return;
  }
  if (!s_inited || !s_debounce_timer) {
    SM_LOG("owe: immediate put (no timer or not inited)");
    // Pre-init (SM_V2_ENABLED=0 builds only): same guard episode as every other write.
    // The panel is normally not up yet, so the guard reports NOT_APPLICABLE.
    // Pre-init (только SM_V2_ENABLED=0): тот же эпизод guard; панель обычно ещё не поднята.
    (void)sm_runGuardedCommitEpisode("NVS-preinit", /*respectOtaSuspend=*/false);
    smLogReadback("immediate");
    return;
  }
  if (s_ota_suspended) {
    SM_LOG("owe: commit deferred (OTA suspend)");
    return;
  }
  if (s_batch_depth > 0) {
    SM_LOG("owe: commit deferred (batch depth=%d)", s_batch_depth);
    return;
  }
  if (xTimerReset(s_debounce_timer, portMAX_DELAY) != pdPASS) {
    SM_LOG("owe: xTimerReset FAIL");
  }
#endif  // SM_V2_ENABLED
}

void syncFullStoreNow() {
  // M5 (SM_V2_ENABLED): **explicit** legacy EEPROM mirror + v2 blob refresh — factory reset,
  // setDefaults, version migration, and downgrade safety. Not used on the debounced hot path.
  if (s_debounce_timer && s_inited) {
    (void)xTimerStop(s_debounce_timer, portMAX_DELAY);
  }
  // Callers: factory reset (reboot follows, async_tcp / loopTask) and boot defaults/migration
  // (pre-panel -> guard NOT_APPLICABLE). Same guard order as every other write.
  // Вызовы: сброс к заводским (далее reboot) и дефолты/миграция на старте (до панели).
  const rgb_flash_guard::Grant grant = rgb_flash_guard::acquire("syncFullStore");
  if (grant == rgb_flash_guard::Grant::Denied) {
    // Nothing written. Keep the whole store dirty so the next flush writes all of it:
    // the v2 sections plus the legacy mirror through the OOB hatch.
    // Ничего не записано. Весь store остаётся dirty: следующий flush запишет v2-секции и
    // legacy-зеркало через OOB-ветку.
    s_dirty = true;
#if SM_V2_ENABLED
    s_dirty_v2_mask = sm_allManagedSectionsMask();
#endif
    sm_retryAfterGuardDenied("syncFullStore");
    return;
  }
  s_guard_denied_streak = 0;
  if (s_eeprom_mutex && xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) != pdTRUE) {
    rgb_flash_guard::release(grant, "syncFullStore");
    return;
  }
  SM_EPISODE_LOG("persistence begin source=syncFullStore guard=%s", rgb_flash_guard::grantName(grant));
  EEPROM.put(EEPROM_START, config.store);
  EEPROM.commit();
  s_dirty = false;
#if SM_V2_ENABLED
  // Keep v2 section blobs consistent with the freshly-written legacy snapshot,
  // and (one-time) seed the full v2 layout + marker if this device hasn't
  // migrated yet — required so boot-time overlay has something to load.
  sm::v2::refreshManagedSectionsFromStore();
  s_dirty_v2_mask = 0u;
#endif
  if (s_eeprom_mutex) {
    xSemaphoreGive(s_eeprom_mutex);
  }
  SM_EPISODE_LOG("persistence complete source=syncFullStore");
  rgb_flash_guard::release(grant, "syncFullStore");
}

void syncIrBlobNow(const void* data, size_t len, int eepromOffset) {
  // IR blob lives outside config_t and outside the v2 section table, so it has no debounced
  // lane — the whole learn/save gesture is this one call. Same guard order as other writes.
  // IR-блоб вне config_t и вне таблицы секций v2, debounce-полосы у него нет. Порядок guard тот же.
  const rgb_flash_guard::Grant grant = rgb_flash_guard::acquire("IR");
  if (s_eeprom_mutex && xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) != pdTRUE) {
    rgb_flash_guard::release(grant, "IR");
    return;
  }
  // EEPROM.write() only updates the RAM image of the "eeprom" blob; commit() writes flash.
  // EEPROM.write() меняет только RAM-образ blob "eeprom"; во flash пишет commit().
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++) {
    EEPROM.write(eepromOffset + static_cast<int>(i), p[i]);
  }
  if (grant != rgb_flash_guard::Grant::Denied) {
    EEPROM.commit();
  } else {
    // Not written to flash. The IR bytes stay in the EEPROM RAM image; s_dirty makes the
    // next flush commit that whole image through the OOB hatch.
    // Во flash не записано. Байты IR остаются в RAM-образе EEPROM; s_dirty заставит следующий
    // flush закоммитить весь образ через OOB-ветку.
    s_dirty = true;
  }
  if (s_eeprom_mutex) {
    xSemaphoreGive(s_eeprom_mutex);
  }
  if (grant == rgb_flash_guard::Grant::Denied) {
    sm_retryAfterGuardDenied("IR");
    return;
  }
  s_guard_denied_streak = 0;
  rgb_flash_guard::release(grant, "IR");
}

void flushSync() {
  if (s_debounce_timer && s_inited) {
    (void)xTimerStop(s_debounce_timer, portMAX_DELAY);
  }
#if SM_V2_ENABLED && SM_DIAG_PERSIST
  SM_LOG("flushSync class=%s mask=0x%x v1=%d",
         sm_commitClass(s_dirty_v2_mask, s_dirty),
         (unsigned)s_dirty_v2_mask, (int)s_dirty);
#endif
  // Callers (S01-5B audit): sleep-timer shutdown on DspTask; resumeAfterOta() on async_tcp;
  // systemRestart() on loopTask / async_tcp. On DspTask the guard drives the PCLK transition
  // itself (owner path), so this cannot wait for a Display::loop() that is blocked here.
  // Вызовы: засыпание по таймеру на DspTask; resumeAfterOta() на async_tcp; systemRestart()
  // на loopTask / async_tcp. На DspTask guard сам ведёт смену PCLK (owner-путь), поэтому
  // ожидания заблокированного здесь Display::loop() нет.
  if (!sm_anyDirty()) {
    // Nothing to write. Keep the historical barrier: wait for an episode the worker is already
    // writing (it cleared the dirty bits when it started). No guard: no write happens here.
    // Писать нечего. Сохраняем прежний барьер: дождаться эпизода, который уже пишет worker.
    if (s_eeprom_mutex && xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) == pdTRUE) {
      xSemaphoreGive(s_eeprom_mutex);
    }
    return;
  }
  (void)sm_runGuardedCommitEpisode("flushSync", /*respectOtaSuspend=*/false);
}

void flushAsync() {
  if (!s_inited || s_ota_suspended || !s_commit_queue) {
    return;
  }
  if (sm_anyDirty()) {
    const uint32_t msg = kQueueMsgCommit;
    (void)xQueueSend(s_commit_queue, &msg, 0);
  }
}

void beginBatch() {
  s_batch_depth++;
}

void endBatch() {
  s_batch_depth--;
  if (s_batch_depth < 0) {
    s_batch_depth = 0;
  }
  if (s_batch_depth == 0 && sm_anyDirty() && !s_ota_suspended && s_debounce_timer && s_inited) {
    (void)xTimerReset(s_debounce_timer, portMAX_DELAY);
  }
}

void suspendForOta() {
  s_ota_suspended = true;
  if (s_debounce_timer && s_inited) {
    (void)xTimerStop(s_debounce_timer, portMAX_DELAY);
  }
}

void resumeAfterOta() {
  s_ota_suspended = false;
  flushSync();
}

bool isOtaSuspended() {
  return s_ota_suspended;
}

void systemRestart() {
  flushSync();
  ESP.restart();
}

void onFieldWrittenV2(const void* field_ptr, size_t field_size, bool commit_requested) {
#if !SM_V2_ENABLED
  (void)field_ptr;
  (void)field_size;
  (void)commit_requested;
  return;
#else

  // ---- Section routing (M4d: entire `config_t` via v2; IR stays separate) ----
  // Managed set is the single source of truth — `isV2ManagedSection`. Widening
  // the cutover is a change there plus a schema-version bump in runBootMigration.
  // Out-of-store pointers and unresolved sections fall back to the legacy
  // v1 full-store path by setting `s_dirty`.
  bool managed_by_v2 = false;
  bool in_store = false;
  SectionId resolved_sid = SectionId::Count;
  size_t resolved_off = 0;
  if (field_ptr && field_size > 0) {
    const uintptr_t store_base = reinterpret_cast<uintptr_t>(&config.store);
    const uintptr_t p = reinterpret_cast<uintptr_t>(field_ptr);
    if (p >= store_base && (p + field_size) <= (store_base + sizeof(config.store))) {
      in_store = true;
      resolved_off = static_cast<size_t>(p - store_base);
      resolved_sid = section_id_for_store_offset(resolved_off);
      if (resolved_sid != SectionId::Count && isV2ManagedSection(resolved_sid)) {
        s_dirty_v2_mask |= (1u << static_cast<uint8_t>(resolved_sid));
        managed_by_v2 = true;
      }
    }
  }
  if (!managed_by_v2) {
    s_dirty = true;
  }

#if SM_DIAG_PERSIST
  // M3.1 resolution log: name every write so mixed-mode noise is attributable.
  {
    const char* key = (resolved_sid != SectionId::Count) ? sm::v2::sectionKey(resolved_sid) : nullptr;
    const unsigned sid_u = (unsigned)static_cast<uint8_t>(resolved_sid);
    if (managed_by_v2) {
      SM_LOG("ofv2: lane=v2     sec=%s(%u) off=%u sz=%u commit=%d -> mask=0x%x",
             key ? key : "?", sid_u,
             (unsigned)resolved_off, (unsigned)field_size,
             (int)commit_requested, (unsigned)s_dirty_v2_mask);
    } else if (in_store && resolved_sid != SectionId::Count) {
      SM_LOG("ofv2: lane=v1(COLD) sec=%s(%u) off=%u sz=%u commit=%d -> s_dirty=1",
             key ? key : "?", sid_u,
             (unsigned)resolved_off, (unsigned)field_size,
             (int)commit_requested);
    } else {
      SM_LOG("ofv2: lane=v1(OOB)  in_store=%d sec=(%u) fp=%p sz=%u commit=%d -> s_dirty=1",
             (int)in_store, sid_u, field_ptr, (unsigned)field_size,
             (int)commit_requested);
    }
  }
#endif

  // ---- Debounce / OTA / batch plumbing, mirrors onStoreWriteCompleted ----
  if (!commit_requested) {
    return;
  }
  if (!s_inited || !s_debounce_timer) {
    // Pre-init window (M5): flush dirty v2 blobs synchronously; legacy EEPROM only
    // if `s_dirty` (OOB / non-store path) — same hatch policy as flushSync().
    SM_LOG("ofv2: immediate flush (no timer or not inited) — M5 v2 + optional legacy hatch");
    // Same guard episode as every other write. Pre-init normally runs before the panel exists,
    // so the guard reports NOT_APPLICABLE. A Denied episode keeps the bits dirty for a later flush.
    // Тот же эпизод guard, что и для других записей. До init панели обычно нет — NOT_APPLICABLE.
    // При Denied биты остаются dirty до следующего flush.
    (void)sm_runGuardedCommitEpisode("NVS-preinit", /*respectOtaSuspend=*/false);
    return;
  }
  if (s_ota_suspended) {
    SM_LOG("ofv2: commit deferred (OTA suspend)");
    return;
  }
  if (s_batch_depth > 0) {
    SM_LOG("ofv2: commit deferred (batch depth=%d)", s_batch_depth);
    return;
  }
  if (xTimerReset(s_debounce_timer, portMAX_DELAY) != pdPASS) {
    SM_LOG("ofv2: xTimerReset FAIL");
  }
#endif  // SM_V2_ENABLED
}

}  // namespace sm

// ===========================================================================
// SaveManager v2 — Preferences-backed per-section blobs + marker (implementation).
// When SM_V2_ENABLED=0, debounced runtime uses legacy EEPROM full-store only.
// ===========================================================================

namespace {

// Preferences (NVS) namespace for v2 section blobs. Distinct from legacy EEPROM.
// Keep ≤15 chars per ESP-IDF NVS limit.
constexpr const char* kV2Namespace = "yo_sm_v2";

// Marker key: presence + uint32 value = active v2 on-disk schema version.
// Schema history:
//   v1 (M2/M3/M3.1): runtime-managed sections = {Meta, Hot}.
//   v2 (M4a):        runtime-managed sections = {Meta, Hot, Time}.
//   v3 (M4b):        runtime-managed sections = {Meta, Hot, Time, Weather}.
//   v4 (M4c):        runtime-managed sections = {Meta, Hot, Time, Weather, Ai}.
//   v5 (M4d):        +Tuning   — reseed from legacy EEPROM before overlay (cold
//                    v1 writes may have updated play_mode etc. after migration).
//   v6 (M4d):        +Controls — GPIO / encoder / irtlp region same rationale.
//   v7 (M4d):        +Screensaver.
//   v8 (M4d):        +Network (mdnsname .. ai_enabled).
//                    On a device jumping v4->v8 in one flash, all four reseed
//                    steps run in one boot (additive `if (stored_ver < N)`).
//   v9:              Ai tail grew (autodim_* fields, CONFIG_VERSION 7). Partial
//                    overlay of older smaller `ai` blobs + tail defaults.
//   v10:             Ai tail grew (sleep_timer_action, CONFIG_VERSION 8). Partial
//                    overlay of older smaller `ai` blobs + tail defaults.
//   v11:             Ai tail grew (performance_monitor, CONFIG_VERSION 9). Partial
//                    overlay of older smaller `ai` blobs + tail defaults.
//   v12:             Ai tail grew (FU6-A text_scroll_speed/type/delay_s, CONFIG_VERSION 10).
//                    Partial overlay of older smaller `ai` blobs + tail defaults; the zero-filled
//                    tail is then seeded by Config::_setupVersion() case 9.
//   v13:             Ai tail grew (deep_sleep_wake_after_minutes, CONFIG_VERSION 11). Partial overlay
//                    of older smaller `ai` blobs + tail defaults; the zero-filled tail is then
//                    seeded by Config::_setupVersion() case 10.
//   v14:             Ai tail grew (three TIMERS presets, CONFIG_VERSION 12). Partial overlay of
//                    the v13 prefix preserves the renamed wake field; case 11 seeds only new bytes.
//   v15:             Ai tail grew (four TIMERS enabled flags, CONFIG_VERSION 13). Case 12 derives
//                    migrated enabled state from the preserved duration presets.
constexpr const char* kV2MarkerKey = "v2m";
constexpr uint32_t kV2SchemaVersion = 15u;

// Legacy blob sentinel — `config.store.config_set` magic (see config.cpp / config.h).
constexpr uint16_t kLegacyMagic = 4262u;

// Per-section NVS blob key table. Order is not semantically tied to SectionId
// enum order — lookup goes through sectionKey(). Keys kept ≤8 chars.
struct SectionKeyEntry {
  sm::SectionId id;
  const char* key;
};
constexpr SectionKeyEntry kV2SectionKeys[] = {
    {sm::SectionId::Meta,        "meta"},
    {sm::SectionId::Hot,         "hot"},
    {sm::SectionId::Time,        "time"},
    {sm::SectionId::Weather,     "wx"},
    {sm::SectionId::Tuning,      "tun"},
    {sm::SectionId::Controls,    "ctrl"},
    {sm::SectionId::Screensaver, "ss"},
    {sm::SectionId::Network,     "net"},
    {sm::SectionId::Ai,          "ai"},
};
static_assert(sizeof(kV2SectionKeys) / sizeof(kV2SectionKeys[0]) == sm::kSectionIdCount,
              "v2 section key table must cover every SectionId");

const sm::ConfigSectionSpan* spanForSection(sm::SectionId sid) {
  for (size_t i = 0; i < sm::kConfigSectionSpanCount; ++i) {
    if (sm::kConfigSectionSpans[i].id == sid) {
      return &sm::kConfigSectionSpans[i];
    }
  }
  return nullptr;
}

}  // namespace

namespace sm {
namespace v2 {

const char* sectionKey(SectionId sid) {
  for (const auto& e : kV2SectionKeys) {
    if (e.id == sid) {
      return e.key;
    }
  }
  return nullptr;
}

bool hasMarker() {
  Preferences p;
  if (!p.begin(kV2Namespace, /*readOnly=*/true)) {
    return false;
  }
  const bool present = p.isKey(kV2MarkerKey);
  p.end();
  return present;
}

bool writeMarker() {
  Preferences p;
  if (!p.begin(kV2Namespace, /*readOnly=*/false)) {
    return false;
  }
  const size_t n = p.putUInt(kV2MarkerKey, kV2SchemaVersion);
  p.end();
  return n == sizeof(uint32_t);
}

namespace {
// Read the on-disk marker value (schema version). Returns 0 if absent / failed,
// which callers treat as "pre-v1, needs full migration" (same as hasMarker()==false).
uint32_t readMarkerVersionImpl() {
  Preferences p;
  if (!p.begin(kV2Namespace, /*readOnly=*/true)) {
    return 0u;
  }
  if (!p.isKey(kV2MarkerKey)) {
    p.end();
    return 0u;
  }
  const uint32_t v = p.getUInt(kV2MarkerKey, 0u);
  p.end();
  return v;
}

// Re-seed a single v2 section blob from the current `config.store`. Used by
// the boot upgrade path to refresh sections that are becoming runtime-managed
// for the first time on this device (their existing blob is stale migration
// data, legacy EEPROM holds the fresh values we want to preserve).
bool reseedSectionFromStoreImpl(sm::SectionId sid) {
  const sm::ConfigSectionSpan* span = spanForSection(sid);
  if (!span) {
    return false;
  }
  const uint8_t* src = reinterpret_cast<const uint8_t*>(&config.store) + span->byte_offset;
  return sm::v2::saveSection(sid, src, span->byte_size);
}
}  // namespace

bool loadSection(SectionId sid, void* dst, size_t expected_size) {
  const char* key = sectionKey(sid);
  if (!key || !dst || expected_size == 0) {
    return false;
  }
  Preferences p;
  if (!p.begin(kV2Namespace, /*readOnly=*/true)) {
    return false;
  }
  const size_t stored = p.getBytesLength(key);
  if (stored == expected_size) {
    const size_t n = p.getBytes(key, dst, expected_size);
    p.end();
    return n == expected_size;
  }
  // Ai tail grew (autodim_*): merge prefix from smaller on-disk blob.
  // Хвост Ai вырос (autodim_*): подмешиваем префикс из меньшего blob.
  if (sid == SectionId::Ai && stored > 0 && stored < expected_size) {
    const size_t n = p.getBytes(key, dst, stored);
    p.end();
    if (n != stored) {
      return false;
    }
    uint8_t* tail = static_cast<uint8_t*>(dst) + stored;
    memset(tail, 0, expected_size - stored);
    return true;
  }
  p.end();
  return false;
}

bool saveSection(SectionId sid, const void* src, size_t size) {
  const char* key = sectionKey(sid);
  if (!key || !src || size == 0) {
    return false;
  }
  Preferences p;
  if (!p.begin(kV2Namespace, /*readOnly=*/false)) {
    return false;
  }
  const size_t n = p.putBytes(key, src, size);
  p.end();
  return n == size;
}

bool loadStoreFromSections() {
  for (size_t i = 0; i < kConfigSectionSpanCount; ++i) {
    const ConfigSectionSpan& s = kConfigSectionSpans[i];
    uint8_t* dst = reinterpret_cast<uint8_t*>(&config.store) + s.byte_offset;
    if (!loadSection(s.id, dst, s.byte_size)) {
      return false;
    }
  }
  return true;
}

bool migrateFromLegacyStore() {
  // Fan out `config.store` section-by-section. Intentionally write marker LAST
  // so a mid-migration crash leaves the device in legacy mode (marker absent →
  // next boot retries). Partial section blobs on crash are harmless: they will
  // be overwritten on retry before the marker is set.
  for (size_t i = 0; i < kConfigSectionSpanCount; ++i) {
    const ConfigSectionSpan& s = kConfigSectionSpans[i];
    const uint8_t* src = reinterpret_cast<const uint8_t*>(&config.store) + s.byte_offset;
    if (!saveSection(s.id, src, s.byte_size)) {
      return false;
    }
  }
  return writeMarker();
}

void runBootMigrationIfNeeded() {
  // Contract: caller (Config::init) has already populated `config.store` from
  // the legacy EEPROM blob AND executed the `setDefaults()` branch if needed.
  // M5: at runtime, debounced writes do not mirror `config_t` back to EEPROM;
  // this boot read is still the **legacy migration / downgrade snapshot** before
  // v2 overlay. If a section blob is missing or wrong-sized, overlay skips it and
  // RAM keeps the EEPROM-backed bytes for that span.

  if (hasMarker()) {
    // --- M4a upgrade path ------------------------------------------------
    // A device at an older schema has v2 blobs only for whatever sections
    // were RUNTIME-AUTHORITATIVE at that schema. Sections that got promoted
    // between then and now carry STALE blobs (migration-time snapshot), while
    // legacy EEPROM holds the fresh bytes (cold writes went through v1).
    // Before overlay, re-seed the newly-promoted sections from `config.store`
    // (= legacy EEPROM snapshot) so post-migration user data survives.
    const uint32_t stored_ver = readMarkerVersionImpl();
    if (stored_ver < kV2SchemaVersion) {
      SM_LOG("v2: schema upgrade %u -> %u", (unsigned)stored_ver, (unsigned)kV2SchemaVersion);
      // Reseed steps are additive: a device at v=1 needs BOTH the v1->v2 and
      // v2->v3 reseeds on the same boot. Keep each step as an independent
      // `if (stored_ver < N)` so adding a new milestone is a one-line append.
      if (stored_ver < 2u) {
        // v1 -> v2 (M4a): Time was promoted to runtime-authoritative.
        if (reseedSectionFromStoreImpl(SectionId::Time)) {
          SM_LOG("v2 upgrade: reseeded sec=time(2) from legacy EEPROM snapshot");
        } else {
          SM_LOG("v2 upgrade: reseed sec=time(2) FAILED — will degrade to stale overlay");
        }
      }
      if (stored_ver < 3u) {
        // v2 -> v3 (M4b): Weather was promoted to runtime-authoritative.
        if (reseedSectionFromStoreImpl(SectionId::Weather)) {
          SM_LOG("v2 upgrade: reseeded sec=wx(3) from legacy EEPROM snapshot");
        } else {
          SM_LOG("v2 upgrade: reseed sec=wx(3) FAILED — will degrade to stale overlay");
        }
      }
      if (stored_ver < 4u) {
        // v3 -> v4 (M4c): Ai was promoted to runtime-authoritative.
        if (reseedSectionFromStoreImpl(SectionId::Ai)) {
          SM_LOG("v2 upgrade: reseeded sec=ai(8) from legacy EEPROM snapshot");
        } else {
          SM_LOG("v2 upgrade: reseed sec=ai(8) FAILED — will degrade to stale overlay");
        }
      }
      if (stored_ver < 5u) {
        // v4 -> v5 (M4d): Tuning promoted; blob may be stale vs EEPROM cold writes.
        if (reseedSectionFromStoreImpl(SectionId::Tuning)) {
          SM_LOG("v2 upgrade: reseeded sec=tun(4) from legacy EEPROM snapshot");
        } else {
          SM_LOG("v2 upgrade: reseed sec=tun(4) FAILED — will degrade to stale overlay");
        }
      }
      if (stored_ver < 6u) {
        if (reseedSectionFromStoreImpl(SectionId::Controls)) {
          SM_LOG("v2 upgrade: reseeded sec=ctrl(5) from legacy EEPROM snapshot");
        } else {
          SM_LOG("v2 upgrade: reseed sec=ctrl(5) FAILED — will degrade to stale overlay");
        }
      }
      if (stored_ver < 7u) {
        if (reseedSectionFromStoreImpl(SectionId::Screensaver)) {
          SM_LOG("v2 upgrade: reseeded sec=ss(6) from legacy EEPROM snapshot");
        } else {
          SM_LOG("v2 upgrade: reseed sec=ss(6) FAILED — will degrade to stale overlay");
        }
      }
      if (stored_ver < 8u) {
        if (reseedSectionFromStoreImpl(SectionId::Network)) {
          SM_LOG("v2 upgrade: reseeded sec=net(7) from legacy EEPROM snapshot");
        } else {
          SM_LOG("v2 upgrade: reseed sec=net(7) FAILED — will degrade to stale overlay");
        }
      }
      // v8 -> v9: Ai tail grew (autodim_*); partial overlay in loadSection().
      // v9 -> v10: Ai tail grew (sleep_timer_action); partial overlay in loadSection().
      // v10 -> v11: Ai tail grew (performance_monitor); partial overlay in loadSection().
      // v11 -> v12: Ai tail grew (FU6-A text scrolling); partial overlay in loadSection().
      // v12 -> v13: Ai tail grew (Deep Sleep wake preset); partial overlay in loadSection().
      // v13 -> v14: Ai tail grew (three TIMERS presets); partial overlay in loadSection().
      // v14 -> v15: Ai tail grew (four TIMERS enabled flags); partial overlay in loadSection().
      // Add future schema steps ABOVE in ascending order.
      if (!writeMarker()) {
        SM_LOG("v2 upgrade: writeMarker FAILED — upgrade will retry on next boot");
      }
    }

    // --- Overlay v2-managed sections onto config.store -------------------
    // Size-mismatch / missing-blob policy: on any per-section load failure we
    // keep whatever bytes were already in `config.store` (legacy EEPROM or
    // defaults). This is the safe, reversible choice — a bad blob just
    // degrades to v1 for that field.
    for (size_t i = 0; i < kConfigSectionSpanCount; ++i) {
      const ConfigSectionSpan& s = kConfigSectionSpans[i];
      if (!isV2ManagedSection(s.id)) {
        continue;
      }
      uint8_t* dst = reinterpret_cast<uint8_t*>(&config.store) + s.byte_offset;
      if (!loadSection(s.id, dst, s.byte_size)) {
        SM_LOG("v2 overlay: sec=%s(%u) size=%u load failed — keeping legacy bytes",
               sectionKey(s.id) ? sectionKey(s.id) : "?",
               (unsigned)static_cast<uint8_t>(s.id), (unsigned)s.byte_size);
      } else {
        SM_LOG("v2 overlay: sec=%s(%u) size=%u OK",
               sectionKey(s.id) ? sectionKey(s.id) : "?",
               (unsigned)static_cast<uint8_t>(s.id), (unsigned)s.byte_size);
      }
    }
    return;
  }

  // No marker yet. Only migrate when the legacy blob actually carries the magic;
  // otherwise this is a fresh device / factory reset and Config::setDefaults
  // will populate `config.store` — migration will run on a later boot.
  if (config.store.config_set != kLegacyMagic) {
    return;
  }

  if (!migrateFromLegacyStore()) {
    SM_LOG("v2: migrateFromLegacyStore failed — marker NOT written, staying on legacy");
  }
}

void refreshManagedSectionsFromStore() {
  // Called from `sm::syncFullStoreNow()` (factory reset / setDefaults path).
  // If the marker is absent, seed the full layout once so future boots can
  // overlay via `runBootMigrationIfNeeded`. Otherwise refresh every
  // `isV2ManagedSection` blob so v2 NVS stays consistent with the EEPROM snapshot.
  if (!hasMarker()) {
    (void)migrateFromLegacyStore();
    return;
  }
  for (size_t i = 0; i < kConfigSectionSpanCount; ++i) {
    const ConfigSectionSpan& s = kConfigSectionSpans[i];
    if (!isV2ManagedSection(s.id)) {
      continue;
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(&config.store) + s.byte_offset;
    (void)saveSection(s.id, src, s.byte_size);
  }
}

}  // namespace v2
}  // namespace sm
