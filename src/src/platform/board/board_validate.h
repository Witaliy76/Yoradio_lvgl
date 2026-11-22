// Compile-time resolver/validator of the selected board against the build family.
// Проверка совместимости выбранной платы с конфигурацией сборки при компиляции.
// Ownership: platform/board.
// COMMON validator: [build] family/build requirements and [common] wiring checks run for
// every board and name no concrete board. Board-specific checks (factory GPIO, panel,
// lifecycle-shared pins) live in profiles/<profile>/board_validate_profile.h.
// ОБЩИЙ валидатор: [build] и [common] выполняются для любой платы и не называют конкретную
// плату. Проверки конкретной платы (заводские GPIO, панель, общие по жизненному циклу
// пины) — в profiles/<профиль>/board_validate_profile.h.
// Included from src/src/core/options.h at the END of that file, because the legacy
// display-selector check below needs DSP_MODEL and DSP_ST7701, and options.h defines
// them itself after myoptions.h. Checks that need user/default values must not run
// before those values exist.
// Включается из src/src/core/options.h в КОНЦЕ файла: проверке старого селектора
// нужны DSP_MODEL и DSP_ST7701, которые options.h определяет сам после myoptions.h.
//
// sdkconfig.h is the authoritative build-family description. It is included here
// explicitly and not assumed: options.h is sometimes parsed before Arduino.h, so the
// CONFIG_* symbols are not guaranteed to be present yet.
// sdkconfig.h — источник параметров сборки. Включается явно: options.h иногда
// обрабатывается раньше Arduino.h, когда CONFIG_* ещё могут быть недоступны.
#ifndef YORADIO_PLATFORM_BOARD_VALIDATE_H
#define YORADIO_PLATFORM_BOARD_VALIDATE_H

#include "board_select.h"
#include "sdkconfig.h"
#include "../family/family_select.h"

// ---------------------------------------------------------------------------
// Legacy display-selector consistency (preprocessor level)
// Согласованность старого селектора дисплея (уровень препроцессора)
// ---------------------------------------------------------------------------
#if !defined(DSP_MODEL)
#  error "YoRadio Board Contract: DSP_MODEL is not defined. board_validate.h must be included from src/src/core/options.h after the legacy display defaults."
#endif
#if !defined(DSP_ST7701)
#  error "YoRadio Board Contract: DSP_ST7701 is not defined. board_validate.h must be included from src/src/core/options.h after the legacy display defaults."
#endif
// The board/display selector agreement is a profile fact: see the profile validator
// included at the end of this file.
// Соответствие платы и селектора дисплея — факт профиля: см. профильный валидатор,
// включаемый в конце файла.

#ifdef __cplusplus
#include <stdint.h>

namespace yoradio {

// Build-family facts, read from ESP-IDF's own configuration. No parallel YoRadio SoC
// or flash macro is introduced: sdkconfig stays the single source of build truth.
namespace build_family {

#if defined(CONFIG_IDF_TARGET_ESP32S3)
inline constexpr SocTarget kSoc = SocTarget::Esp32S3;
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
inline constexpr SocTarget kSoc = SocTarget::Esp32P4;
#else
inline constexpr SocTarget kSoc = SocTarget::Unknown;
#endif

#if defined(CONFIG_ESPTOOLPY_FLASHSIZE_32MB)
inline constexpr uint32_t kFlashBytes = 32u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_16MB)
inline constexpr uint32_t kFlashBytes = 16u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_8MB)
inline constexpr uint32_t kFlashBytes = 8u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_4MB)
inline constexpr uint32_t kFlashBytes = 4u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_2MB)
inline constexpr uint32_t kFlashBytes = 2u * 1024u * 1024u;
#else
inline constexpr uint32_t kFlashBytes = 0u;  // unknown, never silently accepted
#endif

