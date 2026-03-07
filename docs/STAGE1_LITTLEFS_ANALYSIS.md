# Stage 1 — LittleFS Transition Analysis

> Branch: `lvgl_stage1_littlefs` (from `lvgl_stage0`)
> Status: **ANALYSIS ONLY — no code changes**

---

## 1. Current SPIFFS Usage Map

### 1.1 Direct `#include <SPIFFS.h>`

| File | Purpose |
|------|---------|
| `src/src/core/config.h` | Main include, propagated to all consumers of `config.h` |
| `src/src/core/netserver.cpp` | WebServer file serving, playlist upload, AI prompt upload, OTA |
| `src/src/plugins/ai/ai_prompt.cpp` | AI prompt file loading from `/ai/ai_prompt.txt` |
| `src/src/audioI2S/Audio.h` | Audio library header (includes `<SPIFFS.h>` but uses generic `fs::FS&`) |
| `src/src/audioVS1053/audioVS1053Ex.h` | VS1053 audio variant (same pattern as Audio.h) |

### 1.2 SPIFFS.begin() — Mount point

| File | Line | Code | Notes |
|------|------|------|-------|
| `config.cpp` | 279 | `g_spiffs_ready = SPIFFS.begin(true);` | Single mount point. `true` = format on fail. Called from `Config::init()` on Core 1 during `setup()`. |

### 1.3 SPIFFS global object usage — Full inventory

#### `config.cpp` — Configuration & Playlists

| Line | Operation | Path | R/W | When called |
|------|-----------|------|-----|-------------|
| 70 | `SPIFFS.exists()` | `/ai.json` | R | `aiLoadFromFS()` — startup + WebUI save |
| 78 | `SPIFFS.open()` | `/ai.json` | R | `aiLoadFromFS()` |
| 127 | `SPIFFS.open()` | `/ai.json` | W | `aiSaveToFS()` |
| 200 | `SPIFFS.exists()` | `/www/*` (12 files) | R | `_isFSempty()` — startup check |
| 279 | `SPIFFS.begin(true)` | — | — | `Config::init()` — FS mount |
| 320 | `&SPIFFS` | — | — | Assigned to `_SDplaylistFS` pointer |
| 393 | `&SPIFFS` | — | — | Mode change: `_SDplaylistFS = &SPIFFS` |
| 457–460 | `SPIFFS.exists()` / `SPIFFS.remove()` | `/data/playlistsd.csv`, `/data/indexsd.dat`, `/data/index.dat` | R/W | `spiffsCleanup()` |
| 738 | `SPIFFS.open()` | `/data/playlist.csv` | R | `indexPlaylist()` |
| 744 | `SPIFFS.open()` | `/data/index.dat` | W | `indexPlaylist()` |
| 757–760 | `SPIFFS.exists()` / `SPIFFS.open()` | `/data/index.dat` | R | `initPlaylist()` |
| 953 | `SPIFFS.open()` | `/data/wifi.csv` | W | `saveWifiFromNextion()` |
| 970–972 | `SPIFFS.exists()` / `SPIFFS.remove()` / `SPIFFS.rename()` | `/data/tmpfile.txt` → `/data/wifi.csv` | R/W | `saveWifi()` |
| 978 | `SPIFFS.open()` | `/data/wifi.csv` | R | `initNetwork()` |

#### `netserver.cpp` — Web Server

