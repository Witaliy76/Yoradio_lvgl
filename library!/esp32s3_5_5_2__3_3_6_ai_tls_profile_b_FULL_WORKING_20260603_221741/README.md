# esp32s3_5_5_2__3_3_6_ai_tls_profile_b_FULL_WORKING_20260603_221741

**Тип:** **эталонный рабочий** полный набор libs (E21D2 KnownGood).

**Дата снимка:** 2026-06-03 22:17:41 (копия из PIO после E21D1 + device proof)

## Доказанный runtime (E21C2 probe)

- YOVERSION: `0.9.434m-r2-alpha.s8-E21C2-profileB-probe` (bypass guard, только lab)
- **HTTP 200**, AI.FACT / AI.LISTEN на устройстве
- Полный Profile B + **`libmbedtls_2.a`** Profile B `58CDCC87…`

## Что внутри

| Содержимое | |
|------------|--|
| 10× `.a` | Все libs из PIO на момент снимка |
| `manifest.txt` | Размеры и SHA256 каждого файла |
| `sdkconfig.final_after_kconfig.e21c1_1_profile_b` | Kconfig Profile B |
| `sdkconfig.e21c1_profile_b_dtls_off_dynamic` | Входной sdkconfig |
| `manifest_e21d1_libmbedtls2.txt` | Происхождение `libmbedtls_2.a` |

## Ключевые SHA256 (справка)

| Файл | SHA256 (начало) |
|------|-----------------|
| `libmbedtls_2.a` | `58CDCC87…` |
| `libmbedcrypto.a` | `3180F95E…` |
| `libesp-tls.a` | `E63FD4AB…` |
| `liblwip.a` | `95AB0F09…` |

## Восстановление (рекомендуется)

```powershell
$KG = "G:\Github\Yoradio_RGB_Panel-1\library!\esp32s3_5_5_2__3_3_6_ai_tls_profile_b_FULL_WORKING_20260603_221741"
$PIO = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib"
Get-ChildItem $KG -Filter "*.a" | ForEach-Object { Copy-Item $_.FullName $PIO -Force }
pio run -e 4848S040 -t clean
pio run -e 4848S040
```

## Примечание

Product `wifi_flow` **без bypass** по-прежнему блокирует AI при `internal_largest < 50000`. Этот pack решает **mbedTLS/TLS**, не guard policy.
