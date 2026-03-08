# Stage 0 / Step 1 — Baseline Analysis & Plan

> Branch: `lvgl_stage0` (created from `main`)  
> Status: **ANALYSIS ONLY — no code changes**

---

## A. Repo Map (paths)

```
/workspace/                          ← PlatformIO project root
├── platformio.ini                   ← Build config, envs, lib_deps
├── partitions.csv                   ← N16R8 partition table (2×OTA 3.2MB + SPIFFS 3.2MB)
├── boards/
│   ├── esp32s3_n16r8.json           ← 16MB Flash + 8MB PSRAM board def
│   └── esp32s3_n4r8.json            ← 4MB Flash + 8MB PSRAM (future)
├── src/
│   ├── myoptions.h                  ← Active board config (DSP_MODEL, pins, touch, I2S)
│   ├── myoptions_4848S040.h         ← 4848S040 config (copy to myoptions.h to use)
│   ├── myoptions_JC3248W535C.h      ← JC3248W535C config
│   ├── myoptions_UEDX48480021.h     ← UEDX48480021 config
│   ├── mytheme.h                    ← Color theme overrides
│   ├── yoRadio.cpp                  ← Arduino entry (decorative, no code)
│   └── src/
│       ├── main.cpp                 ← setup() + loop() — real entry point
│       ├── core/
│       │   ├── display.h / display.cpp    ← Display orchestrator, DspTask, queue, flush
│       │   ├── player.h / player.cpp      ← Audio player (inherits from Audio)
│       │   ├── config.h / config.cpp      ← Central config, EEPROM/SPIFFS persistence
│       │   ├── controls.h / controls.cpp  ← Encoder/IR/button/touch input dispatcher
│       │   ├── network.h / network.cpp    ← WiFi, time sync, weather
│       │   ├── netserver.h / netserver.cpp ← HTTP + WebSocket server
│       │   ├── spidog.h / spidog.cpp      ← SPI mutex wrapper
│       │   ├── options.h                  ← Default defines, includes myoptions.h
│       │   └── common.h                   ← Shared enums (displayMode_e, pages_e, etc.)
│       ├── displays/
│       │   ├── dspcore.h                  ← #if DSP_MODEL dispatch → per-display header
│       │   ├── displayST7701.h / .cpp     ← ST7701 driver (4848S040: 480×480 RGB Panel)
│       │   ├── displayAXS15231B.h / .cpp  ← AXS15231B driver (JC3248W535C: 320×480 QSPI)
│       │   ├── displayUEDX48480021.h/.cpp ← UEDX driver (480×480 round RGB Panel)
│       │   ├── conf/displayST7701conf.h   ← Widget layout constants for ST7701
│       │   ├── conf/displayAXS15231Bconf.h ← Widget layout constants for AXS15231B
│       │   ├── widgets/widgets.h / .cpp   ← Custom widget system (~1276 lines)
│       │   ├── widgets/pages.h / .cpp     ← Pager + Page container system
│       │   └── tools/
│       │       ├── GFX_Canvas_screen.h/.cpp ← gfxDrawText(), gfxFlushScreen() wrappers
│       │       ├── utf8RusGFX.h / .cpp     ← UTF-8 → CP1251 conversion
│       │       ├── spectrum_analyzer.h/.cpp ← FFT spectrum analysis
│       │       └── spectrum_widget.h/.cpp  ← Spectrum bar renderer
│       ├── audioI2S/                       ← Audio library (MP3/AAC/FLAC/OGG/OPUS/WAV + I2S)
│       └── plugins/                        ← Plugin system + AI Layer
```

### PlatformIO environments

| Env | Board | Display | Resolution | Interface | Notes |
|-----|-------|---------|------------|-----------|-------|
| `4848S040` | esp32s3_n16r8 | ST7701S | 480×480 | RGB Panel | **Primary LVGL target** |
| `UEDX48480021` | esp32s3_n16r8 | ST7701S Type4/BGR | 480×480 | RGB Panel | Round display |
| `JC3248W535C` | esp32s3_n16r8 | AXS15231B | 320×480 | QSPI | Different resolution |
| `yoradio-esp32s3` | esp32s3_n16r8 | Same as JC3248W535C | 320×480 | QSPI | Legacy |

All share `[env:N16R8_base]`: 16 MB Flash, 8 MB OPI PSRAM, SPIFFS, `partitions.csv`.

---

## B. Display Pipeline Baseline

### Where `gfx` / Canvas is created

**File**: `src/src/displays/displayST7701.cpp`, function `DspCore::initDisplay()` (line 134)