| Line | Operation | Path | R/W | When called |
|------|-----------|------|-----|-------------|
| 107 | `webserver.serveStatic("/", SPIFFS, "/www/")` | `/www/*` | R | Static web asset serving |
| 138 | `U_SPIFFS` constant | — | — | OTA update target selection |
| 181 | `SPIFFS.open()` | chunked path buffer | R | Chunked HTTP response handler |
| 1013 | `SPIFFS.open()` | `/data/tmpfile.txt` | R | `importPlaylist()` |
| 1022 | `SPIFFS.rename()` | `/data/tmpfile.txt` → `/data/playlist.csv` | W | `importPlaylist()` CSV path |
| 1027 | `SPIFFS.open()` | `/data/playlist.csv` | W | `importPlaylist()` JSON path |
| 1040, 1045 | `SPIFFS.remove()` | `/data/tmpfile.txt` | W | `importPlaylist()` cleanup |
| 1072–1075 | `SPIFFS.exists()` / `SPIFFS.remove()` | playlist/index paths | W | `handleUpload()` — pre-upload cleanup |
| 1077 | `SPIFFS.totalBytes()` / `SPIFFS.usedBytes()` | — | R | Free space check before upload |
| 1078 | `SPIFFS.open()` | `/data/tmpfile.txt` | W | File upload temp file |
| 1117–1403 | Multiple `SPIFFS.open/exists/remove` | AI prompt paths | R/W | AI prompt upload handler (atomic write with backup/rollback) |
| 1403 | `SPIFFS.open()` | `/www/*` or `/data/*` | W | Generic file upload handler |

#### `ai_prompt.cpp` — AI Prompt Loader

| Line | Operation | Path | R/W | When called |
|------|-----------|------|-----|-------------|
| 35 | `SPIFFS.exists()` | `/ai/ai_prompt.txt` | R | `loadPromptFromFS()` |
| 40 | `SPIFFS.open()` | `/ai/ai_prompt.txt` | R | `loadPromptFromFS()` |
| 109 | `SPIFFS.exists()` | `/ai/ai_prompt.txt` | R | `aiPromptIsAvailable()` |
| 114 | `SPIFFS.open()` | `/ai/ai_prompt.txt` | R | `aiPromptIsAvailable()` |
| 137 | `SPIFFS.exists()` | `/ai/ai_prompt.txt` | R | `aiPromptGetSize()` |
| 141 | `SPIFFS.open()` | `/ai/ai_prompt.txt` | R | `aiPromptGetSize()` |

#### `telnet.cpp` — Telnet CLI

| Line | Operation | Path | R/W | When called |
|------|-----------|------|-----|-------------|
| 257 | `SPIFFS.open()` | `/data/playlist.csv` | R | CLI `list` command |
| 411 | `SPIFFS.open()` | `/data/wifi.csv` | R | CLI `wifi.con` command |
| 427 | `SPIFFS.open()` | `/data/wifi.csv` | R | CLI `wifi.station` command |

### 1.4 Indirect FS usage via `SDPLFS()` pointer

`Config::SDPLFS()` returns `FS*` which is `&SPIFFS` in web mode
or `&sdman` (SD card) in SD mode. These calls use the generic `fs::FS` interface:

| File | Lines | Paths accessed |
|------|-------|----------------|
| `config.cpp` | 780–781, 799–800, 819–820 | `REAL_PLAYL`, `REAL_INDEX` (generic FS pointer) |
| `config.cpp` | 424, 440–441 | `INDEX_SD_PATH` via `SDPLFS()` |
| `netserver.cpp` | 179 | `chunkedPathBuffer` via `config.SDPLFS()` |

### 1.5 AsyncWebServer internal FS usage

`src/src/AsyncWebServer/` uses generic `fs::FS&` references:
- `WebHandlers.cpp` — `_fs.open()` for static file serving
- `SPIFFSEditor.cpp` — filesystem editor (generic FS API)
- `WebResponses.cpp` — `fs.open()` for response file serving

These use the abstract `fs::FS` interface passed at construction time,
not the `SPIFFS` global directly. The only binding is in `netserver.cpp:107`:
`webserver.serveStatic("/", SPIFFS, "/www/")`.

### 1.6 Audio library FS usage

`Audio.h` and `audioVS1053Ex.h` include `<SPIFFS.h>` and `<FS.h>` but use
the generic `fs::FS&` parameter in `connecttoFS()`. The `SPIFFS` global
is never directly called from audio code.

---

## 2. Subsystems using the filesystem

