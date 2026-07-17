#ifndef YORADIO_I18N_LOCALES_EN_CALENDAR_H
#define YORADIO_I18N_LOCALES_EN_CALENDAR_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::en {

inline constexpr std::array<const char*, 0> kMonthsFull{};
inline constexpr std::array<const char*, 0> kMonthsDate{};
inline constexpr std::array<const char*, 0> kWeekdaysFull{};
inline constexpr std::array<const char*, 0> kWeekdaysShort{};
inline constexpr std::array<const char*, 0> kWindDirections{};

inline constexpr CalendarData kCalendar{
  makeStringTableView(kMonthsFull),
  makeStringTableView(kMonthsDate),
  makeStringTableView(kWeekdaysFull),
  makeStringTableView(kWeekdaysShort),
  makeStringTableView(kWindDirections)
};

inline constexpr CalendarTableCounts kCalendarCounts{0, 0, 0, 0, 0};

}  // namespace i18n::locales::en

#endif
