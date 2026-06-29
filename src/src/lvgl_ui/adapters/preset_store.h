#ifndef PRESET_STORE_H
#define PRESET_STORE_H

/*
 * preset_store — Stage 6.4B: positional Preset slots (station number only, LittleFS).
 * preset_store — этап 6.4B: слоты Preset по номеру станции в плейлисте (только LittleFS).
 *
 * RAM cache: 8 × uint16_t (0 = empty). Loaded once via lazy begin(); no name/URL storage.
 * Кэш RAM: 8 × uint16_t; lazy begin(); без имён и URL.
 */

#include <stdint.h>

namespace lvgl_ui {
namespace preset_store {

constexpr uint8_t kSlotCount = 8;

// Lazy load from LittleFS (idempotent). Missing file → empty slots, no write.
// Ленивая загрузка из LittleFS; нет файла → пустые слоты без создания файла.
bool begin();

// True after begin() succeeded at least once (cache ready, even if all slots empty).
// true после успешного begin() — кэш готов.
bool isAvailable();

bool isOccupied(uint8_t slot);
uint16_t getStationNum(uint8_t slot);

// Saves config.lastStation() when valid; atomic 26-byte file write on success.
// Сохраняет lastStation() при валидном номере; атомарная запись файла.
bool saveCurrentStation(uint8_t slot);

} // namespace preset_store
} // namespace lvgl_ui

#endif // PRESET_STORE_H