#if defined(CONFIG_SPIRAM)
inline constexpr bool      kHasPsram = true;
#  if defined(CONFIG_SPIRAM_MODE_OCT)
inline constexpr PsramMode kPsramMode = PsramMode::Octal;
#  else
inline constexpr PsramMode kPsramMode = PsramMode::Quad;
#  endif
#else
inline constexpr bool      kHasPsram  = false;
inline constexpr PsramMode kPsramMode = PsramMode::None;
#endif

}  // namespace build_family

static_assert(build_family::kSoc != SocTarget::Unknown,
    "YoRadio Board Contract: this build's SoC target is not recognised. Expected CONFIG_IDF_TARGET_ESP32S3 (or a target the Board Contract knows) from sdkconfig.");

static_assert(kActiveBoard.identity.build.soc == build_family::kSoc,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD needs a different SoC than this PlatformIO environment builds for. Select a matching environment or a matching board.");

static_assert(build_family::kFlashBytes != 0u,
    "YoRadio Board Contract: this build's flash size is not recognised. Expected one of the CONFIG_ESPTOOLPY_FLASHSIZE_* symbols from sdkconfig.");

static_assert(build_family::kFlashBytes >= kActiveBoard.identity.build.min_flash_bytes,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD needs more flash than this PlatformIO environment provides.");

static_assert(!kActiveBoard.identity.build.requires_psram || build_family::kHasPsram,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD requires PSRAM, but PSRAM is not enabled in this build (CONFIG_SPIRAM).");

static_assert(kActiveBoard.identity.build.psram_mode == PsramMode::Any
                  || kActiveBoard.identity.build.psram_mode == build_family::kPsramMode,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD requires a different PSRAM mode than this build configures (see CONFIG_SPIRAM_MODE_OCT and board_build.arduino.memory_type).");

// ---------------------------------------------------------------------------
// Display transport vs build family (S07_bis-3a)
// Транспорт дисплея и семейство сборки (S07_bis-3a)
// ---------------------------------------------------------------------------
// Build/transport facts come from the family layer; the board's transport is read from
// its descriptor. No board id is consulted here: these checks run for every board.
// Факты сборки/транспорта берутся из слоя семейства; транспорт платы читается из её
// дескриптора. Идентификатор платы не используется: проверки работают для любой платы.

// supportsTransport is a real check since S07_bis-3a (it was an unused helper before).
// supportsTransport стала настоящей проверкой в S07_bis-3a (раньше это был неиспользуемый хелпер).
static_assert(family::supportsTransport(kActiveBoard.display.transport),
    "YoRadio Board Contract: the display transport of the selected board is not supported by this build family.");

// Automatic RGB restart/resync recovery is removed (accepted at beta.3.03). The rule is
// keyed on the board's transport; the family only reports the sdkconfig fact.
// Автоматическое восстановление RGB (restart/resync) удалено (принято в beta.3.03).
// Правило привязано к транспорту платы; семейство лишь сообщает факт из sdkconfig.
static_assert(!(family::kRgbRestartInVsyncEnabled &&
                kActiveBoard.display.transport == DisplayTransport::Rgb565Parallel16),
    "ESP32-S3 RGB transport rule: CONFIG_LCD_RGB_RESTART_IN_VSYNC must remain disabled "
    "for a board with the RGB565 parallel display transport "
    "(check the env sdkconfig and sdkconfig.defaults).");

// ---------------------------------------------------------------------------
// Effective wiring checks (S02-5 / S02-6)
// Проверки итоговой разводки (S02-5 / S02-6)
// ---------------------------------------------------------------------------
// ---- Required lines may not be disconnected; INT/RST may stay at 255 ----
// ---- Обязательные линии нельзя отключать; INT/RST могут остаться 255 ----
static_assert(kWiring.display.data_gpio[0]  != kPinNotConnected, "DISP_B0 cannot be disconnected");
static_assert(kWiring.display.data_gpio[1]  != kPinNotConnected, "DISP_B1 cannot be disconnected");
static_assert(kWiring.display.data_gpio[2]  != kPinNotConnected, "DISP_B2 cannot be disconnected");
static_assert(kWiring.display.data_gpio[3]  != kPinNotConnected, "DISP_B3 cannot be disconnected");
static_assert(kWiring.display.data_gpio[4]  != kPinNotConnected, "DISP_B4 cannot be disconnected");
static_assert(kWiring.display.data_gpio[5]  != kPinNotConnected, "DISP_G0 cannot be disconnected");
static_assert(kWiring.display.data_gpio[6]  != kPinNotConnected, "DISP_G1 cannot be disconnected");
static_assert(kWiring.display.data_gpio[7]  != kPinNotConnected, "DISP_G2 cannot be disconnected");
static_assert(kWiring.display.data_gpio[8]  != kPinNotConnected, "DISP_G3 cannot be disconnected");
static_assert(kWiring.display.data_gpio[9]  != kPinNotConnected, "DISP_G4 cannot be disconnected");
static_assert(kWiring.display.data_gpio[10] != kPinNotConnected, "DISP_G5 cannot be disconnected");
static_assert(kWiring.display.data_gpio[11] != kPinNotConnected, "DISP_R0 cannot be disconnected");
static_assert(kWiring.display.data_gpio[12] != kPinNotConnected, "DISP_R1 cannot be disconnected");
static_assert(kWiring.display.data_gpio[13] != kPinNotConnected, "DISP_R2 cannot be disconnected");
static_assert(kWiring.display.data_gpio[14] != kPinNotConnected, "DISP_R3 cannot be disconnected");
static_assert(kWiring.display.data_gpio[15] != kPinNotConnected, "DISP_R4 cannot be disconnected");
static_assert(kWiring.display.hsync_gpio     != kPinNotConnected, "DISP_HSYNC cannot be disconnected");
static_assert(kWiring.display.vsync_gpio     != kPinNotConnected, "DISP_VSYNC cannot be disconnected");
static_assert(kWiring.display.de_gpio        != kPinNotConnected, "DISP_DE cannot be disconnected");
static_assert(kWiring.display.pclk_gpio      != kPinNotConnected, "DISP_PCLK cannot be disconnected");
static_assert(kWiring.display.cmd_cs_gpio    != kPinNotConnected, "DISP_CS cannot be disconnected");
static_assert(kWiring.display.cmd_sck_gpio   != kPinNotConnected, "DISP_SCK cannot be disconnected");
static_assert(kWiring.display.cmd_sda_gpio   != kPinNotConnected, "DISP_SDA cannot be disconnected");
static_assert(kWiring.display.backlight_gpio != kPinNotConnected, "DISP_BL cannot be disconnected");
static_assert(kWiring.touch.i2c_sda_gpio != kPinNotConnected, "TOUCH_SDA cannot be disconnected");
static_assert(kWiring.touch.i2c_scl_gpio != kPinNotConnected, "TOUCH_SCL cannot be disconnected");

namespace wiring_check {

// The usable-GPIO rule is a build-family fact (platform/family/); call sites below
// are unchanged and resolve to the family function.
// Правило допустимых GPIO — факт семейства (platform/family/); вызовы ниже не менялись
// и обращаются к функции семейства.
using family::isUsableGpio;

static_assert(isUsableGpio(kWiring.display.data_gpio[0]),  "DISP_B0 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[1]),  "DISP_B1 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[2]),  "DISP_B2 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[3]),  "DISP_B3 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[4]),  "DISP_B4 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[5]),  "DISP_G0 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[6]),  "DISP_G1 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[7]),  "DISP_G2 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[8]),  "DISP_G3 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[9]),  "DISP_G4 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[10]), "DISP_G5 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[11]), "DISP_R0 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[12]), "DISP_R1 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[13]), "DISP_R2 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[14]), "DISP_R3 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[15]), "DISP_R4 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.hsync_gpio),     "DISP_HSYNC uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.vsync_gpio),     "DISP_VSYNC uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.de_gpio),        "DISP_DE uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.pclk_gpio),      "DISP_PCLK uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.cmd_cs_gpio),    "DISP_CS uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.cmd_sck_gpio),   "DISP_SCK uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.cmd_sda_gpio),   "DISP_SDA uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.backlight_gpio), "DISP_BL uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.i2c_sda_gpio), "TOUCH_SDA uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.i2c_scl_gpio), "TOUCH_SCL uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.int_gpio),     "TOUCH_INT uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.reset_gpio),   "TOUCH_RST uses an invalid ESP32-S3 GPIO");

