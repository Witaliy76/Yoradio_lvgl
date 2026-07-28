# esp32s3_5_5_2__3_3_6

**Тип:** ранний staged pack — **только balanced LwIP** (до TLS Profile B).

**Проект:** [YoRadio LVGL](https://github.com/Witaliy76/Yoradio_lvgl)
**Для публичной beta** `0.9.434m-r2-lvgl-beta.2` (source baseline
`cc37df814fef36a40578e08a6b4ce0ce2d059d6c`) этого набора **недостаточно**.

Используйте полный KnownGood:

`library!/esp32s3_5_5_2__3_3_6_ai_tls_profile_b_FULL_WORKING_20260603_221741/`

## Что внутри

| Файл | Размер (порядок) |
|------|------------------|
| `liblwip.a` | ~4.11 MB |
| `libesp_netif.a` | ~529 KB |

Собрано под Arduino-ESP32 **3.3.6** / ESP-IDF **5.5.2** (PIOArduino `55.03.36`),
плата ESP32-S3 / ESP32-4848S040:

- `CONFIG_LWIP_MAX_ACTIVE_TCP=64`
- `CONFIG_LWIP_TCP_WND_DEFAULT=32768`

## Назначение

Замена **только** сетевого стека в PIO; mbedTLS/TLS остаются **stock**.
Не смешивайте с архивами других версий.

## Установка (устаревший частичный pack)

Windows: `%USERPROFILE%\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib\`
Linux/macOS: `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/lib/`

После копирования — перезапуск PlatformIO и clean rebuild. После обновления
framework package установку нужно повторить.