```
1. bus = new Arduino_SWSPI(...)                        // line 142
2. rgbpanel = new Arduino_ESP32RGBPanel(...)           // line 154
3. output_display = new Arduino_RGB_Display(480, 480, rgbpanel, ...)  // line 179
4. gfx = new Arduino_Canvas(480, 480, output_display)  // line 193
5. gfx->begin()                                        // line 201
```

Global pointer: `Arduino_Canvas *gfx` — declared at `displayST7701.cpp:51`, referenced via `extern` in `display.cpp:10`, `widgets.cpp:22`, `spectrum_widget.cpp:10`.

### Where DspTask is created

**File**: `src/src/core/display.cpp`, function `Display::_createDspTask()` (line 52)

```cpp
xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE, NULL, 3, &DspTask, 0);
// CORE_STACK_SIZE = 1024*3 = 3072 bytes
// Priority 3, Core 0
```

Called from `Display::init()` (line 110).

### Where flush happens

**File**: `src/src/core/display.cpp`, function `Display::loop()` (lines 722–733)

```cpp
if(g_frameDirty && (millis() - lastFlushMs >= 16)){   // ~60 FPS cap
    sdog.takeMutex();                                  // SPI mutex
    gfxFlushScreen(gfx);                              // → gfx->flush()
    sdog.giveMutex();
    g_frameDirty = false;
    lastFlushMs = millis();
}
```

`gfxFlushScreen()` defined in `src/src/displays/tools/GFX_Canvas_screen.cpp:125`: just calls `gfx->flush()`.

### Where displayQueue / dirty flags are used

**Queue creation**: `display.cpp:103` — `displayQueue = xQueueCreate(5, sizeof(requestParams_t))`

**Queue send**: `display.cpp:575-586` — `Display::putRequest()` calls `xQueueSend(displayQueue, &request, ...)`

**Queue receive**: `display.cpp:639` — `xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)` inside `Display::loop()`

**Dirty flag**: `display.cpp:13` — `static volatile bool g_frameDirty = false;`  
**Set by**: `markFrameDirty()` (line 16), called from every `gfxDraw*()` function in `GFX_Canvas_screen.cpp`.

---

## C. FreeRTOS Tasks Baseline

| Task | Function | Core | Priority | Stack | File |
|------|----------|------|----------|-------|------|
| **DspTask** | `loopDspTask()` | 0 | 3 | 3072 B | `display.cpp:57` |
| **PeriodicTask** (Audio) | `Audio::taskWrapper()` | 1 (`AUDIOTASK_CORE=1`) | 4 | 13200 B (static) | `Audio.cpp:6984` |
| **async_tcp** | `_async_service_task()` | 0 | 5 | 32768 B | `AsyncTCP.cpp:244` |
| **doSync** | `doSync()` | 0 | 0 | 4096 B | `network.cpp:41` |
| **searchWiFi** | `searchWiFi()` | 0 | 0 | 4096 B | `network.cpp:207` |
| **AI_HTTP_Task** | `AITask::_taskWrapper()` | 0 | 1 | 16384 B | `ai_task.cpp:64` |
| Arduino `loop()` | `setup()` + `loop()` | 1 | 1 | default | `main.cpp` |

**Подтверждение**: Audio на Core 1 (P4), Display на Core 0 (P3) — физически изолированы. Это критическое свойство, которое Stage 0 не должен нарушать.

---

## D. Proposed LVGL Dependency Approach

### Рекомендация: Вариант A — `lib_deps`

**Строка для `platformio.ini` секции `[env]`:**

```ini
lib_deps = 
	https://github.com/moononournation/Arduino_GFX.git
	bblanchon/ArduinoJson@^7.4.2
	lvgl/lvgl@~8.3.0
```

**Аргументы за `lib_deps`:**

1. **Стандартный PlatformIO подход** — автоматическая загрузка, кэширование, воспроизводимость.
2. **`lvgl@~8.3.0`** = любая 8.3.x (8.3.0–8.3.11), но не 8.4 и не 9.x. Это фиксирует minor version.
3. **Vendor demos используют тот же подход** (`platformio.ini` в NorthernMan54 repo: `lvgl ^8.3.0-dev`).
4. **`lv_conf.h` подключение**: PlatformIO LVGL library ищет `lv_conf.h` в include path. Добавим build flag `-DLV_CONF_PATH=...` или положим файл в `src/src/lvgl_ui/lv_conf.h` с include path `-I src/src/lvgl_ui`.

**Вариант B (vendoring в `lib/`)** — отвергнут:

