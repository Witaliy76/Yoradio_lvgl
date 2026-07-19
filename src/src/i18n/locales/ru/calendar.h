/*
 * RU: Русские calendar tables и направления ветра выбранного locale package.
 * EN: Russian calendar tables and wind directions for the selected locale package.
 * RU: RU package владеет данными; размеры таблиц проверяются compile-time validators.
 * EN: The RU package owns the data; compile-time validators enforce table sizes.
 */
#ifndef YORADIO_I18N_LOCALES_RU_CALENDAR_H
#define YORADIO_I18N_LOCALES_RU_CALENDAR_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::ru {

inline constexpr std::array<const char*, 0> kMonthsFull{};
inline constexpr std::array<const char*, 12> kMonthsDate{{
  "января", "февраля", "марта", "апреля", "мая", "июня",
  "июля", "августа", "сентября", "октября", "ноября", "декабря"
}};
inline constexpr std::array<const char*, 7> kWeekdaysFull{{
  "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота"
}};
inline constexpr std::array<const char*, 7> kWeekdaysShort{{
  "вс", "пн", "вт", "ср", "чт", "пт", "сб"
}};
inline constexpr std::array<const char*, 17> kWindDirections{{
  "Северный", "Северо-Восточный", "Северо-Восточный", "Восточный", "Восточный",
  "Юго-Восточный", "Юго-Восточный", "Южный", "Южный",
  "Юго-Западный", "Юго-Западный", "Западный", "Западный",
  "Северо-Западный", "Северо-Западный", "Северный", "Северный"
}};

inline constexpr CalendarData kCalendar{
  makeStringTableView(kMonthsFull),
  makeStringTableView(kMonthsDate),
  makeStringTableView(kWeekdaysFull),
  makeStringTableView(kWeekdaysShort),
  makeStringTableView(kWindDirections)
};

inline constexpr CalendarTableCounts kCalendarCounts = kCalendarCountsExpected;

}  // namespace i18n::locales::ru

#endif