| Subsystem | Files | FS paths | Dependency type |
|-----------|-------|----------|-----------------|
| **Config / Init** | `config.cpp`, `config.h` | `/ai.json`, `/data/*` | Direct `SPIFFS.*` calls |
| **WebUI** | `netserver.cpp` | `/www/*`, `/data/*`, `/ai/*` | Direct `SPIFFS.*` + `serveStatic(SPIFFS)` |
| **Playlists** | `config.cpp`, `netserver.cpp` | `/data/playlist.csv`, `/data/index.dat` | Direct `SPIFFS.*` |
| **WiFi config** | `config.cpp`, `telnet.cpp` | `/data/wifi.csv`, `/data/tmpfile.txt` | Direct `SPIFFS.*` |
| **AI Layer** | `ai_prompt.cpp`, `config.cpp`, `netserver.cpp` | `/ai/ai_prompt.txt`, `/ai.json` | Direct `SPIFFS.*` |
| **OTA Update** | `netserver.cpp` | — | `U_SPIFFS` constant for partition target |
| **Telnet CLI** | `telnet.cpp` | `/data/playlist.csv`, `/data/wifi.csv` | Direct `SPIFFS.*` |
| **Static serving** | `AsyncWebServer/*` | `/www/*` | Generic `fs::FS&` (bound to `SPIFFS`) |
| **Audio** | `Audio.h`, `audioVS1053Ex.h` | — | Header include only; uses generic `fs::FS&` |
| **SD card** | `sdmanager.cpp`, `config.cpp` | `/data/playlistsd.csv`, `/data/indexsd.dat` | Uses `sdman` (SDFS), but cleanup uses `SPIFFS.*` |

---

## 3. Data categories

### 3.1 Configuration

| Path | Format | Size | Access pattern |
|------|--------|------|----------------|
| `/ai.json` | JSON | ~200 B | Read at boot, write on WebUI save |
| `/data/wifi.csv` | CSV (ssid\tpassword) | ~200 B | Read at boot, write on WiFi config save |

### 3.2 Web assets (static)

| Path | Format | Size | Access pattern |
|------|--------|------|----------------|
| `/www/index.html` | HTML | ~5 KB | Served via HTTP GET |
| `/www/settings.html` | HTML | ~8 KB | Served via HTTP GET |
| `/www/update.html` | HTML | ~2 KB | Served via HTTP GET |
| `/www/ir.html` | HTML | ~3 KB | Served via HTTP GET |
| `/www/script.js.gz` | Gzip JS | ~15 KB | Served via HTTP GET |
| `/www/style.css.gz` | Gzip CSS | ~3 KB | Served via HTTP GET |
| `/www/settings.css.gz` | Gzip CSS | ~2 KB | Served via HTTP GET |
| `/www/ir.css.gz` | Gzip CSS | ~1 KB | Served via HTTP GET |
| `/www/ir.js.gz` | Gzip JS | ~2 KB | Served via HTTP GET |
| `/www/dragpl.js.gz` | Gzip JS | ~1 KB | Served via HTTP GET |
| `/www/elogo.png` | PNG | ~5 KB | Served via HTTP GET |
| `/www/elogo84.png` | PNG | ~2 KB | Served via HTTP GET |

Total web assets: ~50 KB (12 files). Read-only at runtime.

### 3.3 Radio stations

| Path | Format | Size | Access pattern |
|------|--------|------|----------------|
| `/data/playlist.csv` | CSV (name\turl\tovol) | Variable | Read/write, indexed access |
| `/data/index.dat` | Binary (uint32 offsets) | Variable | Read/write, rebuilt from playlist |
| `/data/playlistsd.csv` | CSV | Variable | SD card playlist cached to SPIFFS |
| `/data/indexsd.dat` | Binary | Variable | SD card index cached to SPIFFS |
| `/data/tmpfile.txt` | Temp | Variable | Upload staging, renamed atomically |

### 3.4 AI prompt files

| Path | Format | Size | Access pattern |
|------|--------|------|----------------|
| `/ai/ai_prompt.txt` | Plain text | ≤8 KB | Read at first AI request, cached in RAM |

### 3.5 Temp / staging

