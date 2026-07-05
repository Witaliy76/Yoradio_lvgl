# Wi-Fi Flow ? LVGL object tree (`scr_wifi_flow`)

**Purpose / ??????????:**
**English:** Layout, state and transition ownership for `LvglWifiFlowScreen` in `scr_wifi_flow.cpp`. Documents five panel surfaces, object hierarchy, dynamic list children, shared style lifecycle, font resources, UI string inventory, partial-allocation, panel transition matrix, password/saved state contracts, recovery idle, backend-operation boundary, and polling dispatcher (WIFIREF-D1).
**???????:** ?????????, state ? ???????? `LvglWifiFlowScreen` ?? `scr_wifi_flow.cpp`: ???? ???????, ????????, dynamic lists, lifecycle ??????, ??????, UI ??????, partial-allocation, transition matrix, state Password/Saved, recovery idle, ??????? ? backend. ??????? guards polling ? ? WIFIREF-D.

**Source of truth:**
`scr_wifi_flow.cpp`:
- `LvglWifiFlowScreen::create()` ? orchestration skeleton
- Private static builders: `create_panel_roots`, `create_home_panel`, `create_networks_panel`, `create_password_panel`, `create_saved_panel`, `create_hotspot_panel`
- `wifi_flow_style_ensure()` / `wifi_flow_style_drop()` ? shared style lifecycle
- `rebuild_saved_list()` / `rebuild_scan_list()` ? dynamic list children

**Maintenance / ?????????:**
After any of the following, refresh the relevant tree sections:
- hierarchy or parent-child relations change;
- panel creation order changes;
- dynamic list user-data encoding changes;
- shared style object set changes;
- font selection logic changes;
- UI string inventory changes;
- button roles or callback registrations change.

---

## Screen type and service-palette policy

`LvglWifiFlowScreen` is a `ScreenType::RebootRequired` service screen ? not a carousel page.

The screen uses `yoradio_palette_service()` (fixed factory Dark) ? not the user Custom or Light preset. `liveReapplyTheme()` is **not implemented** on this screen. The Dark palette is intentional: the recovery context requires a stable, readable background independent of user preferences.

---

## Static object tree

```
_screen  (flex COLUMN; 100%?100%; device_background; not scrollable)
?
??? _panel_home    (flex COLUMN; flex_grow=1; style_service_panel; VISIBLE)
??? _panel_net     (flex COLUMN; flex_grow=1; style_service_panel; HIDDEN)
??? _panel_pass    (flex COLUMN; flex_grow=1; style_service_panel; HIDDEN)
??? _panel_saved   (flex COLUMN; flex_grow=1; style_service_panel; HIDDEN)
??? _panel_hotspot (flex COLUMN; flex_grow=1; style_service_panel; HIDDEN)
```

Only one panel is visible at a time. Visibility is managed by `show_*` methods (see ?Panel-transition ownership).

---

## Home panel

```
_panel_home
??? [hdr_home_row]  local flex ROW with icon  |  OR  _hdr_home directly on panel (fallback)
?   ??? [ico_home]  Wi-Fi glyph; header icon font; text_secondary
?   ??? _hdr_home   "Wi-Fi Recovery"; title font; text_primary
??? _sub_home       subtitle; status font; LONG_WRAP; 100%W; text_secondary
?                   (initial text overwritten by sync_home_boot_failure_ui() on enter)
??? _lbl_recovery_idle_countdown   idle notice; status font; LONG_CLIP; 100%W; text_meta
?                                  (written only on idle arm ? no periodic updates)
??? _list_saved     lv_list; flex_grow=1; 100%W; transparent; scrollbar OFF
?   ??? [dynamic saved rows ? see ?Dynamic Saved rows]
??? [rowh]          local transparent flex ROW; gap=10
    ??? _btn_scan      "Scan"    (Primary)
    ??? _btn_hotspot   "Hotspot" (Secondary)
    ??? _btn_back_home "Back"    (Ghost; hidden if boot-failure entry)
```

---

## Networks panel

```
_panel_net
??? [hdr_net_row]  local flex ROW with icon  |  OR  _hdr_net directly on panel (fallback)
?   ??? [ico_net]  Wi-Fi glyph; header icon font; text_secondary
?   ??? _hdr_net   "Available networks"; title font; text_primary
??? _lbl_net_status  status label; status font; LONG_WRAP; 100%W; initial " "; text_meta
??? _list_scan       lv_list; flex_grow=1; 100%W; transparent
?   ??? [dynamic scan rows ? see ?Dynamic Scan rows]
??? [rown]           local transparent flex ROW; gap=10
    ??? _btn_rescan       "Rescan"  (Primary)
    ??? _btn_cancel_scan  "Cancel"  (Secondary)
    ??? _btn_back_net     "Home"    (Ghost)
```

---

## Password panel

```
_panel_pass  (HIDDEN until open_password_entry())
??? _hdr_pass       "Wi-Fi Setup"; title font; text_primary
??? _lbl_pass_ssid  selected SSID; body font; LONG_DOT; 100%W; initial " "; text_primary
??? _lbl_pass_hint  "Enter the password?"; status font; LONG_WRAP; 100%W; text_secondary
??? _ta_password    lv_textarea; 100%W; one-line; password-mode; body font; text_primary
?                   (keyboard attached only in open_password_entry())
??? [rowp]          local transparent flex ROW; gap=10
?   ??? _btn_connect   "Connect" (Primary; DISABLED initially)
?   ?   ??? [local label] centered inside button
?   ??? _btn_back_pass "Back"   (Secondary)
??? _lbl_pass_status  status label; status font; LONG_WRAP; 100%W; initial " "; text_meta
??? _kbd              lv_keyboard; flex_grow=1; 100%W; TEXT_LOWER mode; not attached on create
```

