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
  {TextId::WeatherFeelsLikePrefix, "Odczuwalna "},
  {TextId::WeatherMetricWind, "Wiatr"},
  {TextId::WeatherMetricHumidity, "Wilgotność"},
  {TextId::WeatherMetricPressure, "Ciśnienie"},
  {TextId::WeatherMetricRain, "Opady"},
  {TextId::WeatherTomorrow, "Jutro"},
  {TextId::WeatherPlus3Hours, "+3 h"},
  {TextId::WeatherPlus6Hours, "+6 h"},
  {TextId::WeatherPlus9Hours, "+9 h"},
  {TextId::WeatherToday, "Dzisiaj"},
  {TextId::WeatherHourlyNearest, "Najbliższe godziny"},
  {TextId::WeatherTodayTomorrow, "Dzisiaj / jutro"},
  {TextId::WeatherTomorrowLater, "Jutro / później"},
  {TextId::WeatherForecastWaiting, "Oczekiwanie na dane pogodowe"},
  {TextId::WeatherForecastNotLoaded, "Prognoza nie została jeszcze wczytana"},
  {TextId::WeatherUnavailable, "Pogoda niedostępna"},
  {TextId::WeatherPleaseWait, "Proszę czekać"},
  {TextId::WeatherMemoryDeferredMessage,
   "Za mało pamięci systemowej. Aktualizację prognozy odłożono; proszę czekać."},
  {TextId::WeatherMemoryDeferredStatus,
   "Za mało pamięci systemowej \xE2\x80\xA2 aktualizację prognozy odłożono"},
  {TextId::WeatherTemporarilyUnavailable, "Pogoda tymczasowo niedostępna"},
  {TextId::WeatherDataMayBeOutdated, "Dane mogą być nieaktualne"},
  {TextId::WeatherRefreshing, "Aktualizacja pogody..."},
  {TextId::WeatherTapToRefresh, "Dotknij, aby odświeżyć"},
  {TextId::WeatherTapToRetry, "Dotknij, aby ponowić"},
  {TextId::WeatherUpdatedJustNow, "Zaktualizowano przed chwilą"},
  {TextId::WeatherUpdatedMinutesAgoFormat, "Zaktualizowano %u min temu"},
  {TextId::WeatherUpdatedHoursAgoFormat, "Zaktualizowano %u godz. temu"},
  {TextId::WeatherHeroDateFullFormat, "Dzisiaj, %d %s %d, %s"},
  {TextId::WeatherHeroDateCompactFormat, "%s, %d %s"},
  {TextId::WeatherDailyDateFormat, "%s %02d.%02d"},
}};

}  // namespace i18n::locales::pl

#endif
