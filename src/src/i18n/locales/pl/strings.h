/*
 * RU: Польский каталог человекочитаемых строк compile-time i18n с нативной диакритикой.
 * EN: Polish catalog of human-readable compile-time i18n strings with native diacritics.
 * RU: PL package владеет значениями; порядок и placeholders обязаны совпадать с TextId/TextSpec.
 * EN: The PL package owns values; ordering and placeholders must match TextId/TextSpec.
 */
#ifndef YORADIO_I18N_LOCALES_PL_STRINGS_H
#define YORADIO_I18N_LOCALES_PL_STRINGS_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::pl {

inline constexpr std::array<TextEntry, textCount()> kStrings{{
  {TextId::PlayerReady, "[gotowy]"},
  {TextId::PlayerStopped, "[zatrzymano]"},
  {TextId::PlayerConnecting, "[łączenie]"},
  {TextId::BootConnectFormat, "Łączenie z %s"},
  {TextId::WaitForSd, "INDEKS SD"},
  {TextId::OverlayConnectionLost, "ROZŁĄCZONO"},
  {TextId::OverlayUpdating, "AKTUALIZACJA"},
  {TextId::WeatherGustsPrefix, ", porywy "},
  {TextId::BootStarting, "Uruchamianie..."},
  {TextId::BootNoSavedWifiNetworks, "Brak zapisanych sieci Wi-Fi"},
  {TextId::BootSavedWifiConnectionFailed, "Nie udało się połączyć z zapisanymi sieciami"},
  {TextId::BootConnectedToFormat, "Połączono z %s"},
  {TextId::BootWifiFallbackName, "Wi-Fi"},
  {TextId::BootOpeningWifiSetup, "Otwieranie ustawień Wi-Fi..."},
  {TextId::BootReconnectStatus, "Ponowne łączenie..."},
  {TextId::BootRecoveryCountdownFormat, "Ponowne łączenie...\nOdzyskiwanie Wi-Fi za %u s"},
  {TextId::BootOpeningRecovery, "Ponowne łączenie...\nOtwieranie odzyskiwania Wi-Fi..."},
}};

}  // namespace i18n::locales::pl

#endif
