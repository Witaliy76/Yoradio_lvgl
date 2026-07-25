/*
 * RU: Собирает EN compile-time locale package и запускает его static validation.
 * EN: Assembles the EN compile-time locale package and runs its static validation.
 * RU: EN folder владеет metadata/строками/calendar data; пакет выбирается только locale_select.h.
 * EN: The EN folder owns metadata, strings, and calendar data; only locale_select.h selects it.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
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
