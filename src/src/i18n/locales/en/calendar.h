/*
 * RU: Английские calendar tables и направления ветра выбранного locale package.
 * EN: English calendar tables and wind directions for the selected locale package.
 * RU: EN package владеет данными; размеры таблиц проверяются compile-time validators.
 * EN: The EN package owns the data; compile-time validators enforce table sizes.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_I18N_LOCALES_EN_CALENDAR_H
#define YORADIO_I18N_LOCALES_EN_CALENDAR_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::en {

inline constexpr std::array<const char*, 0> kMonthsFull{};
inline constexpr std::array<const char*, 12> kMonthsDate{{
  "january", "february", "march", "april", "may", "june",
  "july", "august", "september", "october", "november", "december"
}};
inline constexpr std::array<const char*, 7> kWeekdaysFull{{
  "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"
}};
inline constexpr std::array<const char*, 7> kWeekdaysShort{{
  "su", "mo", "tu", "we", "th", "fr", "sa"
}};
inline constexpr std::array<const char*, 17> kWindDirections{{
  "North", "North-Eastern", "North-Eastern", "Eastern", "Eastern",
  "South-Eastern", "South-Eastern", "Southern", "Southern",
  "South-Western", "South-Western", "Western", "Western",
  "North-West", "North-West", "North", "North"
}};

inline constexpr CalendarData kCalendar{
  makeStringTableView(kMonthsFull),
  makeStringTableView(kMonthsDate),
  makeStringTableView(kWeekdaysFull),
  makeStringTableView(kWeekdaysShort),
  makeStringTableView(kWindDirections)
};

inline constexpr CalendarTableCounts kCalendarCounts = kCalendarCountsExpected;

}  // namespace i18n::locales::en

#endif
