# SaveManager — developer guide

---

## English

### What SaveManager is

SaveManager is YoRadio’s internal persistence layer for `config_t` and related NVS activity. **`Config`** remains the only public API (`saveValue`, setters); the **`sm::`** namespace is implementation-only (init, hooks, flush, OTA, restart).

### Why it exists

- Serialize and debounce flash writes so UI/audio hot paths do not hammer NVS.
- After migration to **v2**, persist `config_t` as **per-section blobs** in a dedicated Preferences namespace instead of a full-store write on every small change (M5: routine runtime no longer mirrors `config_t` to legacy EEPROM when v2 is on).
- Keep a **read** path from legacy EEPROM at boot for migration and older-firmware downgrade windows.
- Isolate **IR codes** in a separate EEPROM region and API (`syncIrBlobNow`).

### Architecture after M5 (SM_V2_ENABLED = 1, default)

| Layer | Role |
|--------|------|
| **`Config::saveValue`** | Updates RAM, then calls `sm::onFieldWrittenV2` (or `onStoreWriteCompleted` for commit-only nudges). |
| **Debounced worker** | On timer expiry, writes **dirty v2 sections** via `Preferences` (`commitV2DirtySectionsUnderMutex`). Does **not** run routine full `EEPROM.put(config.store)` (M5). |
| **`syncFullStoreNow`** | Explicit: full `EEPROM.put` + `EEPROM.commit` for `config_t`, then **`refreshManagedSectionsFromStore`** so all v2 blobs match RAM (factory reset, `setDefaults`, version migration, intentional full sync). |
| **Boot** | `eepromRead(EEPROM_START, store)` → **`sm::v2::runBootMigrationIfNeeded()`** (overlay or first-time migrate + marker). |

### v2-managed sections (current)

All rows in **`kConfigSectionSpans`** are v2-managed when `isV2ManagedSection` returns true for every `SectionId`: **Meta, Hot, Time, Weather, Tuning, Controls, Screensaver, Network, Ai**. Each has a fixed byte range inside `config_t` and a short NVS blob key (see `kV2SectionKeys` in `save_manager.cpp`).

### Legacy / special paths

| Path | Purpose |
|------|---------|
| **Legacy EEPROM read at boot** | Supplies initial RAM + migration snapshot before v2 overlay. |
| **`syncFullStoreNow`** | Writes full legacy `config_t` blob to EEPROM and refreshes v2 blobs. |
| **OOB `s_dirty` hatch** | If a write is not routed to a v2 section, `s_dirty` may still trigger a one-shot full EEPROM write in worker / `flushSync` / pre-init (rare). |
| **IR** | **`syncIrBlobNow`** at `EEPROM_START_IR` — not part of `config_t` or v2 section table. |
| **`SM_V2_ENABLED = 0` build** | Entire debounced path uses legacy **`commitV1FullStoreUnderMutex`** only (bring-up / experiments). |

### Boot flow (v2 on)

1. `EEPROM.begin`, `sm::init`.
2. `eepromRead(EEPROM_START, config.store)`.
3. Magic / `setDefaults` if invalid.
4. **`sm::v2::runBootMigrationIfNeeded()`**  
   - If **marker absent** and magic valid: `migrateFromLegacyStore()` writes all section blobs + marker.  
   - If **marker present**: optional **schema upgrade** (reseed newly promoted sections from current RAM snapshot), update marker, then **load** each managed section blob over RAM (failed load keeps EEPROM-backed bytes for that span).
5. Later init continues (`CONFIG_VERSION`, etc.).

### Runtime save flow (v2 on)

1. Field changes → `Config::saveValue` → **`onFieldWrittenV2(ptr, size, commit)`** sets **`s_dirty_v2_mask`** bit for the section containing the field (or `s_dirty` if out-of-store).
2. If `commit`: debounce timer reset; when it fires, worker runs **`commitV2DirtySectionsUnderMutex`**.
3. Commit-only nudge (`value unchanged`, `commit=true`) → **`onStoreWriteCompleted(true)`** arms debounce without forcing legacy dirty (M3.1).
4. **`flushSync` / `systemRestart` / `resumeAfterOta`**: flush pending v2 dirty mask (+ OOB hatch if `s_dirty`).

### Adding a field inside an existing section

1. Add the member to **`struct config_t`** in `config.h` (respect packing and order).
2. **Update `kConfigSectionSpans`** in `save_manager_sections.h` so the span that should own the field still covers the correct byte range (usually adjust **`byte_size`** of one row or shift boundaries — **must** keep contiguous partition of `config_t`; `static_assert`s enforce this).
3. If the field is in an already v2-managed section, **no schema bump** is required unless you change blob layout semantics for already-shipped devices (then treat as new section / migration).
4. Use existing **`Config::saveValue`** for persistence; routing is automatic by offset.

