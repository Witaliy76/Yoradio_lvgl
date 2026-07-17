#ifndef YORADIO_I18N_H
#define YORADIO_I18N_H

#include "locale_types.h"

namespace i18n {

// All returned data belongs to the compile-time selected locale package and
// has static lifetime. No runtime language state or allocation is involved.
const char* text(TextId id) noexcept;
const LocaleMetadata& locale() noexcept;
const CalendarData& calendar() noexcept;

}  // namespace i18n

#endif