---

## Saved Network panel

```
_panel_saved  (HIDDEN until open_saved_network())
??? _lbl_saved_ssid    selected SSID; title font; LONG_DOT; 100%W; initial " "; text_primary
??? _lbl_saved_sub     subtitle; status font; LONG_WRAP; 100%W; text_secondary
??? _lbl_saved_status  status label; status font; LONG_WRAP; 100%W; initial " "; text_meta
??? _row_saved_normal  transparent flex ROW; gap=8; VISIBLE
?   ??? _btn_saved_connect "Connect"  (Primary)
?   ??? _btn_saved_chpwd   "Chg pwd"  (Secondary)
?   ??? _btn_saved_remove  "Remove"   (Destructive)
?   ??? _btn_saved_back    "Back"     (Ghost)
??? _row_saved_confirm  transparent flex ROW; gap=8; HIDDEN until Remove tapped
    ??? _btn_saved_yes  "Yes"  (Destructive)
    ??? _btn_saved_no   "No"   (Secondary)
```

---

## Hotspot panel

```
_panel_hotspot  (HIDDEN until show_hotspot_panel())
??? _hdr_hotspot       "Open Hotspot Mode"; title font; text_primary
??? _lbl_hotspot_ssid  body font; 100%W; text_primary
??? _lbl_hotspot_pwd   body font; 100%W; text_primary
??? _lbl_hotspot_ip    body font; 100%W; text_primary
??? _lbl_hotspot_help  status font; LONG_WRAP; 100%W; text_secondary
??? [row_ap]           local transparent flex ROW; gap=8
    ??? _btn_hotspot_back  "Back to Recovery"  (Secondary)
```

The Hotspot panel does **not** own AP backend: AP is started by `show_hotspot_panel()` via `network.recoveryEnsureSoftAP()` and stopped by `on_btn_back_hotspot` via `network.recoveryStopSoftAP()`.

---

## Dynamic Saved rows

Created by `rebuild_saved_list()`, called on `enter()` and from `create()`. Up to 5 rows (WIFI_CRED_STORE_CAPACITY).

```
_list_saved
??? [empty row "No saved networks"]   lv_list_add_btn; wifi_apply_list_row_empty
??? OR
    ??? [slot 0 row "1  SSID"]        lv_list_add_btn; wifi_apply_list_row_normal; user_data=1; on_saved_row_click
    ??? [slot 1 row "2  SSID"]        user_data=2
    ...
    ??? [slot 4 row "5  SSID"]        user_data=5
```

User-data encoding: `reinterpret_cast<void*>(static_cast<uintptr_t>(slot_index + 1))` ? always subtract 1 before use.

---

## Dynamic Scan rows

Created by `rebuild_scan_list()`, called on scan completion in `pollOpsSnapshot()`.

```
_list_scan
??? [empty row "No networks found"]   lv_list_add_btn; wifi_apply_list_row_empty
??? OR
    ??? [row 0 "SSID  ?  dBm  ?  Open/Lock"]  lv_list_add_btn; user_data=1; on_scan_row_click
    ??? [row 1 ?]                               user_data=2
    ...
```

User-data encoding: same 1-based index pattern as saved rows.

---

## Creation order

| # | Object | Builder/method | Notes |
|---|--------|----------------|-------|
| 1 | `_screen` | `create()` | All panels are children of `_screen` |
| 2 | `_panel_home` | `create_panel_roots()` | Allocation of all 5 is guarded |
| 3 | `_panel_net` | `create_panel_roots()` | |
| 4 | `_panel_pass` | `create_panel_roots()` | |
| 5 | `_panel_saved` | `create_panel_roots()` | |
| 6 | `_panel_hotspot` | `create_panel_roots()` | |
| 7?11 | Home children | `create_home_panel()` | icon, hdr, sub, idle, list, rowh, 3 btns |
| 12?21 | Networks children | `create_networks_panel()` | icon, hdr, status, list, rown, 3 btns |
| 22?32 | Password children | `create_password_panel()` | hdr, ssid, hint, ta, rowp, connect btn+label, back, status, kbd |
| 33?47 | Saved children | `create_saved_panel()` | ssid, sub, status, normal row + 4 btns, confirm row + 2 btns |
| 48?56 | Hotspot children | `create_hotspot_panel()` | hdr, 4 labels, row_ap, back btn |

**Shared style initialization** (`wifi_flow_style_ensure`) happens after `create_panel_roots` and before panel children are created ? panels must exist for style references to be valid.

**Static direct creation call sites:** 39 (excluding dynamic `lv_list_add_btn`).
**Header-row helper calls:** 2 (Home, Networks).
**Footer-button helper calls:** 14.
**Successful static runtime objects:** 67 (includes fallback title paths counting as one object each).
**Dynamic list children:** variable; excluded from static count.

---

## Partial-allocation behavior

| Failure | create() behavior |
|---------|------------------|
| `_screen` allocation | early return; empty screen |
| Any of 5 panel roots | `create_panel_roots` returns `false`; create() returns with `_screen` and partial panel handles |
| Header-row failure | title label attached directly to panel (fallback path) |
| Individual child failure | builder continues; panel remains partially functional |
| Action-row failure | panel without bottom buttons |
| `_ta_password` failure | password panel unusable |
| `_kbd` failure | password panel unusable (no input) |

`destroy()` is safe for any combination of null/non-null handles.

---

## Shared style lifecycle

```
create():
    create_panel_roots()
    ? wifi_flow_style_ensure(pal)   ? initialize 14 static lv_style_t objects
    ? create_home_panel() ?         ? styled buttons/list rows reference shared styles

destroy():
    lv_obj_del(_screen)             ? LVGL frees all objects that reference styles
    ? wifi_flow_style_drop()        ? reset shared lv_style_t objects
```

