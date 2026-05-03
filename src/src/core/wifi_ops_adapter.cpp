// Wi-Fi 2B+2C+2D: async scan + manual connect + try-saved in one worker (command dispatch). No network.cpp / persist / UI.
// Wi-Fi 2B+2C+2D: scan + connect + try-saved в одном worker (dispatch). Без network.cpp / persist / UI.

#include "wifi_ops_adapter.h"

#include "wifi_credentials_store.h"

#include <Arduino.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>

namespace {

#ifndef WIFI_OPS_SCAN_MAX
constexpr uint16_t WIFI_OPS_SCAN_MAX = 32;
#endif
constexpr uint32_t WIFI_OPS_SCAN_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_OPS_CONNECT_TIMEOUT_MS = 20000;

constexpr uint8_t kTrySavedSlotNone = 255;

enum class PendingCmd : uint8_t {
  None = 0,
  RunScan,
  RunConnect,
  RunTrySaved,
};

SemaphoreHandle_t     s_mutex;
bool                  s_initialized;
TaskHandle_t          s_worker = nullptr;
std::atomic<bool>    s_cancel_requested{false};

PendingCmd            s_pending_cmd = PendingCmd::None;
char                  s_conn_ssid[33]{};
char                  s_conn_pass[40]{};
bool                  s_conn_allow_disconnect = false;
bool                  s_try_saved_allow_disconnect = false;

WifiOpsSnapshot       s_snap;
WifiOpsScanRow        s_results[WIFI_OPS_SCAN_MAX];

static void clear_ssid_buffers() {
  memset(s_snap.currentSsid, 0, sizeof(s_snap.currentSsid));
  s_snap.connectStartMs = 0;
}

// Reset try-saved snapshot fields (sentinel 255 = none) / сброс полей Try-saved в snapshot.
static void reset_try_saved_progress_locked() {
  s_snap.trySavedSlotIndex   = kTrySavedSlotNone;
  s_snap.trySavedSavedTotal  = 0;
  s_snap.trySavedAttemptIndex = 0;
  s_snap.trySavedSuccessSlot = kTrySavedSlotNone;
}

static void default_snapshot() {
  memset(&s_snap, 0, sizeof(s_snap));
  s_snap.phase         = WifiOpsPhase::Idle;
  s_snap.currentOp     = WifiOpsOp::None;
  s_snap.lastResult    = WifiOpsResult::None;
  s_snap.busy          = false;
  s_snap.scanCount     = 0;
  s_snap.seq           = 1;
  s_snap.resultsSeq    = 0;
  s_snap.scanTruncated = false;
  reset_try_saved_progress_locked();
  clear_ssid_buffers();
}

static void clear_results_buffer() {
  memset(s_results, 0, sizeof(s_results));
}

static bool take_mutex(TickType_t ticks) {
  if (!s_initialized || s_mutex == nullptr) {
    return false;
  }
  return xSemaphoreTake(s_mutex, ticks) == pdTRUE;
}

static void give_mutex() {
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
}

static void bump_seq_locked() {
  ++s_snap.seq;
}

static bool op_in_progress_locked() {
  return s_snap.busy || s_snap.phase == WifiOpsPhase::Scanning || s_snap.phase == WifiOpsPhase::Connecting ||
         s_snap.phase == WifiOpsPhase::TryingSaved;
}

static void finish_scan_locked(WifiOpsResult res, uint16_t count, bool truncated) {
  s_snap.phase         = WifiOpsPhase::Idle;
  s_snap.busy          = false;
  s_snap.currentOp     = WifiOpsOp::None;
  s_snap.lastResult    = res;
  s_snap.scanCount     = count;
  s_snap.scanTruncated = truncated;
  ++s_snap.resultsSeq;
  bump_seq_locked();
  reset_try_saved_progress_locked();
  clear_ssid_buffers();
}

static void finish_connect_locked(WifiOpsResult res) {
  s_snap.phase      = WifiOpsPhase::Idle;
  s_snap.busy       = false;
  s_snap.currentOp  = WifiOpsOp::None;
  s_snap.lastResult = res;
  bump_seq_locked();
  reset_try_saved_progress_locked();
  clear_ssid_buffers();
}

// One STA association attempt; no snapshot mutex; restores autoReconnect on exit paths that touched WiFi.
// Одна попытка STA; без mutex snapshot; autoReconnect восстанавливается на выходах после вмешательства.
static WifiOpsResult attempt_sta_connect(const char* ssid,
                                         const char* pass,
                                         bool              allow_disc,
                                         uint32_t          timeout_ms,
                                         bool              guard_already_connected,
                                         bool              disconnect_before_begin) {
  struct CancelClear {
    ~CancelClear() {
      s_cancel_requested.store(false);
    }
  } cancel_clear;

  if (WiFi.getMode() == WIFI_AP) {
    return WifiOpsResult::InternalError;
  }
  if (ssid == nullptr || ssid[0] == '\0') {
    return WifiOpsResult::InternalError;
  }
  if (guard_already_connected && WiFi.status() == WL_CONNECTED && !allow_disc) {
    return WifiOpsResult::AlreadyConnected;
  }

  // If we reach here for guarded explicit connect: not (STA up && !allow_disc) / для cancel disconnect policy.
  const bool was_connected_sta          = (WiFi.status() == WL_CONNECTED);
  const bool may_disconnect_for_abort = !was_connected_sta || allow_disc;

  const bool        prev_auto = WiFi.getAutoReconnect();
  const char* const psk       = (pass != nullptr && pass[0] != '\0') ? pass : "";

  WiFi.setAutoReconnect(false);

  if (disconnect_before_begin) {
    // Try-saved: same as boot wifiBegin — clean switch before each begin / как boot: disconnect перед begin.
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(120));
  } else if (WiFi.status() == WL_CONNECTED && allow_disc) {
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(120));
  }

  const wifi_mode_t wm = WiFi.getMode();
  if (wm == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(80));
  }

  const wl_status_t begin_rc = WiFi.begin(ssid, psk);
  if (begin_rc == WL_CONNECT_FAILED || begin_rc == WL_NO_SHIELD || begin_rc == WL_STOPPED) {
    WiFi.setAutoReconnect(prev_auto);
    return WifiOpsResult::InternalError;
  }

  const uint32_t deadline = millis() + timeout_ms;

  for (;;) {
    if (s_cancel_requested.load(std::memory_order_acquire)) {
      if (may_disconnect_for_abort) {
        WiFi.disconnect(true);
        vTaskDelay(pdMS_TO_TICKS(80));
      }
      WiFi.setAutoReconnect(prev_auto);
      return WifiOpsResult::Cancelled;
    }

    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      WiFi.setAutoReconnect(prev_auto);
      return WifiOpsResult::Success;
    }
    if (st == WL_CONNECT_FAILED) {
      WiFi.disconnect(false);
      vTaskDelay(pdMS_TO_TICKS(50));
      WiFi.setAutoReconnect(prev_auto);
      return WifiOpsResult::AuthFailed;
    }
    if (st == WL_NO_SSID_AVAIL) {
      WiFi.disconnect(false);
      vTaskDelay(pdMS_TO_TICKS(50));
      WiFi.setAutoReconnect(prev_auto);
      return WifiOpsResult::NoNetwork;
    }
    if ((int32_t)(millis() - deadline) >= 0) {
      if (may_disconnect_for_abort) {
        WiFi.disconnect(true);
        vTaskDelay(pdMS_TO_TICKS(80));
      }
      WiFi.setAutoReconnect(prev_auto);
      return WifiOpsResult::Timeout;
    }
    vTaskDelay(pdMS_TO_TICKS(75));
  }
}

