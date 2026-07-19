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
  {TextId::BootStarting, "Starting..."},
  {TextId::BootNoSavedWifiNetworks, "No saved Wi-Fi networks"},
  {TextId::BootSavedWifiConnectionFailed, "Could not connect to saved networks"},
  {TextId::BootConnectedToFormat, "Connected to %s"},
  {TextId::BootWifiFallbackName, "Wi-Fi"},
  {TextId::BootOpeningWifiSetup, "Opening Wi-Fi Setup..."},
  {TextId::BootReconnectStatus, "Trying to reconnect..."},
  {TextId::BootRecoveryCountdownFormat, "Trying to reconnect...\nWi-Fi Recovery in %u s"},
  {TextId::BootOpeningRecovery, "Trying to reconnect...\nOpening Wi-Fi Recovery..."},
  {TextId::WeatherFeelsLikePrefix, "Feels like "},
  {TextId::WeatherMetricWind, "Wind"},
  {TextId::WeatherMetricHumidity, "Humidity"},
  {TextId::WeatherMetricPressure, "Pressure"},
  {TextId::WeatherMetricRain, "Rain"},
  {TextId::WeatherTomorrow, "Tomorrow"},
  {TextId::WeatherPlus3Hours, "+3 h"},
  {TextId::WeatherPlus6Hours, "+6 h"},
  {TextId::WeatherPlus9Hours, "+9 h"},
  {TextId::WeatherToday, "Today"},
  {TextId::WeatherHourlyNearest, "Next few hours"},
  {TextId::WeatherTodayTomorrow, "Today / tomorrow"},
  {TextId::WeatherTomorrowLater, "Tomorrow / later"},
  {TextId::WeatherForecastWaiting, "Waiting for weather data"},
  {TextId::WeatherForecastNotLoaded, "Forecast not loaded yet"},
  {TextId::WeatherUnavailable, "Weather unavailable"},
  {TextId::WeatherPleaseWait, "Please wait"},
  {TextId::WeatherMemoryDeferredMessage,
   "Not enough system memory. Forecast update deferred; please wait."},
  {TextId::WeatherMemoryDeferredStatus,
   "Not enough system memory \xE2\x80\xA2 forecast update deferred"},
  {TextId::WeatherTemporarilyUnavailable, "Weather temporarily unavailable"},
  {TextId::WeatherDataMayBeOutdated, "Data may be outdated"},
  {TextId::WeatherRefreshing, "Updating weather..."},
  {TextId::WeatherTapToRefresh, "Tap to refresh"},
  {TextId::WeatherTapToRetry, "Tap to retry"},
  {TextId::WeatherUpdatedJustNow, "Updated just now"},
  {TextId::WeatherUpdatedMinutesAgoFormat, "Updated %u min ago"},
  {TextId::WeatherUpdatedHoursAgoFormat, "Updated %u h ago"},
  {TextId::WeatherHeroDateFullFormat, "Today, %d %s %d, %s"},
  {TextId::WeatherHeroDateCompactFormat, "%s, %d %s"},
  {TextId::WeatherDailyDateFormat, "%s %02d.%02d"},
}};

}  // namespace i18n::locales::en

#endif
