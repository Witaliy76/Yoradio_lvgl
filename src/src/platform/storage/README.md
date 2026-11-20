# Storage Contract (S07)

Author: Witaliy76 - https://github.com/Witaliy76

## English

`platform/storage/` implements the compile-time contract, Storage Owner and
read-only FS bridge used by the existing Audio MP3/FLAC path. On this branch
that behavior is locally accepted and is not a published release.
`SDC_CS` stays 255 and legacy `USE_SD` / SDManager stay off. The ordinary
`4848S040` build compiles the Storage Owner through the internal flag
`YORADIO_STORAGE_OWNER_ENABLED`. The user switch is `YORADIO_SD_ENABLE` in
`src/myoptions.h`: `1` permits SD, `0` skips early and runtime mount and does
not reserve GPIO 41/42. BoardProfile still only records that the slot exists.
`StorageOwner::status()` reports whether the card is actually ready. Those
three facts meet in `kResolvedStorageConfig.enabled`. Do not turn SD on with
`USE_SD`, `SDC_CS`, or another PlatformIO environment.

### S07 working build

The Git branch stays `feature/s07-storage-shared-resources`. The working
PlatformIO environment is ordinary `4848S040` (`default_envs`).

The build output is `.pio/build/4848S040/firmware.bin`. FATFS LFN/UTF-8 for
this environment comes from `sdkconfig.defaults.storage_owner`.

- Build: `pio run -e 4848S040`
- Flash: `pio run -e 4848S040 -t upload`

IDE Build and Upload follow the status-bar environment. It must show
`4848S040`. The locally accepted image is
`0.9.434m-r2-lvgl-beta.3.08-S07.B083` with WebUI cache tag `webnav6`.
That acceptance is not a publication. Earlier images, including B045
(2732960 bytes, SHA256
`0406a7125f21de3484f6c6bffd0724a783be95d55b9f2c3d0b6fe91764c9428c`)
and B046 (`.pio/artifacts/s07-b046/firmware.bin`, 2731392 bytes, SHA256
`6e816f114b84ba2843282c65a8dee7abd3e336a73ce1d6096cda11dcb24f38b6`),
remain historical records.

`index.html` and `script.js` are replaced by `POST /webboard` with the form
field `www`. A successful upload reboots the device. That path is not
`uploadfs` and does not write a whole `littlefs.bin`. Playback is
`POST /sd/play`. The general `POST /` does not accept `s07_sd_path`.

### Components and entry points

| Component | Responsibility / entry points |
|---|---|
| `BoardProfile.storage.onboard_sd_present` | Factory slot-presence fact; no SD GPIO fields yet |
| `ResolvedStorageConfig` / `storage_validate.h` | Owner build, `YORADIO_SD_ENABLE` and the board slot; `SDC_CS != 255` and legacy `USE_SD` are refused |
| `storage_boot_config.cpp` | Owner binding; `kResolvedStorageConfig.enabled == false` leaves the candidate pins disconnected |
| `storage_owner.h/.cpp` | `StorageOwner::instance()` owns mount lifecycle and operation/handle accounting |
| `storage_sdspi.h/.cpp` | Private IDF SPI2/device/card/FAT/VFS implementation; no raw card or mount exported |
| `media_ref.h` | Exact owned UTF-8 path, mount generation, path validation and `FileError` |
| `storage_fs_bridge.h/.cpp` | `StorageFsBridge::bind(generation)`, `fileStatus()` and `probeMediaFile()`; restricted Arduino FS interface for Audio. The probe uses an owner lease and does not report a media fault |
| `directory_service.h/.cpp` | One folder, async: `directorySubmit`, `directoryPoll`, `directoryRelease` |
| `../../core/path_play_request.h` / `Player` | `PathPlayRequest {media, epoch}`, MP3/FLAC policy, `PathPlayResult`, `requestPlay()` and playback diagnostics |
| `../../core/source_catalog.h` | Shared source list. Radio and SD are Implemented. USB, DLNA, NAS/SMB2 and the later panels are Planned names only; `GET /sources` does not publish them |
| `../../core/netserver.cpp` `GET /sources` | Read-only WebUI adapter over `kCatalog` + `openSource` + `StorageOwner::status()`. Does not call `activate` or `changeMode` |

Boot hooks run on the setup/loop lifecycle task: `prepareBeforeDisplay()` before
Type9, `mountAfterDisplay(display_ready)` afterwards, and `processFault()` in
loop. The enabled implementation enters SPI mode at 400 kHz, confirms full
release for Type9, then mounts at 20 MHz and keeps that mount. If that mount
returns FatFs 13 (`FR_NO_FILESYSTEM`) after a clean release, it tries once at
10 MHz. The earlier 1 MHz runtime result remains historical evidence in the
S07 stage plan; it is not the current setting. When
`kResolvedStorageConfig.enabled` is false this IO stays disconnected.
DisplayCommand protection is enforced by the ST7701 backend.
`status()` copies metadata under a short critical section; `refreshSpace()`
samples free/total only on the lifecycle task. No driver/FS call runs under
the metadata lock. `Absent` means no usable card response, not physical detection:
there is no card-detect GPIO.

### Operation and close ownership

- `OperationLease` admits and counts synchronous IO until it returns. Reserve
  `HandleLease` before open. Neither token imposes a time limit. Tokens are
  move-only, exclusive and must not be accessed concurrently.
