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
}};

}  // namespace i18n::locales::ru

#endif