static void finish_try_saved_locked(WifiOpsResult res, uint8_t winning_slot_0_to_4) {
  s_snap.phase      = WifiOpsPhase::Idle;
  s_snap.busy       = false;
  s_snap.currentOp  = WifiOpsOp::None;
  s_snap.lastResult = res;
  if (res == WifiOpsResult::Success && winning_slot_0_to_4 < WIFI_CRED_STORE_CAPACITY) {
    s_snap.trySavedSuccessSlot = winning_slot_0_to_4;
  } else {
    s_snap.trySavedSuccessSlot = kTrySavedSlotNone;
  }
  s_snap.trySavedSlotIndex    = kTrySavedSlotNone;
  s_snap.trySavedSavedTotal   = 0;
  s_snap.trySavedAttemptIndex = 0;
  bump_seq_locked();
  clear_ssid_buffers();
}

static void run_scan_job();
static void run_connect_job(const char* ssid, const char* pass, bool allow_disc);
static void run_try_saved_job(bool allow_disc);
static void worker_task(void* /*param*/);

static bool ensure_worker_started() {
  if (s_worker != nullptr) {
    return true;
  }
  constexpr uint32_t kStackBytes = 8192;
  if (xTaskCreatePinnedToCore(
          worker_task,
          "wifiOps",
          kStackBytes,
          nullptr,
          2,
          &s_worker,
          1) != pdPASS) {
    s_worker = nullptr;
    return false;
  }
  return true;
}

