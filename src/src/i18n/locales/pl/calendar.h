/*
 * RU: Польские calendar tables и направления ветра с нативной диакритикой.
 * EN: Polish calendar tables and wind directions with native diacritics.
 * RU: PL package владеет данными; размеры таблиц проверяются compile-time validators.
 * EN: The PL package owns the data; compile-time validators enforce table sizes.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_I18N_LOCALES_PL_CALENDAR_H
#define YORADIO_I18N_LOCALES_PL_CALENDAR_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::pl {

inline constexpr std::array<const char*, 0> kMonthsFull{};
inline constexpr std::array<const char*, 12> kMonthsDate{{
  "stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca",
  "lipca", "sierpnia", "września", "października", "listopada", "grudnia"
}};
inline constexpr std::array<const char*, 7> kWeekdaysFull{{
  "niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota"
}};
inline constexpr std::array<const char*, 7> kWeekdaysShort{{
  "nd", "pn", "wt", "śr", "cz", "pt", "so"
}};
inline constexpr std::array<const char*, 17> kWindDirections{{
  "Północny", "Północno-wschodni", "Północno-wschodni", "Wschodni", "Wschodni",
  "Południowo-wschodni", "Południowo-wschodni", "Południowy", "Południowy",
  "Południowo-zachodni", "Południowo-zachodni", "Zachodni", "Zachodni",
  "Północno-zachodni", "Północno-zachodni", "Północny", "Północny"
}};

inline constexpr CalendarData kCalendar{
  makeStringTableView(kMonthsFull),
  makeStringTableView(kMonthsDate),
  makeStringTableView(kWeekdaysFull),
  makeStringTableView(kWeekdaysShort),
  makeStringTableView(kWindDirections)
};

inline constexpr CalendarTableCounts kCalendarCounts = kCalendarCountsExpected;

}  // namespace i18n::locales::pl

#endif
