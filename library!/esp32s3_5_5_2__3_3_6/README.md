# esp32s3_5_5_2__3_3_6

**Тип:** ранний staged pack — **только balanced LwIP** (до E21 TLS экспериментов).

## Что внутри

| Файл | Размер (порядок) |
|------|------------------|
| `liblwip.a` | ~4.11 MB |
| `libesp_netif.a` | ~529 KB |

Собрано под Arduino **3.3.6** / IDF **5.5.2**, профиль YoRadio:

- `CONFIG_LWIP_MAX_ACTIVE_TCP=64`
- `CONFIG_LWIP_TCP_WND_DEFAULT=32768`
- и др. (см. историю LwIP rebuild в docs)

## Назначение

Замена **только** сетевого стека в PIO; mbedTLS/TLS остаются **stock** из Arduino package.

## Не путать с

- `esp32s3_5_5_2__3_3_6_ai_tls_profile_*` — отдельные TLS packs E21C1/C1.1.

## Установка

```powershell
$Pack = "G:\Github\Yoradio_RGB_Panel-1\library!\esp32s3_5_5_2__3_3_6"
$PIO = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib"
Copy-Item "$Pack\liblwip.a","$Pack\libesp_netif.a" $PIO -Force
```