- After earlier IO on a handle finishes, `acquireClose(handle, close)` transfers
  its reservation into a counted close-only `CloseLease`, including after
  Quiescing/Fault prohibits new data operations. It cannot authorize read/open.
  `confirmClosed()` releases both counts exactly once, only after actual close
  or a proven failed open with no descriptor. Storage never closes another
  task's File/DIR. `releaseAfterClose()` remains for already-confirmed closure
  or failed-open bookkeeping; the bridge uses `CloseLease`.
- Unknown/failed native close retains the handle count and records
  `UnclosedHandle`. The bridge does not retry a descriptor that IDF may already
  have discarded/reused. The shared `StorageFileStatus::handles` also stays
  nonzero, so losing the Arduino File wrapper cannot fake successful Audio stop.
- `beginQuiesce()` prohibits new operations and invalidates the active generation.
  `unmountWhenDrained()` refuses teardown until in-flight and handle counts are
  both zero; it does not wait for clients to drain. Teardown itself is synchronous.
  Ordinary STOP/source selection keeps SD mounted. No automatic remount exists.
- `YORADIO_SD_SSR_TIMEOUT_FALLBACK` in `src/myoptions.h` is compiled into
  `storage_sdspi.cpp`. The ordinary `4848S040` Owner build links
  `--wrap=sdmmc_init_sd_ssr`. The real call runs first. `1` continues our
  SDSPI mount after `ESP_ERR_TIMEOUT` from that step, then clears the
  erase-timing fields. `0`, any other error, and a call outside our mount
  keep the IDF error. This fallback is separate from the single 20 MHz to
  10 MHz retry after FatFs 13. Later card-init checks and the real FAT mount
  still run. `ready` is not forced. Write, erase, trim, and format are not
  added. A kept timeout is not reported as an absent or damaged card.

A successful runtime mount gets a nonzero `MediaGeneration`, a runtime mount
incarnation rather than a card UUID. Admission checks it exactly; non-ready
snapshots expose generation/free/total as zero. `reportMediaFault()` immediately
prohibits new operations; `processFault()` consumes the signal on the lifecycle
task. Stale reports cannot fault a different mount. Failed mount with confirmed
cleanup leaves Radio/display boot available; failed cleanup retains ownership.
That boot behavior does not imply Radio can start after an Audio close failure.

### Exact path and Audio adapter

`MediaRef::copy(path, generation, out)` owns the complete absolute SD-root-relative
path, up to 1023 UTF-8 bytes excluding NUL. No trimming, normalization or
truncation. Invalid UTF-8/control bytes, traversal, duplicate/backslash separators
and FAT trailing-dot/space aliases are rejected. Shared byte validation lives in
`../../utils/utf8.h`, without a Storage dependency on Audio helpers.

Player owns `PathPlayRequest::copy(path, generation, player.pathEpoch(), out)`
and `requestPlay(request)`. The two-item queue owns the bytes and submits with
zero wait; `Accepted` means queued, not playing. Generation/epoch are rechecked;
STOP/reset/explicit numeric Radio play cancel stale requests. Player accepts
case-insensitive MP3/FLAC and confirms stop before Radio↔SD or SD↔SD starts.
`PlaybackContext`/Config mode describe the confirmed source, not queue admission.
Failed open after successful stop leaves Stopped/error, without automatic Radio
fallback. NoMedia/stale generation/open/read/seek errors are distinguished.
Path SD playback owns an independent folder queue (`core/folder_queue.*`):
EOF and Next share one root-bounded depth-first search (natural-sorted
playable files of a folder, then its subfolders) bound to the search-start
epoch; Prev peeks played history (depth 8) and returns `NoAdjacentTrack` at
the start, popping only after a confirmed transition. Only the Player owner
mutates queue state; `sendCommand`/`requestPlay` invalidate via epoch, and a
rejected path request does not cancel an active search. Browse cwd in
WebUI/LVGL does not change that queue. Repeat Off/One/Folder/All and Folder
Shuffle are Player RAM modes; WebUI shows confirmed state only while SD plays.
FileError stays separate. A valid filesystem path alone says nothing about
decoder support.

Long names are not read through POSIX `readdir`. IDF 5.5.5 hardcodes
`FF_LFN_BUF` to 255 bytes, and `dirent.d_name` is 256 bytes; a longer UTF-8
LFN would be replaced by its short name. The owner build mirrors the
fatfs sources it compiles with `configure_file(... COPYONLY)` into the build
directory and replaces `ffconf.h` with `fatfs_lfn_override/ffconf.h`, which
raises that byte buffer to 768 plus NUL. Changing the wrapper or the C helper
reconfigures the mirror without deleting the build tree. `vfs_fat_internal.h`
is copied into that same directory: it quote-includes `esp_vfs_fat.h` and
`diskio_impl.h`, and `#pragma once` treats the SDK path as another header.
The installed SDK is
not edited. `listDirectory` uses `f_opendir` / `f_readdir` / `f_closedir`,
copies a name only when it fits exactly, and closes the directory on path,
name, and read errors. `FR_DISK_ERR` / `FR_NOT_READY` call
`StorageOwner::reportMediaFault` with the original FRESULT; missing path and
out-of-memory do not. A short name is returned only when it is the entry's own
name; length checks do not detect an SFN already substituted by FatFs.
`listDirectory` is the synchronous scan. Directory Service, below, is the
asynchronous consumer of that scan. The serial commands `s07dir` and `s07act` are compiled only when Storage Owner
is on and `YORADIO_STORAGE_DIAGNOSTICS` is exactly 1. The default is 0, so an
ordinary build has neither command and does not read GPIO48 or collect
`[S07-ENG]` snapshots. `s07dir` runs the scan on the loop task for the root
directory only, while playback is stopped, and may submit the returned path to
Player. It is not the browser API. `s07act` uses that same compile gate and
the same telnet/serial parser. `s07act radio` and `s07act sd` call `Player::activate` and print the
admission. `s07act status` prints `activation()`, the selected source, the
sounding source or `None`, Playing/Stopped/StopFailed, and `pathEpoch()`.
Status does not call `activate`. The handler does not call `Player::loop`
and does not scan or open the card.

