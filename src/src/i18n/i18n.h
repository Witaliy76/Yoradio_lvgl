/*
 * RU: Стабильный публичный API к выбранным compile-time строкам, metadata и calendar data.
 * EN: Stable public API for selected compile-time strings, metadata, and calendar data.
 * RU: Возвращаемые данные статичны; runtime switching и heap allocation отсутствуют.
 * EN: Returned data is static; runtime switching and heap allocation are not supported.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_I18N_H
#define YORADIO_I18N_H

#include "locale_types.h"

namespace i18n {

// All returned data belongs to the compile-time selected locale package and
// has static lifetime. No runtime language state or allocation is involved.
const char* text(TextId id) noexcept;
const LocaleMetadata& locale() noexcept;
const CalendarData& calendar() noexcept;
const char* dayFull(uint8_t index) noexcept;
const char* dayShort(uint8_t index) noexcept;
const char* monthName(uint8_t index) noexcept;
const char* windDirection(uint8_t index) noexcept;

}  // namespace i18n

#endif
