/*
 * RU: Русский каталог человекочитаемых строк compile-time i18n.
 * EN: Russian catalog of human-readable compile-time i18n strings.
 * RU: RU package владеет значениями; порядок и placeholders обязаны совпадать с TextId/TextSpec.
 * EN: The RU package owns values; ordering and placeholders must match TextId/TextSpec.
 */
#ifndef YORADIO_I18N_LOCALES_RU_STRINGS_H
#define YORADIO_I18N_LOCALES_RU_STRINGS_H

#include <array>

#include "../../locale_types.h"

namespace i18n::locales::ru {

inline constexpr std::array<TextEntry, textCount()> kStrings{{
  {TextId::PlayerReady, "[готов]"},
  {TextId::PlayerStopped, "[остановлено]"},
  {TextId::PlayerConnecting, "[соединение]"},
  {TextId::BootConnectFormat, "Соединяюсь с %s"},
  {TextId::WaitForSd, "ИНДЕКС SD"},
  {TextId::OverlayConnectionLost, "ОТКЛЮЧЕНО"},
  {TextId::OverlayUpdating, "ОБНОВЛЕНИЕ"},
  {TextId::WeatherGustsPrefix, ", порывы "},
  {TextId::BootStarting, "Запуск..."},
  {TextId::BootNoSavedWifiNetworks, "Нет сохранённых сетей Wi-Fi"},
  {TextId::BootSavedWifiConnectionFailed, "Не удалось подключиться к сохранённым сетям"},
  {TextId::BootConnectedToFormat, "Подключено к %s"},
  {TextId::BootWifiFallbackName, "Wi-Fi"},
  {TextId::BootOpeningWifiSetup, "Открываю настройку Wi-Fi..."},
  {TextId::BootReconnectStatus, "Повторное подключение..."},
  {TextId::BootRecoveryCountdownFormat, "Повторное подключение...\nВосстановление Wi-Fi через %u с"},
  {TextId::BootOpeningRecovery, "Повторное подключение...\nОткрываю восстановление Wi-Fi..."},
  {TextId::WeatherFeelsLikePrefix, "Ощущается "},
  {TextId::WeatherMetricWind, "Ветер"},
  {TextId::WeatherMetricHumidity, "Влажность"},
  {TextId::WeatherMetricPressure, "Давление"},
  {TextId::WeatherMetricRain, "Осадки"},
  {TextId::WeatherTomorrow, "Завтра"},
  {TextId::WeatherPlus3Hours, "+3 ч"},
  {TextId::WeatherPlus6Hours, "+6 ч"},
  {TextId::WeatherPlus9Hours, "+9 ч"},
  {TextId::WeatherToday, "Сегодня"},
  {TextId::WeatherHourlyNearest, "Ближайшие часы"},
  {TextId::WeatherTodayTomorrow, "Сегодня / завтра"},
  {TextId::WeatherTomorrowLater, "Завтра / позже"},
  {TextId::WeatherForecastWaiting, "Ожидание данных о погоде"},
  {TextId::WeatherForecastNotLoaded, "Прогноз ещё не загружен"},
  {TextId::WeatherUnavailable, "Погода недоступна"},
  {TextId::WeatherPleaseWait, "Пожалуйста, подождите"},
  {TextId::WeatherMemoryDeferredMessage,
   "Недостаточно системной памяти. Обновление прогноза отложено, подождите."},
  {TextId::WeatherMemoryDeferredStatus,
   "Недостаточно системной памяти \xE2\x80\xA2 обновление прогноза отложено"},
  {TextId::WeatherTemporarilyUnavailable, "Погода временно недоступна"},
  {TextId::WeatherDataMayBeOutdated, "Данные могут быть устаревшими"},
  {TextId::WeatherRefreshing, "Обновление погоды..."},
  {TextId::WeatherTapToRefresh, "Нажать для обновления"},
  {TextId::WeatherTapToRetry, "Нажмите, чтобы повторить"},
  {TextId::WeatherUpdatedJustNow, "Обновлено только что"},
  {TextId::WeatherUpdatedMinutesAgoFormat, "Обновлено %u мин. назад"},
  {TextId::WeatherUpdatedHoursAgoFormat, "Обновлено %u ч. назад"},
  {TextId::WeatherHeroDateFullFormat, "Сегодня, %d %s %d, %s"},
  {TextId::WeatherHeroDateCompactFormat, "%s, %d %s"},
  {TextId::WeatherDailyDateFormat, "%s %02d.%02d"},
}};

}  // namespace i18n::locales::ru

#endif