### Directory Service

`directorySubmit`, `directoryPoll` and `directoryRelease` read one directory.
WebUI lists through `sdBrowserList` (`GET /sd/dir`). LVGL lists through
`DirectoryConsumer::Lvgl` in `lvgl_ui/adapters/browser_provider.cpp`.
Folder-queue search uses `DirectoryConsumer::Playback`. A submit does not
change the sounding source and does not touch the player queue. Browsing
folders is not the same action as `Player::activate` on the source overlay.
One exact path is played with `POST /sd/play` through `requestSdPlayback` and
the existing Player path request. HTTP 202 means the path was queued. It does
not mean sound has started. The general `POST /` does not accept
`s07_sd_path`. An ordinary build prints `[Storage]` for the runtime mount, an
SSR fallback, the 20 to 10 MHz retry, file and directory errors, and a failed
stop barrier. A successful STOP does not print that line;
`stop barrier=CONFIRMED` exists only when `YORADIO_STORAGE_DIAGNOSTICS` is 1.
Those ordinary actions run with diagnostics off. The default is 0. Verbose
`[S07-ENG]` lines, `s07dir` and `s07act` require the flag to be 1.

The first successful submit creates one task, `StorageDir`, priority 1 on core 0.
Audio stays priority 4 on core 1. This task is not the JPEG worker. The static
task stack is **12 KiB in internal DRAM** (ESP-IDF depth in **bytes**);
catalog snapshots and directory slots stay in PSRAM. External task stacks are
allowed in sdkconfig (`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`), but StorageDir
does not use a PSRAM stack until FatFs/SPI on that path is separately proven.
`Ok` means a worker will run the request. `NoMemory` means the request was not
queued. The worker could not be created, or another caller is still creating it.
That caller returns at once and does not wait. Host and device use the same
decision. The public status enum was not extended.

The worker calls `listDirectory`, then sorts directories ahead of MP3/FLAC
with the accepted natural order. Other files are omitted. The WebUI parent
row is `..` (no icon) when the path is not `/`; it is not a directory entry.

The service stores no current folder. Each query has a consumer (`WebUi`,
`Lvgl`, or `Playback`), a nonzero request id, the mount generation and the
path. Playback is reserved for folder-queue next-track search and must not
share the WebUI or LVGL slot. A newer query from the same consumer replaces
that consumer's slot only. The worker checks cancellation between FatFs
calls and closes only the directory that the scan opened.

`directoryPoll` retains an immutable snapshot. The caller frees that retain
with one `directoryRelease`. Until the next submit from the same consumer,
the service also holds the latest published snapshot, so another poll of the
same request can copy a later page without walking the card. A new request
drops that service reference outside the service lock. A reference already
returned by `directoryPoll` stays valid. The next poll is the new request,
not the previous catalog.

A retained snapshot does not mean the card is ready. The caller checks
`StorageOwner` ready and the mount generation before serving it. A generation
mismatch is not served as the current page.

Packed names are the stored bytes; a full `DirEntry` is not kept per row.
`directoryItemPath` writes the joined path into a buffer the caller owns.

The 2 MiB budget (`kDirectorySnapshotBudget`) counts every PSRAM block that
is allocated at the same time: snapshots callers still hold, the catalog
being built, and the previous block while it is copied. A temporary copy is
not free budget. A growth that cannot hold both copies returns `ResourceLimit`
even when the finished catalog alone would fit. The worker stack is 12 KiB.
The queue is three slots, one pending request per consumer. Crossing the budget
does not publish a partial catalog. `ReadFailed`, `StaleMedia` and `NoMemory`
stay distinct. There is no multi-folder cache, whole-card index, or
automatic remount. The folder queue lives in Player (`core/folder_queue.*`),
not in Directory Service.

WebUI stores one identity: client, request, path and generation. `start=1`
accepts a scan. A later poll or page does not. Two browsers that reuse the
same request counter do not receive each other's snapshot. A late poll of a
replaced request is `cancelled` and does not scan again. While a scan is
still running, another start receives `busy`.

One JSON row is sized for a legal name, its full path, and the worst escape
of six bytes per source byte. The page stays at most 40 rows and 12 KiB. A
row that does not fit that page is a limit, not an allocation failure.
Names are escaped as JSON text. The network handler sends the copied page
and does not wait for the scan.

Host checks: `test/s07_6b1b_directory/`, 242 assertions, process exit 0.
They cover live allocator bytes, the peak copy, the dropped service
reference, worker creation already in progress, two clients with the same
request and different paths, a late poll after replacement, one scan for
both pages, and a long name in a nested folder. Folders of 100/500/2000
files and opening a returned name longer than 255 UTF-8 bytes are not
measured on the device. A full Folder Shuffle of 4096 entries and
cancellation races have host coverage only.

