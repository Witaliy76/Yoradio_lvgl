// Tested YoRadio recommendations for the ESP32-S3 4848S040.
// Проверенные рекомендации YoRadio для ESP32-S3 4848S040.
// Ownership: platform/board.
//
// THIS FILE IS NOT PHYSICAL TRUTH AND NOT A RESTRICTION.
// Every value here is a recommendation the user may override in myoptions.h.
// Nothing in it may be read as a hardware constraint, and no compile-time check
// may reject a build merely because a user chose different values.
// S02-5 keeps this file. Nothing in the firmware reads kActiveBoardDefaults yet.
// Live I2S_DOUT / I2S_BCLK / I2S_LRC and WAKE_PIN stay in myoptions.h, because
// Audio.h and the wake-range check use them inside #if. A later slice may either
// start applying these recommendations through #ifndef, or delete the file once
// that role is clearly unwanted.
// S02-5 этот файл сохраняет. Прошивка пока не читает kActiveBoardDefaults.
// Живые I2S и WAKE_PIN остаются в myoptions.h. Позже рекомендации можно начать
// применять через #ifndef либо удалить файл, если эта роль не нужна.
// ЭТО НЕ ФИЗИЧЕСКАЯ ПРАВДА И НЕ ЗАПРЕТ.
// Любое значение здесь — рекомендация, переопределяемая в myoptions.h. Ни одна
// compile-time проверка не вправе отвергнуть сборку только из-за другого выбора.
//
// Deliberately NOT here, because no safe owner exists yet:
//   - display servicing policy, draw buffers, framebuffer count, bounce strategy
//     and the S01 PCLK pair (kPclkNominalHz / kPclkFlashGuardHz) — these still
//     live in the proven display backend and are not duplicated. S04.
//   - touch polling/rotation/calibration policy — touch_geometry.h owns it. S05.
//   - UI profile selection (Square480) — S06.
// Намеренно отсутствует, так как безопасного владельца пока нет:
//   - политика обслуживания дисплея и пара PCLK S01 остаются в backend. S04.
//   - политика тача остаётся в touch_geometry.h. S05.
//   - выбор UI-профиля (Square480). S06.
#ifndef YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_4848S040_H
#define YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_4848S040_H

#include "../../board_id.h"
#include "../../board_contract.h"

// ---------------------------------------------------------------------------
// Section 1 — raw literals (single textual source of these recommendations)
// Секция 1 — числовые литералы (единственный текстовый источник рекомендаций)
// ---------------------------------------------------------------------------

// Recommended external I2S DAC pins: the rear-connector wiring this board is
// normally used with. These are WIRES, not board traces, so they are a default
// and never immutable BoardProfile wiring.
// Рекомендуемые пины внешнего I2S ЦАП: разводка заднего разъёма. Это ПРОВОДА,
// а не дорожки платы, поэтому это умолчание, а не immutable-разводка профиля.
#define YORADIO_BD_4848_I2S_DOUT     40
#define YORADIO_BD_4848_I2S_BCLK      1
#define YORADIO_BD_4848_I2S_LRCK      2

// Recommended deep-sleep wake source: the stock BOOT button on GPIO0, active low.
// Works with no extra wiring because the button provides the pull itself.
// Рекомендуемый источник пробуждения: штатная кнопка BOOT на GPIO0, активный ноль.
#define YORADIO_BD_4848_WAKE_GPIO     0

// ---------------------------------------------------------------------------
// Section 2 — constexpr typed view
// Секция 2 — типизированный constexpr-вид
// ---------------------------------------------------------------------------

#ifdef __cplusplus

namespace yoradio {

inline constexpr BoardDefaults kBoardDefaults_Esp32S3_4848S040 = {
    .audio = {
        .i2s_dout_gpio = YORADIO_BD_4848_I2S_DOUT,
        .i2s_bclk_gpio = YORADIO_BD_4848_I2S_BCLK,
        .i2s_lrck_gpio = YORADIO_BD_4848_I2S_LRCK,
        // MCLK is not used by the current output path.
        // MCLK в текущем тракте не используется.
        .i2s_mclk_gpio = kPinNotConnected,
        // The onboard mono path needs hardware straps, so the external I2S
        // output is the recommended one. The user may still choose otherwise.
        // Встроенный моно-тракт требует перемычек, поэтому рекомендуется внешний
        // выход I2S. Пользователь по-прежнему может выбрать иначе.
        .prefer_external_output = true,
    },
    .power = {
        .wake_gpio       = YORADIO_BD_4848_WAKE_GPIO,
        .wake_active_low = true,
    },
};

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_DEFAULTS_ESP32S3_4848S040_H
