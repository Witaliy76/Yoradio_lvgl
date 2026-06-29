#include "preset_store.h"

#include <cstring>

#include <Arduino.h>
#include <LittleFS.h>

#include "../../core/config.h"
#include "station_list_adapter.h"

namespace lvgl_ui {
namespace preset_store {

namespace {

static constexpr char kMagic[4] = {'Y', 'P', 'S', 'T'};
static constexpr uint8_t kFileVersion = 1u;
static constexpr uint8_t kFileSlotCount = 8u;
static constexpr size_t kFileSize = 26u;
static constexpr size_t kPayloadCrcLen = 24u;

static const char kFinalPath[] = "/data/preset_slots.bin";
static const char kTmpPath[] = "/data/.preset_slots.tmp";

static uint16_t s_slots[kSlotCount] = {};
static bool s_loaded = false;

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over arbitrary bytes.
// CRC-16/CCITT-FALSE для проверки целостности файла.
static uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000u) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

static void write_u16_le(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v & 0xFFu);
    out[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t* in) {
    return static_cast<uint16_t>(in[0]) | (static_cast<uint16_t>(in[1]) << 8);
}

static void clear_slots_ram() {
    for (uint8_t i = 0; i < kSlotCount; ++i) {
        s_slots[i] = 0u;
    }
}

static bool parse_buffer(const uint8_t* buf, size_t len) {
    if (!buf || len != kFileSize) return false;
    if (memcmp(buf, kMagic, 4) != 0) return false;
    if (buf[4] != kFileVersion) return false;
    if (buf[5] != kFileSlotCount) return false;
    if (read_u16_le(buf + 6) != 0u) return false;

    const uint16_t expect_crc = read_u16_le(buf + kPayloadCrcLen);
    const uint16_t actual_crc = crc16_ccitt_false(buf, kPayloadCrcLen);
    if (expect_crc != actual_crc) return false;

    for (uint8_t i = 0; i < kSlotCount; ++i) {
        s_slots[i] = read_u16_le(buf + 8u + static_cast<size_t>(i) * 2u);
    }
    return true;
}

static bool read_file_into_buffer(const char* path, uint8_t* buf, size_t cap) {
    if (!path || !buf || cap < kFileSize) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    const size_t sz = f.size();
    if (sz != kFileSize) {
        f.close();
        return false;
    }
    const size_t n = f.read(buf, kFileSize);
    f.close();
    return n == kFileSize;
}

static bool validate_file_path(const char* path) {
    uint8_t buf[kFileSize];
    if (!read_file_into_buffer(path, buf, sizeof(buf))) return false;
    return parse_buffer(buf, sizeof(buf));
}

static bool load_from_path(const char* path) {
    uint8_t buf[kFileSize];
    if (!read_file_into_buffer(path, buf, sizeof(buf))) return false;
    if (!parse_buffer(buf, sizeof(buf))) {
        clear_slots_ram();
        return false;
    }
    return true;
}

static void serialize_to_buffer(uint8_t* buf) {
    memcpy(buf, kMagic, 4);
    buf[4] = kFileVersion;
    buf[5] = kFileSlotCount;
    write_u16_le(buf + 6, 0u);
    for (uint8_t i = 0; i < kSlotCount; ++i) {
        write_u16_le(buf + 8u + static_cast<size_t>(i) * 2u, s_slots[i]);
    }
    const uint16_t crc = crc16_ccitt_false(buf, kPayloadCrcLen);
    write_u16_le(buf + kPayloadCrcLen, crc);
}

static bool copy_file_fallback(const char* src_path, const char* dst_path) {
    File src = LittleFS.open(src_path, "r");
    if (!src) return false;
    File dst = LittleFS.open(dst_path, "w");
    if (!dst) {
        src.close();
        return false;
    }
    uint8_t buf[kFileSize];
    const size_t n = src.read(buf, kFileSize);
    src.close();
    if (n != kFileSize) {
        dst.close();
        return false;
    }
    const size_t w = dst.write(buf, kFileSize);
    dst.close();
    return w == kFileSize;
}

static bool commit_tmp_to_final() {
    if (!validate_file_path(kTmpPath)) return false;

    if (LittleFS.exists(kFinalPath)) {
        LittleFS.remove(kFinalPath);
    }
    if (LittleFS.rename(kTmpPath, kFinalPath)) {
        return true;
    }
    if (!copy_file_fallback(kTmpPath, kFinalPath)) {
        return false;
    }
    LittleFS.remove(kTmpPath);
    return validate_file_path(kFinalPath);
}

static bool write_slots_file() {
    uint8_t buf[kFileSize];
    serialize_to_buffer(buf);

    if (LittleFS.exists(kTmpPath)) {
        LittleFS.remove(kTmpPath);
    }

    File tmp = LittleFS.open(kTmpPath, "w");
    if (!tmp) return false;
    const size_t written = tmp.write(buf, kFileSize);
    tmp.flush();
    tmp.close();
    if (written != kFileSize) {
        LittleFS.remove(kTmpPath);
        return false;
    }

    if (!validate_file_path(kTmpPath)) {
        LittleFS.remove(kTmpPath);
        return false;
    }

    if (!commit_tmp_to_final()) {
        LittleFS.remove(kTmpPath);
        return false;
    }
    return true;
}

static void recover_and_load() {
    clear_slots_ram();

    const bool final_exists = LittleFS.exists(kFinalPath);
    const bool tmp_exists = LittleFS.exists(kTmpPath);
    const bool final_ok = final_exists && validate_file_path(kFinalPath);
    const bool tmp_ok = tmp_exists && validate_file_path(kTmpPath);

    if (final_ok) {
        if (tmp_exists) {
            LittleFS.remove(kTmpPath);
        }
        (void)load_from_path(kFinalPath);
        return;
    }

    if (final_exists && !final_ok) {
        Serial.println("[preset_store] invalid final file, trying recovery");
    }

    if (tmp_ok) {
        if (commit_tmp_to_final()) {
            (void)load_from_path(kFinalPath);
            return;
        }
        if (load_from_path(kTmpPath)) {
            Serial.println("[preset_store] loaded from valid tmp");
            return;
        }
    }

    if (tmp_exists) {
        LittleFS.remove(kTmpPath);
    }
}

} // namespace

bool begin() {
    if (s_loaded) return true;
    recover_and_load();
    s_loaded = true;
    return true;
}

bool isAvailable() {
    return s_loaded;
}

bool isOccupied(uint8_t slot) {
    if (!s_loaded || slot >= kSlotCount) return false;
    return s_slots[slot] != 0u;
}

uint16_t getStationNum(uint8_t slot) {
    if (!s_loaded || slot >= kSlotCount) return 0u;
    return s_slots[slot];
}

bool saveCurrentStation(uint8_t slot) {
    if (!begin()) return false;
    if (slot >= kSlotCount) return false;

    const uint16_t station = config.lastStation();
    if (!station_list_adapter::is_valid_station_num(station)) {
        return false;
    }

    const uint16_t prev = s_slots[slot];
    s_slots[slot] = station;
    if (!write_slots_file()) {
        s_slots[slot] = prev;
        return false;
    }
    return true;
}

} // namespace preset_store
} // namespace lvgl_ui