The bridge opens regular files read-only on the owner's mount, serializes IO,
counts one physical handle despite Arduino File aliases, and rejects mutation
and directory traversal APIs. It is an Audio adapter, not a Browser API.
Installed Arduino FS uses a recursive FreeRTOS mutex: open error cleanup and
destruction may safely call close while the same task holds the binding lock.
Host fixtures exercise that branch with a recursive mutex double.

Audio owns File/decoder stop. Confirmed close is required by the Stop Barrier
before reset/new playback. After a transient control/decoder-lock timeout,
explicit STOP/toggle can retry; a later Play is separate. After unknown native
close, repeated STOP cannot restore lost close proof: Radio and SD starts,
decoder reset and unmount remain blocked until restart. Volume/mute and
diagnostics remain available. No recovery state machine is implemented here.

### Timing guarantees

`mount_timeout_ms` and `max_mount_retries` are reserved configuration fields:
the owner/driver do not consume them. `mount_path` is bound to the owner;
autoformat remains prohibited. There is one early 400 kHz entry, a release
before Type9, and one runtime mount at 20 MHz. FatFs 13 after a clean release
gets one more mount at 10 MHz. There is no further retry and no automatic
remount. The SSR timeout fallback is a different path. SDSPI retains SDK
per-command defaults (`command_timeout_ms=0`);
FatFs lock timeout is a synchronization timeout, not a whole-call deadline.
The Arduino binding mutex waits with `portMAX_DELAY`. Mount/open/read/seek/close,
space queries and cleanup are synchronous, without a guaranteed completion time.
Audio's 100 ms control/decoder lock waits do not bound subsequent FS close.
Path/queue size limits and zero-wait submission bound resources/admission only.

### Wiring evidence and engineering probe

| Net | Candidate GPIO | Stock owner |
|---|---:|---|
| LCD_CS | 39 | Display; never an SD line |
| TF CMD / DisplayCommand SDA | 47 | Display command |
| TF CLK / DisplayCommand SCK | 48 | Display command |
| TF DAT0 | 41 | None with SD off; conflicts with IR when enabled |
| TF DAT3/CS | 42 | None with SD off; conflicts with MUTE when enabled |

