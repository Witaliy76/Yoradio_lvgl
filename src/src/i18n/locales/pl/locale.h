/*
 * RU: Собирает PL compile-time locale package и запускает его static validation.
 * EN: Assembles the PL compile-time locale package and runs its static validation.
 * RU: PL folder владеет metadata/строками/calendar data; пакет выбирается только locale_select.h.
 * EN: The PL folder owns metadata, strings, and calendar data; only locale_select.h selects it.
 */
#ifndef YORADIO_I18N_LOCALES_PL_LOCALE_H
#define YORADIO_I18N_LOCALES_PL_LOCALE_H

#include "calendar.h"
#include "strings.h"

namespace i18n::locales::pl {

inline constexpr LocaleMetadata kMetadata{"pl", "pl"};

static_assert(validateLocaleMetadata(kMetadata), "PL locale metadata is invalid");
static_assert(validateTextSpecs(kTextSpecs), "Text specification contains an unsupported printf format");
static_assert(validateTextCatalogShape(kStrings, kTextSpecs), "PL text catalog shape is invalid");
static_assert(validateCatalogFormatsSupported(kStrings), "PL text catalog contains an unsupported printf format");
static_assert(validateCatalogFormatSignatures(kStrings, kTextSpecs), "PL text placeholders do not match TextSpec");
static_assert(validateCalendar(kCalendar, kCalendarCounts), "PL calendar tables are invalid");

}  // namespace i18n::locales::pl

#endif
