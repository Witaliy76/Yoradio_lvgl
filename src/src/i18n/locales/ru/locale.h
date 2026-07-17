#ifndef YORADIO_I18N_LOCALES_RU_LOCALE_H
#define YORADIO_I18N_LOCALES_RU_LOCALE_H

#include "calendar.h"
#include "strings.h"

namespace i18n::locales::ru {

inline constexpr LocaleMetadata kMetadata{"ru", "ru"};

static_assert(validateLocaleMetadata(kMetadata), "RU locale metadata is invalid");
static_assert(validateTextSpecs(kTextSpecs), "Text specification contains an unsupported printf format");
static_assert(validateTextCatalogShape(kStrings, kTextSpecs), "RU text catalog shape is invalid");
static_assert(validateCatalogFormatsSupported(kStrings), "RU text catalog contains an unsupported printf format");
static_assert(validateCatalogFormatSignatures(kStrings, kTextSpecs), "RU text placeholders do not match TextSpec");
static_assert(validateCalendar(kCalendar, kCalendarCounts), "RU calendar tables are invalid");

}  // namespace i18n::locales::ru

#endif