// Simultaneous claimants: display + touch (+ INT/RST if wired) + active I2S.
// SD DAT0/CS are not in this base list. storage_validate adds them only when
// the user SD permission is on, so turning SD off releases just those pins.
// GPIO 47/48 stay here as DisplayCommand lines either way.
// Одновременно занятые: дисплей + тач (+ INT/RST) + активный I2S.
// DAT0/CS SD в этот базовый список не входят. storage_validate добавляет их
// только при разрешении SD, поэтому выключение SD освобождает только их.
// GPIO 47/48 остаются здесь линиями DisplayCommand в любом случае.
// 16 RGB + sync/command/backlight + touch + I2S, plus a later SD DAT0/CS pair.
// 16 RGB + синхро/команды/подсветка + тач + I2S и ещё пара SD DAT0/CS.
struct ClaimList {
    uint8_t pin[40];
    PinRole role[40];
    uint8_t count;
};

// True if the board declares that `a` and `b` may hold `gpio` together: one of
// them is the listed owner and the other is in its followers. Exclusive entries
// and untyped (None) claims never share; the order of a/b does not matter.
// True, если плата объявила, что `a` и `b` могут занимать `gpio` вместе: одна роль —
// владелец записи, другая — среди followers. None и Exclusive не делятся никогда.
inline constexpr bool sharingDeclared(const SharedResources& shared, uint8_t gpio,
                                      PinRole a, PinRole b) {
    if (a == PinRole::None || b == PinRole::None) {
        return false;
    }
    for (uint8_t i = 0; i < shared.count && i < kMaxSharedPins; ++i) {
        const SharedPin& e = shared.pins[i];
        if (e.gpio != gpio || e.kind == ResourceShare::Exclusive) {
            continue;
        }
        if ((e.owner == a && (e.followers & pinRoleBit(b)) != 0u) ||
            (e.owner == b && (e.followers & pinRoleBit(a)) != 0u)) {
            return true;
        }
    }
    return false;
}

