# VIEWE UEDX48480040E-WB-A — board profile

ESP32-S3 N16R8, 4" square IPS 480×480, GC9503 RGB panel (UE040WV-RH40-A044C), FT6336U touch.
Build family: `env:S3_N16R8`. Select with `#define YORADIO_BOARD YORADIO_BOARD_UEDX48480040`
in `src/myoptions.h`.

Status: profile from the vendor schematic V1.3 (SCHEMATIC CONFIRMED). Device-verified
on YoRadio: image, colours, touch, Wi-Fi, SD playback (S07_bis-8) and audio through an
external PCM5102 DAC (S07_bis-9).

**Header silkscreen:** the labels printed next to the header pins are **GPIO numbers**
("19", "20", "48", "GND", "5V"), not J1 pin numbers. Wire by the labels. The J1 numbers
below are the schematic pin numbers, for reference only.

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
| Native USB D− / D+ ("USB" port; J1-9 / J1-10) | 19 / 20 |

## Default external I2S DAC (BoardDefaults)

| I2S | DAC pin (PCM5102) | Silkscreen label = GPIO | J1 pin | Note |
|---|---|---|---|---|
| BCLK | BCK | 19 | 9 | native USB D− |
| WS (LRC) | LCK | 48 | 19 | shared with the panel command SCK after panel init (`BoardProfile.shared`) |
| DOUT | DIN | 20 | 10 | native USB D+ |
| — | GND | GND | 1 / 3 | |
| — | VIN | 5V | 2 | the PCM5102 module has its own 3.3 V regulators |

Do not use the labels "9", "10" or "17": those are GPIO9/10 (RGB G1/G0) and GPIO17 (VSYNC).

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
working ESPHome config for this board (espcontrol #2140).

PCLK: D6 structure at 6 / 12 / 6 MHz (6 MHz first scanout, one promotion to 12 MHz,
flash guard 6 MHz). The panel datasheet asks for DCLK >= 16 MHz (>= 54 fps):

| Runtime PCLK | Frame rate | Result on device |
|---|---|---|
| 6 MHz | 21.9 fps | visible flicker |
| 10 MHz | 36.4 fps | intermittent flicker |
| 12 MHz | 43.7 fps | clean under Radio + HTTPS + NVS writes; cold start clean (chosen) |
| 16 MHz, bounce 0 | 58.3 fps | static frame clean; image shift under load (PSRAM bandwidth) |

Bounce buffers would allow 16–18 MHz (the vendor BSP uses 18 MHz with bounce) but cost
too much CPU for this product.
Colour: COLMOD 0x60 (18-bit panel interface), not the official 0x50 (S07_bis-8b).
Open on device: boot orientation 180° is not offered for GC9503 (Q3).