| Path | Format | Size | Access pattern |
|------|--------|------|----------------|
| `/data/tmpfile.txt` | Various | Variable | Upload staging (playlist, WiFi config) |
| `/ai/ai_prompt.txt.tmp` | Plain text | ≤8 KB | Atomic AI prompt upload |
| `/ai/ai_prompt.txt.bak` | Plain text | ≤8 KB | Backup during AI prompt upload |

---

## 4. Limitations and risks of current implementation

### 4.1 Hard dependency on `SPIFFS` global object

**Problem**: ~80+ direct calls to `SPIFFS.open()`, `SPIFFS.exists()`,
`SPIFFS.remove()`, `SPIFFS.rename()`, `SPIFFS.totalBytes()`,
`SPIFFS.usedBytes()` across 5 source files.

**Impact**: Switching to LittleFS requires changing every call site,
or creating an abstraction layer.

### 4.2 Hardcoded `U_SPIFFS` OTA constant

**Problem**: `netserver.cpp:138` uses `U_SPIFFS` for OTA filesystem
update target detection. The HTML form (`update.html`) also displays
"SPIFFS" as user-facing text.

**Impact**: LittleFS OTA uses the same partition type (`data/spiffs`),
so `U_SPIFFS` constant still works. But `update.html` UI text should
be updated for clarity.

### 4.3 Partition table labels

**Problem**: Both `partitions.csv` and `minimal_spiffs.csv` define
the filesystem partition with SubType `spiffs`:

```
spiffs, data, spiffs, 0x670000, 0x330000,
```

**Impact**: LittleFS on ESP32 Arduino 3.x uses the same partition
SubType (`spiffs` = 0x82). The partition label is cosmetic. No
partition table change is strictly required, but renaming to
`littlefs` improves clarity.

### 4.4 `board_build.filesystem = spiffs` in platformio.ini

**Problem**: PlatformIO uses this setting for:
1. Selecting the mkspiffs/mklittlefs tool for `pio run -t uploadfs`
2. Determining the filesystem image format

**Impact**: Must change to `board_build.filesystem = littlefs`.
Also need to switch `platform_packages` from `tool-mkspiffs` to
`tool-mklittlefs`.

### 4.5 `SPIFFS.begin(true)` format-on-fail behavior

**Problem**: `config.cpp:279` calls `SPIFFS.begin(true)` which
auto-formats on mount failure. This is a migration safety net
for first-boot.

**Impact**: `LittleFS.begin(true)` has the same behavior —
auto-format on mount failure. No code logic change needed, but
first boot after migration will trigger a format (losing existing
SPIFFS data).

### 4.6 SPIFFS filename limitations

**Problem**: SPIFFS has a flat namespace (no real directories).
Paths like `/data/playlist.csv` and `/www/index.html` are stored
as flat filenames. LittleFS has real directory support.

**Impact**: LittleFS requires parent directories to exist before
creating files. Currently the code does not call `mkdir()`. Need
to ensure `/data/`, `/www/`, and `/ai/` directories are created
at first format, or use `LittleFS.begin(true)` + directory creation
at startup.

### 4.7 `SPIFFS.rename()` cross-directory behavior

**Problem**: `config.cpp:972` and `netserver.cpp:1022` use
`SPIFFS.rename()` for atomic file replacement. In SPIFFS,
rename is a metadata operation (fast).

**Impact**: LittleFS `rename()` works identically for files
within the same directory. Cross-directory rename works in
LittleFS (unlike some other filesystems). No issue expected.

### 4.8 Space calculation formula

**Problem**: `netserver.cpp:1077` uses a magic formula:
`freeSpace = (float)SPIFFS.totalBytes()/100*68 - SPIFFS.usedBytes()`

The `68%` factor is a SPIFFS-specific overhead compensation
(SPIFFS uses ~32% of space for metadata/wear-leveling).

**Impact**: LittleFS has different overhead characteristics (~10–15%).
The formula must be recalculated for LittleFS, or simplified to
`totalBytes() - usedBytes()` with a safety margin.

