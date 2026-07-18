/*
 * RU: Английский каталог человекочитаемых строк compile-time i18n.
 * EN: English catalog of human-readable compile-time i18n strings.
 * RU: EN package владеет значениями; порядок и placeholders обязаны совпадать с TextId/TextSpec.
 * EN: The EN package owns values; ordering and placeholders must match TextId/TextSpec.
 */
#ifndef YORADIO_I18N_LOCALES_EN_STRINGS_H
#define YORADIO_I18N_LOCALES_EN_STRINGS_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::en {

inline constexpr std::array<TextEntry, textCount()> kStrings{{
  {TextId::PlayerReady, "[ready]"},
  {TextId::PlayerStopped, "[stopped]"},
  {TextId::PlayerConnecting, "[connecting]"},
  {TextId::BootConnectFormat, "Trying to %s"},
  {TextId::WaitForSd, "INDEX SD"},
  {TextId::OverlayConnectionLost, "* LOST *"},
  {TextId::OverlayUpdating, "* UPDATING *"},
  {TextId::WeatherGustsPrefix, ", gusts "},
}};

}  // namespace i18n::locales::en

#endif
