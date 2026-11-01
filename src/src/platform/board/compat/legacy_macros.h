// Reject retired ST7701 GPIO names and temporarily bridge TS_* touch names.
// Отвергаем старые GPIO-имена ST7701 и временно сохраняем TS_* для тача.
// Ownership: platform/board.
//
// Display runtime reads typed kResolvedDisplayConfig/kWiring. User GPIO overrides
// use DISP_*/TOUCH_* in myoptions.h with YORADIO_CUSTOM_WIRING=1. This header
// keeps explicit migration errors for old ST7701_* definitions and provides
// only four TS_* aliases through effective kWiring.touch until S05.
// Runtime дисплея читает typed-контракт. Пользовательские GPIO задаются через
// DISP_*/TOUCH_* в myoptions.h. Старые ST7701_* дают подсказку миграции;
// четыре TS_* пока читают итоговый kWiring.touch до S05.
//
// ST7701_BOOT_ORIENTATION_180 is deliberately NOT here: it is display policy.
// ST7701_BOOT_ORIENTATION_180 намеренно отсутствует: это политика дисплея.
//
// Included from options.h after board_select.h and before generic TS_* fallbacks.
// Подключается после board_select.h и до запасных TS_* в options.h.
#ifndef YORADIO_PLATFORM_BOARD_COMPAT_LEGACY_MACROS_H
#define YORADIO_PLATFORM_BOARD_COMPAT_LEGACY_MACROS_H

// board_select.h resolves YORADIO_BOARD, reports an unset or unknown id itself, and
// includes the matching profile. Including it here rather than assuming an earlier
// include makes this header self-sufficient; its guard makes re-inclusion free.
// board_select.h разрешает YORADIO_BOARD, сам сообщает о неустановленном или
// неизвестном id и включает нужный профиль. Делает этот заголовок самодостаточным.
#include "../board_select.h"

#if YORADIO_BOARD == YORADIO_BOARD_4848S040

// ---------------------------------------------------------------------------
// Section 1 — migration guards for retired display names and touch overrides
// Секция 1 — подсказки миграции старых display-имён и защиты touch
// ---------------------------------------------------------------------------
// Reject old user definitions explicitly instead of silently ignoring a pin change.
// Для переназначения используйте DISP_*/TOUCH_* с YORADIO_CUSTOM_WIRING=1.

#ifdef ST7701_R0
#  error "YoRadio Board Contract: do not define ST7701_R0 in myoptions.h. Use DISP_R0 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_R1
#  error "YoRadio Board Contract: do not define ST7701_R1 in myoptions.h. Use DISP_R1 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_R2
#  error "YoRadio Board Contract: do not define ST7701_R2 in myoptions.h. Use DISP_R2 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_R3
#  error "YoRadio Board Contract: do not define ST7701_R3 in myoptions.h. Use DISP_R3 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_R4
#  error "YoRadio Board Contract: do not define ST7701_R4 in myoptions.h. Use DISP_R4 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_G0
#  error "YoRadio Board Contract: do not define ST7701_G0 in myoptions.h. Use DISP_G0 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_G1
#  error "YoRadio Board Contract: do not define ST7701_G1 in myoptions.h. Use DISP_G1 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_G2
#  error "YoRadio Board Contract: do not define ST7701_G2 in myoptions.h. Use DISP_G2 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_G3
#  error "YoRadio Board Contract: do not define ST7701_G3 in myoptions.h. Use DISP_G3 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_G4
#  error "YoRadio Board Contract: do not define ST7701_G4 in myoptions.h. Use DISP_G4 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_G5
#  error "YoRadio Board Contract: do not define ST7701_G5 in myoptions.h. Use DISP_G5 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_B0
#  error "YoRadio Board Contract: do not define ST7701_B0 in myoptions.h. Use DISP_B0 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_B1
#  error "YoRadio Board Contract: do not define ST7701_B1 in myoptions.h. Use DISP_B1 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_B2
#  error "YoRadio Board Contract: do not define ST7701_B2 in myoptions.h. Use DISP_B2 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_B3
#  error "YoRadio Board Contract: do not define ST7701_B3 in myoptions.h. Use DISP_B3 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_B4
#  error "YoRadio Board Contract: do not define ST7701_B4 in myoptions.h. Use DISP_B4 under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_HSYNC
#  error "YoRadio Board Contract: do not define ST7701_HSYNC in myoptions.h. Use DISP_HSYNC under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_VSYNC
#  error "YoRadio Board Contract: do not define ST7701_VSYNC in myoptions.h. Use DISP_VSYNC under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_DE
#  error "YoRadio Board Contract: do not define ST7701_DE in myoptions.h. Use DISP_DE under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_PCLK
#  error "YoRadio Board Contract: do not define ST7701_PCLK in myoptions.h. Use DISP_PCLK under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_CS
#  error "YoRadio Board Contract: do not define ST7701_CS in myoptions.h. Use DISP_CS under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_SCK
#  error "YoRadio Board Contract: do not define ST7701_SCK in myoptions.h. Use DISP_SCK under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_SDA
#  error "YoRadio Board Contract: do not define ST7701_SDA in myoptions.h. Use DISP_SDA under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef ST7701_BL
#  error "YoRadio Board Contract: do not define ST7701_BL in myoptions.h. Use DISP_BL under YORADIO_CUSTOM_WIRING=1."
#endif

