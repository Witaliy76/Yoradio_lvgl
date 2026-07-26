/*
 * RU: Собирает SK compile-time locale package и запускает его static validation.
 * EN: Assembles the SK compile-time locale package and runs its static validation.
 * RU: SK folder владеет metadata/строками/calendar data; пакет выбирается только locale_select.h.
 * EN: The SK folder owns metadata, strings, and calendar data; only locale_select.h selects it.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_I18N_LOCALES_SK_LOCALE_H
#define YORADIO_I18N_LOCALES_SK_LOCALE_H

#include "calendar.h"
#include "strings.h"

namespace i18n::locales::sk {

inline constexpr LocaleMetadata kMetadata{"sk", "sk"};

static_assert(validateLocaleMetadata(kMetadata), "SK locale metadata is invalid");
static_assert(validateTextSpecs(kTextSpecs), "Text specification contains an unsupported printf format");
static_assert(validateTextCatalogShape(kStrings, kTextSpecs), "SK text catalog shape is invalid");
static_assert(validateCatalogFormatsSupported(kStrings), "SK text catalog contains an unsupported printf format");
static_assert(validateCatalogFormatSignatures(kStrings, kTextSpecs), "SK text placeholders do not match TextSpec");
static_assert(validateCalendar(kCalendar, kCalendarCounts), "SK calendar tables are invalid");

}  // namespace i18n::locales::sk

#endif
