// Custom Wiring resolver: factory BoardProfile -> effective kWiring.
// Резолвер Custom Wiring: заводской BoardProfile -> итоговый kWiring.
// Ownership: platform/board.
// Included from board_select.h after the selected profile is bound to kActiveBoard.
// myoptions.h may define YORADIO_CUSTOM_WIRING and optional DISP_*/TOUCH_* overrides
// before this header runs. kActiveBoard is never modified here.
// Включается из board_select.h после привязки профиля к kActiveBoard.
// myoptions.h может задать YORADIO_CUSTOM_WIRING и опциональные DISP_*/TOUCH_*.
// kActiveBoard здесь не изменяется.
#ifndef YORADIO_PLATFORM_BOARD_WIRING_H
#define YORADIO_PLATFORM_BOARD_WIRING_H

#ifndef YORADIO_CUSTOM_WIRING
#  define YORADIO_CUSTOM_WIRING 0
#endif

// ---------------------------------------------------------------------------
// Flag off, but an override line was uncommented -> clear compile error.
// Флаг выключен, а строка override раскомментирована -> понятная ошибка.
// ---------------------------------------------------------------------------
#if !YORADIO_CUSTOM_WIRING
#  ifdef DISP_B0
#    error "YoRadio Custom Wiring: DISP_B0 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_B0 out."
#  endif
#  ifdef DISP_B1
#    error "YoRadio Custom Wiring: DISP_B1 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_B1 out."
#  endif
#  ifdef DISP_B2
#    error "YoRadio Custom Wiring: DISP_B2 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_B2 out."
#  endif
#  ifdef DISP_B3
#    error "YoRadio Custom Wiring: DISP_B3 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_B3 out."
#  endif
#  ifdef DISP_B4
#    error "YoRadio Custom Wiring: DISP_B4 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_B4 out."
#  endif
#  ifdef DISP_G0
#    error "YoRadio Custom Wiring: DISP_G0 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_G0 out."
#  endif
#  ifdef DISP_G1
#    error "YoRadio Custom Wiring: DISP_G1 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_G1 out."
#  endif
#  ifdef DISP_G2
#    error "YoRadio Custom Wiring: DISP_G2 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_G2 out."
#  endif
#  ifdef DISP_G3
#    error "YoRadio Custom Wiring: DISP_G3 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_G3 out."
#  endif
#  ifdef DISP_G4
#    error "YoRadio Custom Wiring: DISP_G4 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_G4 out."
#  endif
#  ifdef DISP_G5
#    error "YoRadio Custom Wiring: DISP_G5 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_G5 out."
#  endif
#  ifdef DISP_R0
#    error "YoRadio Custom Wiring: DISP_R0 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_R0 out."
#  endif
#  ifdef DISP_R1
#    error "YoRadio Custom Wiring: DISP_R1 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_R1 out."
#  endif
#  ifdef DISP_R2
#    error "YoRadio Custom Wiring: DISP_R2 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_R2 out."
#  endif
#  ifdef DISP_R3
#    error "YoRadio Custom Wiring: DISP_R3 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_R3 out."
#  endif
#  ifdef DISP_R4
#    error "YoRadio Custom Wiring: DISP_R4 is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_R4 out."
#  endif
#  ifdef DISP_HSYNC
#    error "YoRadio Custom Wiring: DISP_HSYNC is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_HSYNC out."
#  endif
#  ifdef DISP_VSYNC
#    error "YoRadio Custom Wiring: DISP_VSYNC is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_VSYNC out."
#  endif
#  ifdef DISP_DE
#    error "YoRadio Custom Wiring: DISP_DE is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_DE out."
#  endif
#  ifdef DISP_PCLK
#    error "YoRadio Custom Wiring: DISP_PCLK is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_PCLK out."
#  endif
#  ifdef DISP_CS
#    error "YoRadio Custom Wiring: DISP_CS is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_CS out."
#  endif
#  ifdef DISP_SCK
#    error "YoRadio Custom Wiring: DISP_SCK is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_SCK out."
#  endif
#  ifdef DISP_SDA
#    error "YoRadio Custom Wiring: DISP_SDA is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_SDA out."
#  endif
#  ifdef DISP_BL
#    error "YoRadio Custom Wiring: DISP_BL is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment DISP_BL out."
#  endif
#  ifdef TOUCH_SDA
#    error "YoRadio Custom Wiring: TOUCH_SDA is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment TOUCH_SDA out."
#  endif
#  ifdef TOUCH_SCL
#    error "YoRadio Custom Wiring: TOUCH_SCL is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment TOUCH_SCL out."
#  endif
#  ifdef TOUCH_INT
#    error "YoRadio Custom Wiring: TOUCH_INT is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment TOUCH_INT out."
#  endif
#  ifdef TOUCH_RST
#    error "YoRadio Custom Wiring: TOUCH_RST is set, but YORADIO_CUSTOM_WIRING is 0. Set YORADIO_CUSTOM_WIRING to 1 in src/myoptions.h, or comment TOUCH_RST out."
#  endif
#endif  // !YORADIO_CUSTOM_WIRING

