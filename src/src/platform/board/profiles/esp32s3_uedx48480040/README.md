# VIEWE UEDX48480040E-WB-A — board profile

ESP32-S3 N16R8, 4" square IPS 480×480, GC9503 RGB panel (UE040WV-RH40-A044C), FT6336U touch.
Build family: `env:S3_N16R8`. Select with `#define YORADIO_BOARD YORADIO_BOARD_UEDX48480040`
in `src/myoptions.h`.

Status (S07_bis-8): profile from the vendor schematic V1.3 (SCHEMATIC CONFIRMED);
not yet device-verified on YoRadio.

## Wiring (V1.3 schematic)

| Function | GPIO |
|---|---|
| RGB B0..B4 / G0..G5 / R0..R4 | 15..11 / 10..5 / 4..0 |
| HSYNC / VSYNC / DE / PCLK | 16 / 17 / 18 / 21 |
| Panel command CS / SCK / SDA (3-wire) | 39 / 48 / 47 |
| Backlight (LP3321 PWM) | 38 |
| Touch FT6336U SDA / SCL, address 0x38 (no INT, no RST) | 40 / 41 |
| TF card CS / CLK / CMD / DAT0 (SPI mode) | 47 / 45 / 42 / 46 |
| BOOT button (= RGB R4) | 0 |
| UART0 TX / RX → CH340 ("UART" port) | 43 / 44 |
| Native USB D− / D+ ("USB" port, J1-9/10) | 19 / 20 |

## Default external I2S DAC (BoardDefaults)

| I2S | GPIO | Note |
|---|---|---|
| BCLK | 19 | native USB D−, J1-9 |
| WS (LRC) | 48 | shared with the panel command SCK after panel init (`BoardProfile.shared`) |
| DOUT | 20 | native USB D+, J1-10 |

Because I2S uses GPIO19/20, leave the "USB" connector unplugged; flash and read logs
through the CH340 "UART" port. The "USB" port cannot power an OTG device anyway
(VBUS2 goes in through D2).

## Shared lines (`BoardProfile.shared`)

- GPIO47: panel command SDA during the init recipe, then TF CS (wired through R17 0R).
- GPIO48: panel command SCK during the init recipe, then I2S WS.
- Command CS (39) is never shared.

Keep `USE_BUILTIN_LED false` (default): Arduino's builtin LED drives GPIO48 through RMT.
The board's WS2812 sits on GPIO42 (TF CMD) and is not driven.

## Panel

`platform/display/panel_gc9503_rgb565_480.h`; init recipe `displays/gc9503_ops.h`,
transcribed from `espressif/esp_lcd_gc9503` 3.0.1 (`vendor_specific_init_default`,
esp-bsp `fb5714e`) and replayed by the Rgb3Wire host (D10 step 2). Porches from the
working ESPHome config for this board (espcontrol #2140). PCLK policy D6:
6 MHz first scanout, one promotion to 10 MHz, flash guard 6 MHz.
Open on device: colour order (Q2), boot orientation 180° is not offered for GC9503 (Q3).
