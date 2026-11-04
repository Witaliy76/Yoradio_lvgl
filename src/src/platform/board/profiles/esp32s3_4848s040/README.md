# ESP32-S3 4848S040 profile

## English

This is the only production BoardProfile. Select it in src/myoptions.h with
YORADIO_BOARD_4848S040. Factory facts live in board_profile.h; tested
recommendations live in board_defaults.h.

`BoardDefaults.ui.profile` explicitly selects `UiProfile::Square480`. The
[UI contract](../../../../lvgl_ui/README.md#ui-contract--ui-контракт) explains
how this differs from physical panel geometry.

| Hardware | Current contract |
|---|---|
| SoC / memory | ESP32-S3, 16 MB flash minimum, octal PSRAM required |
| Display | ST7701, RGB565 parallel-16, 480x480 |
| Touch | GT911 at I2C address 0x5D; polling, 100 kHz |
| Panel reset | Not routed to SoC; Custom Wiring cannot assign RESX |
| External I2S recommendation | DOUT 40, BCLK 1, LRC 2. These are ESP32 outputs. The live macros are `I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` in `src/myoptions.h`. `BoardDefaults.audio` is not applied |
| Wake recommendation | GPIO0, active low. The live macros are `WAKE_PIN` and `WAKE_LEVEL` |

### Factory display GPIO

| Signal | GPIO | Signal | GPIO |
|---|---:|---|---:|
| B0/B1/B2/B3/B4 | 4/5/6/7/15 | G0/G1/G2/G3/G4/G5 | 8/20/3/46/9/10 |
| R0/R1/R2/R3/R4 | 11/12/13/14/0 | HSYNC/VSYNC/DE/PCLK | 16/17/18/21 |
| Type9 CS/SCK/SDA | 39/48/47 | Backlight | 38 |

### Factory touch GPIO

| Signal | GPIO | Meaning |
|---|---:|---|
| SDA | 19 | I2C data |
| SCL | 45 | I2C clock |
| INT | 255 | not connected |
| RST | 255 | not connected |

255 is kPinNotConnected, not GPIO0. The board uses polling, so INT/RST are
intentionally absent. Rewire only through TOUCH_* Custom Wiring entries.

### Important constraints

- GPIO0 is both RGB R4 and the BOOT button/wake default; pressing BOOT while
  RGB output is active can affect the image.
- GPIO19/20 are GT911 SDA/RGB G1 and overlap native USB D-/D+ roles
  respectively; stock native USB pins are not free.
- GPIO47/48 are the panel Type9 command bus and overlap unvalidated SD comments.
- The onboard SD slot is only a presence fact; its pin map is not validated here.
- The onboard mono audio path requires physical R21/R22/R23 rework and is not
  selected by software.

The display policy is one 160-line PSRAM draw buffer, one panel-driver PSRAM
framebuffer, bounce 0 and PCLK 6/10/6 MHz. It is validated 4848S040 evidence,
not a transferable recipe. See [Display Contract](../../../display/README.md)
and [Touch Contract](../../../touch/README.md).

## Русский

Это единственный production BoardProfile. Выбирайте его в src/myoptions.h через
YORADIO_BOARD_4848S040. Factory facts находятся в board_profile.h; tested
recommendations — в board_defaults.h.

`BoardDefaults.ui.profile` явно выбирает `UiProfile::Square480`.
[UI-контракт](../../../../lvgl_ui/README.md#ui-contract--ui-контракт) объясняет
отличие этого выбора от физической геометрии панели.

| Hardware | Текущий контракт |
|---|---|
| SoC / memory | ESP32-S3, минимум 16 MB flash, требуется octal PSRAM |
| Display | ST7701, RGB565 parallel-16, 480x480 |
| Touch | GT911 по I2C адресу 0x5D; polling, 100 кГц |
| Panel reset | Не подключён к SoC; Custom Wiring не назначает RESX |
| Рекомендация внешнего I2S | DOUT 40, BCLK 1, LRC 2. Это выходы ESP32. Живые макросы — `I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` в `src/myoptions.h`. `BoardDefaults.audio` не применяется |
| Рекомендация пробуждения | GPIO0, active low. Живые макросы — `WAKE_PIN` и `WAKE_LEVEL` |

### Заводские display GPIO

| Signal | GPIO | Signal | GPIO |
|---|---:|---|---:|
| B0/B1/B2/B3/B4 | 4/5/6/7/15 | G0/G1/G2/G3/G4/G5 | 8/20/3/46/9/10 |
| R0/R1/R2/R3/R4 | 11/12/13/14/0 | HSYNC/VSYNC/DE/PCLK | 16/17/18/21 |
| Type9 CS/SCK/SDA | 39/48/47 | Backlight | 38 |

### Заводские touch GPIO

| Signal | GPIO | Смысл |
|---|---:|---|
| SDA | 19 | I2C data |
| SCL | 45 | I2C clock |
| INT | 255 | not connected |
| RST | 255 | not connected |

255 — это kPinNotConnected, не GPIO0. Плата использует polling, поэтому INT/RST
намеренно отсутствуют. Переподключайте только через TOUCH_* Custom Wiring.

### Важные ограничения

- GPIO0 одновременно RGB R4 и BOOT button/wake default; нажатие BOOT при
  активном RGB output может повлиять на изображение.
- GPIO19/20 являются GT911 SDA/RGB G1 и пересекаются с native USB D-/D+
  соответственно; stock native USB pins не свободны.
- GPIO47/48 — command bus Type9 панели и пересекаются с непроверенными SD comments.
- Onboard SD slot — только presence fact; его pin map здесь не validated.
- Onboard mono audio path требует physical rework R21/R22/R23 и не выбирается
  software.

Display policy: один 160-line PSRAM draw buffer, один panel-driver PSRAM
framebuffer, bounce 0, PCLK 6/10/6 МГц. Это validated evidence 4848S040, не
переносимый рецепт. См. [Display Contract](../../../display/README.md) и
[Touch Contract](../../../touch/README.md).