// GPIO the board wires to a single-pin role, or NC when the role is not a
// single board line (RGB data, I2S, SD are not BoardProfile pin fields).
// GPIO, разведённый платой под роль с одним пином, иначе NC.
inline constexpr uint8_t boardPinOf(const BoardProfile& p, PinRole role) {
    switch (role) {
        case PinRole::DisplayHsync:      return p.display.hsync_gpio;
        case PinRole::DisplayVsync:      return p.display.vsync_gpio;
        case PinRole::DisplayDe:         return p.display.de_gpio;
        case PinRole::DisplayPclk:       return p.display.pclk_gpio;
        case PinRole::DisplayCommandCs:  return p.display.cmd_cs_gpio;
        case PinRole::DisplayCommandSck: return p.display.cmd_sck_gpio;
        case PinRole::DisplayCommandSda: return p.display.cmd_sda_gpio;
        case PinRole::DisplayBacklight:  return p.display.backlight_gpio;
        case PinRole::TouchSda:          return p.touch.i2c_sda_gpio;
        case PinRole::TouchScl:          return p.touch.i2c_scl_gpio;
        case PinRole::TouchInt:          return p.touch.int_gpio;
        case PinRole::TouchRst:          return p.touch.reset_gpio;
        default:                         return kPinNotConnected;
    }
}