### Adding a new section

1. Add **`SectionId`** value (before `Count`) and a **`kConfigSectionSpans`** row that tiles the new bytes (split an existing span only if you are physically splitting `config_t` — normally you add fields at the end and extend **Ai** or insert a new enum row and **re-tile** all spans with new `static_assert`s).
2. Add **`kV2SectionKeys`** entry in `save_manager.cpp`.
3. Extend **`isV2ManagedSection`** if the new span should be v2-authoritative.
4. **Bump `kV2SchemaVersion`** and add an **`if (stored_ver < N)`** **`reseedSectionFromStoreImpl(NewId)`** branch in **`runBootMigrationIfNeeded`** so devices with an old marker do not keep stale blobs for the promoted span (same pattern as M4a–M4d).
5. **`writeMarker`** after successful upgrade path.

### When to bump schema version

Bump **`kV2SchemaVersion`** whenever you **change which sections are v2-managed at runtime** or change blob semantics in a way that requires **re-seeding from legacy EEPROM** on upgrade. Increment monotonically; document the new version in the schema history comment next to `kV2SchemaVersion`.

### When reseed is needed

Reseed copies bytes for one section from **current `config.store`** (already loaded from legacy EEPROM before overlay) into the v2 blob **before** overlay when upgrading from an older schema. Use it when a section **was cold on an older firmware** so its NVS blob might be stale relative to EEPROM. Not needed for routine field edits after the device is already on the current schema.

### IR (separate path)

IR payloads live outside **`config_t`**, at **`EEPROM_START_IR`**. **`sm::syncIrBlobNow`** writes raw bytes and commits; it does not use the section table or v2 marker. **`Config::saveIR`** calls this API.

### Debug / compile flags

| Flag | Where | Effect |
|------|--------|--------|
| **`SM_DIAG_PERSIST`** | Default `0` in `save_manager.cpp`; override with e.g. `-DSM_DIAG_PERSIST=1` | Enables **`[sm]`** Serial logs (init, worker class, v2 commits, `ofv2` routing, etc.). |
| **`SM_V2_ENABLED`** | Default `1` in `save_manager_sections.h`; `-DSM_V2_ENABLED=0` for legacy v1-only worker | Switches between v2 debounced path and full-store EEPROM debounced path. |

### Verification after changes

- Boot with and without v2 marker; confirm overlay logs / no data loss for managed sections.
- Change station, volume, weather, AI toggle, tuning/control/web settings; confirm **no routine** `EEPROM.put+commit` for `config_t` when v2 + M5 (use `SM_DIAG_PERSIST=1` if needed).
- **`syncFullStoreNow`** path (factory reset / defaults): EEPROM + v2 blobs consistent.
- IR save (if enabled): still works.
- OTA suspend/resume: no pending loss; **`flushSync`** after resume.
- Optional: downgrade scenario — legacy EEPROM still updated on explicit sync.

---

## Русская версия

### Что такое SaveManager

Внутренний слой сохранения настроек **`config_t`** и связанной работы с NVS в YoRadio. Снаружи доступен только **`Config`** (`saveValue`, сеттеры); пространство имён **`sm::`** — реализация (init, хуки, flush, OTA, перезагрузка).

### Зачем нужен

- Сериализация и debounce записей во flash, чтобы горячие пути UI/аудио не забивали NVS.
- В режиме **v2** — хранение **`config_t` по секциям** (отдельный namespace Preferences), без полного `EEPROM.put` на каждое мелкое изменение (M5: в обычном runtime полный зеркальный EEPROM для `config_t` не пишется при включённом v2).
- Сохранение **чтения** legacy EEPROM при старте для миграции и окна отката на старую прошивку.
- Отдельный путь для **ИК-кодов** (`syncIrBlobNow`, другой регион EEPROM).

### Как устроено после M5 (SM_V2_ENABLED = 1 по умолчанию)

| Уровень | Роль |
|---------|------|
| **`Config::saveValue`** | Правит RAM, вызывает `sm::onFieldWrittenV2` или `onStoreWriteCompleted` для commit-only nudge. |
| **Worker по debounce** | По таймеру пишет только **грязные v2-секции** в Preferences. Рутинный полный `EEPROM.put(store)` не вызывается (M5). |
| **`syncFullStoreNow`** | Явно: полный снимок `config_t` в EEPROM + обновление **всех** v2-блобов из RAM (сброс, дефолты, миграция версии). |
| **Бут** | `eepromRead` → **`runBootMigrationIfNeeded`** (overlay или первая миграция + маркер). |

