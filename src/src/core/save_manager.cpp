/**
 * SaveManager Phase 1 — debounced full-store NVS commits, OTA suspend/resume, flush/restart helpers.
 * Config remains the public facade; this file is the internal engine only.
 *
 * Design:
 * - FreeRTOS one-shot debounce timer (SM_DEBOUNCE_MS): each commit=true save resets it; on expiry it
 *   queues a commit request to a low-priority worker task (not the timer service task).
 * - Worker performs EEPROM.put(EEPROM_START, config.store) + EEPROM.commit() under a mutex.
 * - RAM is source of truth between commits; no per-field EEPROM.put before flush.
 */
#include "config.h"
#include "save_manager.h"
#include <EEPROM.h>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/queue.h>

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
static volatile bool s_dirty;
static volatile bool s_ota_suspended;
static int s_batch_depth;

static void debounceTimerCallback(TimerHandle_t t);
static void workerTask(void* arg);
static void runDebouncedCommitFromWorker();

static void commitFullStoreUnderMutex() {
#if DEBUG_GLITCH_SUSPEND_NVS_WRITES
  return;
#else
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
#endif
}

static void runDebouncedCommitFromWorker() {
  if (!s_eeprom_mutex) {
    if (!s_ota_suspended) {
      commitFullStoreUnderMutex();
    }
    return;
  }
  if (xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  if (!s_ota_suspended) {
    commitFullStoreUnderMutex();
  }
  xSemaphoreGive(s_eeprom_mutex);
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
}

void onStoreWriteCompleted(bool commitRequested) {
#if SM_DIAG_PERSIST
  SM_LOG("owe commit=%d glitch_suspend=%d inited=%d", (int)commitRequested,
         (int)DEBUG_GLITCH_SUSPEND_NVS_WRITES, (int)s_inited);
#endif
#if DEBUG_GLITCH_SUSPEND_NVS_WRITES
  (void)commitRequested;
  return;
#endif
  // valueChanged path sets s_dirty when RAM was updated; commit-only (unchanged field) may still
  // need to flush prior commit=false updates — treat commitRequested as "ensure NVS will catch up".
  s_dirty = true;
  if (!commitRequested) {
    return;
  }
  if (!s_inited || !s_debounce_timer) {
#if !DEBUG_GLITCH_SUSPEND_NVS_WRITES
    SM_LOG("owe: immediate put (no timer or not inited)");
    if (s_eeprom_mutex) {
      if (xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) == pdTRUE) {
        EEPROM.put(EEPROM_START, config.store);
        EEPROM.commit();
        s_dirty = false;
        smLogReadback("immediate");
        xSemaphoreGive(s_eeprom_mutex);
      }
    } else {
      EEPROM.put(EEPROM_START, config.store);
      EEPROM.commit();
      s_dirty = false;
      smLogReadback("immediate");
    }
#endif
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
}

void syncFullStoreNow() {
#if DEBUG_GLITCH_SUSPEND_NVS_WRITES
  return;
#endif
  if (s_debounce_timer && s_inited) {
    (void)xTimerStop(s_debounce_timer, portMAX_DELAY);
  }
  if (!s_eeprom_mutex) {
    EEPROM.put(EEPROM_START, config.store);
    EEPROM.commit();
    s_dirty = false;
    return;
  }
  if (xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  EEPROM.put(EEPROM_START, config.store);
  EEPROM.commit();
  s_dirty = false;
  xSemaphoreGive(s_eeprom_mutex);
}

void syncIrBlobNow(const void* data, size_t len, int eepromOffset) {
#if DEBUG_GLITCH_SUSPEND_NVS_WRITES
  (void)data;
  (void)len;
  (void)eepromOffset;
  return;
#endif
  if (!s_eeprom_mutex) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
      EEPROM.write(eepromOffset + static_cast<int>(i), p[i]);
    }
    EEPROM.commit();
    return;
  }
  if (xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++) {
    EEPROM.write(eepromOffset + static_cast<int>(i), p[i]);
  }
  EEPROM.commit();
  xSemaphoreGive(s_eeprom_mutex);
}

void flushSync() {
#if DEBUG_GLITCH_SUSPEND_NVS_WRITES
  return;
#endif
  if (s_debounce_timer && s_inited) {
    (void)xTimerStop(s_debounce_timer, portMAX_DELAY);
  }
  if (!s_eeprom_mutex) {
    commitFullStoreUnderMutex();
    return;
  }
  if (xSemaphoreTake(s_eeprom_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  commitFullStoreUnderMutex();
  xSemaphoreGive(s_eeprom_mutex);
}

void flushAsync() {
#if DEBUG_GLITCH_SUSPEND_NVS_WRITES
  return;
#endif
  if (!s_inited || s_ota_suspended || !s_commit_queue) {
    return;
  }
  if (s_dirty) {
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
#if !DEBUG_GLITCH_SUSPEND_NVS_WRITES
  if (s_batch_depth == 0 && s_dirty && !s_ota_suspended && s_debounce_timer && s_inited) {
    (void)xTimerReset(s_debounce_timer, portMAX_DELAY);
  }
#endif
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

}  // namespace sm
