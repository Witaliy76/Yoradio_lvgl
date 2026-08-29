/*
 * RU: Семантические TextId и центральные format specs compile-time i18n.
 * EN: Semantic TextId values and central format specs for compile-time i18n.
 * RU: Переводы принадлежат locale-пакетам; порядок и placeholders общие для всех языков.
 * EN: Locale packages own translations; ordering and placeholders are shared by all languages.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_I18N_TEXT_IDS_H
#define YORADIO_I18N_TEXT_IDS_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace i18n {

// IDs are ordered identically in every compile-time selected locale catalog.
enum class TextId : uint16_t {
  PlayerReady,
  PlayerStopped,
  PlayerConnecting,
  BootConnectFormat,
  WaitForSd,
  OverlayConnectionLost,
  OverlayUpdating,
  WeatherGustsPrefix,
  BootStarting,
  BootNoSavedWifiNetworks,
  BootSavedWifiConnectionFailed,
  BootConnectedToFormat,
  BootLoadingBackground,
  BootWifiFallbackName,
  BootOpeningWifiSetup,
  BootReconnectStatus,
  BootRecoveryCountdownFormat,
  BootOpeningRecovery,
  WeatherFeelsLikePrefix,
  WeatherMetricWind,
  WeatherMetricHumidity,
  WeatherMetricPressure,
  WeatherMetricRain,
  WeatherTomorrow,
  WeatherPlus3Hours,
  WeatherPlus6Hours,
  WeatherPlus9Hours,
  WeatherToday,
  WeatherHourlyNearest,
  WeatherTodayTomorrow,
  WeatherTomorrowLater,
  WeatherForecastWaiting,
  WeatherForecastNotLoaded,
  WeatherUnavailable,
  WeatherPleaseWait,
  WeatherMemoryDeferredMessage,
  WeatherMemoryDeferredStatus,
  WeatherTemporarilyUnavailable,
  WeatherDataMayBeOutdated,
  WeatherRefreshing,
  WeatherTapToRefresh,
  WeatherTapToRetry,
  WeatherUpdatedJustNow,
  WeatherUpdatedMinutesAgoFormat,
  WeatherUpdatedHoursAgoFormat,
  WeatherHeroDateFullFormat,
  WeatherHeroDateCompactFormat,
  WeatherDailyDateFormat,
  StationTitle,
  StationCountCurrentTotalFormat,
  StationCountUnknownTotalFormat,
  StationFooter,
  StationEmptyList,
  StationListUnavailable,
  StationFallbackNameFormat,
  PresetTitle,
  PresetEmptySlot,
  PresetUnavailable,
  PresetNoCurrentStation,
  PresetSaveFailed,
  PresetCountdownFormat,
  PresetSavedFormat,
  WifiHomeTitle,
  WifiNetworksTitle,
  WifiPasswordTitle,
  WifiHotspotTitle,
  WifiHomeSubtitleManual,
  WifiHomeSubtitleBootFailure,
  WifiHomeSubtitleDisconnected,
  WifiPasswordHint,
  WifiPasswordChangeHint,
  WifiSavedSubtitle,
  WifiActionScan,
  WifiActionHotspot,
  WifiActionBack,
  WifiActionRescan,
  WifiActionCancel,
  WifiActionHome,
  WifiActionConnect,
  WifiActionChangePassword,
  WifiActionRemove,
  WifiActionYes,
  WifiActionNo,
  WifiActionBackToRecovery,
  WifiHotspotAutoStartFormat,
  WifiHotspotSsidFormat,
  WifiHotspotPasswordOpen,
  WifiHotspotIpFormat,
  WifiHotspotIpPendingFormat,
  WifiHotspotHelpFormat,
  WifiNoNetwork,
  WifiScanning,
  WifiScanStarting,
  WifiScanNotStarted,
  WifiScanComplete,
  WifiScanCancelled,
  WifiScanTimeout,
  WifiBusy,
  WifiConnectingWarning,
  WifiConnectingOpen,
  WifiConnecting,
  WifiConnectCannotStart,
  WifiConnectBusyRetry,
  WifiConnectErrorRetry,
  WifiWrongPassword,
  WifiConnectTimeout,
  WifiCancelled,
  WifiInternalError,
  WifiConnectFinished,
  WifiOpenConnectSignalFailed,
  WifiSavedAuthOrSignalFailed,
  WifiConnectedRestarting,
  WifiSavedRestarting,
  WifiCannotSaveNetwork,
  WifiSavedNetworksFull,
  WifiCouldNotSaveNetwork,
  WifiCouldNotUpdatePassword,
  WifiPasswordMinChars,
  WifiPasswordPrompt,
  WifiNoNetworksFound,
  WifiUiMemoryLow,
  WifiScanMemoryLow,
  WifiScanOpenRowFormat,
  WifiScanSecuredRowFormat,
  WifiRemoveConfirm,
  WifiRemoveConfirmFormat,
  Count
};

constexpr std::size_t textCount() noexcept {
  return static_cast<std::size_t>(TextId::Count);
}

enum class FormatArgKind : uint8_t {
  SignedInteger,
  UnsignedInteger,
  String,
  Character,
  DynamicWidth,
  DynamicPrecision
};

enum class FormatLength : uint8_t {
  None,
  Char,
  Short,
  Long,
  LongLong,
  Size
};

struct FormatArgument {
  FormatArgKind kind;
  FormatLength length;
};

enum class FormatParseStatus : uint8_t {
  Valid,
  NullFormat,
  IncompleteSpecifier,
  TooManyArguments,
  UnsupportedPositionalArgument,
  UnsupportedLength,
  UnsupportedConversion
};

constexpr std::size_t kMaxFormatArguments = 8;

struct FormatSignature {
  std::array<FormatArgument, kMaxFormatArguments> arguments{};
  std::size_t count = 0;
  FormatParseStatus status = FormatParseStatus::Valid;

  constexpr bool valid() const noexcept {
    return status == FormatParseStatus::Valid;
  }
};

constexpr bool isFormatDigit(char value) noexcept {
  return value >= '0' && value <= '9';
}

constexpr bool isFormatFlag(char value) noexcept {
  return value == '-' || value == '+' || value == ' ' || value == '#' || value == '0';
}

constexpr bool hasPositionalSuffix(const char* format, std::size_t index) noexcept {
  if (format == nullptr || !isFormatDigit(format[index])) return false;
  while (isFormatDigit(format[index])) ++index;
  return format[index] == '$';
}

constexpr bool appendFormatArgument(FormatSignature& signature,
                                    FormatArgKind kind,
                                    FormatLength length = FormatLength::None) noexcept {
  if (signature.count >= signature.arguments.size()) {
    signature.status = FormatParseStatus::TooManyArguments;
    return false;
  }
  signature.arguments[signature.count++] = {kind, length};
  return true;
}

// Deliberately supports only the printf subset needed by the localization
// catalogs: integer, string, character, numeric width/precision, dynamic '*'
// width/precision and common integer length modifiers. Unsupported formats
// remain compile-time failures when catalog validators are instantiated.
constexpr FormatSignature parseFormatSignature(const char* format) noexcept {
  FormatSignature signature{};
  if (format == nullptr) {
    signature.status = FormatParseStatus::NullFormat;
    return signature;
  }

  for (std::size_t index = 0; format[index] != '\0'; ++index) {
    if (format[index] != '%') continue;

    ++index;
    if (format[index] == '\0') {
      signature.status = FormatParseStatus::IncompleteSpecifier;
      return signature;
    }
    if (format[index] == '%') continue;
    if (hasPositionalSuffix(format, index)) {
      signature.status = FormatParseStatus::UnsupportedPositionalArgument;
      return signature;
    }

    while (isFormatFlag(format[index])) ++index;

    if (format[index] == '*') {
      if (!appendFormatArgument(signature, FormatArgKind::DynamicWidth)) return signature;
      ++index;
      if (hasPositionalSuffix(format, index)) {
        signature.status = FormatParseStatus::UnsupportedPositionalArgument;
        return signature;
      }
    } else {
      while (isFormatDigit(format[index])) ++index;
    }

    if (format[index] == '.') {
      ++index;
      if (format[index] == '*') {
        if (!appendFormatArgument(signature, FormatArgKind::DynamicPrecision)) return signature;
        ++index;
        if (hasPositionalSuffix(format, index)) {
          signature.status = FormatParseStatus::UnsupportedPositionalArgument;
          return signature;
        }
      } else {
        while (isFormatDigit(format[index])) ++index;
      }
    }

    FormatLength length = FormatLength::None;
    if (format[index] == 'h') {
      if (format[index + 1] == 'h') {
        length = FormatLength::Char;
        index += 2;
      } else {
        length = FormatLength::Short;
        ++index;
      }
    } else if (format[index] == 'l') {
      if (format[index + 1] == 'l') {
        length = FormatLength::LongLong;
        index += 2;
      } else {
        length = FormatLength::Long;
        ++index;
      }
    } else if (format[index] == 'z') {
      length = FormatLength::Size;
      ++index;
    }

    const char conversion = format[index];
    if (conversion == '\0') {
      signature.status = FormatParseStatus::IncompleteSpecifier;
      return signature;
    }

    switch (conversion) {
      case 'd':
      case 'i':
        if (!appendFormatArgument(signature, FormatArgKind::SignedInteger, length)) return signature;
        break;
      case 'u':
      case 'o':
      case 'x':
      case 'X':
        if (!appendFormatArgument(signature, FormatArgKind::UnsignedInteger, length)) return signature;
        break;
      case 's':
        if (length != FormatLength::None) {
          signature.status = FormatParseStatus::UnsupportedLength;
          return signature;
        }
        if (!appendFormatArgument(signature, FormatArgKind::String)) return signature;
        break;
      case 'c':
        if (length != FormatLength::None) {
          signature.status = FormatParseStatus::UnsupportedLength;
          return signature;
        }
        if (!appendFormatArgument(signature, FormatArgKind::Character)) return signature;
        break;
      default:
        signature.status = FormatParseStatus::UnsupportedConversion;
        return signature;
    }
  }

  return signature;
}

constexpr bool formatSignaturesEqual(const FormatSignature& left,
                                     const FormatSignature& right) noexcept {
  if (!left.valid() || !right.valid() || left.count != right.count) return false;
  for (std::size_t index = 0; index < left.count; ++index) {
    if (left.arguments[index].kind != right.arguments[index].kind ||
        left.arguments[index].length != right.arguments[index].length) {
      return false;
    }
  }
  return true;
}

struct TextSpec {
  FormatSignature format;
  std::size_t maxBytes;
  bool allowEmpty;
};

constexpr TextSpec makeTextSpec(const char* format,
                                std::size_t maxBytes,
                                bool allowEmpty = false) noexcept {
  return {parseFormatSignature(format), maxBytes, allowEmpty};
}

inline constexpr std::array<TextSpec, textCount()> kTextSpecs{{
  makeTextSpec("", 32),    // PlayerReady
  makeTextSpec("", 32),    // PlayerStopped
  makeTextSpec("", 32),    // PlayerConnecting
  makeTextSpec("%s", 48),  // BootConnectFormat: saved SSID (29 bytes maximum)
  makeTextSpec("", 32),    // WaitForSd
  makeTextSpec("", 48),    // OverlayConnectionLost
  makeTextSpec("", 48),    // OverlayUpdating
  makeTextSpec("", 20),    // WeatherGustsPrefix: prefix for an integer gust value
  makeTextSpec("", 32),    // BootStarting
  makeTextSpec("", 64),    // BootNoSavedWifiNetworks
  makeTextSpec("", 112),   // BootSavedWifiConnectionFailed
  makeTextSpec("%s", 64),  // BootConnectedToFormat: connected SSID (32 bytes maximum)
  makeTextSpec("", 48),    // BootLoadingBackground
  makeTextSpec("", 16),    // BootWifiFallbackName
  makeTextSpec("", 64),    // BootOpeningWifiSetup
  makeTextSpec("", 64),    // BootReconnectStatus: safe non-formatted fallback
  makeTextSpec("%u", 120), // BootRecoveryCountdownFormat: seconds until Recovery
  makeTextSpec("", 120),   // BootOpeningRecovery
  makeTextSpec("", 32),    // WeatherFeelsLikePrefix
  makeTextSpec("", 24),    // WeatherMetricWind
  makeTextSpec("", 24),    // WeatherMetricHumidity
  makeTextSpec("", 24),    // WeatherMetricPressure
  makeTextSpec("", 24),    // WeatherMetricRain
  makeTextSpec("", 24),    // WeatherTomorrow
  makeTextSpec("", 12),    // WeatherPlus3Hours
  makeTextSpec("", 12),    // WeatherPlus6Hours
  makeTextSpec("", 12),    // WeatherPlus9Hours
  makeTextSpec("", 24),    // WeatherToday
  makeTextSpec("", 48),    // WeatherHourlyNearest
  makeTextSpec("", 48),    // WeatherTodayTomorrow
  makeTextSpec("", 48),    // WeatherTomorrowLater
  makeTextSpec("", 80),    // WeatherForecastWaiting
  makeTextSpec("", 80),    // WeatherForecastNotLoaded
  makeTextSpec("", 64),    // WeatherUnavailable
  makeTextSpec("", 48),    // WeatherPleaseWait
  makeTextSpec("", 160),   // WeatherMemoryDeferredMessage: centered WRAP text
  makeTextSpec("", 144),   // WeatherMemoryDeferredStatus: circular footer text
  makeTextSpec("", 80),    // WeatherTemporarilyUnavailable
  makeTextSpec("", 80),    // WeatherDataMayBeOutdated
  makeTextSpec("", 64),    // WeatherRefreshing
  makeTextSpec("", 64),    // WeatherTapToRefresh
  makeTextSpec("", 64),    // WeatherTapToRetry
  makeTextSpec("", 64),    // WeatherUpdatedJustNow
  makeTextSpec("%u", 80),  // WeatherUpdatedMinutesAgoFormat
  makeTextSpec("%u", 80),  // WeatherUpdatedHoursAgoFormat
  makeTextSpec("%d %s %d %s", 80),  // WeatherHeroDateFullFormat
  makeTextSpec("%s %d %s", 48),     // WeatherHeroDateCompactFormat
  makeTextSpec("%s %02d.%02d", 32), // WeatherDailyDateFormat
  makeTextSpec("", 24),       // StationTitle: uppercase page title
  makeTextSpec("%u %u", 24),  // StationCountCurrentTotalFormat: current, total
  makeTextSpec("%u", 24),     // StationCountUnknownTotalFormat: total
  makeTextSpec("", 80),       // StationFooter: fixed navigation helper, CLIP
  makeTextSpec("", 64),       // StationEmptyList: multiline renderer state
  makeTextSpec("", 64),       // StationListUnavailable: normalized renderer error
  makeTextSpec("%u", 32),     // StationFallbackNameFormat: one-based station number
  makeTextSpec("", 24),       // PresetTitle: uppercase Temporary title
  makeTextSpec("", 24),       // PresetEmptySlot: row state
  makeTextSpec("", 32),       // PresetUnavailable: invalid/missing station row
  makeTextSpec("", 64),       // PresetNoCurrentStation: footer feedback
  makeTextSpec("", 48),       // PresetSaveFailed: footer feedback
  makeTextSpec("%d", 96),     // PresetCountdownFormat: remaining seconds
  makeTextSpec("%u", 48),     // PresetSavedFormat: one-based preset slot
  makeTextSpec("", 64),       // WifiHomeTitle
  makeTextSpec("", 64),       // WifiNetworksTitle
  makeTextSpec("", 64),       // WifiPasswordTitle
  makeTextSpec("", 64),       // WifiHotspotTitle
  makeTextSpec("", 128),      // WifiHomeSubtitleManual: WRAP
  makeTextSpec("", 128),      // WifiHomeSubtitleBootFailure: WRAP
  makeTextSpec("", 128),      // WifiHomeSubtitleDisconnected: WRAP
  makeTextSpec("", 128),      // WifiPasswordHint: WRAP
  makeTextSpec("", 160),      // WifiPasswordChangeHint: WRAP
  makeTextSpec("", 128),      // WifiSavedSubtitle: WRAP
  makeTextSpec("", 32),       // WifiActionScan
  makeTextSpec("", 32),       // WifiActionHotspot
  makeTextSpec("", 32),       // WifiActionBack
  makeTextSpec("", 32),       // WifiActionRescan
  makeTextSpec("", 32),       // WifiActionCancel
  makeTextSpec("", 32),       // WifiActionHome
  makeTextSpec("", 32),       // WifiActionConnect
  makeTextSpec("", 32),       // WifiActionChangePassword
  makeTextSpec("", 32),       // WifiActionRemove
  makeTextSpec("", 16),       // WifiActionYes
  makeTextSpec("", 16),       // WifiActionNo
  makeTextSpec("", 64),       // WifiActionBackToRecovery
  makeTextSpec("%u", 96),     // WifiHotspotAutoStartFormat: seconds
  makeTextSpec("%s", 32),     // WifiHotspotSsidFormat: AP SSID
  makeTextSpec("", 48),       // WifiHotspotPasswordOpen
  makeTextSpec("%s", 32),     // WifiHotspotIpFormat: AP IP
  makeTextSpec("%s", 96),     // WifiHotspotIpPendingFormat: expected AP IP
  makeTextSpec("%s", 160),    // WifiHotspotHelpFormat: setup URL
  makeTextSpec("", 128),      // WifiNoNetwork
  makeTextSpec("", 64),       // WifiScanning
  makeTextSpec("", 64),       // WifiScanStarting
  makeTextSpec("", 96),       // WifiScanNotStarted
  makeTextSpec("", 64),       // WifiScanComplete
  makeTextSpec("", 64),       // WifiScanCancelled
  makeTextSpec("", 64),       // WifiScanTimeout
  makeTextSpec("", 48),       // WifiBusy
  makeTextSpec("", 128),      // WifiConnectingWarning
  makeTextSpec("", 96),       // WifiConnectingOpen
  makeTextSpec("", 64),       // WifiConnecting
  makeTextSpec("", 96),       // WifiConnectCannotStart
  makeTextSpec("", 96),       // WifiConnectBusyRetry
  makeTextSpec("", 96),       // WifiConnectErrorRetry
  makeTextSpec("", 64),       // WifiWrongPassword
  makeTextSpec("", 64),       // WifiConnectTimeout
  makeTextSpec("", 48),       // WifiCancelled
  makeTextSpec("", 64),       // WifiInternalError
  makeTextSpec("", 64),       // WifiConnectFinished
  makeTextSpec("", 128),      // WifiOpenConnectSignalFailed
  makeTextSpec("", 160),      // WifiSavedAuthOrSignalFailed
  makeTextSpec("", 96),       // WifiConnectedRestarting
  makeTextSpec("", 96),       // WifiSavedRestarting
  makeTextSpec("", 96),       // WifiCannotSaveNetwork
  makeTextSpec("", 160),      // WifiSavedNetworksFull
  makeTextSpec("", 96),       // WifiCouldNotSaveNetwork
  makeTextSpec("", 96),       // WifiCouldNotUpdatePassword
  makeTextSpec("", 64),       // WifiPasswordMinChars
  makeTextSpec("", 64),       // WifiPasswordPrompt
  makeTextSpec("", 64),       // WifiNoNetworksFound
  makeTextSpec("", 128),      // WifiUiMemoryLow
  makeTextSpec("", 96),       // WifiScanMemoryLow
  makeTextSpec("%s %d", 64),  // WifiScanOpenRowFormat: SSID, RSSI
  makeTextSpec("%s %d", 64),  // WifiScanSecuredRowFormat: SSID, RSSI
  makeTextSpec("", 48),       // WifiRemoveConfirm: no-SSID fallback
  makeTextSpec("%s", 64),     // WifiRemoveConfirmFormat: saved SSID
}};

// Parser foundation checks. These stay next to the constexpr implementation so
// every supported locale build verifies the exact signature semantics.
inline constexpr auto kFormatSigned = parseFormatSignature("%d");
inline constexpr auto kFormatUnsigned = parseFormatSignature("%u");
inline constexpr auto kFormatString = parseFormatSignature("%s");
inline constexpr auto kFormatZeroPaddedUnsigned = parseFormatSignature("%08u");
inline constexpr auto kFormatLongUnsigned = parseFormatSignature("%lu");
inline constexpr auto kFormatLongLongUnsigned = parseFormatSignature("%llu");
inline constexpr auto kFormatSizeUnsigned = parseFormatSignature("%zu");
inline constexpr auto kFormatDynamicString = parseFormatSignature("%*.*s");
inline constexpr auto kFormatEscapedPercent = parseFormatSignature("%%");

static_assert(kFormatSigned.valid() && kFormatSigned.count == 1 &&
              kFormatSigned.arguments[0].kind == FormatArgKind::SignedInteger,
              "%d must produce one signed-integer argument");
static_assert(kFormatUnsigned.valid() && kFormatUnsigned.count == 1 &&
              kFormatUnsigned.arguments[0].kind == FormatArgKind::UnsignedInteger,
              "%u must produce one unsigned-integer argument");
static_assert(kFormatString.valid() && kFormatString.count == 1 &&
              kFormatString.arguments[0].kind == FormatArgKind::String,
              "%s must produce one string argument");
static_assert(formatSignaturesEqual(kFormatZeroPaddedUnsigned, kFormatUnsigned),
              "Numeric width in %08u must not change the argument signature");
static_assert(kFormatLongUnsigned.valid() &&
              kFormatLongUnsigned.arguments[0].length == FormatLength::Long,
              "%lu must retain the long modifier");
static_assert(kFormatLongLongUnsigned.valid() &&
              kFormatLongLongUnsigned.arguments[0].length == FormatLength::LongLong,
              "%llu must retain the long-long modifier");
static_assert(kFormatSizeUnsigned.valid() &&
              kFormatSizeUnsigned.arguments[0].length == FormatLength::Size,
              "%zu must retain the size modifier");
static_assert(kFormatDynamicString.valid() && kFormatDynamicString.count == 3 &&
              kFormatDynamicString.arguments[0].kind == FormatArgKind::DynamicWidth &&
              kFormatDynamicString.arguments[1].kind == FormatArgKind::DynamicPrecision &&
              kFormatDynamicString.arguments[2].kind == FormatArgKind::String,
              "%*.*s must preserve width, precision, then value argument order");
static_assert(kFormatEscapedPercent.valid() && kFormatEscapedPercent.count == 0,
              "%% must not create a format argument");

static_assert(!formatSignaturesEqual(kFormatUnsigned, kFormatString),
              "%u and %s signatures must not match");
static_assert(!formatSignaturesEqual(parseFormatSignature("%u %s"), kFormatUnsigned),
              "A missing format argument must be detected");
static_assert(!formatSignaturesEqual(kFormatUnsigned, kFormatLongUnsigned),
              "Different integer length modifiers must not match");
static_assert(!parseFormatSignature("%1$s").valid(), "Positional printf arguments are unsupported");
static_assert(!parseFormatSignature("%f").valid(), "Unsupported printf conversions must fail closed");

}  // namespace i18n

#endif