static void run_scan_job() {
  WiFi.scanDelete();

  const int8_t start_rc = WiFi.scanNetworks(true, false);
  if (start_rc == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    if (take_mutex(pdMS_TO_TICKS(50))) {
      clear_results_buffer();
      finish_scan_locked(WifiOpsResult::InternalError, 0, false);
      give_mutex();
    }
    s_cancel_requested.store(false);
    return;
  }

  const uint32_t deadline = millis() + WIFI_OPS_SCAN_TIMEOUT_MS;
  int16_t         final_n = -1;

  for (;;) {
    if (s_cancel_requested.load(std::memory_order_acquire)) {
      WiFi.scanDelete();
      if (take_mutex(pdMS_TO_TICKS(50))) {
        clear_results_buffer();
        finish_scan_locked(WifiOpsResult::Cancelled, 0, false);
        give_mutex();
      }
      s_cancel_requested.store(false);
      return;
    }
    if ((int32_t)(millis() - deadline) >= 0) {
      WiFi.scanDelete();
      if (take_mutex(pdMS_TO_TICKS(50))) {
        clear_results_buffer();
        finish_scan_locked(WifiOpsResult::Timeout, 0, false);
        give_mutex();
      }
      s_cancel_requested.store(false);
      return;
    }

    const int16_t st = WiFi.scanComplete();
    if (st >= 0) {
      final_n = st;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  WifiOpsScanRow tmp[WIFI_OPS_SCAN_MAX];
  memset(tmp, 0, sizeof(tmp));

  const int      n_raw = final_n;
  const uint16_t n_cap = (n_raw > WIFI_OPS_SCAN_MAX) ? WIFI_OPS_SCAN_MAX : static_cast<uint16_t>(n_raw);
  const bool     truncated = (n_raw > WIFI_OPS_SCAN_MAX);

  for (uint16_t i = 0; i < n_cap; i++) {
    strlcpy(tmp[i].ssid, WiFi.SSID(i).c_str(), sizeof(tmp[i].ssid));
    tmp[i].rssi    = static_cast<int32_t>(WiFi.RSSI(i));
    tmp[i].auth    = static_cast<uint8_t>(WiFi.encryptionType(i));
    const int ch = WiFi.channel(i);
    tmp[i].channel = (ch > 0 && ch <= 255) ? static_cast<uint8_t>(ch) : 0;
  }

  WiFi.scanDelete();

  if (s_cancel_requested.load(std::memory_order_acquire)) {
    if (take_mutex(pdMS_TO_TICKS(50))) {
      clear_results_buffer();
      finish_scan_locked(WifiOpsResult::Cancelled, 0, false);
      give_mutex();
    }
    s_cancel_requested.store(false);
    return;
  }

  if (take_mutex(pdMS_TO_TICKS(100))) {
    memcpy(s_results, tmp, sizeof(tmp));
    finish_scan_locked(WifiOpsResult::Success, n_cap, truncated);
    give_mutex();
  }
  s_cancel_requested.store(false);
}

static void run_connect_job(const char* ssid, const char* pass, bool allow_disc) {
  const WifiOpsResult r = attempt_sta_connect(ssid, pass, allow_disc, WIFI_OPS_CONNECT_TIMEOUT_MS, true, false);
  if (take_mutex(pdMS_TO_TICKS(50))) {
    finish_connect_locked(r);
    give_mutex();
  }
}

static void run_try_saved_job(bool allow_disc) {
  if (WiFi.getMode() == WIFI_AP) {
    if (take_mutex(pdMS_TO_TICKS(50))) {
      finish_try_saved_locked(WifiOpsResult::InternalError, kTrySavedSlotNone);
      give_mutex();
    }
    return;
  }

  const uint8_t count = wifiCredStoreSavedCount();
  if (count == 0) {
    if (take_mutex(pdMS_TO_TICKS(50))) {
      finish_try_saved_locked(WifiOpsResult::NoSavedNetworks, kTrySavedSlotNone);
      give_mutex();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED && !allow_disc) {
    if (take_mutex(pdMS_TO_TICKS(50))) {
      finish_try_saved_locked(WifiOpsResult::AlreadyConnected, kTrySavedSlotNone);
      give_mutex();
    }
    return;
  }

  uint8_t start = 0;
  {
    const uint8_t last1 = wifiCredStoreLastSuccessId1();
    if (last1 != 0 && last1 <= count) {
      start = static_cast<uint8_t>(last1 - 1U);
    }
  }

  char ssid_buf[33]{};
  char pass_buf[WIFI_CRED_PASS_CAP]{};

  for (uint8_t attempt = 0; attempt < count; ++attempt) {
    if (s_cancel_requested.load(std::memory_order_acquire)) {
      if (take_mutex(pdMS_TO_TICKS(50))) {
        finish_try_saved_locked(WifiOpsResult::Cancelled, kTrySavedSlotNone);
        give_mutex();
      }
      return;
    }

    const uint8_t slot = static_cast<uint8_t>((static_cast<uint16_t>(start) + attempt) % count);

    WifiCredStoreEntryView view{};
    if (!wifiCredStoreGetEntry(slot, &view) || view.ssid == nullptr) {
      if (take_mutex(pdMS_TO_TICKS(50))) {
        finish_try_saved_locked(WifiOpsResult::InternalError, kTrySavedSlotNone);
        give_mutex();
      }
      return;
    }
    strlcpy(ssid_buf, view.ssid, sizeof(ssid_buf));
    memset(pass_buf, 0, sizeof(pass_buf));
    if (!wifiCredStoreResolvePasswordForSlot(slot, pass_buf, sizeof(pass_buf))) {
      if (take_mutex(pdMS_TO_TICKS(50))) {
        finish_try_saved_locked(WifiOpsResult::InternalError, kTrySavedSlotNone);
        give_mutex();
      }
      return;
    }

    if (take_mutex(pdMS_TO_TICKS(50))) {
      s_snap.phase                = WifiOpsPhase::TryingSaved;
      s_snap.busy                 = true;
      s_snap.currentOp            = WifiOpsOp::TrySaved;
      s_snap.lastResult           = WifiOpsResult::None;
      s_snap.trySavedSlotIndex    = slot;
      s_snap.trySavedSavedTotal   = count;
      s_snap.trySavedAttemptIndex = attempt;
      strlcpy(s_snap.currentSsid, ssid_buf, sizeof(s_snap.currentSsid));
      s_snap.connectStartMs = millis();
      bump_seq_locked();
      give_mutex();
    }

    const WifiOpsResult r =
        attempt_sta_connect(ssid_buf, pass_buf, allow_disc, WIFI_OPS_CONNECT_TIMEOUT_MS, false, true);
    memset(pass_buf, 0, sizeof(pass_buf));

    if (r == WifiOpsResult::Success) {
      if (take_mutex(pdMS_TO_TICKS(50))) {
        finish_try_saved_locked(WifiOpsResult::Success, slot);
        give_mutex();
      }
      return;
    }
    if (r == WifiOpsResult::Cancelled) {
      if (take_mutex(pdMS_TO_TICKS(50))) {
        finish_try_saved_locked(WifiOpsResult::Cancelled, kTrySavedSlotNone);
        give_mutex();
      }
      return;
    }
    if (r == WifiOpsResult::InternalError || r == WifiOpsResult::AlreadyConnected) {
      if (take_mutex(pdMS_TO_TICKS(50))) {
        finish_try_saved_locked(r, kTrySavedSlotNone);
        give_mutex();
      }
      return;
    }
  }

  if (take_mutex(pdMS_TO_TICKS(50))) {
    finish_try_saved_locked(WifiOpsResult::AllFailed, kTrySavedSlotNone);
    give_mutex();
  }
}

static void worker_task(void* /*param*/) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    PendingCmd cmd = PendingCmd::None;
    char       ssid[33]{};
    char       pass[40]{};
    bool       allow_dc          = false;
    bool       try_saved_allow   = false;

    if (!take_mutex(pdMS_TO_TICKS(50))) {
      continue;
    }
    cmd = s_pending_cmd;
    s_pending_cmd = PendingCmd::None;
    if (cmd == PendingCmd::RunConnect) {
      memcpy(ssid, s_conn_ssid, sizeof(ssid));
      memcpy(pass, s_conn_pass, sizeof(pass));
      allow_dc = s_conn_allow_disconnect;
    } else if (cmd == PendingCmd::RunTrySaved) {
      try_saved_allow = s_try_saved_allow_disconnect;
    }
    give_mutex();

    switch (cmd) {
      case PendingCmd::RunScan:
        run_scan_job();
        break;
      case PendingCmd::RunConnect:
        run_connect_job(ssid, pass, allow_dc);
        break;
      case PendingCmd::RunTrySaved:
        run_try_saved_job(try_saved_allow);
        break;
      default:
        break;
    }
  }
}

}  // namespace