**Load-bearing invariant:** `wifi_flow_style_drop()` must be called **after** `lv_obj_del(_screen)`. Resetting styles while live objects reference them would corrupt rendering.

**14 shared `lv_style_t` objects:**
`s_wf_btn_base`, `s_wf_btn_pri_d/p`, `s_wf_btn_sec_d/p`, `s_wf_btn_gho_d/p`, `s_wf_btn_des_d/p`, `s_wf_btn_dis`, `s_wf_lr_base`, `s_wf_lr_row_d/p`, `s_wf_lr_empty`.

---

## Font resources

| Role | Narrow (?320) | Wide (>320) |
|------|--------------|-------------|
| Title | Montserrat 18 Cyr | Montserrat 20 Cyr |
| Body | `profile.font_normal` (fallback M16) | `profile.font_normal` (fallback M16) |
| Status / help | Montserrat 14 Cyr | Montserrat 16 Cyr |
| List row | body font | Montserrat 18 Cyr |
| Header icon | Wi-Fi Icons 24 | Wi-Fi Icons 36 |

Keyboard (`_kbd`) font is **never replaced** ? it must retain the LVGL built-in symbol glyphs for special keys.

---

## Static UI strings

| Constant | Value | Used in |
|----------|-------|---------|
| `kStrHomeTitle` | `"Wi-Fi Recovery"` | Home header |
| `kStrHomeSubtitleDefault` | `"Tap a saved network, or Scan to choose another."` | Home subtitle (overwritten at enter) |
| `kStrNetworksTitle` | `"Available networks"` | Networks header |
| `kStrPasswordTitle` | `"Wi-Fi Setup"` | Password header |
| `kStrPasswordHint` | `"Enter the password for the selected network."` | Password hint |
| `kStrSavedSubtitle` | `"Saved network. Use saved password or change it."` | Saved subtitle |
| `kStrHotspotTitle` | `"Open Hotspot Mode"` | Hotspot header |
| `kStrButtonScan` | `"Scan"` | Home action row |
| `kStrButtonHotspot` | `"Hotspot"` | Home action row |
| `kStrButtonBack` | `"Back"` | Home, Password action rows |
| `kStrButtonRescan` | `"Rescan"` | Networks action row |
| `kStrButtonCancel` | `"Cancel"` | Networks action row |
| `kStrButtonHome` | `"Home"` | Networks action row |
| `kStrButtonConnect` | `"Connect"` | Password action row |
| `kStrButtonChangePassword` | `"Chg pwd"` | Saved normal row |
| `kStrButtonRemove` | `"Remove"` | Saved normal row |
| `kStrButtonYes` | `"Yes"` | Saved confirm row |
| `kStrButtonNo` | `"No"` | Saved confirm row |
| `kStrButtonBackToRecovery` | `"Back to Recovery"` | Hotspot action row |
| `kStrEmpty` | `""` | Idle countdown reset |
| `kStrStatusPlaceholder` | `" "` | Status label initial value |

`kStrEmpty` and `kStrStatusPlaceholder` are intentionally distinct: `""` resets a label; `" "` occupies a visible slot without visible content.

Runtime messages from operation pipelines (connect results, scan status) are centralized in the `// Operation UI strings` section. Persistence status strings are centralized in the `// Credential persistence UI strings` section (WIFIREF-C2).

---

## Initial visibility

After `create()` completes:
- `_panel_home` ? VISIBLE
- `_panel_net`, `_panel_pass`, `_panel_saved`, `_panel_hotspot` ? HIDDEN

`show_home_panel()` is called at the end of `create()` to establish initial state. `rebuild_saved_list()` populates the Home saved list.

---

## Panel-transition ownership

Panel visibility is managed exclusively by:

```
show_home_panel()      ? Home visible; all others hidden
show_networks_panel()  ? Networks visible; all others hidden
show_password_panel()  ? Password visible; all others hidden
show_saved_panel()     ? Saved visible; all others hidden
show_hotspot_panel()   ? Hotspot visible; all others hidden
```

Each `show_*` method also calls the appropriate state-reset methods and disarms idle timers. Panel builders do not manage visibility transitions ? they only set the initial `HIDDEN` flag where needed.

---

## Backend-operation boundary

The layout builders (`create_*` methods) do **not** invoke any of:

- Backend operations (`wifiOpsRequestScan`, `wifiOpsRequestConnectWithPassword`, `wifiOpsCancel`)
- Credential store reads or writes
- Network API calls (`recoveryEnsureSoftAP`, `recoveryStopSoftAP`)
- Timer creation (poll and reboot timers are created in `enter()` and persistence handlers)

All backend interaction happens in `enter()`, `exit()`, operation start/finish methods, and `pollOpsSnapshot()`.

---

## Password and keyboard ownership

- `_ta_password` stores the typed password transiently in LVGL's internal buffer.
- `_passwordScratch[40]` mirrors the content for validation without re-reading LVGL.
- `_connectCandidatePass[40]` holds the password after typing for post-success persistence.
- The keyboard `_kbd` is detached (`lv_keyboard_set_textarea(_kbd, nullptr)`) at create time and is bound to `_ta_password` in `open_password_entry()`.
- All secret buffers are explicitly zeroed in `clear_password_secrets()` on Back, exit, and destroy.
- The keyboard must be detached before the textarea is destroyed or cleared ? no refactor may shorten or reorder this cleanup.

---

## Timer ownership