### Какие секции в v2

Все строки **`kConfigSectionSpans`**: **Meta, Hot, Time, Weather, Tuning, Controls, Screensaver, Network, Ai** — у каждой свой непрерывный диапазон байт в `config_t` и ключ NVS (см. `kV2SectionKeys` в `save_manager.cpp`).

### Что осталось legacy / особый путь

| Путь | Назначение |
|------|------------|
| **Чтение EEPROM при буте** | Начальный снимок RAM и миграция до overlay v2. |
| **`syncFullStoreNow`** | Полная запись legacy `config_t` в EEPROM + синхронизация v2-блобов. |
| **Hatch по `s_dirty` (OOB)** | Редкий полный EEPROM, если запись не попала в секцию v2. |
| **IR** | **`syncIrBlobNow`**, регион **`EEPROM_START_IR`** — не в таблице секций. |
| **Сборка с `SM_V2_ENABLED=0`** | Только legacy full-store в worker. |

### Boot flow (v2 включён)

1. `EEPROM.begin`, `sm::init`.
2. Чтение `config.store` из EEPROM.
3. Проверка magic / `setDefaults`.
4. **`runBootMigrationIfNeeded`**: нет маркера и валидный magic → миграция во все блобы + маркер; маркер есть → при необходимости **апгрейд схемы** (reseed) и **overlay** секций в RAM.
5. Дальнейший init (`CONFIG_VERSION` и т.д.).

### Обычный runtime save (v2)

1. Изменение поля → `saveValue` → **`onFieldWrittenV2`** → бит **`s_dirty_v2_mask`** (или `s_dirty` вне `config_t`).
2. При `commit` — сброс debounce-таймера → worker → **`commitV2DirtySectionsUnderMutex`**.
3. Commit-only nudge → **`onStoreWriteCompleted(true)`** без ложного «полного» dirty на v1 (M3.1).
4. **`flushSync` / перезапуск / resume OTA`** — сброс очереди грязи v2 (+ hatch при `s_dirty`).

### Новый параметр в существующей секции

1. Поле в **`struct config_t`** (`config.h`).
2. Правка **`kConfigSectionSpans`** — размер/границы строки секции; **`static_assert`** на непрерывность `config_t`.
3. Если секция уже в v2 и семантика блоба для уже выпущенных устройств не ломается — **отдельный bump схемы не обязателен**.
4. Сохранение через **`saveValue`** — маршрутизация по смещению автоматическая.

### Новая секция

1. Новый **`SectionId`** (перед `Count`) + строка в **`kConfigSectionSpans`** (переразметка `config_t` с валидными `static_assert`).
2. Ключ в **`kV2SectionKeys`**.
3. **`isV2ManagedSection`** — если секция должна быть в v2.
4. **`kV2SchemaVersion++`** и ветка **`reseedSectionFromStoreImpl`** в **`runBootMigrationIfNeeded`** для старых маркеров.
5. Обновить комментарий истории схемы у маркера.

### Когда поднимать schema version

При расширении множества v2-секций или изменении смысла блоба так, что на апгрейде нужен **reseed из EEPROM-снимка**. Номер только растёт; зафиксировать в комментарии к `kV2SchemaVersion`.

### Когда нужен reseed

Чтобы перед overlay на новой прошивке **перезаписать блоб секции** из актуального `config.store` (после чтения EEPROM), если на старой схеме эта секция писалась только в EEPROM и блоб в NVS мог устареть.

### IR отдельно

Данные ИК **вне `config_t`**. Пишутся через **`syncIrBlobNow`**, не через секции и не через v2-маркер в том же смысле, что `config_t`.

### Флаги отладки

| Флаг | Где | Эффект |
|------|-----|--------|
| **`SM_DIAG_PERSIST`** | По умолчанию `0` в `save_manager.cpp`; `-DSM_DIAG_PERSIST=1` | Краткие логи `[sm]` в Serial. |
| **`SM_V2_ENABLED`** | По умолчанию `1` в `save_manager_sections.h`; `-DSM_V2_ENABLED=0` | Полный legacy-путь debounce в EEPROM. |

### Что проверить после правок

- Холодный старт с/без маркера v2; целостность настроек.
- Станция, громкость, погода, AI, веб-настройки — без рутинного `EEPROM.put+commit` для `config_t` при v2+M5 (при необходимости включить `SM_DIAG_PERSIST`).
- **`syncFullStoreNow`** после сброса/дефолтов.
- IR (если есть).
- OTA: suspend/resume + `flushSync`.
- По желанию: сценарий даунгрейда после явного full sync.
