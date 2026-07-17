#ifndef YORADIO_I18N_LOCALE_TYPES_H
#define YORADIO_I18N_LOCALE_TYPES_H

#include <array>
#include <cstddef>

#include "text_ids.h"

namespace i18n {

struct TextEntry {
  TextId id;
  const char* value;
};

struct LocaleMetadata {
  const char* languageCode;
  const char* weatherApiLanguage;
};

struct StringTableView {
  const char* const* values;
  std::size_t count;
};

struct CalendarData {
  StringTableView monthsFull;
  StringTableView monthsDate;
  StringTableView weekdaysFull;
  StringTableView weekdaysShort;
  StringTableView windDirections;
};

struct CalendarTableCounts {
  std::size_t monthsFull;
  std::size_t monthsDate;
  std::size_t weekdaysFull;
  std::size_t weekdaysShort;
  std::size_t windDirections;
};

template <std::size_t Size>
constexpr StringTableView makeStringTableView(const std::array<const char*, Size>& values) noexcept {
  return {values.data(), values.size()};
}

constexpr bool isLowercaseLanguageCode(const char* code) noexcept {
  return code != nullptr &&
         code[0] >= 'a' && code[0] <= 'z' &&
         code[1] >= 'a' && code[1] <= 'z' &&
         code[2] == '\0';
}

constexpr bool validateLocaleMetadata(const LocaleMetadata& metadata) noexcept {
  return isLowercaseLanguageCode(metadata.languageCode) &&
         isLowercaseLanguageCode(metadata.weatherApiLanguage);
}

constexpr std::size_t cStringBytes(const char* value) noexcept {
  if (value == nullptr) return 0;
  std::size_t size = 1;
  while (*value++ != '\0') ++size;
  return size;
}

template <std::size_t SpecCount>
constexpr bool validateTextSpecs(const std::array<TextSpec, SpecCount>& specs) noexcept {
  if (SpecCount != textCount()) return false;
  for (std::size_t index = 0; index < SpecCount; ++index) {
    if (!specs[index].format.valid()) return false;
  }
  return true;
}

template <std::size_t EntryCount, std::size_t SpecCount>
constexpr bool validateTextCatalogShape(const std::array<TextEntry, EntryCount>& entries,
                                        const std::array<TextSpec, SpecCount>& specs) noexcept {
  if (EntryCount != textCount() || SpecCount != textCount() || EntryCount != SpecCount) return false;
  for (std::size_t index = 0; index < EntryCount; ++index) {
    if (entries[index].id != static_cast<TextId>(index) || entries[index].value == nullptr) return false;
    if (!specs[index].allowEmpty && entries[index].value[0] == '\0') return false;
    const std::size_t bytes = cStringBytes(entries[index].value);
    if (specs[index].maxBytes != 0 && bytes > specs[index].maxBytes) return false;
  }
  return true;
}

template <std::size_t EntryCount>
constexpr bool validateCatalogFormatsSupported(const std::array<TextEntry, EntryCount>& entries) noexcept {
  for (std::size_t index = 0; index < EntryCount; ++index) {
    if (!parseFormatSignature(entries[index].value).valid()) return false;
  }
  return true;
}

template <std::size_t EntryCount, std::size_t SpecCount>
constexpr bool validateCatalogFormatSignatures(const std::array<TextEntry, EntryCount>& entries,
                                               const std::array<TextSpec, SpecCount>& specs) noexcept {
  if (EntryCount != SpecCount) return false;
  for (std::size_t index = 0; index < EntryCount; ++index) {
    if (!formatSignaturesEqual(parseFormatSignature(entries[index].value), specs[index].format)) return false;
  }
  return true;
}

constexpr bool validateStringTable(StringTableView table, std::size_t expectedCount) noexcept {
  if (table.count != expectedCount) return false;
  if (table.count == 0) return true;
  if (table.values == nullptr) return false;
  for (std::size_t index = 0; index < table.count; ++index) {
    if (table.values[index] == nullptr || table.values[index][0] == '\0') return false;
  }
  return true;
}

constexpr bool validateCalendar(const CalendarData& calendar,
                                const CalendarTableCounts& counts) noexcept {
  return validateStringTable(calendar.monthsFull, counts.monthsFull) &&
         validateStringTable(calendar.monthsDate, counts.monthsDate) &&
         validateStringTable(calendar.weekdaysFull, counts.weekdaysFull) &&
         validateStringTable(calendar.weekdaysShort, counts.weekdaysShort) &&
         validateStringTable(calendar.windDirections, counts.windDirections);
}

}  // namespace i18n

#endif
