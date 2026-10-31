# platform — аппаратный слой YoRadio

Author: Witaliy76 - https://github.com/Witaliy76

`src/src/platform/` отделяет факты платы и будущие аппаратные контракты от продуктового UI.
Экраны и виджеты LVGL живут в [`src/src/lvgl_ui/`](../lvgl_ui/README.md) и не включают заголовки драйверов.
Здесь лежит описание железа, а не вёрстка, темы и страницы.

Сейчас реализован только Board Contract. Каталогов Display, Touch, Audio, Storage и Controls в этом дереве нет.

## Что есть сейчас

```text
platform/
├── README.md
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

Имена ниже — направления следующих контрактов, не существующие модули:

| Направление | Когда | Где факты живут сейчас |
|---|---|---|
| Display Contract | S04 | Драйвер ST7701 в `src/src/displays/`. Рецепт панели (тайминги, init, PCLK) не скопирован в Board Contract. |
| Touch Contract | S05 | GT911 в `src/src/GT911_Touchscreen/`, геометрия в `src/src/core/touch_geometry.h`. |
| UI Profile | S06 | Профили экрана в `src/src/lvgl_ui/profiles/`. |
| Audio, Storage, Controls | позже | I2S, SD, IR, кнопки и wake остаются в `src/myoptions.h` и соответствующих драйверах. |

Board Contract уже хранит присутствие встроенного аудио и слота microSD и номера заводских GPIO дисплея и тача. Он не выбирает активный выход звука и не назначает пины SD.

## Граница

- UI не владеет пинами и не включает `esp_lcd`, I2S и Wi-Fi.
- Плата выбирается одним макросом `YORADIO_BOARD` в `src/myoptions.h`.
- Окружение PlatformIO (`4848S040` в `platformio.ini`) задаёт семейство сборки: SoC, flash, PSRAM, тулчейн, разделы. Это не выбор модели платы.
- `SharedResources` и `ResourceManager` в S02 не реализованы. Совпадения пинов описаны в [профиле 4848S040](board/profiles/esp32s3_4848s040/README.md); автоматический реестр ресурсов не создавался.