// Touch (S02-4). TS_INT / TS_RST are guarded exactly like a wired pin: on this board
// their value is the "not connected" sentinel, and that absence is itself a fixed
// physical fact of the board, not a user preference. A user who sets TS_INT expecting
// interrupt-driven touch would get silently ignored wiring, so it is rejected here.
// Тач (S02-4). TS_INT / TS_RST защищены так же, как разведённый пин: их значение —
// сентинел «не подключено», и само это отсутствие — фиксированный физический факт
// платы, а не пользовательское предпочтение.
#ifdef TS_SDA
#  error "YoRadio Board Contract: do not define TS_SDA in myoptions.h. Use TOUCH_SDA under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef TS_SCL
#  error "YoRadio Board Contract: do not define TS_SCL in myoptions.h. Use TOUCH_SCL under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef TS_INT
#  error "YoRadio Board Contract: do not define TS_INT in myoptions.h. Use TOUCH_INT under YORADIO_CUSTOM_WIRING=1."
#endif
#ifdef TS_RST
#  error "YoRadio Board Contract: do not define TS_RST in myoptions.h. Use TOUCH_RST under YORADIO_CUSTOM_WIRING=1."
#endif

// ---------------------------------------------------------------------------
// Section 2 — the 4 bridged touch GPIO (S02-4, retained until S05)
// Секция 2 — 4 GPIO-алиаса тача (S02-4, до S05)
// ---------------------------------------------------------------------------
// Defining these before options.h's `#ifndef TS_*` block makes the generic
// fallbacks (33/32/21/25) inert for this board. The fallbacks stay for boards
// this bridge does not cover. TS_INT / TS_RST carry kPinNotConnected (255);
// TAMC_GT911.cpp compares that number at runtime, not in #if.
// Эти определения стоят раньше `#ifndef TS_*` в options.h, поэтому запасные
// 33/32/21/25 для этой платы не срабатывают. TS_INT / TS_RST несут сентинел 255;
// TAMC_GT911.cpp сравнивает его в рантайме, не в #if.
#define TS_SDA        (yoradio::kWiring.touch.i2c_sda_gpio)  // SDA
#define TS_SCL        (yoradio::kWiring.touch.i2c_scl_gpio)  // SCL
#define TS_INT        (yoradio::kWiring.touch.int_gpio)      // INT
#define TS_RST        (yoradio::kWiring.touch.reset_gpio)    // RST

// ---------------------------------------------------------------------------
// Section 3 — touch completeness self-check (preprocessor level)
// Секция 3 — проверка полноты touch-алиасов (уровень препроцессора)
// ---------------------------------------------------------------------------
// The same for the 4 touch names. Kept as its own check so the error message points
// at one domain: a missing TS_* here would otherwise be masked by the generic
// `#ifndef TS_*` fallbacks further down src/src/core/options.h, which would quietly
// substitute a different board's pins instead of failing.
// То же для 4 имён тача. Отдельная проверка: пропавшее TS_* иначе было бы
// замаскировано generic-fallback'ами в options.h, которые тихо подставили бы пины
// другой платы вместо падения сборки.
#if !defined(TS_SDA) || !defined(TS_SCL) || !defined(TS_INT) || !defined(TS_RST)
#  error "YoRadio Board Contract: the legacy touch bridge did not define all 4 TS_* names. A profile literal is missing or misspelled in src/src/platform/board/compat/legacy_macros.h."
#endif

#endif  // YORADIO_BOARD == YORADIO_BOARD_4848S040

#endif  // YORADIO_PLATFORM_BOARD_COMPAT_LEGACY_MACROS_H
