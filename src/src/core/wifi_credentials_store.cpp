// Credential store adapter for LVGL Wi-Fi flow over Config::ssids / wifi.csv.
// Адаптер хранилища учётных данных для LVGL Wi-Fi поверх Config::ssids и wifi.csv.
// Author: Witaliy76 - https://github.com/Witaliy76

#include "wifi_credentials_store.h"

#include "config.h"
#include "display.h"
#include "../lvgl_ui/lvgl_ui.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <cstring>

namespace {

struct StagingState {
  bool        active;
  uint8_t     slot;
  char        candidatePass[WIFI_CRED_PASS_CAP];
};

StagingState g_stage;

void stagingDiscardInternal() {
  g_stage.active = false;
  g_stage.slot = 0;
  memset(g_stage.candidatePass, 0, sizeof(g_stage.candidatePass));
}

void adjustLastSsidAfterRemove(uint8_t removedIdx0) {
  const uint8_t ls = config.store.lastSSID;
  if (ls == 0) {
    return;
  }
  const uint8_t ls0 = static_cast<uint8_t>(ls - 1);
  if (ls0 == removedIdx0) {
    config.setLastSSID(0);
  } else if (ls0 > removedIdx0) {
    config.setLastSSID(static_cast<uint8_t>(ls - 1));
  }
}

}  // namespace

bool wifiCredStoreReloadFromFs() {
  stagingDiscardInternal();
  if (!fsIsReady()) {
    return false;
  }
  memset(config.ssids, 0, sizeof(config.ssids));
  config.ssidsCount = 0;
  return config.initNetwork();
}

uint8_t wifiCredStoreSavedCount() {
  return config.ssidsCount;
}

bool wifiCredStoreGetEntry(uint8_t index, WifiCredStoreEntryView* out) {
  if (!out || index >= config.ssidsCount) {
    return false;
  }
  out->ssid     = config.ssids[index].ssid;
  out->password = config.ssids[index].password;
  return true;
}

