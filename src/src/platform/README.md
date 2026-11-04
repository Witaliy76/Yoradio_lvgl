# platform — аппаратный слой YoRadio

Author: Witaliy76 - https://github.com/Witaliy76

`src/src/platform/` отделяет факты платы и будущие аппаратные контракты от продуктового UI.
Экраны и виджеты LVGL живут в [`src/src/lvgl_ui/`](../lvgl_ui/README.md) и не включают заголовки драйверов.
Здесь лежит описание железа, а не вёрстка, темы и страницы.

Сейчас здесь Board Contract, каталог `display/` с типами рецепта панели и каталог `touch/` с resolved touch policy, официальным `esp_lcd_touch_gt911` backend и LVGL-neutral `TouchPort`. `TouchScreen` реализует порт поверх official backend. Init, scanout и таблица Type9 остаются в `src/src/displays/`. Каталогов Audio, Storage и Controls в этом дереве нет.

Единственный production board profile — 4848S040 с обязательным ST7701/LVGL display path. Display-less profile или PlatformIO environment отсутствует; устаревший `DUMMYDISPLAY` fallback удалён в S05-3C-FIX1.

## Что есть сейчас

```text
platform/
├── README.md
├── display/        PanelMode и один рецепт ST7701 RGB565 480×480
│   ├── panel_mode.h
│   ├── panel_st7701_rgb565_480.h
│   └── resolved_display_config.h
├── touch/          resolved policy + LVGL-neutral primary-pointer contract
│   ├── gt911_backend.h/.cpp
│   ├── resolved_touch_config.h
│   └── touch_port.h
└── board/          Board Contract: выбор платы, заводской профиль, итоговая разводка
    ├── README.md
    ├── board_id.h
    ├── board_contract.h
    ├── board_select.h
    ├── board_wiring.h
    ├── board_validate.h
    ├── compat/
    └── profiles/
```

Подробности, пользовательская настройка и ограничения — в [board/README.md](board/README.md).

## Чего здесь ещё нет

Display Contract сейчас связывает данные панели, платы и проверенной политики. Следующие направления остаются отдельными:

| Направление | Когда | Где факты живут сейчас |
|---|---|---|
| Display Contract | S04, код готов; аппаратный Gate B впереди | `PanelMode` хранит controller, 480×480 RGB565, timing и идентичность Type9. `BoardProfile` и `kWiring` задают GPIO. `BoardDefaults.display` хранит проверенную host-политику: LVGL 160 строк, PSRAM, PCLK 6/10/6 МГц, bounce 0. `ResolvedDisplayConfig` собирает их для backend и DisplayPort; LVGL берёт высоту partial buffer из BoardDefaults. Байты Type9 и scanout остаются в драйвере. |
| Touch Contract | S05 ACTIVE; S05-3C legacy cleanup complete | `Gt911Backend` переиспользует Arduino Wire bus через typed new-IDF handle; `ResolvedTouchConfig` передаёт official mirror flags для Flip. `TouchScreen` реализует только LVGL-neutral `TouchPort`; жесты и scrolling принадлежат LVGL adapter/UI path. |
| UI Profile | S06 | Профили экрана в `src/src/lvgl_ui/profiles/`. |
| Audio, Storage, Controls | позже | I2S, SD, IR, кнопки и wake остаются в `src/myoptions.h` и соответствующих драйверах. |

Board Contract уже хранит присутствие встроенного аудио и слота microSD и номера заводских GPIO дисплея и тача. Он не выбирает активный выход звука и не назначает пины SD.

Штатная ESP32-4848S040 использует готовую заводскую разводку: перечислять GPIO вручную не нужно. Если провод физически перенесён, `DISP_*` в `src/myoptions.h` переопределяет отдельные линии при `YORADIO_CUSTOM_WIRING=1`. Профиль UI (Square480) — отдельный слой S06, а контракт тача — S05.

## Граница

- UI не владеет пинами и не включает `esp_lcd`, I2S и Wi-Fi.
- Плата выбирается одним макросом `YORADIO_BOARD` в `src/myoptions.h`.
- Окружение PlatformIO (`4848S040` в `platformio.ini`) задаёт семейство сборки: SoC, flash, PSRAM, тулчейн, разделы. Это не выбор модели платы.
- `SharedResources` и `ResourceManager` в S02 не реализованы. Совпадения пинов описаны в [профиле 4848S040](board/profiles/esp32s3_4848s040/README.md); автоматический реестр ресурсов не создавался.