bool wifiOpsInit() {
  if (s_initialized) {
    return s_worker != nullptr;
  }
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == nullptr) {
    return false;
  }
  default_snapshot();
  clear_results_buffer();
  if (!ensure_worker_started()) {
    vSemaphoreDelete(s_mutex);
    s_mutex = nullptr;
    return false;
  }
  s_initialized = true;
  return true;
}

void wifiOpsReset() {
  if (!take_mutex(pdMS_TO_TICKS(50))) {
    return;
  }
  s_cancel_requested.store(true);
  if (!s_snap.busy) {
    clear_results_buffer();
    default_snapshot();
    s_pending_cmd = PendingCmd::None;
  }
  give_mutex();
}

bool wifiOpsIsBusy() {
  bool busy = false;
  if (!take_mutex(pdMS_TO_TICKS(10))) {
    return false;
  }
  busy = s_snap.busy;
  give_mutex();
  return busy;
}

bool wifiOpsGetSnapshot(WifiOpsSnapshot* out) {
  if (out == nullptr) {
    return false;
  }
  if (!take_mutex(pdMS_TO_TICKS(50))) {
    return false;
  }
  memcpy(out, &s_snap, sizeof(WifiOpsSnapshot));
  give_mutex();
  return true;
}

void wifiOpsCancel() {
  if (!take_mutex(pdMS_TO_TICKS(50))) {
    return;
  }
  s_cancel_requested.store(true);
  if (!s_snap.busy) {
    s_snap.phase      = WifiOpsPhase::Idle;
    s_snap.currentOp  = WifiOpsOp::None;
    s_snap.lastResult = WifiOpsResult::Cancelled;
    s_snap.busy       = false;
    reset_try_saved_progress_locked();
    clear_ssid_buffers();
    bump_seq_locked();
    s_pending_cmd = PendingCmd::None;
  }
  give_mutex();
}