| Timer | Created | Period | Repeat | Deleted |
|-------|---------|-------:|--------|---------|
| `_poll_timer` | `enter()` | `kPollIntervalMs` (200 ms) | infinite | `exit()` |
| `_reboot_timer` | persistence handlers | `kRebootDelayMs` (1800 ms) | 1 | `exit()` |

Both timers are guarded with `if (!_timer)` before creation to prevent duplicates. `exit()` deletes and nulls both. The reboot timer fires `ESP.restart()` ? it must not execute after `exit()`.

---

## Destroy order

```
destroy():
    exit()                  ? clear secrets, delete timers, cancel ops
    clear_password_secrets() ? explicit zeroing (belt-and-suspenders)
    lv_obj_del(_screen)     ? LVGL frees all child objects
    wifi_flow_style_drop()  ? reset 14 shared lv_style_t (AFTER object deletion)
    null all handles
```

---

## Shared-helper boundary

Wi-Fi Flow button and list-row styles are **not** shared with `wgt_footer_pill`. The Wi-Fi compact multi-button footer bar (`add_footer_button`) uses `lv_btn_create` with flex-grow and `WifiBtnRole`-based shared styles ? it is architecturally distinct from the full-width pill action surface (Weather/Station/Preset).

No Wi-Fi Flow helpers are exported to `widgets/` in WIFIREF-A. Wi-Fi-specific helpers remain local because there is no proven cross-screen user with an identical lifecycle and visual contract.

---

## Scan operation pipeline

```
start_scan_from_user()
    ? guard: no open connect, no saving
    ? check if already scanning ? show status, return
    ? wifiOpsRequestScan()
        failure ? show error, return
    ? _await_scan_ui = true
    ? show_networks_panel()
    ? show "Starting scan..."

pollOpsSnapshot() handles scan completion:
    ? rebuild_scan_list()
    ? show result status (kStrScanComplete / Cancelled / Timeout / Busy / Finished)
```

## Dynamic Scan rows

`rebuild_scan_list()` rebuilds from current WifiOps snapshot.

```
_list_scan
??? [empty row: kStrNoNetworksFound]   if scanCount == 0
??? [result rows: "SSID  ?  dBm  ?  Open/Lock"]
    user_data = result_index + 1  (1-based; on_scan_row_click subtracts 1)
    LV_EVENT_CLICKED ? on_scan_row_click ? this
```

## Dynamic Saved rows

`rebuild_saved_list()` rebuilds from credential store.

```
_list_saved
??? [empty row: kStrNoSavedNetworks]   if no entries
??? [saved rows: "N  SSID"]
    user_data = slot + 1  (1-based; on_saved_row_click subtracts 1)
    LV_EVENT_CLICKED ? on_saved_row_click ? this
```

**User-data encoding:** `reinterpret_cast<void*>(static_cast<uintptr_t>(index + 1))`. Zero is never used as a valid encoded index.

## Password-protected connect pipeline

```
start_connect_from_user()
    ? guards
    ? _pass_status_terminal = false
    ? validate password length
    ? copy to _connectCandidatePass (before any textarea clear)
    ? on-stack tmp ? wifiOpsRequestConnectWithPassword ? memset(tmp)
    ? _await_connect_ui = true
    ? set_password_panel_connecting_ui(true)

handle_connect_finished()
    ? _await_connect_ui = false
    ? result switch:
        Success/AlreadyConnected ? clear textarea ? handle_successful_connect_persist()
        AuthFailed ? terminal status
        Timeout ? terminal status
        NoNetwork ? terminal status (kWifiOpsNoNetworkUserMsg)
        Cancelled ? clear_password_panel_state() ? show_networks_panel()
        InternalError/Busy ? terminal status
```

## Open-network connect pipeline

Open networks use empty credentials ? this is a SEPARATE flow from password connect.

```
start_open_connect_from_user(ssid)
    ? guards
    ? copy to _selectedOpenSsid
    ? wifiOpsRequestConnectWithPassword(ssid, "", true)
    ? _await_open_connect_ui = true
    ? set_networks_panel_connecting_ui(true)

handle_open_connect_finished()
    ? result switch:
        Success ? handle_open_network_success_persist()
        AuthFailed/Timeout ? timeout message
        NoNetwork ? kWifiOpsNoNetworkUserMsg
        Cancelled ? cancelled message
        InternalError/Busy ? error message
    ? set_networks_panel_connecting_ui(false)

cancel_open_connect_state()
    ? called from show_home/password/saved/hotspot
    ? resets _await_open_connect_ui, _open_status_terminal, _selectedOpenSsid
    ? restores Networks buttons
```

## Saved-network connect pipeline

Successful Saved connect does NOT rewrite `wifi.csv`. It only updates last-success and schedules reboot.

```
start_connect_from_saved()
    ? guards (slot, no in-flight, no save pending)
    ? wifiCredStoreResolvePasswordForSlot ? on-stack tmpPass ? backend ? memset(tmpPass)
    ? _await_saved_connect_ui = true
    ? set_saved_panel_connecting_ui(true)

handle_saved_connect_finished()
    ? result switch:
        Success/AlreadyConnected ? wifiCredStoreSetLastSuccessFromSlot() (no wifi.csv write)
                                  ? schedule reboot
        NoNetwork ? kWifiOpsNoNetworkUserMsg
        Timeout/AuthFailed ? kStrSavedAuthFailed
        Cancelled ? cancelled message
        default ? error message
```

## Result-routing ownership

The four result handlers are intentionally separate:

| Handler | Panel | await flag | Persistence | wifi.csv |
|---------|-------|-----------|-------------|---------|
| `handle_connect_finished` | Password | `_await_connect_ui` | `handle_successful_connect_persist()` | Yes (Cases A?E) |
| `handle_open_connect_finished` | Networks | `_await_open_connect_ui` | `handle_open_network_success_persist()` | Yes (Cases A?E) |
| `handle_saved_connect_finished` | Saved | `_await_saved_connect_ui` | `wifiCredStoreSetLastSuccessFromSlot()` only | No |
| scan completion | Networks | `_await_scan_ui` | none | No |

