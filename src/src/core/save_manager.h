/**
 * @file save_manager.h
 * @brief YoRadio SaveManager — public declarations for the internal `sm::` persistence engine
 *        (debounced commits, v2 field hook, full-store sync, IR blob, OTA, flush, restart).
 *
 * @author https://github.com/Witaliy76
 * @license MIT License v1.0, dated 18/04/2026
 *
 * Application code should use `Config::saveValue` / `config.*`; call `sm::init()` from `Config::init` only.
 * Long-form documentation: `save_manager.md` in this directory.
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

/// SaveManager v2: field-level intent; maps `field_ptr` into a `config_t` section when SM_V2_ENABLED is 1.
void onFieldWrittenV2(const void* field_ptr, size_t field_size, bool commit_requested);

/// Synchronous full `config_t` EEPROM mirror + commit; when SM_V2_ENABLED also refreshes all v2
/// section blobs from RAM. Explicit path only (factory reset / setDefaults / migration) — not the debounced runtime writer (M5).
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
