/**
 * SaveManager — Phase 1 internal persistence engine (debounced NVS commits).
 * Config remains the public facade; application code should use Config::saveValue / config.* setters.
 * Namespace sm::* is for internal use, OTA hooks, and flush/restart helpers unless explicitly extended later.
 */
#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#include <Arduino.h>

namespace sm {

/// Call once after EEPROM.begin() in Config::init (before any saveValue that schedules flash).
/// Starts debounce timer + low-priority `savemgr` worker task (commit queue).
void init();

/// Called from Config::saveValue. `commitRequested` matches legacy `commit` flag.
/// May be invoked with commitRequested=true even when the field value was unchanged — Config uses that
/// to schedule an NVS flush after a commit=false / commit=true pair (see config.h saveValue).
void onStoreWriteCompleted(bool commitRequested);

/// SaveManager v2 (M1 stub): field-level intent hook. `SM_V2_ENABLED` defaults to 0 — no persistence side effects.
/// When enabled in a later milestone, maps `field_ptr` to a `config_t` section for granular dirty tracking.
void onFieldWrittenV2(const void* field_ptr, size_t field_size, bool commit_requested);

/// Synchronous full `config_t` write + NVS commit (factory reset / setDefaults). Bypasses debounce queue.
void syncFullStoreNow();

/// Synchronous IR blob write + NVS commit (same semantics as legacy eepromWrite for IR region).
void syncIrBlobNow(const void* data, size_t len, int eepromOffset);

/// Block until any pending debounced commit is flushed (or no-op if clean / diagnostic suppress).
void flushSync();

/// Request worker to run a commit soon (non-blocking). Optional; used if design needs explicit wake.
void flushAsync();

void beginBatch();
void endBatch();

void suspendForOta();
void resumeAfterOta();
bool isOtaSuspended();

/// flushSync() then ESP.restart(). Use for intentional reboots when pending NVS must land before reset.
void systemRestart();

}  // namespace sm

#endif  // SAVE_MANAGER_H