Do not merge these handlers ? they differ in panel, status label, buffer ownership, Back semantics and persistence behavior.

Operation result handlers decide when persistence starts. Persistence handlers do not interpret Wi-Fi operation results.

## Credential persistence ownership

Persistence is owned by three separate entry points ? never merged:

| Handler | Trigger | SSID source | Password source | Status label |
|---------|---------|-------------|-----------------|--------------|
| `handle_successful_connect_persist()` | Password connect Success | `_selectedSsid` | `_connectCandidatePass` | `_lbl_pass_status` |
| `handle_open_network_success_persist()` | Open connect Success | `_selectedOpenSsid` | empty `""` | `_lbl_net_status` |
| `on_btn_saved_yes()` | Saved Remove confirmation | slot via `_selectedSavedSlot` | none (no password read) | `_lbl_saved_status` (errors only) |

Saved-connect success (`handle_saved_connect_finished`) updates last-success only ? it does not call any of the above handlers.

## Password success decision table

Called from `handle_connect_finished()` on `Success` / `AlreadyConnected` only.

```
handle_successful_connect_persist()
  ? Case E: wifiCredStoreEntryFitsLegacyFile(_selectedSsid, _connectCandidatePass) fails ? terminal status, no reboot
  ? existingIdx = wifiCredStoreFindIndexBySsid(_selectedSsid)
  ? if existingIdx < 0:
       Case D: savedCount >= CAPACITY ? terminal status, no reboot
       Case A: AddOrUpdate ? PersistToFs ? (fail: ReloadFromFs) ? SetLastSuccessFromSlot(newIdx)
  ? else:
       read stored password from existingIdx
       Case B: same password ? SetLastSuccessFromSlot(existingIdx) only (no file write)
       Case C: changed password ? StagingBegin/SetCandidate/Commit ? PersistToFs ? (fail: ReloadFromFs) ? SetLastSuccessFromSlot
  ? success path: memset(_connectCandidatePass), _saving_in_progress, reboot timer (kRebootDelayMs)
```

Password must already be in `_connectCandidatePass` before this handler runs. Never read from textarea or `_passwordScratch`.

## Open-network success decision table

Called from `handle_open_connect_finished()` on `Success` / `AlreadyConnected` only. Separate from password handler.

```
handle_open_network_success_persist()
  ? Case E: wifiCredStoreEntryFitsLegacyFile(_selectedOpenSsid, "") fails ? terminal status, restore Networks buttons, no reboot
  ? existingIdx = wifiCredStoreFindIndexBySsid(_selectedOpenSsid)
  ? if existingIdx < 0:
       Case D: savedCount >= CAPACITY ? terminal status, restore Networks buttons, no reboot
       Case A: AddOrUpdate(ssid, "") ? PersistToFs ? (fail: ReloadFromFs) ? SetLastSuccessFromSlot(newIdx)
  ? else:
       read stored password from existingIdx
       Case B: stored password is "" ? SetLastSuccessFromSlot(existingIdx) only
       Case C: stored password non-empty ? StagingBegin/SetCandidate("")/Commit ? PersistToFs ? (fail: ReloadFromFs) ? SetLastSuccessFromSlot
  ? success path: _saving_in_progress, reboot timer (kRebootDelayMs)
```

Never routes through `_connectCandidatePass` or `handle_successful_connect_persist()`.

## Persistence Cases A?E

Cases apply to both Password and Open success handlers with the same slot logic; password source differs.

| Case | Trigger | Existing credential match | Target slot | Password source | Credential mutation | Last-success update | Reboot | Failure behavior |
|------|---------|---------------------------|-------------|-----------------|---------------------|---------------------|--------|------------------|
| A | New SSID, free slot | not found | new insert | `_connectCandidatePass` or `""` | `AddOrUpdate` + `PersistToFs` | `SetLastSuccessFromSlot(newIdx)` | yes | reload + terminal status on persist fail |
| B | Existing SSID, same password | found, password unchanged | existing | stored or candidate match | none (no file write) | `SetLastSuccessFromSlot(existingIdx)` | yes | n/a |
| C | Existing SSID, password changed | found, password differs | existing | candidate or `""` via staging | `StagingBegin/SetCandidate/Commit` + `PersistToFs` | `SetLastSuccessFromSlot(existingIdx)` | yes | staging discard or reload + terminal status |
| D | New SSID, store full | not found, count >= CAPACITY | none | n/a | none | none | no | terminal status |
| E | Legacy format rejection | n/a | none | n/a | none | none | no | terminal status |

Change-password flow (Saved ? Chg pwd ? Connect) uses the same Cases via `_selectedSsid` + `_connectCandidatePass`; existing slot is found by SSID lookup.

## Saved-connect no-rewrite contract

Successful Saved connect updates last-success state but does not rewrite the credential list.

```
handle_saved_connect_finished() Success/AlreadyConnected:
  ? wifiCredStoreSetLastSuccessFromSlot(_selectedSavedSlot)
  ? schedule reboot
  ? no AddOrUpdate, no Staging, no PersistToFs
```

## Saved-network removal pipeline

Owned by `on_btn_saved_yes()` ? no reboot on success or failure.

```
on_btn_saved_yes()
  Phase 1: validate _selectedSavedSlot (255 or >= savedCount) ? error status, hide confirm row
  Phase 2: wifiCredStoreRemoveAt(_selectedSavedSlot) ? RemoveAt adjusts lastSSID internally
           ? on fail: error status, hide confirm row
  Phase 3: wifiCredStorePersistToFs() ? on fail: wifiCredStoreReloadFromFs()
  Phase 4: rebuild_saved_list() + show_home_panel() (clears saved state regardless of persist outcome)
```

