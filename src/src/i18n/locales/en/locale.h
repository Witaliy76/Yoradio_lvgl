#ifndef YORADIO_I18N_LOCALES_EN_LOCALE_H
#define YORADIO_I18N_LOCALES_EN_LOCALE_H

#include "calendar.h"
#include "strings.h"

namespace i18n::locales::en {

inline constexpr LocaleMetadata kMetadata{"en", "en"};

static_assert(validateLocaleMetadata(kMetadata), "EN locale metadata is invalid");
static_assert(validateTextSpecs(kTextSpecs), "Text specification contains an unsupported printf format");
static_assert(validateTextCatalogShape(kStrings, kTextSpecs), "EN text catalog shape is invalid");
static_assert(validateCatalogFormatsSupported(kStrings), "EN text catalog contains an unsupported printf format");
static_assert(validateCatalogFormatSignatures(kStrings, kTextSpecs), "EN text placeholders do not match TextSpec");
static_assert(validateCalendar(kCalendar, kCalendarCounts), "EN calendar tables are invalid");

}  // namespace i18n::locales::en

#endif