`schematic_candidate_4848s040.h` records SCHEMATIC CONFIRMED nets from
[gys2005 sheets](https://github.com/gys2005/ESP32-4848S040). SD GPIO are not
BoardProfile fields. When `YORADIO_SD_ENABLE` is 1, `storage_validate.h`
checks GPIO 41/42 against display, touch, I2S, IR, MUTE and WAKE. GPIO 47/48
stay DisplayCommand lines and are shared with TF CMD/CLK only across the
early release and the later runtime mount. The check does not prove the wire.

The disposable SDSPI probe and its PlatformIO environment were removed after
the Owner moved into `4848S040`. FATFS LFN heap, UTF-8 and `MAX_LFN=255` for
that build come from `sdkconfig.defaults.storage_owner`. The mirrored
`ffconf.h` sets `FF_LFN_BUF` to 768 and `FF_FS_EXFAT` to 1, so the owner
build includes FAT12/16/32 and exFAT. One successful card is not a general
filesystem proof. Directories of 100/500/2000 entries and a listing name
longer than 255 UTF-8 bytes are not measured on the device.

Targeted tests: `test/s07_4a2_storage_owner/`, `test/s07_4a3_single_file/`
(bridge) and `test/s07_4a4a_source_transitions/` (current Player transitions).
Numeric IDs remain Radio-only; mode-only SD needs an explicit path. Commands,
seek and EOF run on the Player owner without Wi-Fi or legacy SD index/scanning.
B003–B005 remain the early mount evidence; they are not the current image.
WebUI, LVGL and folder-queue search are the three Directory Service consumers.
Legacy `USE_SD` stays off. Local acceptance of this behavior is not a
publication.

## Русский

`platform/storage/` реализует контракт при сборке, Storage Owner и адаптер FS
только для чтения существующим Audio MP3/FLAC. На этой ветке такое поведение
принято локально и не является публикацией. `SDC_CS` остаётся 255, legacy
`USE_SD` / SDManager выключены. Штатная сборка `4848S040` компилирует Storage
Owner внутренним флагом `YORADIO_STORAGE_OWNER_ENABLED`. Пользовательский
переключатель — `YORADIO_SD_ENABLE` в `src/myoptions.h`: `1` разрешает SD,
`0` пропускает ранний и runtime mount и не резервирует GPIO 41/42.
BoardProfile по-прежнему только отмечает наличие слота.
`StorageOwner::status()` сообщает, готова ли карта. Эти три факта сходятся в
`kResolvedStorageConfig.enabled`. Не включайте SD через `USE_SD`, `SDC_CS`
или другое окружение PlatformIO.

### Рабочая сборка S07

Ветка Git остаётся `feature/s07-storage-shared-resources`. Рабочее окружение
PlatformIO — обычный `4848S040` (`default_envs`).

Результат сборки — `.pio/build/4848S040/firmware.bin`. FATFS LFN/UTF-8 для
этого окружения берётся из `sdkconfig.defaults.storage_owner`.

- Сборка: `pio run -e 4848S040`
- Прошивка: `pio run -e 4848S040 -t upload`

Кнопки Build и Upload в IDE берут окружение из строки состояния. Там должно
быть `4848S040`. Локально принятый образ —
`0.9.434m-r2-lvgl-beta.3.08-S07.B083`, метка кэша WebUI `webnav6`.
Эта приёмка не является публикацией. Прежние образы, включая B045
(2732960 байт, SHA256
`0406a7125f21de3484f6c6bffd0724a783be95d55b9f2c3d0b6fe91764c9428c`)
и B046 (`.pio/artifacts/s07-b046/firmware.bin`, 2731392 байт, SHA256
`6e816f114b84ba2843282c65a8dee7abd3e336a73ce1d6096cda11dcb24f38b6`),
остаются историческими записями.

`index.html` и `script.js` заменяются запросом `POST /webboard` с полем
формы `www`. Успешная загрузка перезагружает устройство. Это не `uploadfs`
и не запись целого `littlefs.bin`. Воспроизведение — `POST /sd/play`.
Общий `POST /` не принимает `s07_sd_path`.

### Компоненты и точки входа

| Компонент | Ответственность / точки входа |
|---|---|
| `BoardProfile.storage.onboard_sd_present` | Заводской факт наличия слота; полей GPIO SD пока нет |
| `ResolvedStorageConfig` / `storage_validate.h` | Сборка Owner, `YORADIO_SD_ENABLE` и слот платы; `SDC_CS != 255` и legacy `USE_SD` отвергаются |
| `storage_boot_config.cpp` | Привязка Owner; при `kResolvedStorageConfig.enabled == false` кандидатные пины отключены |
| `storage_owner.h/.cpp` | `StorageOwner::instance()` владеет монтированием и учётом операций/дескрипторов |
| `storage_sdspi.h/.cpp` | Внутренняя реализация IDF SPI2/device/card/FAT/VFS; сырые card/mount наружу не выдаются |
| `media_ref.h` | Полный UTF-8 путь, поколение монтирования, проверка пути и `FileError` |
| `storage_fs_bridge.h/.cpp` | `StorageFsBridge::bind(generation)`, `fileStatus()` и `probeMediaFile()`; ограниченный интерфейс Arduino FS для Audio. Проверка файла берёт lease владельца и не сообщает media fault |
| `directory_service.h/.cpp` | Одна папка, асинхронно: `directorySubmit`, `directoryPoll`, `directoryRelease` |
| `../../core/path_play_request.h` / `Player` | `PathPlayRequest {media, epoch}`, правила MP3/FLAC, `PathPlayResult`, `requestPlay()` и диагностика воспроизведения |
| `../../core/source_catalog.h` | Общий список источников. Radio и SD — Implemented. USB, DLNA, NAS/SMB2 и дальнейшие панели — только имена Planned; `GET /sources` их не публикует |
| `../../core/netserver.cpp` `GET /sources` | Read-only адаптер WebUI над `kCatalog` + `openSource` + `StorageOwner::status()`. Не вызывает `activate` и `changeMode` |

Загрузочные вызовы выполняет задача setup/loop: `prepareBeforeDisplay()` до
Type9, `mountAfterDisplay(display_ready)` после него, `processFault()` в loop.
Включённая реализация входит в SPI mode на 400 кГц, подтверждает полное
освобождение ресурсов для Type9, затем монтирует на 20 МГц и сохраняет mount.
Если этот mount возвращает FatFs 13 (`FR_NO_FILESYSTEM`) после чистого
освобождения, выполняется одна попытка на 10 МГц. Прежний результат 1 МГц
остаётся историческим свидетельством в stage-plan S07 и не является текущей
настройкой. При `kResolvedStorageConfig.enabled == false` этот IO не
занимается. Защиту DisplayCommand обеспечивает
ST7701 backend. `status()` копирует метаданные под короткой критической секцией;
`refreshSpace()` получает free/total только в задаче владельца. Под блокировкой
метаданных нет драйверных/FS-вызовов. `Absent` означает отсутствие ответа
пригодной карты, а не физическое обнаружение: отдельного CD GPIO нет.

### Допуск операций и владение закрытием

- `OperationLease` учитывает допущенный синхронный IO до возврата вызова.
  `HandleLease` резервируется до open. Ограничения времени эти объекты не задают.
  Они передаются только перемещением; одновременный доступ к одному объекту
  недопустим.
- После завершения прежнего IO вызов `acquireClose(handle, close)` передаёт
  резервирование в учитываемую операцию закрытия `CloseLease`, в том числе при
  запрете новых операций в Quiescing/Fault. Этот объект не разрешает read/open.
  `confirmClosed()` ровно один раз уменьшает оба счётчика, только после реального
  закрытия или подтверждённого failed open без дескриптора. Storage не закрывает
  чужие File/DIR. `releaseAfterClose()` остаётся для учёта уже подтверждённого
  закрытия/failed open; адаптер использует `CloseLease`.
- Неизвестный результат/ошибка native close сохраняет счётчик дескриптора и
  фиксирует `UnclosedHandle`. Адаптер не повторяет close номера fd, который IDF
  уже мог удалить/использовать повторно. Общий `StorageFileStatus::handles`
  также остаётся ненулевым: потеря оболочки Arduino File не доказывает остановку.
- `beginQuiesce()` запрещает новые операции и аннулирует активное поколение.
  `unmountWhenDrained()` отказывает до обнуления in-flight и handles, не ожидая
  завершения клиентов. Само освобождение ресурсов синхронное. Обычный STOP и
  выбор источника сохраняют SD смонтированной. Автоматического remount нет.
- `YORADIO_SD_SSR_TIMEOUT_FALLBACK` в `src/myoptions.h` собирается в
  `storage_sdspi.cpp`. Штатная сборка Owner `4848S040` линкует
  `--wrap=sdmmc_init_sd_ssr`. Сначала выполняется настоящий вызов. `1`
  продолжает наш SDSPI mount после `ESP_ERR_TIMEOUT` этого шага и обнуляет
  поля времени стирания. `0`, любая другая ошибка и вызов вне нашего mount
  сохраняют ошибку IDF. Этот fallback отделён от единственного повтора
  20 → 10 МГц после FatFs 13. Дальнейшие проверки init и настоящий FAT mount
  остаются. `ready` не выставляется вручную. Запись, erase, trim и формат не
  добавляются. Сохранённый таймаут не сообщается как отсутствие или
  повреждение карты.

Успешное runtime-монтирование получает ненулевой `MediaGeneration` — номер
экземпляра монтирования, не UUID карты. Допуск проверяет точное поколение;
при неготовности snapshot возвращает generation/free/total=0.
`reportMediaFault()` немедленно запрещает новые операции; `processFault()`
обрабатывает сигнал в задаче владельца. Устаревший сигнал не повреждает другое
монтирование. Ошибка mount при подтверждённом cleanup оставляет загрузку
Radio/дисплея доступной; ошибка cleanup сохраняет владение. Это поведение
загрузки не гарантирует запуск Radio после ошибки закрытия файла в Audio.

### Полный путь и адаптер Audio

`MediaRef::copy(path, generation, out)` хранит полный абсолютный путь от корня SD:
до 1023 байтов UTF-8 без NUL. Нет trim, нормализации или усечения. Отвергаются
повреждённый UTF-8/управляющие символы, переходы через `..`, повторные/обратные
разделители и FAT-псевдонимы с точкой/пробелом в конце компонента. Общая проверка
байтов находится в `../../utils/utf8.h`; зависимости Storage от Audio helpers нет.

Player владеет `PathPlayRequest::copy(path, generation, player.pathEpoch(), out)`
и `requestPlay(request)`. Очередь из двух элементов владеет байтами; отправка
не ждёт свободного места. `Accepted` означает постановку в очередь, не запуск.
Поколение/epoch проверяются повторно; STOP/reset/явный числовой Radio play
отменяют устаревшие запросы. Player принимает MP3/FLAC без учёта регистра и
подтверждает stop до Radio↔SD или SD↔SD. `PlaybackContext`/режим Config отражают
подтверждённый источник, не приём запроса. Ошибка open после успешного stop
оставляет Stopped/error без автоматического возврата Radio. Различаются отсутствие
карты, stale generation и ошибки open/read/seek. Path SD владеет независимой
очередью папок (`core/folder_queue.*`): EOF и Next используют один
depth-first поиск от корня, привязанный к epoch старта; Prev смотрит историю
(глубина 8) и даёт `NoAdjacentTrack` в её начале, снимая запись только после
подтверждённого перехода. Состояние очереди меняет только Player-owner;
`sendCommand`/`requestPlay` инвалидируют через epoch, а отклонённый path не
отменяет активный поиск. Каталог просмотра WebUI/LVGL очередь не меняет.
Repeat Off/One/Folder/All и Folder Shuffle — RAM-режимы Player; WebUI
показывает подтверждённое состояние только во время SD-воспроизведения.
FileError доступен отдельно.
Допустимый путь файловой системы сам по себе не означает поддержку декодером.

Длинные имена не читаются через POSIX `readdir`. IDF 5.5.5 жёстко задаёт
`FF_LFN_BUF` в 255 байт, а `dirent.d_name` вмещает 256 байт; более длинный
UTF-8 LFN подменялся бы коротким именем. Сборка Owner зеркалирует
собираемые исходники fatfs через `configure_file(... COPYONLY)` в каталог
сборки и заменяет `ffconf.h` на `fatfs_lfn_override/ffconf.h`, поднимая
байтовый буфер до 768 плюс NUL. Изменение обёртки или C-helper обновляет
зеркало без удаления дерева сборки. `vfs_fat_internal.h` копируется в тот
же каталог: он кавычечно включает `esp_vfs_fat.h` и `diskio_impl.h`, а
`#pragma once` считает путь SDK другим заголовком. Установленный SDK не
редактируется.
`listDirectory` вызывает `f_opendir` / `f_readdir` / `f_closedir`, копирует
имя только целиком и закрывает каталог при ошибке пути, имени и чтения.
`FR_DISK_ERR` / `FR_NOT_READY` вызывают `StorageOwner::reportMediaFault` с
исходным FRESULT; отсутствие пути и нехватка памяти — нет. Короткое имя
возвращается только когда оно и есть имя записи; проверка длины не обнаруживает
SFN, уже подставленный FatFs. `listDirectory` — синхронный проход. Directory
Service ниже — его асинхронный потребитель. Команды `s07dir` и `s07act` компилируются только когда Storage Owner включён
и `YORADIO_STORAGE_DIAGNOSTICS` равен ровно 1. По умолчанию флаг 0: в обычной
прошивке этих команд нет, GPIO48 не читается и снимки `[S07-ENG]` не собираются.
`s07dir` выполняет проход на loop task только для корня и только при
остановленном воспроизведении и может отдать возвращённый путь в Player. Это
не API браузера. `s07act` использует тот же compile gate и тот же telnet/serial
parser.
`s07act radio` и `s07act sd` вызывают `Player::activate` и печатают приём.
`s07act status` печатает `activation()`, выбранный источник, звучащий источник
или `None`, Playing/Stopped/StopFailed и `pathEpoch()`. Status не вызывает
`activate`. Обработчик не вызывает `Player::loop` и не сканирует и не открывает карту.

### Directory Service

`directorySubmit`, `directoryPoll` и `directoryRelease` читают один каталог.
WebUI читает через `sdBrowserList` (`GET /sd/dir`). LVGL читает через
`DirectoryConsumer::Lvgl` в `lvgl_ui/adapters/browser_provider.cpp`.
Поиск folder-queue использует `DirectoryConsumer::Playback`. Запрос не меняет
звучащий источник и не трогает очередь Player. Просмотр папок и
`Player::activate` в оверлее источников — разные действия. Один точный путь
запускается через `POST /sd/play`, `requestSdPlayback` и существующий запрос
пути Player. HTTP 202 означает, что путь поставлен в очередь. Это не значит,
что звук уже начался. Общий `POST /` не принимает `s07_sd_path`. Обычная
прошивка печатает `[Storage]` для runtime mount, SSR fallback, повтора
20 → 10 МГц, ошибок файла и каталога и отказа барьера STOP. Успешный STOP
эту строку не печатает; `stop barrier=CONFIRMED` есть только при
`YORADIO_STORAGE_DIAGNOSTICS=1`. Обычные действия выполняются и при
выключенной диагностике. Значение по умолчанию — 0. Подробные строки
`[S07-ENG]`, `s07dir` и `s07act` требуют значения 1.

Первый успешный запрос создаёт одну задачу `StorageDir`, приоритет 1 на ядре 0.
Audio остаётся приоритетом 4 на ядре 1. Эта задача не является JPEG worker.
Статический стек задачи — **12 КиБ во внутреннем DRAM** (глубина ESP-IDF в
**байтах**); снимки каталогов и слоты остаются в PSRAM. В sdkconfig разрешены
внешние стеки (`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`), но StorageDir не
использует стек в PSRAM, пока FatFs/SPI на этом пути не доказаны отдельно.
`Ok` значит, что запрос выполнит worker. `NoMemory` значит, что запрос не
поставлен. Worker создать не удалось, либо его ещё создаёт другой вызывающий.
Этот вызывающий сразу возвращается и не ждёт. Хост и устройство решают
одинаково. Публичный набор статусов не расширялся.

Worker вызывает `listDirectory`, затем ставит каталоги перед MP3/FLAC в
принятом natural order. Прочие файлы не попадают в список. Строку `..` без
значка рисует браузер, если путь не `/`; это не запись каталога.

Сервис не хранит текущую папку. В запросе есть потребитель (`WebUi`, `Lvgl`
или `Playback`), ненулевой идентификатор, поколение mount и путь. `Playback`
зарезервирован для поиска следующего трека folder-queue и не делит слот с
WebUI/LVGL. Более новый запрос того же потребителя заменяет только его слот.
Worker проверяет отмену между вызовами FatFs и закрывает только тот каталог,
который открыл этот проход.

`directoryPoll` удерживает неизменяемый снимок. Вызывающий снимает это
удержание одним `directoryRelease`. До следующего запроса того же потребителя
сервис тоже держит последний опубликованный снимок, поэтому повторный poll
того же запроса может скопировать следующую страницу без обхода карты. Новый
запрос снимает эту ссылку сервиса вне его блокировки. Ссылка, уже выданная
`directoryPoll`, остаётся действительной. Следующий poll — это новый запрос,
а не прежний каталог.

Удержанный снимок не означает, что карта готова. Перед выдачей вызывающий
проверяет готовность `StorageOwner` и поколение mount. Чужое поколение не
отдаётся как текущая страница.

Имена упакованы; полный `DirEntry` на каждую строку не хранится.
`directoryItemPath` записывает собранный путь в буфер вызывающего.

Бюджет 2 МиБ (`kDirectorySnapshotBudget`) считает каждый блок PSRAM,
выделенный одновременно: снимки, которые ещё держат вызывающие, собираемый
каталог и предыдущий блок, пока он копируется. Временная копия не бесплатна.
Рост, при котором обе копии не помещаются, даёт `ResourceLimit`, даже если
готовый каталог один уместился бы. Стек worker — 12 КиБ. Очередь — три слота,
по одному ожидающему запросу на потребителя. Выход за бюджет не публикует
неполный каталог. `ReadFailed`, `StaleMedia` и `NoMemory` остаются отдельными.
Нет кэша многих папок, индекса всей карты и автоматического remount.
Последовательная folder-queue живёт в Player (`core/folder_queue.*`), а не в
Directory Service.

WebUI хранит одну личность: клиент, запрос, путь и поколение. `start=1`
принимает проход. Поздний poll и следующая страница — нет. Две вкладки с
одним и тем же счётчиком запроса не получают чужой снимок. Поздний poll
вытесненного запроса — `cancelled`, без нового прохода. Пока проход ещё
идёт, другой start получает `busy`.

Одна JSON-запись рассчитана на допустимое имя, полный путь и худшее
экранирование в шесть байт на исходный байт. Страница остаётся не больше
40 строк и 12 КиБ. Строка, которая не влезает в страницу, — это предел, а
не ошибка выделения памяти. Имена экранируются как текст JSON. Сетевой
обработчик отправляет скопированную страницу и не ждёт окончания прохода.

Хостовые проверки: `test/s07_6b1b_directory/`, 242 утверждения, код выхода 0.
Они покрывают живые байты allocator, пик копии, снятие ссылки сервиса,
создание worker, которое уже идёт, двух клиентов с одним req и разными
путями, поздний poll после вытеснения, один проход на обе страницы и длинное
имя во вложенной папке. Каталоги на 100/500/2000 файлов и открытие
возвращённого имени длиннее 255 байт UTF-8 на плате не измерены. Полный
Folder Shuffle на 4096 записей и гонки отмены покрыты только host-проверками.

Адаптер открывает обычные файлы только для чтения на mount владельца,
сериализует IO, учитывает один физический дескриптор при нескольких оболочках
Arduino File и отвергает изменение файлов/обход каталогов. Это адаптер Audio,
не Browser API. Установленная Arduino FS использует рекурсивный FreeRTOS mutex:
обработка ошибки open и деструктор могут вызвать close под блокировкой той же
задачи. Host-тесты проверяют эту ветку с рекурсивной моделью mutex.

Audio владеет остановкой File/декодера. Stop Barrier требует подтверждения
закрытия до reset/нового запуска. После временного timeout блокировки
control/decoder явный STOP/toggle допускает повторную попытку; последующий Play
остаётся отдельной командой. После unknown native close повторный STOP не
восстанавливает потерянное подтверждение: запуск Radio и SD, reset декодера и
unmount остаются запрещены до перезапуска. Громкость/mute и диагностика доступны.
Отдельная система восстановления здесь не реализована.

### Гарантии времени

`mount_timeout_ms` и `max_mount_retries` — зарезервированные поля: владелец и
драйвер их не читают. `mount_path` передаётся владельцу; autoformat запрещён.
Есть один ранний вход на 400 кГц, освобождение линий до Type9 и один runtime
mount на 20 МГц. FatFs 13 после чистого освобождения даёт ещё один mount на
10 МГц. Дальнейшего повтора и автоматического remount нет. SSR timeout
fallback — другой путь. SDSPI сохраняет
стандартные timeout отдельных команд SDK (`command_timeout_ms=0`); timeout
блокировки FatFs ограничивает ожидание синхронизации, не весь FS-вызов.
Mutex адаптера Arduino ожидает с `portMAX_DELAY`. Mount/open/read/seek/close,
запрос объёма и cleanup синхронные, без гарантии времени завершения. Ожидания
control/decoder по 100 мс в Audio не ограничивают последующий FS close.
Размер пути/очереди и отправка без ожидания ограничивают ресурсы и допуск.

### Разводка и инженерный probe

| Линия | Кандидатный GPIO | Владелец в штатной конфигурации |
|---|---:|---|
| LCD_CS | 39 | Дисплей; не линия SD |
| TF CMD / DisplayCommand SDA | 47 | Команды дисплея |
| TF CLK / DisplayCommand SCK | 48 | Команды дисплея |
| TF DAT0 | 41 | Нет при выключенной SD; конфликт с IR при включении |
| TF DAT3/CS | 42 | Нет при выключенной SD; конфликт с MUTE при включении |

`schematic_candidate_4848s040.h` хранит линии SCHEMATIC CONFIRMED из
[схем gys2005](https://github.com/gys2005/ESP32-4848S040). GPIO SD не являются
полями BoardProfile. При `YORADIO_SD_ENABLE=1` `storage_validate.h` проверяет
GPIO 41/42 на конфликт с дисплеем, тачем, I2S, IR, MUTE и WAKE. GPIO 47/48
остаются линиями DisplayCommand и совмещаются с TF CMD/CLK только через
раннее освобождение и последующий runtime mount. Проверка не доказывает провод.

Одноразовый SDSPI probe и его окружение PlatformIO удалены после переноса
Owner в `4848S040`. FATFS LFN heap, UTF-8 и `MAX_LFN=255` для этой сборки
берутся из `sdkconfig.defaults.storage_owner`. Зеркальный `ffconf.h` задаёт
`FF_LFN_BUF` 768 и `FF_FS_EXFAT` 1, поэтому сборка Owner включает
FAT12/16/32 и exFAT. Успех одной карты не является общим доказательством
файловой системы. Каталоги 100/500/2000 и имя из списка длиннее 255 байт
UTF-8 на плате не измерены.

Адресные тесты: `test/s07_4a2_storage_owner/`, `test/s07_4a3_single_file/`
(адаптер FS) и `test/s07_4a4a_source_transitions/` (переходы Player). Числовые
station IDs остаются Radio-only; для SD нужен явный путь, одного mode недостаточно.
Управление/seek/EOF выполняет Player без зависимости от Wi-Fi и legacy SD index.
B003–B005 остаются ранним свидетельством mount; это не текущий образ.
WebUI, LVGL и поиск folder-queue — три потребителя Directory Service.
Legacy `USE_SD` выключен. Локальная приёмка этого поведения не является
публикацией.