No `_saving_in_progress` flag. No reboot timer. Password is never read during removal.

## Staging and rollback

Case C (password or open overwrite) sequence ? order is load-bearing:

1. `wifiCredStagingBegin(slot)`
2. `wifiCredStagingSetCandidatePassword(newPass)`
3. `wifiCredStagingCommitToStoredPassword()`
4. On staging failure: `wifiCredStagingDiscard()` ? return with terminal status
5. `wifiCredStorePersistToFs()`
6. On persist failure: `wifiCredStoreReloadFromFs()` ? return with terminal status

Case A uses direct `AddOrUpdate` without staging. Case B skips staging and persist entirely.

## Last-success ownership

| Path | last-success update | wifi.csv write |
|------|---------------------|----------------|
| Password success Cases A/B/C | `SetLastSuccessFromSlot` | A/C yes, B no |
| Open success Cases A/B/C | `SetLastSuccessFromSlot` | A/C yes, B no |
| Saved connect success | `SetLastSuccessFromSlot(_selectedSavedSlot)` | no |
| Saved removal | adjusted inside `RemoveAt` | yes (persist after remove) |
| Persistence failure (any case) | none | none (reload restores file state on A/C persist fail) |

No extra `ClearLastSuccess` call after removal ? `RemoveAt` handles lastSSID adjustment internally.

## Persistence failure behavior

| Failure | Status message (Password) | Status message (Open) | `_saving_in_progress` | Reboot | Buttons |
|---------|---------------------------|----------------------|----------------------|--------|---------|
| Case E legacy reject | kStrPersistCannotSaveNetwork | kStrOpenCannotSaveNetwork | no | no | restored via connecting_ui(false) on Open |
| Case D full store | kStrPersistNetworkFull | kStrOpenNetworkFull | no | no | Open: connecting_ui(false) |
| AddOrUpdate fail | kStrPersistCouldNotSave | kStrOpenCouldNotSave | no | no | Open: connecting_ui(false) |
| Staging fail | kStrPersistCouldNotUpdate | kStrOpenCouldNotSave | no | no | Open: connecting_ui(false) |
| Persist fail | kStrPersistCouldNotWrite + ReloadFromFs | kStrOpenCouldNotSave + ReloadFromFs | no | no | Open: connecting_ui(false) |
| Saved remove invalid slot | kStrConnectWifiError | ? | no | no | confirm row hidden |
| Saved remove fail | kStrConnectWifiError | ? | no | no | confirm row hidden |

Password panel uses `_pass_status_terminal`; Open uses `_open_status_terminal`. Saved removal errors use `_saved_status_terminal`.

## Reboot scheduling boundary

Reboot is scheduled only after full persistence success in Password and Open handlers:

- Timer: `lv_timer_create(on_reboot_timer, kRebootDelayMs=1800, this)` with repeat count 1
- Guard: `if (!_reboot_timer)` before creation
- Callback: `ESP.restart()`
- Preceded by: `_saving_in_progress = true`, saving UI lock, success status string
- Password success also zeroes `_connectCandidatePass` before scheduling
- Saved connect success schedules reboot without credential rewrite
- Saved removal, all failure paths: no reboot

## Polling dispatcher ownership

`pollOpsSnapshot()` is the sole top-level polling dispatcher. It is invoked from `on_poll_timer()` every `kPollIntervalMs` (200 ms).

Polling dispatches completed operations. It does not interpret their product result semantics ? that remains in `handle_*_finished()` and persistence handlers.

Private helpers (WIFIREF-D1) are called only from `pollOpsSnapshot()`:

| Helper | Role |
|--------|------|
| `poll_handle_password_result` | Route Password connect completion |
| `poll_handle_saved_result` | Route Saved connect completion |
| `poll_handle_open_result` | Route Open connect completion |
| `poll_scan_progress_blocks_ui` | S6V9F scan-progress blocking guard |
| `poll_restore_operation_buttons` | Re-enable Scan/Rescan after scan block |
| `poll_handle_scan_completion` | Scan await completion + list rebuild |
| `poll_emit_diagnostics` | Periodic diag summary (if enabled) |

## Polling phase order

Load-bearing order (must not be reordered):

```
1. Snapshot acquisition (wifiOpsGetSnapshot ? once per tick)
2. Recovery idle tick (process_boot_idle_timer_tick)
3. Panel visibility flags (pass_visible, saved_visible, net_visible)
4. Password result routing
5. Saved result routing
6. Open result routing
7. Scan-progress blocking guard
8. Button restoration
9. Scan completion
10. Password button sync (sync_connect_button_enabled)
11. Diagnostics
```

## Snapshot ownership

A single immutable operation snapshot is used for the complete polling tick.

- `pollOpsSnapshot()` calls `wifiOpsGetSnapshot()` exactly once.
- All helpers receive `const WifiOpsSnapshot& snap` ? no second snapshot fetch.
- Snapshot is not stored in a member field.

## Password result routing

`poll_handle_password_result(snap, pass_visible, pal)`:

- Condition: `pass_visible && _await_connect_ui`
- Busy Connect: update status, `set_password_panel_connecting_ui(true)` ? **early return**
- Idle: call `handle_connect_finished()` ? fall through

Helper does not reset `_await_connect_ui`, interpret result, call persistence, or schedule reboot.

## Saved result routing

`poll_handle_saved_result(snap, saved_visible, pal)`:

- Condition: `saved_visible && _await_saved_connect_ui`
- Busy Connect: update status (if not terminal) ? **early return**
- Idle: call `handle_saved_connect_finished()` ? fall through

