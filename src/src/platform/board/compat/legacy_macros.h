// Temporary compatibility aliases: kWiring fields -> legacy ST7701_* / TS_* names.
// Временные алиасы совместимости: поля kWiring -> legacy-имена ST7701_* / TS_*.
// Ownership: platform/board.
//
// Dependency direction is strictly one-way and must stay that way:
//
//     BoardProfile  ->  kWiring  ->  these aliases  ->  existing consumers
//
// This file DEFINES NO GPIO VALUE OF ITS OWN. Each alias expands to a field of
// yoradio::kWiring. With Custom Wiring off, kWiring equals the factory profile.
// With Custom Wiring on, kWiring may differ for the overridden fields only.
// The expansion is a C++ expression, not an integer for #if. User overrides use
// DISP_*/TOUCH_* in myoptions.h — never redefine ST7701_* / TS_* there.
// Этот файл НЕ ЗАДАЁТ НИ ОДНОГО СВОЕГО GPIO. Каждый алиас — поле yoradio::kWiring.
// При выключенном Custom Wiring kWiring совпадает с заводом; при включённом
// отличаются только явно переопределённые поля. Пользователь пишет DISP_*/TOUCH_*
// в myoptions.h — не ST7701_* / TS_*.
//
// ST7701_BOOT_ORIENTATION_180 is deliberately NOT here: it is display policy.
// ST7701_BOOT_ORIENTATION_180 намеренно отсутствует: это политика дисплея.
//
// Included exactly once, from src/src/core/options.h, AFTER board_select.h (which
// resolves YORADIO_BOARD and pulls in the profile) and BEFORE any consumer can read
// a bridged name. It never includes options.h or myoptions.h — that back-include is
// what created the include cycle in earlier plan revisions.
// Включается ровно один раз из src/src/core/options.h, ПОСЛЕ board_select.h и ДО
// любого потребителя. options.h/myoptions.h не включает — это давало бы цикл.
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
// Section 1 — fixed-fact redefinition guard
// Секция 1 — защита от переопределения фиксированного факта
// ---------------------------------------------------------------------------
// These 24 names are now owned by the board profile. A definition surviving in
// myoptions.h would either collide with the bridge or silently win, so it is
// rejected with a message that names the symbol and the sanctioned escape route.
//
// Reject leftover ST7701_* / TS_* definitions in myoptions.h. Deliberate re-pins
// use DISP_*/TOUCH_* under YORADIO_CUSTOM_WIRING=1 (board_wiring.h), not these names.
// Отвергает оставшиеся ST7701_* / TS_* в myoptions.h. Переназначение — через
// DISP_*/TOUCH_* при YORADIO_CUSTOM_WIRING=1 (board_wiring.h), не этими именами.

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
// Section 2 — the 24 display aliases, reading kWiring
// Секция 2 — 24 алиаса дисплея, читающие kWiring
// ---------------------------------------------------------------------------
// data_gpio slot order is B0..B4, G0..G5, R0..R4. The backend copies these
// names into data_gpio_nums[] in that same order; this file does not reorder them.
// Порядок слотов data_gpio: B0..B4, G0..G5, R0..R4. Backend копирует эти имена
// в data_gpio_nums[] в том же порядке; здесь порядок не меняется.

#define ST7701_B0     (yoradio::kWiring.display.data_gpio[0])   // B0
#define ST7701_B1     (yoradio::kWiring.display.data_gpio[1])   // B1
#define ST7701_B2     (yoradio::kWiring.display.data_gpio[2])   // B2
#define ST7701_B3     (yoradio::kWiring.display.data_gpio[3])   // B3
#define ST7701_B4     (yoradio::kWiring.display.data_gpio[4])   // B4
#define ST7701_G0     (yoradio::kWiring.display.data_gpio[5])   // G0
#define ST7701_G1     (yoradio::kWiring.display.data_gpio[6])   // G1
#define ST7701_G2     (yoradio::kWiring.display.data_gpio[7])   // G2
#define ST7701_G3     (yoradio::kWiring.display.data_gpio[8])   // G3
#define ST7701_G4     (yoradio::kWiring.display.data_gpio[9])   // G4
#define ST7701_G5     (yoradio::kWiring.display.data_gpio[10])  // G5
#define ST7701_R0     (yoradio::kWiring.display.data_gpio[11])  // R0
#define ST7701_R1     (yoradio::kWiring.display.data_gpio[12])  // R1
#define ST7701_R2     (yoradio::kWiring.display.data_gpio[13])  // R2
#define ST7701_R3     (yoradio::kWiring.display.data_gpio[14])  // R3
#define ST7701_R4     (yoradio::kWiring.display.data_gpio[15])  // R4
#define ST7701_HSYNC  (yoradio::kWiring.display.hsync_gpio)     // HSYNC
#define ST7701_VSYNC  (yoradio::kWiring.display.vsync_gpio)     // VSYNC
#define ST7701_DE     (yoradio::kWiring.display.de_gpio)        // DE
#define ST7701_PCLK   (yoradio::kWiring.display.pclk_gpio)      // PCLK
#define ST7701_CS     (yoradio::kWiring.display.cmd_cs_gpio)    // command CS
#define ST7701_SCK    (yoradio::kWiring.display.cmd_sck_gpio)   // command SCK
#define ST7701_SDA    (yoradio::kWiring.display.cmd_sda_gpio)   // command SDA
#define ST7701_BL     (yoradio::kWiring.display.backlight_gpio) // BL

// ---------------------------------------------------------------------------
// Section 3 — the 4 bridged touch GPIO (S02-4)
// Секция 3 — 4 перенесённых GPIO тача (S02-4)
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
// Section 4 — completeness self-check (preprocessor level)
// Секция 4 — проверка полноты (уровень препроцессора)
// ---------------------------------------------------------------------------
// All 24 names must exist once the aliases have run. This checks definedness only.
// It does not evaluate the C++ expression: an undefined name inside a later #if
// would become 0, and 0 is a real GPIO on this board (R4). Numeric parity with
// the S02-4 baseline is asserted in board_validate.h.
// Все 24 имени должны существовать. Проверяется только определённость, не значение
// выражения C++. Неопределённое имя в чужом #if стало бы 0, а 0 — реальный GPIO
// (R4). Числовой паритет с baseline S02-4 проверяет board_validate.h.
#if !defined(ST7701_R0)    || !defined(ST7701_R1)    || !defined(ST7701_R2) \
 || !defined(ST7701_R3)    || !defined(ST7701_R4)                           \
 || !defined(ST7701_G0)    || !defined(ST7701_G1)    || !defined(ST7701_G2) \
 || !defined(ST7701_G3)    || !defined(ST7701_G4)    || !defined(ST7701_G5) \
 || !defined(ST7701_B0)    || !defined(ST7701_B1)    || !defined(ST7701_B2) \
 || !defined(ST7701_B3)    || !defined(ST7701_B4)                           \
 || !defined(ST7701_HSYNC) || !defined(ST7701_VSYNC)                        \
 || !defined(ST7701_DE)    || !defined(ST7701_PCLK)                         \
 || !defined(ST7701_CS)    || !defined(ST7701_SCK)   || !defined(ST7701_SDA) \
 || !defined(ST7701_BL)
#  error "YoRadio Board Contract: the legacy display bridge did not define all 24 ST7701_* names. A profile literal is missing or misspelled in src/src/platform/board/compat/legacy_macros.h."
#endif

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