// Table sanity for any board: real shared kinds only, no untyped or CS roles,
// one entry per GPIO, and the owner really sits on that GPIO in the factory profile.
// Проверка таблицы для любой платы: только реальные виды разделения, без None и CS,
// одна запись на GPIO, и владелец действительно стоит на этом GPIO в заводском профиле.
inline constexpr bool sharedTableValid(const BoardProfile& p) {
    constexpr uint32_t kForbiddenFollowers =
        (1u << static_cast<uint8_t>(PinRole::None)) | (1u << static_cast<uint8_t>(PinRole::DisplayCommandCs));
    if (p.shared.count > kMaxSharedPins) {
        return false;
    }
    for (uint8_t i = 0; i < p.shared.count; ++i) {
        const SharedPin& e = p.shared.pins[i];
        if (e.kind == ResourceShare::Exclusive || e.owner == PinRole::None ||
            e.owner == PinRole::DisplayCommandCs || e.followers == 0u ||
            (e.followers & kForbiddenFollowers) != 0u ||
            (e.followers & pinRoleBit(e.owner)) != 0u) {
            return false;
        }
        if (e.gpio == kPinNotConnected || !isUsableGpio(e.gpio) ||
            boardPinOf(p, e.owner) != e.gpio) {
            return false;
        }
        for (uint8_t j = static_cast<uint8_t>(i + 1u); j < p.shared.count; ++j) {
            if (p.shared.pins[j].gpio == e.gpio) {
                return false;
            }
        }
    }
    return true;
}
static_assert(static_cast<uint8_t>(PinRole::BuiltinLed) < 32u,
              "PinRole ordinals are bit positions in SharedPin::followers; keep them below 32.");
static_assert(sharedTableValid(kActiveBoard),
              "YoRadio Board Contract: BoardProfile.shared is invalid. Each entry needs a "
              "non-Exclusive kind, a typed owner sitting on that GPIO in the factory profile, "
              "non-empty followers without None/command CS, and one entry per GPIO.");

inline constexpr ClaimList buildClaims() {
    ClaimList c{};
    auto push = [&](uint8_t pin, PinRole role) {
        if (pin == kPinNotConnected) {
            return;
        }
        c.pin[c.count] = pin;
        c.role[c.count] = role;
        ++c.count;
    };
    for (uint8_t i = 0; i < 16; ++i) {
        push(kWiring.display.data_gpio[i], PinRole::DisplayData);
    }
    push(kWiring.display.hsync_gpio,     PinRole::DisplayHsync);
    push(kWiring.display.vsync_gpio,     PinRole::DisplayVsync);
    push(kWiring.display.de_gpio,        PinRole::DisplayDe);
    push(kWiring.display.pclk_gpio,      PinRole::DisplayPclk);
    push(kWiring.display.cmd_cs_gpio,    PinRole::DisplayCommandCs);
    push(kWiring.display.cmd_sck_gpio,   PinRole::DisplayCommandSck);
    push(kWiring.display.cmd_sda_gpio,   PinRole::DisplayCommandSda);
    push(kWiring.display.backlight_gpio, PinRole::DisplayBacklight);
    push(kWiring.touch.i2c_sda_gpio,     PinRole::TouchSda);
    push(kWiring.touch.i2c_scl_gpio,     PinRole::TouchScl);
    push(kWiring.touch.int_gpio,         PinRole::TouchInt);
    push(kWiring.touch.reset_gpio,       PinRole::TouchRst);
#if defined(I2S_DOUT) && (I2S_DOUT != 255)
    // Validate original values before narrowing; explicit 255 keeps its NC policy.
    // Проверяем исходные значения до преобразования в uint8_t; 255 не занимает GPIO.
    static_assert((I2S_DOUT) >= 0 && (I2S_DOUT) <= 48,
                  "YoRadio Board Contract: I2S_DOUT must be GPIO 0..48 or explicit 255 (NC) before uint8_t conversion.");
    static_assert(isUsableGpio(static_cast<uint8_t>(I2S_DOUT)),
                  "I2S_DOUT uses an invalid ESP32-S3 GPIO");
    push(static_cast<uint8_t>(I2S_DOUT), PinRole::I2sDout);
#endif
#if defined(I2S_BCLK) && (I2S_BCLK != 255)
    static_assert((I2S_BCLK) >= 0 && (I2S_BCLK) <= 48,
                  "YoRadio Board Contract: I2S_BCLK must be GPIO 0..48 or explicit 255 (NC) before uint8_t conversion.");
    static_assert(isUsableGpio(static_cast<uint8_t>(I2S_BCLK)),
                  "I2S_BCLK uses an invalid ESP32-S3 GPIO");
    push(static_cast<uint8_t>(I2S_BCLK), PinRole::I2sBclk);
#endif
#if defined(I2S_LRC) && (I2S_LRC != 255)
    static_assert((I2S_LRC) >= 0 && (I2S_LRC) <= 48,
                  "YoRadio Board Contract: I2S_LRC must be GPIO 0..48 or explicit 255 (NC) before uint8_t conversion.");
    static_assert(isUsableGpio(static_cast<uint8_t>(I2S_LRC)),
                  "I2S_LRC uses an invalid ESP32-S3 GPIO");
    push(static_cast<uint8_t>(I2S_LRC), PinRole::I2sWs);
#endif
    return c;
}