No wifi.csv rewrite from polling ? handler owns last-success + reboot.

## Open result routing

`poll_handle_open_result(snap, net_visible, pal)`:

- Condition: `net_visible && _await_open_connect_ui`
- Busy Connect: update status, `set_networks_panel_connecting_ui(true)` ? **early return**
- Idle: call `handle_open_connect_finished()` ? fall through

Separate from Password routing; never uses `_connectCandidatePass`.

## Scan-progress blocking guard

S6V9F workaround ? scan-in-progress blocks UI only when:

```
scan_progress_blocks_ui = net_visible || _await_scan_ui
AND (phase == Scanning OR (busy AND currentOp == Scan))
```

Not equivalent to `if (snapshot.scanning) return` ? Home-only after stop AP must not block forever.

When blocking: show kStrScanning, disable Scan/Rescan ? **early return** (skips button restore, scan completion, sync, diagnostics).

## Button restoration boundary

`poll_restore_operation_buttons(net_visible)` runs only after scan-progress guard does not early-return.

- `_btn_scan`: always enabled
- `_btn_rescan`: disabled if `net_visible && (_saving_in_progress || _await_open_connect_ui)`

Not moved into scan-progress helper or result handlers.

## Scan completion and sequence ownership

`poll_handle_scan_completion` runs when:

```
_await_scan_ui && !pass_visible && !saved_visible && !_await_open_connect_ui
&& phase == Idle && !busy
```

Order: `_await_scan_ui = false` ? rebuild_scan_list() ? status by lastResult ? color ? `_last_results_seq = snap.resultsSeq`.

Not triggered on Password or Saved panel visibility.

## Polling early-return matrix

| Phase | Condition | Returns from tick | Later phases skipped | State mutation owner |
|-------|-----------|-------------------|----------------------|----------------------|
| Snapshot fail | !wifiOpsGetSnapshot | yes | all | none |
| Password busy | pass + await + busy Connect | yes | scan block, restore, completion, sync, diag | connecting_ui helper |
| Saved busy | saved + await + busy Connect | yes | scan block, restore, completion, sync, diag | status label only |
| Open busy | net + await + busy Connect | yes | scan block, restore, completion, sync, diag | connecting_ui helper |
| Scan progress | S6V9F guard active | yes | restore, completion, sync, diag | scan/rescan disabled |
| Password/Saved/Open idle | handler called | no | none | result handler owns await reset |

## Diagnostics boundary

`poll_emit_diagnostics()` ? `wifi_flow_diag_maybe_periodic_summary()` (under `WIFI_FLOW_DIAG_GLITCH`).

Runs last ? not before early-return points. No new Serial logs added in D1.

## Panel visibility owner

`_showOnlyPanel(lv_obj_t* panel)` hides all five panel roots and shows only the specified target. Sets `_home_visible = (panel == _panel_home)`. Does not touch state, cancel ops, or control AP.

Each `show_*` method calls `_showOnlyPanel` after its specific cleanup phase.

Panel visibility ownership contract:

```
Panel show methods own:
  - UI visibility of all five panels
  - _home_visible flag
  - local UI state cleanup before transition
  - recovery idle arm/disarm calls

Panel show methods do NOT own:
  - backend result interpretation
  - persistence
  - timers (created in enter/persistence handlers)
  - SoftAP lifecycle (owned by show_hotspot_panel entry/exit callers)
```

---

## Panel transition matrix

| From | Action | Side effects | To |
|------|--------|-------------|-----|
| Home | Scan button | clear_password_panel_state, start scan | Networks |
| Home | saved-row tap | clear_saved_state, populate slot | Saved |
| Home | Hotspot button | disarm idle, start AP | Hotspot |
| Home | Back (non-boot-failure) | ? | Player (dismiss) |
| Home | idle timeout (60s) | disarm idle, start AP | Hotspot |
| Networks | locked row tap | cancel_open_connect, clear_password | Password |
| Networks | open row tap | start open-connect (stays Networks) | Networks |
| Networks | Home button | cancel open-connect if active | Home |
| Password | Connect (success) | persist ? reboot | (reboot) |
| Password | Back (from scanned) | clear_password_panel_state | Networks |
| Password | Back (from Saved) | clear_password_panel_state | Saved |
| Saved | Connect (success) | lastSSID update ? reboot | (reboot) |
| Saved | Chg pwd | _password_from_saved=true | Password |
| Saved | Remove ? Yes | store.remove, persist | Home |
| Saved | Remove ? No | restore normal row | Saved |
| Saved | Back | clear_saved_state | Home |
| Hotspot | Back | stop AP, cancel ops | Home |

---

## Home entry contexts

Three entry contexts set different Home UI:

| Context | Subtitle | Back visibility |
|---------|----------|-----------------|
| Boot-failure | `"Could not connect. Tap a saved network or Scan."` | Hidden |
| Runtime-disconnect | `"Wi-Fi disconnected. Tap a saved network or Scan."` | Visible |
| Manual | `"Tap a saved network, or Scan to choose another."` | Visible |

Entry flags are consumed in `enter()` and read by `sync_home_boot_failure_ui()`. WIFIREF-B does not fix the deferred `_entered_from_runtime_disconnect` cleanup issue.

---

## Password state ownership

`clear_password_panel_state()` resets in this order:
1. Await/operation flags (`_await_connect_ui`, `_saving_in_progress`)
2. Terminal/status lock flags
3. Selected SSID buffer
4. `clear_password_secrets()` ? keyboard detach ? textarea clear ? buffer zeroes