### 4.9 `tool-mkspiffs` dependency

**Problem**: `platformio.ini` includes:
`platform_packages = platformio/tool-mkspiffs@^2.230.0`

**Impact**: Must be changed to `platformio/tool-mklittlefs` or
removed if using PlatformIO's built-in LittleFS support.

---

## 5. Proposed migration strategy

### 5.1 Approach: Global `SPIFFS` → `LittleFS` object replacement

The simplest and least invasive approach is a global search-and-replace
of the `SPIFFS` object with `LittleFS`, combined with build system changes.

**Why this works**: Both `SPIFFS` and `LittleFS` implement the `fs::FS`
interface. The API is 100% compatible at the call-site level:
- `open()`, `exists()`, `remove()`, `rename()` — identical signatures
- `totalBytes()`, `usedBytes()` — identical signatures
- `begin(true)` — identical behavior (mount + format-on-fail)

### 5.2 Migration steps (ordered)

#### Step 1: Build system changes (no runtime impact)

1. `platformio.ini`:
   - Change `board_build.filesystem = spiffs` → `littlefs`
   - Change `platform_packages` from `tool-mkspiffs` → `tool-mklittlefs`
     (or remove if built-in)
2. `partitions.csv` / `minimal_spiffs.csv`:
   - Optional: rename partition label `spiffs` → `littlefs` (cosmetic)
   - SubType remains `spiffs` (0x82) — this is the ESP-IDF constant
     for "generic data filesystem" and is shared by both SPIFFS and LittleFS

#### Step 2: Header replacement

1. Replace `#include <SPIFFS.h>` with `#include <LittleFS.h>` in:
   - `src/src/core/config.h`
   - `src/src/core/netserver.cpp`
   - `src/src/plugins/ai/ai_prompt.cpp`
   - `src/src/audioI2S/Audio.h`
   - `src/src/audioVS1053/audioVS1053Ex.h`

#### Step 3: Global object replacement

Replace all `SPIFFS.` calls with `LittleFS.` in:
- `src/src/core/config.cpp` (~20 call sites)
- `src/src/core/netserver.cpp` (~40 call sites)
- `src/src/plugins/ai/ai_prompt.cpp` (~6 call sites)
- `src/src/core/telnet.cpp` (~3 call sites)

Also replace `&SPIFFS` pointer assignments with `&LittleFS`:
- `config.cpp:320` — `_SDplaylistFS = &LittleFS`
- `config.cpp:393` — same

#### Step 4: OTA constant

Check if `U_SPIFFS` needs to change. On ESP32 Arduino 3.x:
- `U_SPIFFS` = `U_PART` = 100 (partition-type based update)
- LittleFS uses the same partition type, so `U_SPIFFS` still works
- Consider adding `#define U_FS U_SPIFFS` alias for clarity

#### Step 5: Directory creation at startup

Add directory creation after `LittleFS.begin()` in `Config::init()`:

```cpp
LittleFS.mkdir("/data");
LittleFS.mkdir("/www");
LittleFS.mkdir("/ai");
```

LittleFS requires real directories. These are idempotent (no error
if already exists).

#### Step 6: Free space formula

Update `netserver.cpp:1077` space calculation:

```cpp
// SPIFFS (old): freeSpace = SPIFFS.totalBytes()/100*68 - SPIFFS.usedBytes()
// LittleFS: much lower overhead, use direct calculation with safety margin
freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes() - 4096;  // 4KB safety margin
```

Apply same fix to AI prompt upload space check (~line 1138).

#### Step 7: UI text updates

Update `data/www/update.html`:
- Change "SPIFFS" label to "Filesystem" or "LittleFS"
- Change form value `"spiffs"` if needed (or keep for backward
  compatibility with existing update scripts)

#### Step 8: Comments and documentation

- Update comments referencing SPIFFS
- Update `STAGE0_STEP1_ANALYSIS.md` partition table description
- Update README files mentioning SPIFFS flashing

### 5.3 Data migration considerations

**First boot after firmware update**:

