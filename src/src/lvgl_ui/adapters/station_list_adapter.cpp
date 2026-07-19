/*
 * RU: Граница Station data для LVGL; внешние имена сохраняются без перевода.
 * EN: Station data boundary for LVGL; external names remain untranslated.
 * RU: Только application-owned empty/fallback text поступает из выбранного i18n-пакета.
 * EN: Only application-owned empty/fallback text comes from the selected i18n package.
 */
#include "station_list_adapter.h"

#include <cstdio>
#include <cstring>

#include "../../core/config.h"
#include "../../core/player.h"
#include "../../i18n/i18n.h"

namespace lvgl_ui {
namespace station_list_adapter {

namespace {

static bool copy_complete(char* out, size_t cap, const char* text) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (!text) return false;
    const size_t bytes = strlen(text) + 1u;
    if (bytes > cap) return false;
    memcpy(out, text, bytes);
    return true;
}

static bool format_fallback_name(char* out, size_t cap, uint16_t num) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    const int written = snprintf(out, cap,
                                 i18n::text(i18n::TextId::StationFallbackNameFormat),
                                 static_cast<unsigned>(num));
    if (written >= 0 && static_cast<size_t>(written) < cap) return true;
    return copy_complete(out, cap, "--");
}

static void truncate_utf8_in_place(char* s, size_t max_bytes) {
    if (!s) return;
    const size_t len = strlen(s);
    if (len <= max_bytes) return;
    if (max_bytes == 0u) {
        s[0] = '\0';
        return;
    }
    size_t cut = max_bytes;
    while (cut > 0u && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) {
        --cut;
    }
    if (cut == 0u) cut = max_bytes;
    s[cut] = '\0';
}

static bool read_playlist_name(File& playlist, char* out, size_t cap) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    size_t used = 0;
    bool got_any = false;
    bool skip_rest = false;

    while (playlist.available()) {
        const int raw = playlist.read();
        if (raw < 0) break;
        const char c = static_cast<char>(raw);
        if (c == '\r') continue;
        if (c == '\n') break;
        if (c == '\t') {
            skip_rest = true;
            continue;
        }
        if (skip_rest) continue;
        got_any = true;
        if (used + 1u < cap) {
            out[used++] = c;
            out[used] = '\0';
        }
    }
    return got_any && out[0] != '\0';
}

static bool append_station_line(char* out, size_t cap, size_t& used, uint16_t num, const char* name) {
    if (!out || cap == 0u || used >= cap - 1u || !name) return false;
    // Double gap between index and name vs orig. 2 spaces (Stage 6.3 layout tweak).
    // Вдвое больше зазор между номером и названием (было 2 пробела).
    const char* fmt = (num < 100u) ? "%02u    %s\n" : "%u    %s\n";
    const int written = snprintf(out + used, cap - used, fmt, static_cast<unsigned>(num), name);
    if (written <= 0) {
        out[used] = '\0';
        return false;
    }
    if (static_cast<size_t>(written) >= cap - used) {
        // snprintf already null-terminated within cap; keep partial line / не затирать out[used].
        return false;
    }
    used += static_cast<size_t>(written);
    return true;
}

} // namespace

uint16_t station_count() {
    return config.store.countStation;
}

uint16_t current_station_num() {
    return config.lastStation();
}

bool is_valid_station_num(uint16_t num) {
    return num >= 1u && num <= station_count();
}

bool station_name(uint16_t num, char* out, size_t cap) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (!is_valid_station_num(num)) return false;

    // config.stationByNum() returns a shared internal buffer; copy immediately.
    // config.stationByNum() возвращает общий внутренний буфер; сразу копируем.
    const char* name = config.stationByNum(num);
    if (!name || name[0] == '\0') return false;
    strlcpy(out, name, cap);
    return out[0] != '\0';
}

bool list_signature(StationListSignature* out) {
    if (!out) return false;
    out->station_count = station_count();
    out->play_mode = config.getMode();
    out->playlist_file_bytes = 0;
    const char* path = (out->play_mode == PM_WEB) ? PLAYLIST_PATH : PLAYLIST_SD_PATH;
    FS* fs = config.SDPLFS();
    if (fs && fs->exists(path)) {
        File f = fs->open(path, "r");
        if (f) {
            out->playlist_file_bytes = f.size();
            f.close();
        }
    }
    return true;
}

bool list_signature_equal(const StationListSignature& a, const StationListSignature& b) {
    return a.station_count == b.station_count && a.playlist_file_bytes == b.playlist_file_bytes && a.play_mode == b.play_mode;
}

bool station_list_text(char* out, size_t cap, size_t name_limit) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';

    const uint16_t total = station_count();
    if (total == 0u) {
        return copy_complete(out, cap, i18n::text(i18n::TextId::StationEmptyList));
    }

    FS* fs = config.SDPLFS();
    const char* path = (config.getMode() == PM_WEB) ? PLAYLIST_PATH : PLAYLIST_SD_PATH;
    File playlist = fs ? fs->open(path, "r") : File();
    if (!playlist) return false;

    size_t used = 0;
    for (uint16_t n = 1u; n <= total && playlist.available(); ++n) {
        char name_buf[160];
        if (!read_playlist_name(playlist, name_buf, sizeof(name_buf))) {
            format_fallback_name(name_buf, sizeof(name_buf), n);
        }
        truncate_utf8_in_place(name_buf, name_limit);
        if (!append_station_line(out, cap, used, n, name_buf)) {
            break;
        }
    }

    playlist.close();
    return out[0] != '\0';
}

bool play_station(uint16_t station_num) {
    if (!is_valid_station_num(station_num)) {
        return false;
    }
    // Same queue path as controls/telnet — player task owns connect / NEWSTATION display flow.
    // Та же очередь, что у крутилок: аудиозадача и Display по событиям обновят маркер/шапку.
    player.sendCommand({PR_PLAY, static_cast<int>(station_num)});
    return true;
}

} // namespace station_list_adapter
} // namespace lvgl_ui