#ifdef __cplusplus

namespace yoradio {
namespace detail {

// Copy the factory profile, then replace only the fields the user explicitly named.
// Копирует заводской профиль и подменяет только явно названные пользователем поля.
inline constexpr BoardProfile makeWiring(const BoardProfile& factory) {
    BoardProfile w = factory;
#if YORADIO_CUSTOM_WIRING
#  ifdef DISP_B0
    w.display.data_gpio[0] = static_cast<uint8_t>(DISP_B0);
#  endif
#  ifdef DISP_B1
    w.display.data_gpio[1] = static_cast<uint8_t>(DISP_B1);
#  endif
#  ifdef DISP_B2
    w.display.data_gpio[2] = static_cast<uint8_t>(DISP_B2);
#  endif
#  ifdef DISP_B3
    w.display.data_gpio[3] = static_cast<uint8_t>(DISP_B3);
#  endif
#  ifdef DISP_B4
    w.display.data_gpio[4] = static_cast<uint8_t>(DISP_B4);
#  endif
#  ifdef DISP_G0
    w.display.data_gpio[5] = static_cast<uint8_t>(DISP_G0);
#  endif
#  ifdef DISP_G1
    w.display.data_gpio[6] = static_cast<uint8_t>(DISP_G1);
#  endif
#  ifdef DISP_G2
    w.display.data_gpio[7] = static_cast<uint8_t>(DISP_G2);
#  endif
#  ifdef DISP_G3
    w.display.data_gpio[8] = static_cast<uint8_t>(DISP_G3);
#  endif
#  ifdef DISP_G4
    w.display.data_gpio[9] = static_cast<uint8_t>(DISP_G4);
#  endif
#  ifdef DISP_G5
    w.display.data_gpio[10] = static_cast<uint8_t>(DISP_G5);
#  endif
#  ifdef DISP_R0
    w.display.data_gpio[11] = static_cast<uint8_t>(DISP_R0);
#  endif
#  ifdef DISP_R1
    w.display.data_gpio[12] = static_cast<uint8_t>(DISP_R1);
#  endif
#  ifdef DISP_R2
    w.display.data_gpio[13] = static_cast<uint8_t>(DISP_R2);
#  endif
#  ifdef DISP_R3
    w.display.data_gpio[14] = static_cast<uint8_t>(DISP_R3);
#  endif
#  ifdef DISP_R4
    w.display.data_gpio[15] = static_cast<uint8_t>(DISP_R4);
#  endif
#  ifdef DISP_HSYNC
    w.display.hsync_gpio = static_cast<uint8_t>(DISP_HSYNC);
#  endif
#  ifdef DISP_VSYNC
    w.display.vsync_gpio = static_cast<uint8_t>(DISP_VSYNC);
#  endif
#  ifdef DISP_DE
    w.display.de_gpio = static_cast<uint8_t>(DISP_DE);
#  endif
#  ifdef DISP_PCLK
    w.display.pclk_gpio = static_cast<uint8_t>(DISP_PCLK);
#  endif
#  ifdef DISP_CS
    w.display.cmd_cs_gpio = static_cast<uint8_t>(DISP_CS);
#  endif
#  ifdef DISP_SCK
    w.display.cmd_sck_gpio = static_cast<uint8_t>(DISP_SCK);
#  endif
#  ifdef DISP_SDA
    w.display.cmd_sda_gpio = static_cast<uint8_t>(DISP_SDA);
#  endif
#  ifdef DISP_BL
    w.display.backlight_gpio = static_cast<uint8_t>(DISP_BL);
#  endif
#  ifdef TOUCH_SDA
    w.touch.i2c_sda_gpio = static_cast<uint8_t>(TOUCH_SDA);
#  endif
#  ifdef TOUCH_SCL
    w.touch.i2c_scl_gpio = static_cast<uint8_t>(TOUCH_SCL);
#  endif
#  ifdef TOUCH_INT
    w.touch.int_gpio = static_cast<uint8_t>(TOUCH_INT);
#  endif
#  ifdef TOUCH_RST
    w.touch.reset_gpio = static_cast<uint8_t>(TOUCH_RST);
#  endif
#endif  // YORADIO_CUSTOM_WIRING
    return w;
}

}  // namespace detail
}  // namespace yoradio

#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_WIRING_H