When a device running SPIFFS firmware is updated to LittleFS firmware:

1. `LittleFS.begin(true)` will fail to mount (incompatible format)
2. `true` parameter triggers auto-format → clean LittleFS filesystem
3. All user data (playlists, WiFi config, AI prompts) is lost

**Mitigation options**:

- **Option A (recommended)**: Document that filesystem update requires
  re-uploading `data/` via `pio run -t uploadfs`. This is already the
  standard process for initial setup.

- **Option B**: Add one-time migration code that detects SPIFFS format,
  reads critical files (wifi.csv, playlist.csv, ai.json), formats as
  LittleFS, and writes them back. Complex and fragile — not recommended
  for initial migration.

- **Option C**: Provide a WebUI "backup/restore" feature before migration.
  Good UX but requires implementation effort outside Stage 1 scope.

### 5.4 Testing strategy

1. **Build verification**: `pio run` for all 4 environments
2. **Upload filesystem**: `pio run -t uploadfs -e 4848S040`
3. **Functional tests**:
   - WebUI loads (`/www/*` served correctly)
   - WiFi config persistence (save/reboot/verify)
   - Playlist upload and playback
   - AI prompt upload and loading
   - OTA firmware update
   - OTA filesystem update
   - Telnet CLI `list` and `wifi.con` commands
   - SD card mode switching
   - `spiffsCleanup()` equivalent function
4. **Edge cases**:
   - First boot (format-on-fail)
   - Empty filesystem detection (`_isFSempty()`)
   - Free space calculation accuracy
   - Large playlist handling
   - Concurrent WebSocket + file operations

### 5.5 Risk assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| User data loss on update | Medium | Document re-upload requirement |
| Space calculation inaccuracy | Low | Test with real data, adjust formula |
| Missing directories on first boot | Low | Add `mkdir()` calls at startup |
| `U_SPIFFS` constant changes in future Arduino | Low | Monitor Arduino Core releases |
| SD+SPIFFS cleanup function naming | Cosmetic | Rename `spiffsCleanup` → `fsCleanup` |
| LittleFS slightly lower throughput than SPIFFS for reads | Low | Not significant for this use case (small files, infrequent access) |
| LittleFS slightly higher RAM usage | Low | ~1 KB more than SPIFFS, negligible with 8 MB PSRAM |

### 5.6 Files to modify (complete list)

| File | Change type |
|------|-------------|
| `platformio.ini` | Build system: filesystem type, tool package |
| `partitions.csv` | Optional: rename label |
| `minimal_spiffs.csv` | Optional: rename label and file |
| `src/src/core/config.h` | Header: `SPIFFS.h` → `LittleFS.h` |
| `src/src/core/config.cpp` | ~20 SPIFFS → LittleFS replacements + mkdir + formula |
| `src/src/core/netserver.cpp` | ~40 SPIFFS → LittleFS replacements + formula |
| `src/src/core/telnet.cpp` | ~3 SPIFFS → LittleFS replacements |
| `src/src/plugins/ai/ai_prompt.cpp` | ~6 SPIFFS → LittleFS replacements |
| `src/src/audioI2S/Audio.h` | Header: `SPIFFS.h` → `LittleFS.h` |
| `src/src/audioVS1053/audioVS1053Ex.h` | Header: `SPIFFS.h` → `LittleFS.h` |
| `data/www/update.html` | UI text: "SPIFFS" → "LittleFS" |

### 5.7 Files NOT modified

- `src/src/core/display.cpp` — no FS usage
- `src/src/displays/*` — no FS usage
- `src/src/main.cpp` — no direct FS usage
- `src/src/lvgl_ui/*` — no FS usage (Stage 0 stubs)
- `src/src/AsyncWebServer/*` — uses generic `fs::FS&`, no SPIFFS direct calls
- `src/src/audioI2S/Audio.cpp` — uses generic `fs::FS&` parameter
- `src/myoptions*.h` — no FS references
- `src/mytheme.h` — no FS references

---

*Analysis complete. No code changes made. Ready for design review
before implementation begins.*
