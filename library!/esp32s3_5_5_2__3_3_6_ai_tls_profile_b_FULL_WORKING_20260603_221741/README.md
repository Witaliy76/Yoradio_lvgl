# KnownGood libraries — LwIP + mbedTLS Profile B

Каталог: `library!/esp32s3_5_5_2__3_3_6_ai_tls_profile_b_FULL_WORKING_20260603_221741/`

**Проект:** [YoRadio LVGL](https://github.com/Witaliy76/Yoradio_lvgl)
**Плата:** ESP32-4848S040 / ESP32-S3
**Совместимость:** `0.9.434m-r2-lvgl-beta.2`
**Source baseline готовых бинарников:** `cc37df814fef36a40578e08a6b4ce0ce2d059d6c`

**Тип:** эталонный полный набор из **10** согласованных `.a`
(PIOArduino `55.03.36` / Arduino-ESP32 `3.3.6` /
`framework-arduinoespressif32-libs` `5.5.0+sha.f56bea3d1f`, ESP-IDF 5.5.2).

Снимок датирован 2026-06-03 (device-proven KnownGood). Файлы `.a` **не изменяются**
для beta.2 — обновляется только эта документация.

## Зачем нужен этот набор

1. **LwIP + `libesp_netif.a`** — согласованный сетевой профиль для устойчивого
   длительного воспроизведения тяжёлых потоков (включая FLAC / высокий битрейт).
2. **mbedTLS Profile B** (включая `libmbedtls_2.a`) — снижает требование TLS handshake
   к крупному непрерывному блоку внутренней RAM, чтобы AI HTTPS мог выполняться
   одновременно с LVGL и аудио.

**Нельзя смешивать** эти архивы с другими версиями или подменять по отдельности.

Готовые пакеты в `build_bin/` уже собраны с этим согласованным профилем.
Замена нужна только при сборке из исходников.

## Полный список архивов (размер + SHA256)

| Файл | Size | SHA256 |
|------|-----:|--------|
| `libesp-tls.a` | 273384 | `E63FD4ABC10ED93B73DF5E97229BA87BCD025BF91FF0A8E13C2F7E7D49065E68` |
| `libesp_http_client.a` | 326770 | `2291DF3B20BECEE403F7FC773E101AB25A62A16F9B670152221DDB887D33FF96` |
| `libesp_netif.a` | 528956 | `6A6DCBA43FBDE0D811D85315364BE767EFC61E57F7C5EFB0379A994A80A99749` |
| `libhttp_parser.a` | 154464 | `74DBFC8C47C849F5172C551F79ADF54CF3F3DC630B314CD7535B548FA9B025C1` |
| `liblwip.a` | 4111086 | `95AB0F09BE3A7B588CAA26DB68CCD27CC20BB5E32F68C2B6ADAAAE4087A204AF` |
| `libmbedcrypto.a` | 4942338 | `3180F95E27B503B0A05A7981C0232B49D10C490C408332F42BAF8C107FEF5532` |
| `libmbedtls.a` | 120864 | `2ACDA20BF932B0016422264CF90B729705729E423B0F31779B960B1435A31DCE` |
| `libmbedtls_2.a` | 1432462 | `58CDCC87D5EFC7B63AEAB37F249EF6A8A569A1970CB5194D5C99FD79210C3347` |
| `libmbedx509.a` | 675410 | `9C7A300CFFA30C6FAC218DA734C525D0585ED6CAA4C50CEDE6ADAF922C4D1375` |
| `libtcp_transport.a` | 340662 | `CC71B7DB287608DEA854BE16A14D6A7E009A8FB0D0BE7529168950EAA3834DF4` |

Дополнительно в каталоге: `manifest.txt`, `sdkconfig.*`.

## Установка (source build)

1. Скопируйте все 10 `.a` с заменой в каталог framework libs.
2. Перезапустите PlatformIO / IDE.
3. Выполните `pio run -e 4848S040 -t clean`, затем полную сборку.
4. После обновления или переустановки framework package повторите установку.

**Windows:**

```powershell
$KG = "<repo>\library!\esp32s3_5_5_2__3_3_6_ai_tls_profile_b_FULL_WORKING_20260603_221741"
$PIO = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib"
Get-ChildItem $KG -Filter "*.a" | ForEach-Object { Copy-Item $_.FullName $PIO -Force }
```

**Linux / macOS:**

```bash
KG="<repo>/library!/esp32s3_5_5_2__3_3_6_ai_tls_profile_b_FULL_WORKING_20260603_221741"
PIO="$HOME/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/lib"
cp -f "$KG"/*.a "$PIO"/
```

## Примечание

Product `wifi_flow` может блокировать AI при слишком малом `internal_largest` —
это отдельная runtime-защита. Этот pack решает **TLS/сеть**, не policy guard.
