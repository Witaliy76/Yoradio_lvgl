// ESP32-P4 build-family rules: STUB (S07_bis-1). Not implemented.
// Правила семейства ESP32-P4: ЗАГОТОВКА (S07_bis-1). Не реализовано.
//
// Purpose / Назначение:
//   Reserves the place of the P4 family layer so family_select.h has a real target
//   and a P4 build fails loudly instead of silently using S3 rules. The first P4
//   board and env arrive in S19; this file must be filled before that.
//   Резервирует место слоя семейства P4: family_select.h получает реальную цель, а
//   сборка под P4 падает явно, а не использует правила S3 молча. Первая плата P4 и
//   env появятся в S19; файл нужно заполнить до этого.
//
// Gaps to fill / Что нужно заполнить:
//   - P4 isUsableGpio rule (valid range, reserved pins, strapping, flash/PSRAM pins).
//     Правило isUsableGpio для P4 (диапазон, зарезервированные пины, strapping,
//     пины flash/PSRAM).
//   - Display transports: RGB (LCD_CAM / RGB peripheral) and MIPI-DSI;
//     supportsTransport(DisplayTransport) for both; the DisplayTransport enum in
//     board_contract.h knows only Rgb565Parallel16 today.
//     Транспорты дисплея: RGB и MIPI-DSI; supportsTransport() для обоих; enum
//     DisplayTransport в board_contract.h сегодня знает только Rgb565Parallel16.
//   - SoC-level sdkconfig requirements (the P4 analogue of the S3 RGB transport rule).
//     Требования sdkconfig уровня SoC (аналог правила RGB-транспорта S3).
//   - Co-processor networking facts (Wi-Fi via hosted/co-processor); not a GPIO rule.
//     Факты сети через сопроцессор (Wi-Fi через hosted/сопроцессор); не правило GPIO.
#ifndef YORADIO_PLATFORM_FAMILY_ESP32P4_FAMILY_RULES_H
#define YORADIO_PLATFORM_FAMILY_ESP32P4_FAMILY_RULES_H

#error "ESP32-P4 family rules are not implemented yet (S07_bis stub)"

#endif  // YORADIO_PLATFORM_FAMILY_ESP32P4_FAMILY_RULES_H
