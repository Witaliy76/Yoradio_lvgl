/*
 * RU: Единственный владелец выбранного locale package и O(1) i18n accessors.
 * EN: Sole owner of the selected locale package and O(1) i18n accessors.
 * RU: Только этот translation unit подключает locale_select.h, исключая дублирование пакетов.
 * EN: Only this translation unit includes locale_select.h, preventing package duplication.
 */
#include "../core/options.h"

#include "i18n.h"
#include "locale_select.h"

namespace i18n {

namespace {

const char* tableValue(StringTableView table, std::size_t index) noexcept {
  if (table.values == nullptr || index >= table.count || table.values[index] == nullptr) return "";
  return table.values[index];
}

}  // namespace

const char* text(TextId id) noexcept {
  const std::size_t index = static_cast<std::size_t>(id);
  if (index >= selected_locale::kStrings.size()) return "";
  return selected_locale::kStrings[index].value;
}

const LocaleMetadata& locale() noexcept {
  return selected_locale::kMetadata;
}

const CalendarData& calendar() noexcept {
  return selected_locale::kCalendar;
}

const char* dayShort(uint8_t index) noexcept {
  return tableValue(selected_locale::kCalendar.weekdaysShort, index);
}

const char* monthName(uint8_t index) noexcept {
  return tableValue(selected_locale::kCalendar.monthsDate, index);
}

const char* windDirection(uint8_t index) noexcept {
  return tableValue(selected_locale::kCalendar.windDirections, index);
}

}  // namespace i18n
