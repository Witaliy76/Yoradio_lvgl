#ifndef WIFI_CREDENTIALS_STORE_H
#define WIFI_CREDENTIALS_STORE_H

#include <stddef.h>
#include <stdint.h>

// Thin credential-store adapter over legacy /data/wifi.csv + Config::ssids[5].
// Wi-Fi UI / scan / connect stay out of this module.
// Тонкий адаптер учётных данных поверх legacy wifi.csv и Config::ssids[5].
// Author: Witaliy76 - https://github.com/Witaliy76

constexpr uint8_t WIFI_CRED_STORE_CAPACITY = 5;
// Mirrors struct neworkItem in config.h (ssid[30], password[40]) / как в config.h
constexpr size_t WIFI_CRED_SSID_CAP = 30;
constexpr size_t WIFI_CRED_PASS_CAP = 40;

struct WifiCredStoreEntryView {
  const char* ssid;
  const char* password;
};

bool wifiCredStoreReloadFromFs();

uint8_t wifiCredStoreSavedCount();

bool wifiCredStoreGetEntry(uint8_t index, WifiCredStoreEntryView* out);

int wifiCredStoreFindIndexBySsid(const char* ssid);

// Adds a new saved row, or no-op if SSID exists with same password.
// If SSID exists and password differs → false (use staging + commit after successful connect).
// Новая строка или no-op при совпадении пароля; смена пароля только через staging → commit.
bool wifiCredStoreAddOrUpdate(const char* ssid, const char* password);

bool wifiCredStoreRemoveAt(uint8_t index);

bool wifiCredStoreRemoveBySsid(const char* ssid);

bool wifiCredStorePersistToFs();

uint8_t wifiCredStoreLastSuccessId1();

bool wifiCredStoreSetLastSuccessFromSlot(uint8_t slotIndex0);

bool wifiCredStoreClearLastSuccess();

bool wifiCredStoreEntryFitsLegacyFile(const char* ssid, const char* password);

// --- Staged password edit (RAM only until commit + persist) / Двухфазное редактирование пароля ---
bool wifiCredStagingBegin(uint8_t slotIndex0);

void wifiCredStagingDiscard();

bool wifiCredStagingActive();

uint8_t wifiCredStagingSlot();

bool wifiCredStagingSetCandidatePassword(const char* password);

bool wifiCredStagingGetCandidatePassword(char* out, size_t outCap);

bool wifiCredStagingCommitToStoredPassword();

bool wifiCredStoreResolvePasswordForSlot(uint8_t slotIndex0, char* out, size_t outCap);

#endif