bool wifiOpsRequestScan() {
  if (!wifiOpsInit()) {
    return false;
  }
  if (!take_mutex(0)) {
    return false;
  }
  if (op_in_progress_locked()) {
    s_snap.lastResult = WifiOpsResult::Busy;
    bump_seq_locked();
    give_mutex();
    return false;
  }
  s_cancel_requested.store(false);
  s_pending_cmd        = PendingCmd::RunScan;
  clear_results_buffer();
  s_snap.scanCount     = 0;
  s_snap.scanTruncated = false;
  reset_try_saved_progress_locked();
  s_snap.phase         = WifiOpsPhase::Scanning;
  s_snap.busy          = true;
  s_snap.currentOp     = WifiOpsOp::Scan;
  s_snap.lastResult    = WifiOpsResult::None;
  bump_seq_locked();
  give_mutex();

  if (s_worker != nullptr) {
    xTaskNotifyGive(s_worker);
  }
  return true;
}

bool wifiOpsGetScanResult(uint16_t index, WifiOpsScanRow* out) {
  if (out == nullptr) {
    return false;
  }
  if (!take_mutex(pdMS_TO_TICKS(50))) {
    return false;
  }
  if (index >= s_snap.scanCount) {
    give_mutex();
    return false;
  }
  memcpy(out, &s_results[index], sizeof(WifiOpsScanRow));
  give_mutex();
  return true;
}