int wifiCredStoreFindIndexBySsid(const char* ssid) {
  if (!ssid) {
    return -1;
  }
  for (uint8_t i = 0; i < config.ssidsCount; i++) {
    if (strcmp(config.ssids[i].ssid, ssid) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool wifiCredStoreAddOrUpdate(const char* ssid, const char* password) {
  if (!ssid || ssid[0] == '\0') {
    return false;
  }
  const char* pass = password ? password : "";
  if (!wifiCredStoreEntryFitsLegacyFile(ssid, pass)) {
    return false;
  }
  stagingDiscardInternal();
  const int idx = wifiCredStoreFindIndexBySsid(ssid);
  if (idx >= 0) {
    if (strcmp(config.ssids[idx].password, pass) == 0) {
      return true;
    }
    return false;
  }
  if (config.ssidsCount >= WIFI_CRED_STORE_CAPACITY) {
    return false;
  }
  const uint8_t slot = config.ssidsCount;
  memset(&config.ssids[slot], 0, sizeof(config.ssids[slot]));
  strlcpy(config.ssids[slot].ssid, ssid, WIFI_CRED_SSID_CAP);
  strlcpy(config.ssids[slot].password, pass, WIFI_CRED_PASS_CAP);
  config.ssidsCount++;
  return true;
}

bool wifiCredStoreRemoveAt(uint8_t index) {
  if (index >= config.ssidsCount) {
    return false;
  }
  stagingDiscardInternal();
  adjustLastSsidAfterRemove(index);
  for (uint8_t i = index; i + 1 < config.ssidsCount; i++) {
    config.ssids[i] = config.ssids[i + 1];
  }
  memset(&config.ssids[config.ssidsCount - 1], 0, sizeof(config.ssids[config.ssidsCount - 1]));
  config.ssidsCount--;
  return true;
}

bool wifiCredStoreRemoveBySsid(const char* ssid) {
  const int idx = wifiCredStoreFindIndexBySsid(ssid);
  if (idx < 0) {
    return false;
  }
  return wifiCredStoreRemoveAt(static_cast<uint8_t>(idx));
}

bool wifiCredStorePersistToFs() {
  // Writes SSIDS_PATH directly (same rows as legacy reader); does NOT reboot — LVGL flow reboots separately.
  // Пишет SSIDS_PATH напрямую (те же строки, что читает legacy); без reboot — LVGL‑флоу перезагрузит отдельно.
  if (!fsIsReady()) {
    return false;
  }
  // This whole-file rewrite is one logical transaction. Open-for-write already mutates
  // LittleFS (including a mid-loop abort that leaves a truncated file). Repair E: mark runtime
  // recovery here; do not requestRgbResync on DspTask inside the LVGL click/timer path.
  // Полная перезапись файла — одна логическая транзакция. open("w") уже мутирует LittleFS,
  // включая обрыв цикла с усечённым файлом. Repair E: помечаем recovery, без immediate restart.
  File f = LittleFS.open(SSIDS_PATH, "w");
  if (!f) {
    return false;
  }
  bool ok = true;
  for (uint8_t i = 0; i < config.ssidsCount; i++) {
    const char* s = config.ssids[i].ssid;
    const char* p = config.ssids[i].password;
    if (!wifiCredStoreEntryFitsLegacyFile(s, p)) {
      f.close();
      ok = false;
      break;
    }
    f.print(s);
    f.print('\t');
    f.print(p);
    f.print('\n');
  }
  if (ok) {
    f.close();
  }
  lvgl_ui::requestRuntimeRgbRecovery();
  return ok;
}

uint8_t wifiCredStoreLastSuccessId1() {
  return config.store.lastSSID;
}

bool wifiCredStoreSetLastSuccessFromSlot(uint8_t slotIndex0) {
  if (slotIndex0 >= config.ssidsCount) {
    return false;
  }
  config.setLastSSID(static_cast<uint8_t>(slotIndex0 + 1));
  return true;
}

bool wifiCredStoreClearLastSuccess() {
  config.setLastSSID(0);
  return true;
}

bool wifiCredStoreEntryFitsLegacyFile(const char* ssid, const char* password) {
  if (!ssid || !password) {
    return false;
  }
  const size_t sl = strnlen(ssid, WIFI_CRED_SSID_CAP);
  const size_t pl = strnlen(password, WIFI_CRED_PASS_CAP);
  if (sl == 0 || sl >= WIFI_CRED_SSID_CAP || sl > 29) {
    return false;
  }
  if (pl >= WIFI_CRED_PASS_CAP) {
    return false;
  }
  if (sl + 1 + pl > 71) {
    return false;
  }
  return true;
}

bool wifiCredStagingBegin(uint8_t slotIndex0) {
  if (slotIndex0 >= config.ssidsCount) {
    return false;
  }
  stagingDiscardInternal();
  g_stage.active = true;
  g_stage.slot   = slotIndex0;
  memset(g_stage.candidatePass, 0, sizeof(g_stage.candidatePass));
  return true;
}

void wifiCredStagingDiscard() {
  stagingDiscardInternal();
}

bool wifiCredStagingActive() {
  return g_stage.active;
}

uint8_t wifiCredStagingSlot() {
  return g_stage.slot;
}

bool wifiCredStagingSetCandidatePassword(const char* password) {
  if (!g_stage.active) {
    return false;
  }
  const char* pass = password ? password : "";
  if (!wifiCredStoreEntryFitsLegacyFile(config.ssids[g_stage.slot].ssid, pass)) {
    return false;
  }
  strlcpy(g_stage.candidatePass, pass, WIFI_CRED_PASS_CAP);
  return true;
}

bool wifiCredStagingGetCandidatePassword(char* out, size_t outCap) {
  if (!g_stage.active || !out || outCap == 0) {
    return false;
  }
  strlcpy(out, g_stage.candidatePass, outCap);
  return true;
}

bool wifiCredStagingCommitToStoredPassword() {
  if (!g_stage.active) {
    return false;
  }
  const uint8_t s = g_stage.slot;
  if (s >= config.ssidsCount) {
    stagingDiscardInternal();
    return false;
  }
  strlcpy(config.ssids[s].password, g_stage.candidatePass, WIFI_CRED_PASS_CAP);
  stagingDiscardInternal();
  return true;
}

bool wifiCredStoreResolvePasswordForSlot(uint8_t slotIndex0, char* out, size_t outCap) {
  if (slotIndex0 >= config.ssidsCount || !out || outCap == 0) {
    return false;
  }
  if (wifiCredStagingActive() && wifiCredStagingSlot() == slotIndex0) {
    return wifiCredStagingGetCandidatePassword(out, outCap);
  }
  strlcpy(out, config.ssids[slotIndex0].password, outCap);
  return true;
}
