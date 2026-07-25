/*
 * RU: Выбирает ровно один compile-time пакет локализации EN/RU/PL.
 * EN: Selects exactly one compile-time EN/RU/PL locale package.
 * RU: Подключается только владельцем данных i18n.cpp, не экранными модулями.
 * EN: Included only by the i18n.cpp data owner, never directly by screen modules.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_I18N_LOCALE_SELECT_H
#define YORADIO_I18N_LOCALE_SELECT_H

#ifndef L10N_LANGUAGE
  #error "L10N_LANGUAGE must be resolved before including locale_select.h"
#endif

#if L10N_LANGUAGE == EN
  #include "locales/en/locale.h"
  namespace i18n { namespace selected_locale = locales::en; }
#elif L10N_LANGUAGE == RU
  #include "locales/ru/locale.h"
  namespace i18n { namespace selected_locale = locales::ru; }
#elif L10N_LANGUAGE == PL
  #include "locales/pl/locale.h"
  namespace i18n { namespace selected_locale = locales::pl; }
#else
  #error "Unsupported L10N_LANGUAGE"
#endif

#endif
