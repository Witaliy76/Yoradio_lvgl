/*
 * RU: Словацкие calendar tables и направления ветра с нативной диакритикой.
 * EN: Slovak calendar tables and wind directions with native diacritics.
 * RU: SK package владеет данными; размеры таблиц проверяются compile-time validators.
 * EN: The SK package owns the data; compile-time validators enforce table sizes.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_I18N_LOCALES_SK_CALENDAR_H
#define YORADIO_I18N_LOCALES_SK_CALENDAR_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::sk {

inline constexpr std::array<const char*, 0> kMonthsFull{};
inline constexpr std::array<const char*, 12> kMonthsDate{{
  "januára", "februára", "marca", "apríla", "mája", "júna",
  "júla", "augusta", "septembra", "októbra", "novembra", "decembra"
}};
inline constexpr std::array<const char*, 7> kWeekdaysFull{{
  "nedeľa", "pondelok", "utorok", "streda", "štvrtok", "piatok", "sobota"
}};
inline constexpr std::array<const char*, 7> kWeekdaysShort{{
  "ne", "po", "ut", "st", "št", "pi", "so"
}};
inline constexpr std::array<const char*, 17> kWindDirections{{
  "Severný", "Severovýchodný", "Severovýchodný", "Východný", "Východný",
  "Juhovýchodný", "Juhovýchodný", "Južný", "Južný",
  "Juhozápadný", "Juhozápadný", "Západný", "Západný",
  "Severozápadný", "Severozápadný", "Severný", "Severný"
}};

inline constexpr CalendarData kCalendar{
  makeStringTableView(kMonthsFull),
  makeStringTableView(kMonthsDate),
  makeStringTableView(kWeekdaysFull),
  makeStringTableView(kWeekdaysShort),
  makeStringTableView(kWindDirections)
};

inline constexpr CalendarTableCounts kCalendarCounts = kCalendarCountsExpected;

}  // namespace i18n::locales::sk

#endif
