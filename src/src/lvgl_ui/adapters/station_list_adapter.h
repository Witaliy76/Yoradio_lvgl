#ifndef STATION_LIST_ADAPTER_H
#define STATION_LIST_ADAPTER_H

/*
 * station_list_adapter — thin LVGL-local station data boundary (Stage 6.3B).
 * station_list_adapter — тонкая граница данных станций для LVGL (этап 6.3B).
 *
 * Keeps scr_station independent from legacy Display/Canvas list code.
 * Не даёт scr_station зависеть от legacy Display/Canvas списка.
 */

#include <stddef.h>
#include <stdint.h>

namespace lvgl_ui {
namespace station_list_adapter {

// Cheap playlist identity for Station Page: count + active file size + play mode.
// Сигнатура плейлиста для Station: число станций, размер файла, режим.
struct StationListSignature {
    uint16_t station_count;
    uint32_t playlist_file_bytes;
    uint8_t play_mode;
};

uint16_t station_count();
uint16_t current_station_num();
bool is_valid_station_num(uint16_t num);
bool station_name(uint16_t num, char* out, size_t cap);
bool station_list_text(char* out, size_t cap, size_t name_limit);
bool list_signature(StationListSignature* out);
bool list_signature_equal(const StationListSignature& a, const StationListSignature& b);

// Stage 6.3G: request playback for one-based station index (same path as PR_PLAY elsewhere).
// Этап 6.3G: запуск станции через очередь плеера, без legacy UI.
bool play_station(uint16_t station_num);

} // namespace station_list_adapter
} // namespace lvgl_ui

#endif // STATION_LIST_ADAPTER_H
