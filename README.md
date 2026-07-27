[English version](readme_english.md) — *English documentation, may lag behind this file.*

# YoRadio LVGL

YoRadio LVGL — Wi-Fi интернет-радио на ESP32-S3 с сенсорным квадратным дисплеем. Интерфейс построен на LVGL 8.3 и включает шесть основных страниц, а также отдельные экраны настройки Wi-Fi, быстрого доступа к избранным станциям и заставки.

Кроме экрана устройства есть Web UI: управление воспроизведением, список станций, настройки поведения и раздел Appearance — темы, фоны Main screen и обложка текущей станции. Для звука проект рассчитан на внешний I2S ЦАП или усилитель.

Проект основан на [e2002/yoradio](https://github.com/e2002/yoradio). Адаптацию проекта к графической библиотеке LVGL и дальнейшее развитие интерфейса ведёт [Witaliy76](https://github.com/Witaliy76) в репозитории [Yoradio_lvgl](https://github.com/Witaliy76/Yoradio_lvgl).

> Публичная beta: `0.9.434m-r2-lvgl-beta.2`. Поддерживаемая плата: **ESP32-4848S040**.

<p align="center">
  <img src="readme/device-front.jpg" alt="YoRadio LVGL — музыкальный интернет-радиоприёмник" width="450">
</p>

## Дизайн и характер прибора

YoRadio задуман как самостоятельный музыкальный прибор и объект присутствия, а не как приложение, перенесённое на маленький экран. Музыка остаётся главным содержанием: интерфейс помогает слушать, не требует постоянного внимания и не конкурирует с воспроизведением.

Визуальный язык опирается на спокойный скандинавский hi-fi-подход и эстетику аналоговой аудиотехники: ясная типографика, спокойная композиция, сдержанные акценты и образы классических приборов. В продукте это видно, например, в Visual (индикация в духе Beocord) и в палитре Amber Hi-Fi для темы Custom. Устройство должно естественно находиться в интерьере и не выглядеть как смартфон или планшет.

Разные страницы несут разные роли: Main — прослушивание, Visual — атмосфера, Info — состояние прибора, Settings — функциональное управление.

## Основные возможности

- Воспроизведение интернет-радиостанций: MP3, AAC, FLAC, OGG/Vorbis, Opus.
- Шестистраничный сенсорный интерфейс на LVGL 8.3: Info, Main, Visual, Station, Weather, Settings.
- Список станций с постраничной навигацией.
- Weather — текущая погода и прогноз (OpenWeatherMap).
- Visual — VU-индикация в стиле винтажных приборов Beocord.
- Preset Temporary — быстрый доступ к 8 сохранённым станциям.
- Analog Screensaver — аналоговые часы в режиме ожидания.
- Web UI — плеер, станции, настройки, Appearance, обновление прошивки.
- Темы Dark / Light / Custom, station artwork и отдельные фоны Main для каждой темы.
- Локализация интерфейса RU / EN / PL / SK (выбирается при сборке).
- AI Layer — необязательный тихий информационный слой поверх музыки.

## История изменений

### 0.9.434m-r2-lvgl-beta.2 — текущая beta

- подготовлена публичная beta для ESP32-4848S040;
- шестистраничный сенсорный интерфейс на LVGL 8.3: Info, Main, Visual, Station, Weather, Settings;
- Preset Temporary, аналоговая заставка и Sleep Device;
- темы Dark, Light и Custom; Web UI Appearance (artwork, фоны Main, custom palette);
- локализации RU, EN, PL и SK;
- уточнена геометрия строк постраничного списка Station.

Ранние alpha-сборки использовались для внутренней разработки и отдельно не публиковались.

## Поддерживаемое оборудование

Публичная beta официально поддерживает одну плату. Для каждой поддерживаемой платы используется отдельная инструкция с подключением, бинарниками и аппаратными особенностями.

| Параметр | Значение |
|---|---|
| Плата | ESP32-4848S040 |
| Модуль | ESP32-S3-WROOM-1-N16R8 |
| Flash | 16 MB |
| PSRAM | 8 MB |
| Дисплей | ST7701S, 480×480 |
| Тачскрин | GT911 (I2C) |

→ **[README_4848S040.md](README_4848S040.md)** — подключение, звук, конфигурация, бинарники и диагностика этой платы.

## Интерфейс

Страницы объединены в кольцо и переключаются горизонтальным свайпом:

```text
Info ↔ Main ↔ Visual ↔ Station ↔ Weather ↔ Settings
```

### Main

Основной экран воспроизведения: обложка станции, название станции и трека, панель управления, регулировка громкости и информационная строка.

<p align="center">
  <img src="readme/main-screen-guide.png" alt="Экран Main" width="450">
</p>

### Info

Техническая информация об устройстве: сеть и Wi-Fi, версия прошивки, чип и время работы, дисплей и LVGL, память и PSRAM.

<p align="center">
  <img src="readme/info-screen-guide.png" alt="Экран Info" width="450">
</p>

### Visual

Декоративная визуализация воспроизведения в стиле винтажных VU-индикаторов Beocord. Работает только во время проигрывания потока.

<p align="center">
  <img src="readme/visual-screen-guide.png" alt="Экран Visual" width="450">
</p>

### Station

Список радиостанций с вертикальным листанием, текущей позицией и индикатором активной станции.

<p align="center">
  <img src="readme/station-screen-guide.png" alt="Экран Station" width="450">
</p>

### Weather

Текущая погода, ощущаемая температура, ветер, влажность, давление, почасовой прогноз и прогноз на ближайшие дни.

<p align="center">
  <img src="readme/weather-screen-guide.png" alt="Экран Weather" width="450">
</p>

### Settings

Быстрый доступ к яркости и теме экрана, музыкальному индикатору, автовозобновлению, таймеру сна и настройке Wi-Fi.

<p align="center">
  <img src="readme/settings-screen-guide.png" alt="Экран Settings" width="450">
</p>

## Дополнительные режимы

### Preset Temporary

Быстрый доступ к избранным станциям. Открывается свайпом вниз от верхнего края на Info, Main, Visual и Weather. 8 слотов: короткое нажатие запускает станцию, долгое — сохраняет текущую. Экран закрывается примерно через 15 секунд бездействия.

<p align="center">
  <img src="readme/preset-screen-guide.png" alt="Экран Preset Temporary" width="450">
</p>

### Display settings

Раздел Settings → Display: яркость, Auto Dim и выбор темы оформления.

<p align="center">
  <img src="readme/display-settings-guide.png" alt="Настройки Display" width="450">
</p>

### Screensaver

Полноэкранная заставка с аналоговыми часами. Управляется через Web UI; выход — касание экрана.

<p align="center">
  <img src="readme/screensaver-guide.png" alt="Экран Screensaver" width="450">
</p>

### Sleep Device

Режим глубокого сна: экран и работа устройства отключаются. Пробуждение касанием не поддерживается — нужен Reset или переподключение питания. Выбирается как действие окончания Sleep Timer.

<p align="center">
  <img src="readme/sleep-device-guide.png" alt="Режим Sleep Device" width="450">
</p>

## Web UI и Appearance

Web UI доступен по адресу `http://<IP-адрес-устройства>/` (IP показан на экране Info): управление воспроизведением, список станций, настройки поведения, Appearance и обновление прошивки/файловой системы.

<p align="center">
  <img src="readme/webui-appearance-entry.png" alt="Web UI: иконка Appearance" width="700">
</p>

### Station Artwork

Обложка действует только для текущей воспроизводимой станции. **Choose image** выбирает файл и показывает preview; **Upload to device** записывает изображение на устройство; **Remove artwork** удаляет его. Перед загрузкой картинка приводится к 120×120.

<p align="center">
  <img src="readme/webui-station-artwork-guide.png" alt="Web UI: Station Artwork" width="700">
</p>

### Color Theme и Custom Palette

Темы **Dark**, **Light** и **Custom** переключаются сразу и сохраняются. Custom использует загруженный `theme_custom.txt` (`key=#RRGGBB`) или встроенную палитру **Amber Hi-Fi**. **Upload & Apply** загружает палитру; **Remove custom palette** возвращает Amber Hi-Fi.

<p align="center">
  <img src="readme/webui-color-theme-guide.png" alt="Web UI: Color Theme и Custom Palette" width="700">
</p>

### Main Screen Backgrounds

Отдельные слоты фона Main для Dark, Light и Custom. **Choose image** только готовит preview; **Upload to device** записывает слот; **Remove image** удаляет только выбранный слот. Загрузка фона не переключает тему. Изображения приводятся к 480×480.

<p align="center">
  <img src="readme/webui-main-backgrounds-guide.png" alt="Web UI: фоны Main для Dark и Light" width="700">
</p>

<p align="center">
  <img src="readme/webui-custom-background-guide.png" alt="Web UI: фон Main для Custom" width="700">
</p>

## Отличия от оригинального проекта

Форк развивает YoRadio как проект с полноценным сенсорным интерфейсом на LVGL и отдельными аппаратными профилями поддерживаемых плат. По сравнению с исходным [e2002/yoradio](https://github.com/e2002/yoradio):

- новый UI на LVGL 8.3 вместо legacy Canvas-экранов на поддерживаемой плате;
- шестистраничное кольцо навигации и отдельные режимы Boot / Wi-Fi / Preset / Screensaver;
- Web UI Appearance: темы, фоны Main и station artwork;
- файловая система LittleFS;
- compile-time локализация RU / EN / PL / SK;
- optional AI Layer как тихий информационный слой поверх музыки;
- акцент на внешнем I2S ЦАП как основном варианте звука для повседневного использования.

Другие board-профили могут оставаться в исходниках, но в публичную beta входят только проверенные сценарии для ESP32-4848S040.

## AI Layer

AI Layer — необязательный тихий слой поверх музыки. Он не является ассистентом, не ведёт диалог и не стремится заполнять экран текстом: молчание для него — нормальное состояние. Для работы нужны OpenAI-compatible API, ключ, модель и prompt; без них YoRadio остаётся обычным интернет-радио.

Подробнее:

- [AI Layer в YoRadio](readme_ai_layer_rus.md) — назначение и философия слоя;
- [как устроен prompt](readme_ai_prompt_explained_rus.md) — правила языка, тона и формата вывода.

## Ограничения beta

- Публично поддерживается только ESP32-4848S040.
- Язык интерфейса (RU / EN / PL / SK) выбирается при компиляции; runtime-переключения нет.
- Выход из Sleep Device — только Reset или переподключение питания.
- Web UI работает в локальной сети, без HTTPS.
- Для полноценного звука рекомендуется внешний I2S ЦАП.

## С чего начать

1. Возьмите плату ESP32-4848S040 и подготовьте питание.
2. Используйте готовые файлы из [`build_bin/4848S040/`](build_bin/4848S040/) **или** соберите прошивку из исходников.
3. Пройдите Wi-Fi Setup при первом запуске.
4. Откройте Web UI по IP-адресу устройства.

Аппаратная инструкция для ESP32-4848S040 — подключение ЦАП, конфигурация, первый запуск и диагностика:

→ **[README_4848S040.md](README_4848S040.md)**

### Замечание для сборки из исходников

- Для source build требуется полный KnownGood-набор из `library!/esp32s3_5_5_2__3_3_6_ai_tls_profile_b_FULL_WORKING_20260603_221741/`. В нём находятся 10 согласованных архивов LwIP, esp_netif, HTTP и mbedTLS/TLS; ранний каталог только с `liblwip.a` и `libesp_netif.a` для текущей beta недостаточен.
- Набор предназначен для PIOArduino `55.03.36`, Arduino-ESP32 `3.3.6` и пакета `framework-arduinoespressif32-libs` `5.5.0+sha.f56bea3d1f` (ESP-IDF 5.5.2). Архивы копируются с заменой в `framework-arduinoespressif32-libs/esp32s3/lib/`, после чего нужно перезапустить PlatformIO, выполнить clean и собрать проект заново.
- Файлы набора: `libesp_http_client.a`, `libesp_netif.a`, `libesp-tls.a`, `libhttp_parser.a`, `liblwip.a`, `libmbedcrypto.a`, `libmbedtls.a`, `libmbedtls_2.a`, `libmbedx509.a`, `libtcp_transport.a`.
- Целевой каталог: Windows — `%USERPROFILE%\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib\`; Linux/macOS — `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/lib/`.
- После переустановки или обновления framework package замену нужно повторить.
- Эта операция нужна только при сборке из исходников. Готовые файлы из `build_bin/` уже собираются с нужным набором библиотек.
- Язык интерфейса задаётся в `src/myoptions.h` через `L10N_LANGUAGE`: `RU`, `EN`, `PL` или `SK`. Переключения языка во время работы нет.
- Исходники поддерживают RU, EN, PL и SK. Набор готовых бинарников конкретного release перечислен в [`build_bin/4848S040/README.md`](build_bin/4848S040/README.md).

> Файлы прошивки в `build_bin/` будут добавлены отдельным release-этапом.

## Благодарности

- **e2002** — автор оригинального проекта YoRadio;
- **Wolle (schreibfaul1)** — библиотека AudioI2S;
- **Maleksm** (4pda.to) — доработки AudioI2S;
- **moononournation** — библиотека Arduino_GFX;
- проект **LVGL** — графический движок интерфейса.

## Лицензия и авторы

Проект основан на [YoRadio](https://github.com/e2002/yoradio) (e2002) и распространяется на условиях **GNU General Public License v3 or later** — полный текст в [`LICENSE`](LICENSE).

Сторонние компоненты перечислены в [`NOTICE`](NOTICE). При распространении скомпилированной прошивки (`.bin`) GPL v3 требует обеспечить доступ к соответствующим исходникам — этот репозиторий и инструкции в `platformio.ini`.

## Обратная связь

Вопросы, ошибки и предложения — через [Issues](https://github.com/Witaliy76/Yoradio_lvgl/issues) и [Pull Requests](https://github.com/Witaliy76/Yoradio_lvgl/pulls) репозитория [Witaliy76/Yoradio_lvgl](https://github.com/Witaliy76/Yoradio_lvgl).
