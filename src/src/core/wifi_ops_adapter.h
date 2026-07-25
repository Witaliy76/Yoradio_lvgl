#ifndef WIFI_OPS_ADAPTER_H
#define WIFI_OPS_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

// Manual Wi-Fi operations adapter: scan and connect in one worker (command dispatch).
// Адаптер ручных Wi-Fi операций: scan и connect в одном worker (dispatch команд).
// Author: Witaliy76 - https://github.com/Witaliy76

// Phase of the manual Wi-Fi ops state machine.
// Фаза state machine ручных Wi‑Fi операций.
enum class WifiOpsPhase : uint8_t {
  Idle = 0,
  Scanning,
  Connecting,
  TryingSaved,
  Success,
  Failed,
  Cancelled,
};

// Last completed / rejected outcome for UI polling.
// Последний исход для опроса UI.
enum class WifiOpsResult : uint8_t {
  None = 0,
  Success,
  Busy,
  NotImplemented,
  Cancelled,
  Timeout,
  AuthFailed,
  NoNetwork,
  InternalError,
  AlreadyConnected,
  NoSavedNetworks,
  AllFailed,
};

// High-level operation hint for snapshot.
// Подсказка операции для snapshot.
enum class WifiOpsOp : uint8_t {
  None = 0,
  Scan,
  Connect,
  TrySaved,
};

// One scanned AP row — POD only, fixed SSID buffer (no String / no pointers out).
// Одна строка скана — только POD, фиксированный буфер SSID.
struct WifiOpsScanRow {
  char     ssid[33];
  int32_t  rssi;
  uint8_t  auth;
  uint8_t  channel;
  uint8_t  reserved[2];
};

// Small snapshot for polling — no full scan list (use wifiOpsGetScanResult).
// Компактный snapshot для polling — не весь список (см. wifiOpsGetScanResult).
struct WifiOpsSnapshot {
  uint32_t     seq;
  uint32_t     resultsSeq;
  WifiOpsPhase phase;
  WifiOpsOp    currentOp;
  WifiOpsResult lastResult;
  bool         busy;
  uint16_t     scanCount;
  bool         scanTruncated;
  char         currentSsid[33];
  uint32_t     connectStartMs;
  // Try-saved progress (255 = none) / прогресс Try-saved (255 = нет).
  uint8_t      trySavedSlotIndex;
  uint8_t      trySavedSavedTotal;
  uint8_t      trySavedAttemptIndex;
  uint8_t      trySavedSuccessSlot;
};

bool wifiOpsInit();

void wifiOpsReset();

bool wifiOpsIsBusy();

bool wifiOpsGetSnapshot(WifiOpsSnapshot* out);

void wifiOpsCancel();

bool wifiOpsRequestScan();

bool wifiOpsGetScanResult(uint16_t index, WifiOpsScanRow* out);

bool wifiOpsRequestConnectOpen(const char* ssid, bool allowDisconnectCurrent);

bool wifiOpsRequestConnectWithPassword(const char* ssid, const char* password, bool allowDisconnectCurrent);

bool wifiOpsRequestTrySavedNetworks(bool allowDisconnectCurrent);

#endif