- LVGL v8.3 = ~2000 файлов, засоряет репозиторий.
- Теряется PlatformIO dependency resolution.
- Нет преимуществ при фиксации через `@~8.3.0`.

### Гарантия `lv_conf.h`

PlatformIO LVGL library использует `lv_conf.h` если он найден в include path. Два способа:

**Способ 1** (рекомендуемый): Положить `lv_conf.h` в `src/src/lvgl_ui/` и добавить build flag:
```ini
build_flags = 
	...
	-I src/src/lvgl_ui
	-DLV_CONF_INCLUDE_SIMPLE
```

**Способ 2**: Использовать `-DLV_CONF_PATH`:
```ini
build_flags = 
	...
	-DLV_CONF_PATH=${PROJECT_DIR}/src/src/lvgl_ui/lv_conf.h
```

Способ 1 проще и совместим с Arduino IDE (если кто-то захочет). Способ 2 — точнее, но менее портабелен.

### Риски

- **Нулевой runtime-риск**: LVGL линкуется, но если `lv_init()` не вызывается — никакой код LVGL не исполняется. Проверено: LVGL v8 не имеет static constructors с side effects.
- **Риск сборки**: `lv_conf.h` должен существовать и быть в include path, иначе LVGL не скомпилируется. Решение: создать `lv_conf.h` на этом же шаге.
- **Размер бинарника**: LVGL добавит ~50–100 KB к Flash (без виджетов, только core). При 3.2 MB app partition — не проблема.

---

## E. Stage 0 File Plan (NO CHANGES YET)

### Новые файлы

| Файл | Назначение |
|------|------------|
| `src/src/lvgl_ui/lv_conf.h` | LVGL конфигурация (основан на vendor demo: 16-bit color, custom tick via millis(), LV_MEM_SIZE=128KB, LV_DISP_DEF_REFR_PERIOD=20ms) |
| `src/src/lvgl_ui/.gitkeep` (или README) | Маркер директории для git (пустые директории не трекаются) |
| `src/src/lvgl_ui/profiles/` | Пустая директория (заполняется на Stage 3) |
| `src/src/lvgl_ui/screens/` | Пустая директория (заполняется на Stage 4) |
| `src/src/lvgl_ui/widgets/` | Пустая директория (заполняется на Stage 7) |
| `src/src/lvgl_ui/fonts/` | Пустая директория (заполняется на Stage 4) |

### Изменяемые файлы

| Файл | Изменение |
|------|-----------|
| `platformio.ini` | Добавить `lvgl/lvgl@~8.3.0` в `[env]` `lib_deps`. Добавить `-I src/src/lvgl_ui` и `-DLV_CONF_INCLUDE_SIMPLE` в `[env]` `build_flags`. |

### Файлы, которые НЕ меняются

- `src/src/core/display.cpp` — без изменений
- `src/src/core/display.h` — без изменений
- `src/src/displays/*` — без изменений
- `src/src/main.cpp` — без изменений
- `src/src/audioI2S/*` — без изменений
- `src/myoptions*.h` — без изменений
- Все FreeRTOS tasks — без изменений

---

## F. Safety Check

Почему предложенный план не меняет runtime:

1. **LVGL не инициализируется** — нет вызовов `lv_init()`, `lv_timer_handler()`, `lv_disp_drv_register()` нигде в коде.
2. **DspTask не затрагивается** — `display.cpp` не модифицируется. Очередь, dirty-flag, flush — всё остаётся как есть.
3. **Canvas pipeline не затрагивается** — `Arduino_Canvas *gfx`, `gfxFlushScreen()`, `markFrameDirty()` — без изменений.
4. **Audio task не затрагивается** — `Audio.cpp`, `player.cpp` — без изменений. Core/priority — без изменений.
5. **Ввод не затрагивается** — `controls.cpp`, `touchscreen.cpp` — без изменений.
6. **`lv_conf.h` — compile-time only** — это header с `#define`-ами, не имеющий runtime-эффекта без вызова `lv_init()`.
7. **LVGL в `lib_deps` — только компиляция** — PlatformIO скомпилирует LVGL как статическую библиотеку, но линкер включит только реально используемые символы (LTO / dead code elimination). Без вызовов `lv_*` — ноль кода в бинарнике.
8. **Build flags `-I` и `-DLV_CONF_INCLUDE_SIMPLE`** — только добавляют include path, не меняют существующие defines.

**Единственный побочный эффект**: размер скомпилированной библиотеки в `.pio/` увеличится (LVGL source кэшируется). На Flash устройства это не влияет, пока LVGL код не используется.