`clear_password_secrets()` contract:
- Keyboard must be detached **before** textarea is cleared (avoids spurious `VALUE_CHANGED`).
- `_passwordScratch` and `_connectCandidatePass` are explicitly zeroed.
- Keyboard is attached in `open_password_entry()` only.

---

## Saved Network state ownership

`clear_saved_state()` resets in this order:
1. Selected slot/SSID (`_selectedSavedSlot=255`, `_selectedSavedSsid={}`)
2. Await and terminal flags
3. Password-origin and remove-confirmation state

`_setSavedRemoveConfirmationVisible(bool)`:
- Hides/shows `_row_saved_normal` and `_row_saved_confirm`.
- Synchronizes `_remove_confirm_pending`.
- Called by: `open_saved_network`, `on_btn_saved_remove`, `on_btn_saved_no`, `on_btn_saved_yes` (error paths).

---

## Saved remove confirmation

When Remove is tapped:
- Status label shows confirmation prompt.
- `_row_saved_normal` hidden, `_row_saved_confirm` visible.
- `_remove_confirm_pending = true`.

When No is tapped:
- Status label cleared.
- `_row_saved_confirm` hidden, `_row_saved_normal` visible.
- `_remove_confirm_pending = false`.

When Yes is tapped (success):
- `wifiCredStoreRemoveAt` + `wifiCredStorePersistToFs`.
- `rebuild_saved_list()` + `show_home_panel()`.

---

## UI-lock states

| Helper | Condition | Effect |
|--------|-----------|--------|
| `set_password_panel_connecting_ui(true)` | connect started | TA, kbd, Connect disabled |
| `set_password_panel_saving_ui()` | persist success | TA, kbd, Connect, Back all disabled (permanent) |
| `set_saved_panel_connecting_ui(true)` | connect started | Connect, Chg pwd, Remove disabled |
| `set_saved_panel_saving_ui()` | connect success ? persist | All four Saved buttons disabled |
| `set_networks_panel_connecting_ui(true)` | open connect started | Rescan, Cancel, Home disabled |
| `set_networks_panel_saving_ui()` | open persist success | Rescan, Cancel, Home disabled (permanent) |

---

## Recovery idle state

Idle auto-Hotspot is implemented via the 200 ms poll timer, not a separate lv_timer:

```
arm_recovery_idle_if_home_only()
  ? _boot_idle_armed = true
  ? _boot_idle_deadline_ms = millis() + kRecoveryIdleToHotspotTimeoutMs (60000)
  ? countdown label set once

process_boot_idle_timer_tick() (called every 200 ms from pollOpsSnapshot)
  ? checks Home-only visible
  ? checks no ops-block flags
  ? on timeout: show_hotspot_panel()

disarm_boot_idle_timer()
  ? _boot_idle_armed = false
  ? countdown label cleared
```

Ops-block flags: `_await_scan_ui`, `_await_connect_ui`, `_await_saved_connect_ui`, `_await_open_connect_ui`, `_saving_in_progress`.

---

## Hotspot entry and exit ownership

`show_hotspot_panel()` order (load-bearing, must not be reordered):
1. `disarm_boot_idle_timer()`
2. `cancel_open_connect_state()` ? stops in-flight open connect
3. `network.recoveryEnsureSoftAP()` ? starts yoRadioAP
4. `sync_hotspot_panel_labels()` ? populates SSID/pwd/IP/help
5. `_showOnlyPanel(_panel_hotspot)` ? reveals panel

SoftAP stop happens in `on_btn_back_hotspot` via `network.recoveryStopSoftAP()`. The builder `create_hotspot_panel()` owns only the static object tree ? no AP logic.

---

## State reset boundaries

| Event | Resets |
|-------|--------|
| `show_home_panel()` | Password + Saved state, open-connect state |
| `show_networks_panel()` | Password state |
| `show_password_panel()` | Open-connect state |
| `show_saved_panel()` | Open-connect state |
| `show_hotspot_panel()` | Open-connect state |
| `open_saved_network()` | Previous Saved state (slot/flags/rows) |
| `open_password_entry()` | Password flags, SSID, textarea, keyboard |
| `enter()` | All await flags, candidate pass, entry context consumed |
| `exit()` | Timers, secrets, open-connect, wifiOpsCancel |

---

## Notes / ???????

- **WIFIREF-A** (`052bc4d`, `E50W`): resources/styles/layout-builders refactor. No behavioral changes.
- **WIFIREF-B** (`f790521`, `E51W`): panel navigation, local UI state, and transition contracts. `_showOnlyPanel` helper, `_setSavedRemoveConfirmationVisible` helper, section headers. No behavioral changes.
- **WIFIREF-C1** (`E52W`): operation pipelines and dynamic list pipelines. Section headers for Scan, Password-protected, Open-network and Saved-network pipelines. Operation UI strings centralized. No behavioral changes; persistence Cases A-E deferred to WIFIREF-C2.
- **WIFIREF-C2** (`E53W`): credential persistence decision tables and Saved-network removal organization. Persistence UI strings centralized. Cases A-E semantics unchanged. No behavioral changes.
- **WIFIREF-D1** (`E54W`): polling dispatcher decomposition. `pollOpsSnapshot()` split into phase helpers; guard order unchanged. No behavioral changes.
- **WIFIREF-D2** (deferred): callback refactor.
- Deferred hardening: stale `_screen` validity check, `_entered_from_runtime_disconnect` cleanup in `exit()`.
- `_panel_pass` hidden flag is set inside `create_password_panel()` to keep the builder self-contained.
- `_panel_saved` hidden flag is set inside `create_saved_panel()` for the same reason.
- `_panel_hotspot` hidden flag is set inside `create_hotspot_panel()` for the same reason.
- `_panel_net` starts HIDDEN by default (only Home is initially visible).
