/*
 * RU: Временный forwarding bridge от legacy имён к публичному compile-time i18n API.
 * EN: Temporary forwarding bridge from legacy names to the public compile-time i18n API.
 * RU: Не владеет переводами/selector и удаляется после прямой миграции в L30/L70.
 * EN: Owns no translations or selector and is removed after direct migration in L30/L70.
 */
#ifndef YORADIO_I18N_LEGACY_COMPAT_H
#define YORADIO_I18N_LEGACY_COMPAT_H

#include <pgmspace.h>

#include "i18n.h"

// Forwarding-only L20 bridge for consumers intentionally deferred to L30/L70.
// It owns no locale data and is removed after those consumers use i18n directly.
namespace i18n::legacy {

class TextRef {
 public:
  constexpr explicit TextRef(TextId id) noexcept : id_(id) {}

  operator const char*() const noexcept {
    return text(id_);
  }

 private:
  TextId id_;
};

enum class CalendarTable : uint8_t {
  WeekdaysShort,
  MonthsDate
};

class CalendarTableRef {
 public:
  constexpr explicit CalendarTableRef(CalendarTable table) noexcept : table_(table) {}

  const char* operator[](std::size_t index) const noexcept {
    if (index > 0xFFU) return "";
    const uint8_t boundedIndex = static_cast<uint8_t>(index);
    return table_ == CalendarTable::WeekdaysShort
               ? dayShort(boundedIndex)
               : monthName(boundedIndex);
  }

 private:
  CalendarTable table_;
};

}  // namespace i18n::legacy

inline constexpr i18n::legacy::TextRef const_DlgLost{i18n::TextId::OverlayConnectionLost};
inline constexpr i18n::legacy::TextRef const_DlgUpdate{i18n::TextId::OverlayUpdating};
inline constexpr i18n::legacy::CalendarTableRef dow{i18n::legacy::CalendarTable::WeekdaysShort};
inline constexpr i18n::legacy::CalendarTableRef mnths{i18n::legacy::CalendarTable::MonthsDate};

#endif