static bool begin_connect_request(const char* ssid, const char* password, bool allow_disconnect_current) {
  if (!wifiOpsInit()) {
    return false;
  }
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }
  if (!take_mutex(0)) {
    return false;
  }
  if (op_in_progress_locked()) {
    s_snap.lastResult = WifiOpsResult::Busy;
    bump_seq_locked();
    give_mutex();
    return false;
  }

  memset(s_conn_ssid, 0, sizeof(s_conn_ssid));
  memset(s_conn_pass, 0, sizeof(s_conn_pass));
  strlcpy(s_conn_ssid, ssid, sizeof(s_conn_ssid));
  if (password != nullptr) {
    strlcpy(s_conn_pass, password, sizeof(s_conn_pass));
  }
  s_conn_allow_disconnect = allow_disconnect_current;

  s_cancel_requested.store(false);
  s_pending_cmd = PendingCmd::RunConnect;

  reset_try_saved_progress_locked();
  strlcpy(s_snap.currentSsid, ssid, sizeof(s_snap.currentSsid));
  s_snap.connectStartMs = millis();
  s_snap.phase          = WifiOpsPhase::Connecting;
  s_snap.busy           = true;
  s_snap.currentOp      = WifiOpsOp::Connect;
  s_snap.lastResult     = WifiOpsResult::None;
  bump_seq_locked();
  give_mutex();

  if (s_worker != nullptr) {
    xTaskNotifyGive(s_worker);
  }
  return true;
}

bool wifiOpsRequestConnectOpen(const char* ssid, bool allowDisconnectCurrent) {
  return begin_connect_request(ssid, "", allowDisconnectCurrent);
}

bool wifiOpsRequestConnectWithPassword(const char* ssid, const char* password, bool allowDisconnectCurrent) {
  return begin_connect_request(ssid, password, allowDisconnectCurrent);
}

bool wifiOpsRequestTrySavedNetworks(bool allowDisconnectCurrent) {
  if (!wifiOpsInit()) {
    return false;
  }
  if (!take_mutex(0)) {
    return false;
  }
  if (op_in_progress_locked()) {
    s_snap.lastResult = WifiOpsResult::Busy;
    bump_seq_locked();
    give_mutex();
    return false;
  }

  const uint8_t cnt = wifiCredStoreSavedCount();
  if (cnt == 0) {
    // Synchronous completion: no worker wake — caller polls lastResult / синхронно, без worker.
    reset_try_saved_progress_locked();
    s_snap.lastResult = WifiOpsResult::NoSavedNetworks;
    bump_seq_locked();
    give_mutex();
    return true;
  }

  s_cancel_requested.store(false);
  s_pending_cmd                 = PendingCmd::RunTrySaved;
  s_try_saved_allow_disconnect  = allowDisconnectCurrent;
  reset_try_saved_progress_locked();
  s_snap.trySavedSavedTotal = cnt;
  s_snap.phase              = WifiOpsPhase::TryingSaved;
  s_snap.busy               = true;
  s_snap.currentOp          = WifiOpsOp::TrySaved;
  s_snap.lastResult         = WifiOpsResult::None;
  bump_seq_locked();
  give_mutex();

  if (s_worker != nullptr) {
    xTaskNotifyGive(s_worker);
  }
  return true;
}