// Two claims on one GPIO are a conflict unless BoardProfile.shared declares
// exactly that owner/follower pair on that GPIO.
// Два требования на один GPIO — конфликт, если BoardProfile.shared не объявил
// именно эту пару владелец/follower на этом GPIO.
inline constexpr bool hasDuplicateGpio(const ClaimList& claims) {
    for (uint8_t i = 0; i < claims.count; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1u); j < claims.count; ++j) {
            if (claims.pin[i] == claims.pin[j] &&
                !sharingDeclared(kWiring.shared, claims.pin[i], claims.role[i], claims.role[j])) {
                return true;
            }
        }
    }
    return false;
}

inline constexpr ClaimList kClaims = buildClaims();
static_assert(!hasDuplicateGpio(kClaims),
              "YoRadio Custom Wiring: two active signals share the same GPIO and the board does not "
              "declare that pair in BoardProfile.shared. Check DISP_*/TOUCH_* overrides against each "
              "other and against I2S_DOUT/BCLK/LRC in src/myoptions.h.");
static_assert(kClaims.count < 40, "YoRadio GPIO resolver: claim list is full.");

inline constexpr bool containsPin(const ClaimList& claims, uint8_t pin) {
    for (uint8_t i = 0; i < claims.count; ++i) {
        if (claims.pin[i] == pin) {
            return true;
        }
    }
    return false;
}

// One extra claim with a role. The caller checks hasDuplicateGpio; 255 is not a claim.
// Одно дополнительное требование с ролью. Повторы проверяет hasDuplicateGpio.
inline constexpr ClaimList withPin(ClaimList base, uint8_t pin, PinRole role) {
    if (pin != kPinNotConnected && base.count < 40) {
        base.pin[base.count] = pin;
        base.role[base.count] = role;
        base.count = static_cast<uint8_t>(base.count + 1u);
    }
    return base;
}

// Untyped extra claim (PinRole::None): never shares with anything.
// Требование без роли (PinRole::None): ни с чем не делится.
inline constexpr ClaimList withPin(ClaimList base, uint8_t pin) {
    return withPin(base, pin, PinRole::None);
}

}  // namespace wiring_check

}  // namespace yoradio
#endif  // __cplusplus

// Profile-specific checks of the selected board. The file name is chosen by the single
// YORADIO_BOARD dispatch in board_select.h; nothing here names a concrete board.
// Профильные проверки выбранной платы. Имя файла выбирает единственная диспетчеризация
// по YORADIO_BOARD в board_select.h; здесь конкретная плата не упоминается.
#ifndef YORADIO_BOARD_PROFILE_VALIDATE_HEADER
#  error "YoRadio Board Contract: board_select.h did not define YORADIO_BOARD_PROFILE_VALIDATE_HEADER for the selected board."
#endif
#include YORADIO_BOARD_PROFILE_VALIDATE_HEADER

#endif  // YORADIO_PLATFORM_BOARD_VALIDATE_H
