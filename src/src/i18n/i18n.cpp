#include "../core/options.h"

#include "i18n.h"
#include "locale_select.h"

namespace i18n {

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

}  // namespace i18n
