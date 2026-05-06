/*
 * LvglWifiFlowScreen — Wi-Fi 3A–6A service shell (PageChain RebootRequired, not carousel page).
 * Home + Networks + PasswordEntry (4B connect + 5F status lock + 6A save credentials + reboot).
 * Shell: RebootRequired; scan через ops; 6A — save/update after Success + ESP.restart.
 */

#include "scr_wifi_flow.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include <cstdio>
#include <cstring>

#include "../../core/display.h"
#include "../../core/wifi_credentials_store.h"
#include "../../core/wifi_ops_adapter.h"
#include "../lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

#include <WiFi.h>

namespace lvgl_ui {

namespace {

constexpr uint32_t kPollMs = 200;
// Legacy credential field is 40 bytes; allow 39 typed chars + room / legacy поле 40 байт, ввод ≤39.
constexpr uint32_t kPasswordMaxInputChars = 39U;
constexpr size_t   kMinPasswordLen        = 8U;

void style_service_panel(lv_obj_t* panel, const YoRadioPalette& pal) {
    if (!panel) return;
    lv_obj_set_style_bg_color(panel, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, pal.panel_border, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding), LV_PART_MAIN);
}

lv_obj_t* add_footer_button(lv_obj_t* row, const char* txt, const YoRadioPalette& pal, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = lv_btn_create(row);
    if (!b) return nullptr;
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(b, 44, LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_set_style_text_color(l, pal.text_primary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_normal) {
        lv_obj_set_style_text_font(l, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_normal), LV_PART_MAIN);
    }
    return b;
}

} // namespace

ScreenType LvglWifiFlowScreen::screenType() const {
    return ScreenType::RebootRequired;
}

void LvglWifiFlowScreen::clear_password_secrets() {
    memset(_passwordScratch, 0, sizeof(_passwordScratch));
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
    if (_kbd) {
        lv_keyboard_set_textarea(_kbd, nullptr);
    }
    if (_ta_password) {
        lv_textarea_set_text(_ta_password, "");
    }
}

void LvglWifiFlowScreen::clear_password_panel_state() {
    clear_password_secrets();
    memset(_selectedSsid, 0, sizeof(_selectedSsid));
    _await_connect_ui                = false;
    _pass_status_terminal            = false;
    _skip_next_ta_pass_status_sync = false;
    _saving_in_progress              = false;
}

void LvglWifiFlowScreen::show_home_panel() {
    clear_password_panel_state();
    _home_visible = true;
    if (_panel_home) lv_obj_clear_flag(_panel_home, LV_OBJ_FLAG_HIDDEN);
    if (_panel_net) lv_obj_add_flag(_panel_net, LV_OBJ_FLAG_HIDDEN);
    if (_panel_pass) lv_obj_add_flag(_panel_pass, LV_OBJ_FLAG_HIDDEN);
    sync_home_boot_failure_ui();
}

void LvglWifiFlowScreen::sync_home_boot_failure_ui() {
    if (!_sub_home) return;
    const YoRadioPalette& pal = yoradio_palette();
    if (_entered_from_boot_failure) {
        lv_label_set_text(_sub_home,
                          "Could not connect to saved Wi-Fi. Saved list below is read-only - tap Scan to pick a "
                          "network / сохранённые не подключились; список ниже только для просмотра - выберите сеть "
                          "через Scan.");
        if (_btn_back_home) lv_obj_add_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(_sub_home,
                          "Saved networks below (read-only). Tap Scan to choose a network / сохранённые ниже "
                          "(только просмотр). Выберите сеть через Scan.");
        if (_btn_back_home) lv_obj_clear_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(_sub_home, pal.text_secondary, LV_PART_MAIN);
}

void LvglWifiFlowScreen::show_networks_panel() {
    clear_password_panel_state();
    _home_visible = false;
    if (_panel_net) lv_obj_clear_flag(_panel_net, LV_OBJ_FLAG_HIDDEN);
    if (_panel_home) lv_obj_add_flag(_panel_home, LV_OBJ_FLAG_HIDDEN);
    if (_panel_pass) lv_obj_add_flag(_panel_pass, LV_OBJ_FLAG_HIDDEN);
}

void LvglWifiFlowScreen::show_password_panel() {
    _home_visible = false;
    if (_panel_pass) lv_obj_clear_flag(_panel_pass, LV_OBJ_FLAG_HIDDEN);
    if (_panel_home) lv_obj_add_flag(_panel_home, LV_OBJ_FLAG_HIDDEN);
    if (_panel_net) lv_obj_add_flag(_panel_net, LV_OBJ_FLAG_HIDDEN);
}

void LvglWifiFlowScreen::open_password_entry(const char* ssid_utf8) {
    if (!ssid_utf8 || !ssid_utf8[0] || !_lbl_pass_ssid || !_ta_password || !_kbd) return;
    clear_password_secrets();
    _await_connect_ui                = false;
    _pass_status_terminal            = false;
    _skip_next_ta_pass_status_sync = false;
    memset(_selectedSsid, 0, sizeof(_selectedSsid));
    strlcpy(_selectedSsid, ssid_utf8, sizeof(_selectedSsid));
    lv_label_set_text(_lbl_pass_ssid, _selectedSsid);
    lv_textarea_set_text(_ta_password, "");
    lv_keyboard_set_textarea(_kbd, _ta_password);
    if (_lbl_pass_status) {
        const YoRadioPalette& pal = yoradio_palette();
        lv_label_set_text(_lbl_pass_status, "Enter password / введите пароль");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    }
    show_password_panel();
    sync_connect_button_enabled();
}

void LvglWifiFlowScreen::sync_connect_button_enabled() {
    if (!_btn_connect) return;
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;
    // While connect op runs, keep Connect disabled / пока идёт connect — кнопка выкл.
    if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
        lv_obj_add_state(_btn_connect, LV_STATE_DISABLED);
        return;
    }
    bool enable = (_selectedSsid[0] != '\0') && !snap.busy;
    if (enable && _ta_password) {
        const char* t = lv_textarea_get_text(_ta_password);
        const size_t  n = t ? strlen(t) : 0U;
        enable          = (n >= kMinPasswordLen);
    } else if (enable && !_ta_password) {
        enable = false;
    }
    if (enable) {
        lv_obj_clear_state(_btn_connect, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(_btn_connect, LV_STATE_DISABLED);
    }
}

void LvglWifiFlowScreen::set_password_panel_connecting_ui(bool connecting) {
    if (!_ta_password || !_kbd || !_btn_connect) return;
    if (connecting) {
        lv_obj_add_state(_ta_password, LV_STATE_DISABLED);
        lv_obj_add_state(_kbd, LV_STATE_DISABLED);
        lv_obj_add_state(_btn_connect, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(_ta_password, LV_STATE_DISABLED);
        lv_obj_clear_state(_kbd, LV_STATE_DISABLED);
    }
    sync_connect_button_enabled();
}

void LvglWifiFlowScreen::set_password_panel_saving_ui() {
    // Disable all input while saving/rebooting; no re-enable expected / блок ввода до reboot.
    if (_ta_password)   lv_obj_add_state(_ta_password,  LV_STATE_DISABLED);
    if (_kbd)           lv_obj_add_state(_kbd,           LV_STATE_DISABLED);
    if (_btn_connect)   lv_obj_add_state(_btn_connect,   LV_STATE_DISABLED);
    if (_btn_back_pass) lv_obj_add_state(_btn_back_pass, LV_STATE_DISABLED);
}

void LvglWifiFlowScreen::on_reboot_timer(lv_timer_t* t) {
    // Called once after save-success delay; self pointer not needed — just restart / однократный вызов после задержки.
    (void)t;
    ESP.restart();
}

// Wi-Fi 6A: persist credentials after successful connect, then schedule reboot.
// Wi-Fi 6A: сохраняем учётные данные после успешного connect, затем перезагружаем.
void LvglWifiFlowScreen::handle_successful_connect_persist() {
    if (!_lbl_pass_status) return;
    const YoRadioPalette& pal = yoradio_palette();

    // Case E: SSID or candidate password does not fit legacy csv constraints.
    // Case E: SSID или пароль не проходят проверку формата legacy csv.
    if (!wifiCredStoreEntryFitsLegacyFile(_selectedSsid, _connectCandidatePass)) {
        lv_label_set_text(_lbl_pass_status, "Cannot save this network / нельзя сохранить эту сеть");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        return;
    }

    const int existingIdx = wifiCredStoreFindIndexBySsid(_selectedSsid);

    if (existingIdx < 0) {
        // Case D: store full and SSID is new.
        // Case D: нет свободных слотов, новый SSID.
        if (wifiCredStoreSavedCount() >= WIFI_CRED_STORE_CAPACITY) {
            lv_label_set_text(_lbl_pass_status,
                              "Saved networks full. Remove one first. / хранилище заполнено - удалите сеть");
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }

        // Case A: new SSID, free slot / новый SSID, есть слот.
        if (!wifiCredStoreAddOrUpdate(_selectedSsid, _connectCandidatePass)) {
            lv_label_set_text(_lbl_pass_status, "Could not save network / ошибка сохранения сети");
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }
        const int newIdx = wifiCredStoreFindIndexBySsid(_selectedSsid);
        if (!wifiCredStorePersistToFs()) {
            // RAM mutated but file failed: reload to restore consistency / RAM изменён, но файл не записан.
            wifiCredStoreReloadFromFs();
            lv_label_set_text(_lbl_pass_status, "Could not save network / ошибка записи на диск");
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }
        if (newIdx >= 0) {
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(newIdx));
        }

    } else {
        const char* storedPass = "";
        WifiCredStoreEntryView ev{};
        if (wifiCredStoreGetEntry(static_cast<uint8_t>(existingIdx), &ev)) {
            storedPass = ev.password;
        }

        if (strcmp(storedPass, _connectCandidatePass) == 0) {
            // Case B: SSID exists, same password — no file write needed, only update lastSSID.
            // Case B: тот же пароль — файл не меняем, только lastSSID.
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        } else {
            // Case C: SSID exists, password changed — use staging to update.
            // Case C: пароль изменился — обновляем через staging.
            if (!wifiCredStagingBegin(static_cast<uint8_t>(existingIdx)) ||
                !wifiCredStagingSetCandidatePassword(_connectCandidatePass) ||
                !wifiCredStagingCommitToStoredPassword()) {
                wifiCredStagingDiscard();
                lv_label_set_text(_lbl_pass_status, "Could not update password / ошибка обновления пароля");
                lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
                _pass_status_terminal = true;
                return;
            }
            if (!wifiCredStorePersistToFs()) {
                wifiCredStoreReloadFromFs();
                lv_label_set_text(_lbl_pass_status, "Could not save network / ошибка записи на диск");
                lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
                _pass_status_terminal = true;
                return;
            }
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        }
    }

    // Persist and lastSSID both succeeded — clear candidate, lock UI, schedule reboot.
    // Сохранено и lastSSID обновлён — очищаем кандидата, блокируем UI, планируем reboot.
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
    _saving_in_progress   = true;
    _pass_status_terminal = true;
    lv_label_set_text(_lbl_pass_status,
                      "Saved. Restarting... / сохранено, перезагрузка...");
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
    set_password_panel_saving_ui();

    // One-shot timer ~1800 ms then ESP.restart() / однократный таймер ~1800 мс, затем перезагрузка.
    if (!_reboot_timer) {
        _reboot_timer = lv_timer_create(on_reboot_timer, 1800, this);
        lv_timer_set_repeat_count(_reboot_timer, 1);
    }
}

void LvglWifiFlowScreen::handle_connect_finished() {
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    _await_connect_ui = false;
    const YoRadioPalette& pal = yoradio_palette();
    set_password_panel_connecting_ui(false);

    if (!_lbl_pass_status) return;

    switch (snap.lastResult) {
    case WifiOpsResult::Success:
    case WifiOpsResult::AlreadyConnected:
        // Wi-Fi 6A: persist + schedule reboot; clears textarea inside persist fn / 6A: сохраняем и планируем reboot.
        if (_ta_password) {
            _skip_next_ta_pass_status_sync = true;
            lv_textarea_set_text(_ta_password, "");
        }
        handle_successful_connect_persist();
        return; // persist fn controls further UI; skip sync_connect_button_enabled below.
    case WifiOpsResult::AuthFailed:
        lv_label_set_text(_lbl_pass_status, "Wrong password / неверный пароль");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Timeout:
        lv_label_set_text(_lbl_pass_status, "Timeout / таймаут");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::NoNetwork:
        lv_label_set_text(_lbl_pass_status, "Network not found / сеть не найдена");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Cancelled:
        // Same policy as Back: leave password panel via Networks + clear / как Back — сети + очистка.
        clear_password_panel_state();
        show_networks_panel();
        return;
    case WifiOpsResult::InternalError:
        lv_label_set_text(_lbl_pass_status, "Wi-Fi error / ошибка Wi-Fi");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Busy:
        lv_label_set_text(_lbl_pass_status, "Busy / занято");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    default:
        lv_label_set_text(_lbl_pass_status, "Connect finished / подключение завершено");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    }
    sync_connect_button_enabled();
}

void LvglWifiFlowScreen::start_connect_from_user() {
    if (!_ta_password || !_lbl_pass_status || !_selectedSsid[0]) return;
    if (_await_connect_ui) return;
    // Wi-Fi 6A: do not allow new connect while saving/rebooting / блок нового connect при сохранении.
    if (_saving_in_progress) return;

    // New Connect attempt: allow status + TA validation again / новая попытка — снова валидация и статусы.
    _pass_status_terminal            = false;
    _skip_next_ta_pass_status_sync = false;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) && cur.busy) {
        const YoRadioPalette& pal = yoradio_palette();
        lv_label_set_text(_lbl_pass_status, "Wi-Fi busy / занято");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    const char* pw_in = lv_textarea_get_text(_ta_password);
    const size_t plen = pw_in ? strlen(pw_in) : 0U;
    if (plen < kMinPasswordLen) {
        const YoRadioPalette& pal = yoradio_palette();
        lv_label_set_text(_lbl_pass_status, "Min 8 characters / минимум 8 символов");
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    // Wi-Fi 6A: save candidate for post-Success persist; pass separate tmp to ops / кандидат для сохранения после Success.
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
    strlcpy(_connectCandidatePass, pw_in, sizeof(_connectCandidatePass));

    char tmp[40]{};
    strlcpy(tmp, pw_in, sizeof(tmp));
    const bool started = wifiOpsRequestConnectWithPassword(_selectedSsid, tmp, true);
    memset(tmp, 0, sizeof(tmp));

    const YoRadioPalette& pal = yoradio_palette();
    if (!started) {
        memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
        WifiOpsSnapshot after{};
        (void)wifiOpsGetSnapshot(&after);
        if (after.busy) {
            lv_label_set_text(_lbl_pass_status, "Busy - cannot start / занято");
        } else {
            lv_label_set_text(_lbl_pass_status, "Cannot start connect / не удалось запустить");
        }
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    _await_connect_ui = true;
    lv_label_set_text(
        _lbl_pass_status,
        "Connecting... current Wi-Fi may disconnect / подключение... возможен разрыв Wi-Fi");
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    set_password_panel_connecting_ui(true);
}

void LvglWifiFlowScreen::start_scan_from_user() {
    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) &&
        (cur.phase == WifiOpsPhase::Scanning || (cur.busy && cur.currentOp == WifiOpsOp::Scan))) {
        const YoRadioPalette& pal = yoradio_palette();
        lv_label_set_text(_lbl_net_status, "Scan in progress... / уже идёт");
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }
    if (!wifiOpsRequestScan()) {
        const YoRadioPalette& pal = yoradio_palette();
        lv_label_set_text(_lbl_net_status, "Scan not started (busy?) / не стартовало");
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }
    _await_scan_ui = true;
    show_networks_panel();
    lv_label_set_text(_lbl_net_status, "Starting scan... / запуск...");
}

void LvglWifiFlowScreen::rebuild_saved_list() {
    if (!_list_saved) return;
    lv_obj_clean(_list_saved);
    const uint8_t n = wifiCredStoreSavedCount();
    if (n == 0) {
        lv_list_add_btn(_list_saved, LV_SYMBOL_LIST, "(none)");
        return;
    }
    const uint8_t cap = (n > 5) ? 5 : n;
    for (uint8_t i = 0; i < cap; ++i) {
        WifiCredStoreEntryView v{};
        if (!wifiCredStoreGetEntry(i, &v) || !v.ssid) continue;
        char line[40];
        snprintf(line, sizeof(line), "%u  %s", static_cast<unsigned>(i + 1U), v.ssid);
        lv_list_add_btn(_list_saved, LV_SYMBOL_LIST, line);
    }
}

void LvglWifiFlowScreen::rebuild_scan_list() {
    if (!_list_scan) return;
    lv_obj_clean(_list_scan);
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;
    for (uint16_t i = 0; i < snap.scanCount; ++i) {
        WifiOpsScanRow row{};
        if (!wifiOpsGetScanResult(i, &row)) continue;
        const bool is_open = (row.auth == WIFI_AUTH_OPEN);
        char line[96];
        snprintf(line,
                 sizeof(line),
                 "%s  %ddBm  %s",
                 row.ssid,
                 static_cast<int>(row.rssi),
                 is_open ? "open" : "lock");
        lv_obj_t* btn = lv_list_add_btn(_list_scan, LV_SYMBOL_LIST, line);
        if (!btn) continue;
        // Wi‑Fi 5C: store idx+1 — idx 0 must not become nullptr user_data / индекс 0 ≠ nullptr.
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(i) + 1U)));
        lv_obj_add_event_cb(btn, on_scan_row_click, LV_EVENT_CLICKED, this);
    }
    if (snap.scanCount == 0) {
        lv_list_add_btn(_list_scan, LV_SYMBOL_LIST, "(empty)");
    }
}

void LvglWifiFlowScreen::create() {
    if (_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    const int32_t         pad = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, pad, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_size(_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    _panel_home = lv_obj_create(_screen);
    _panel_net  = lv_obj_create(_screen);
    _panel_pass = lv_obj_create(_screen);
    if (!_panel_home || !_panel_net || !_panel_pass) return;
    lv_obj_set_width(_panel_home, LV_PCT(100));
    lv_obj_set_flex_grow(_panel_home, 1);
    lv_obj_set_width(_panel_net, LV_PCT(100));
    lv_obj_set_flex_grow(_panel_net, 1);
    lv_obj_set_width(_panel_pass, LV_PCT(100));
    lv_obj_set_flex_grow(_panel_pass, 1);
    lv_obj_set_flex_flow(_panel_home, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(_panel_net, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(_panel_pass, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_panel_home, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel_net, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel_pass, 8, LV_PART_MAIN);
    style_service_panel(_panel_home, pal);
    style_service_panel(_panel_net, pal);
    style_service_panel(_panel_pass, pal);

    _hdr_home = lv_label_create(_panel_home);
    lv_label_set_text(_hdr_home, "Wi-Fi Setup");
    lv_obj_set_style_text_color(_hdr_home, pal.text_primary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_header) {
        lv_obj_set_style_text_font(_hdr_home, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_header), LV_PART_MAIN);
    }

    _sub_home = lv_label_create(_panel_home);
    lv_label_set_text(_sub_home,
                      "Saved networks below (read-only). Tap Scan to choose a network / сохранённые ниже "
                      "(только просмотр). Выберите сеть через Scan.");
    lv_obj_set_style_text_color(_sub_home, pal.text_secondary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_small) {
        lv_obj_set_style_text_font(_sub_home, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_small), LV_PART_MAIN);
    }
    lv_label_set_long_mode(_sub_home, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_sub_home, LV_PCT(100));

    _list_saved = lv_list_create(_panel_home);
    lv_obj_set_width(_list_saved, LV_PCT(100));
    lv_obj_set_flex_grow(_list_saved, 1);
    lv_obj_set_style_bg_opa(_list_saved, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list_saved, 0, LV_PART_MAIN);

    lv_obj_t* rowh = lv_obj_create(_panel_home);
    lv_obj_set_width(rowh, LV_PCT(100));
    lv_obj_set_height(rowh, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rowh, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(rowh, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rowh, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(rowh, 0, LV_PART_MAIN);
    _btn_scan = add_footer_button(rowh, "Scan", pal, on_btn_scan, this);
    add_footer_button(rowh, "Try saved", pal, on_btn_try_stub, this);
    add_footer_button(rowh, "Hotspot", pal, on_btn_hotspot_stub, this);
    _btn_back_home = add_footer_button(rowh, "Back", pal, on_btn_back_home, this);

    _hdr_net = lv_label_create(_panel_net);
    lv_label_set_text(_hdr_net, "Available networks");
    lv_obj_set_style_text_color(_hdr_net, pal.text_primary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_header) {
        lv_obj_set_style_text_font(_hdr_net, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_header), LV_PART_MAIN);
    }

    _lbl_net_status = lv_label_create(_panel_net);
    lv_label_set_text(_lbl_net_status, " ");
    lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_small) {
        lv_obj_set_style_text_font(_lbl_net_status, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_small), LV_PART_MAIN);
    }
    lv_label_set_long_mode(_lbl_net_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lbl_net_status, LV_PCT(100));

    _list_scan = lv_list_create(_panel_net);
    lv_obj_set_width(_list_scan, LV_PCT(100));
    lv_obj_set_flex_grow(_list_scan, 1);
    lv_obj_set_style_bg_opa(_list_scan, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list_scan, 0, LV_PART_MAIN);

    lv_obj_t* rown = lv_obj_create(_panel_net);
    lv_obj_set_width(rown, LV_PCT(100));
    lv_obj_set_height(rown, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rown, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(rown, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rown, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(rown, 0, LV_PART_MAIN);
    _btn_rescan = add_footer_button(rown, "Rescan", pal, on_btn_rescan, this);
    add_footer_button(rown, "Cancel", pal, on_btn_cancel_scan, this);
    add_footer_button(rown, "Home", pal, on_btn_back_net, this);

    // Wi-Fi 4A: Password panel / панель пароля (UI only).
    _hdr_pass = lv_label_create(_panel_pass);
    lv_label_set_text(_hdr_pass, "Wi-Fi password");
    lv_obj_set_style_text_color(_hdr_pass, pal.text_primary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_header) {
        lv_obj_set_style_text_font(_hdr_pass, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_header), LV_PART_MAIN);
    }

    _lbl_pass_ssid = lv_label_create(_panel_pass);
    lv_label_set_text(_lbl_pass_ssid, " ");
    lv_obj_set_style_text_color(_lbl_pass_ssid, pal.text_primary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_normal) {
        lv_obj_set_style_text_font(_lbl_pass_ssid, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_normal), LV_PART_MAIN);
    }
    lv_label_set_long_mode(_lbl_pass_ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_lbl_pass_ssid, LV_PCT(100));

    _lbl_pass_hint = lv_label_create(_panel_pass);
    lv_label_set_text(
        _lbl_pass_hint,
        "Connect may disconnect current Wi-Fi (explicit) / подключение может разорвать текущий Wi-Fi (явное действие)");
    lv_obj_set_style_text_color(_lbl_pass_hint, pal.text_secondary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_small) {
        lv_obj_set_style_text_font(_lbl_pass_hint, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_small), LV_PART_MAIN);
    }
    lv_label_set_long_mode(_lbl_pass_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lbl_pass_hint, LV_PCT(100));

    _ta_password = lv_textarea_create(_panel_pass);
    lv_obj_set_width(_ta_password, LV_PCT(100));
    lv_obj_set_style_min_height(_ta_password, 44, LV_PART_MAIN);
    lv_textarea_set_one_line(_ta_password, true);
    lv_textarea_set_max_length(_ta_password, kPasswordMaxInputChars);
    lv_textarea_set_password_mode(_ta_password, true);
    lv_obj_set_style_text_color(_ta_password, pal.text_primary, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (LV_ACTIVE_PROFILE.font_normal) {
        lv_obj_set_style_text_font(_ta_password, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_normal),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_add_event_cb(_ta_password, on_ta_password_changed, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* rowp = lv_obj_create(_panel_pass);
    lv_obj_set_width(rowp, LV_PCT(100));
    lv_obj_set_height(rowp, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rowp, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(rowp, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rowp, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(rowp, 0, LV_PART_MAIN);

    _btn_connect = lv_btn_create(rowp);
    if (_btn_connect) {
        lv_obj_set_flex_grow(_btn_connect, 1);
        lv_obj_set_style_min_height(_btn_connect, 44, LV_PART_MAIN);
        lv_obj_add_state(_btn_connect, LV_STATE_DISABLED);
        lv_obj_add_event_cb(_btn_connect, on_btn_connect, LV_EVENT_CLICKED, this);
        lv_obj_t* lc = lv_label_create(_btn_connect);
        lv_label_set_text(lc, "Connect");
        lv_obj_center(lc);
        lv_obj_set_style_text_color(lc, pal.text_secondary, LV_PART_MAIN);
        if (LV_ACTIVE_PROFILE.font_normal) {
            lv_obj_set_style_text_font(lc, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_normal), LV_PART_MAIN);
        }
    }
    _btn_back_pass = add_footer_button(rowp, "Back", pal, on_btn_back_pass, this);

    _lbl_pass_status = lv_label_create(_panel_pass);
    lv_label_set_text(_lbl_pass_status, " ");
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_small) {
        lv_obj_set_style_text_font(_lbl_pass_status, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_small), LV_PART_MAIN);
    }
    lv_label_set_long_mode(_lbl_pass_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lbl_pass_status, LV_PCT(100));

    _kbd = lv_keyboard_create(_panel_pass);
    if (_kbd) {
        lv_obj_set_width(_kbd, LV_PCT(100));
        lv_obj_set_flex_grow(_kbd, 1);
        lv_obj_set_style_min_height(_kbd, 120, LV_PART_MAIN);
        lv_keyboard_set_mode(_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(_kbd, nullptr);
        lv_obj_add_event_cb(_kbd, on_keyboard_event, LV_EVENT_ALL, this);
    }

    lv_obj_add_flag(_panel_pass, LV_OBJ_FLAG_HIDDEN);

    show_home_panel();
    rebuild_saved_list();
}

void LvglWifiFlowScreen::enter() {
    (void)wifiOpsInit();
    _last_results_seq              = 0;
    _await_scan_ui                 = false;
    _await_connect_ui              = false;
    _pass_status_terminal          = false;
    _skip_next_ta_pass_status_sync = false;
    _saving_in_progress            = false;
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
    _entered_from_boot_failure = lvgl_ui::consumeWifiRecoveryEnteredFromBootFailure();
    rebuild_saved_list();
    sync_home_boot_failure_ui();
    if (!_poll_timer) {
        _poll_timer = lv_timer_create(on_poll_timer, kPollMs, this);
    }
}

void LvglWifiFlowScreen::update() {}

void LvglWifiFlowScreen::exit() {
    clear_password_panel_state();
    _entered_from_boot_failure = false;
    sync_home_boot_failure_ui();
    if (_poll_timer) {
        lv_timer_del(_poll_timer);
        _poll_timer = nullptr;
    }
    if (_reboot_timer) {
        lv_timer_del(_reboot_timer);
        _reboot_timer = nullptr;
    }
    wifiOpsCancel();
}

void LvglWifiFlowScreen::destroy() {
    exit();
    clear_password_secrets();
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _panel_home = _panel_net = _panel_pass = nullptr;
    _hdr_home = _sub_home = _list_saved = nullptr;
    _hdr_net = _lbl_net_status = _list_scan = nullptr;
    _hdr_pass = _lbl_pass_ssid = _lbl_pass_hint = _ta_password = _lbl_pass_status = nullptr;
    _btn_connect = _btn_back_pass = _kbd = nullptr;
    _btn_scan = _btn_back_home = _btn_rescan = nullptr;
}

lv_obj_t* LvglWifiFlowScreen::screen() {
    return _screen;
}

void LvglWifiFlowScreen::on_poll_timer(lv_timer_t* t) {
    auto* self = static_cast<LvglWifiFlowScreen*>(t->user_data);
    if (self) self->pollOpsSnapshot();
}

void LvglWifiFlowScreen::pollOpsSnapshot() {
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    const YoRadioPalette& pal = yoradio_palette();

    const bool pass_visible =
        _panel_pass && !lv_obj_has_flag(_panel_pass, LV_OBJ_FLAG_HIDDEN);

    // 4B: Connect polling — do not mix with scan-list rebuild / connect отдельно от scan.
    if (pass_visible && _await_connect_ui) {
        if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
            if (_lbl_pass_status) {
                lv_label_set_text(
                    _lbl_pass_status,
                    "Connecting... current Wi-Fi may disconnect / подключение... возможен разрыв Wi-Fi");
                lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
            }
            set_password_panel_connecting_ui(true);
            return;
        }
        if (!snap.busy && snap.phase == WifiOpsPhase::Idle) {
            handle_connect_finished();
            // Fall through: refresh scan buttons etc. / дальше — кнопки скана.
        }
    }

    if (snap.phase == WifiOpsPhase::Scanning || (snap.busy && snap.currentOp == WifiOpsOp::Scan)) {
        lv_label_set_text(_lbl_net_status, "Scanning...");
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
        if (_btn_scan) lv_obj_add_state(_btn_scan, LV_STATE_DISABLED);
        if (_btn_rescan) lv_obj_add_state(_btn_rescan, LV_STATE_DISABLED);
        return;
    }
    if (_btn_scan) lv_obj_clear_state(_btn_scan, LV_STATE_DISABLED);
    if (_btn_rescan) lv_obj_clear_state(_btn_rescan, LV_STATE_DISABLED);

    // Completing scan await only when Networks (not Password) visible / завершение скана не на пароле.
    if (_await_scan_ui && !pass_visible && snap.phase == WifiOpsPhase::Idle && !snap.busy) {
        _await_scan_ui = false;
        if (snap.lastResult == WifiOpsResult::Success) {
            rebuild_scan_list();
            lv_label_set_text(_lbl_net_status, "Scan complete");
        } else if (snap.lastResult == WifiOpsResult::Cancelled) {
            rebuild_scan_list();
            lv_label_set_text(_lbl_net_status, "Scan cancelled");
        } else if (snap.lastResult == WifiOpsResult::Timeout) {
            rebuild_scan_list();
            lv_label_set_text(_lbl_net_status, "Scan timeout");
        } else if (snap.lastResult == WifiOpsResult::Busy) {
            rebuild_scan_list();
            lv_label_set_text(_lbl_net_status, "Busy");
        } else {
            rebuild_scan_list();
            lv_label_set_text(_lbl_net_status, "Scan finished");
        }
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        (void)snap.resultsSeq;
        _last_results_seq = snap.resultsSeq;
    }

    if (pass_visible && !_await_connect_ui) {
        sync_connect_button_enabled();
    }
}

void LvglWifiFlowScreen::on_btn_scan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (self) self->start_scan_from_user();
}

void LvglWifiFlowScreen::on_btn_try_stub(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    // Stub: try-saved op not implemented; saved SSIDs only on Home / заглушка - сохранённые только на Home.
    self->show_home_panel();
}

void LvglWifiFlowScreen::on_btn_hotspot_stub(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_lbl_net_status) return;
    const YoRadioPalette& pal = yoradio_palette();
    self->show_networks_panel();

    const wifi_mode_t wm = WiFi.getMode();
    const bool          ap_up = (wm == WIFI_AP || wm == WIFI_AP_STA);
    if (ap_up) {
        const IPAddress ap_ip = WiFi.softAPIP();
        if (ap_ip) {
            char line[112];
            snprintf(line,
                     sizeof(line),
                     "Hotspot active - WebUI http://%s / точка доступа - откройте http://%s",
                     ap_ip.toString().c_str(),
                     ap_ip.toString().c_str());
            lv_label_set_text(self->_lbl_net_status, line);
            lv_obj_set_style_text_color(self->_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            return;
        }
    }
    lv_label_set_text(self->_lbl_net_status,
                      "Hotspot fallback not visible yet / точка доступа пока не видна по IP");
    lv_obj_set_style_text_color(self->_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
}

void LvglWifiFlowScreen::on_btn_back_home(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self || self->_entered_from_boot_failure) return;
    lvgl_ui::dismissWifiFlowReturnToPlayer();
}

void LvglWifiFlowScreen::on_btn_back_net(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    wifiOpsCancel();
    self->show_home_panel();
}

void LvglWifiFlowScreen::on_btn_rescan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (self) self->start_scan_from_user();
}

void LvglWifiFlowScreen::on_btn_cancel_scan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    wifiOpsCancel();
}

void LvglWifiFlowScreen::on_btn_back_pass(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    // Wi-Fi 6A: disallow Back while save/reboot is scheduled / Back заблокирован при ожидании reboot.
    if (self->_saving_in_progress) return;
    if (self->_await_connect_ui) {
        wifiOpsCancel();
    }
    self->show_networks_panel();
}

void LvglWifiFlowScreen::on_btn_connect(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (self) self->start_connect_from_user();
}

void LvglWifiFlowScreen::on_ta_password_changed(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_ta_password || !self->_lbl_pass_status) return;
    if (self->_await_connect_ui) return;
    // Wi-Fi 6A: saving in progress — ignore all TA events / идёт сохранение — игнорируем TA события.
    if (self->_saving_in_progress) return;

    const char* t = lv_textarea_get_text(self->_ta_password);
    const size_t  n = t ? strlen(t) : 0U;
    const YoRadioPalette& pal = yoradio_palette();

    // Wi-Fi 5F: ignore one empty TA event after Success clears password / одно пустое событие после успеха.
    if (self->_skip_next_ta_pass_status_sync) {
        self->_skip_next_ta_pass_status_sync = false;
        if (n == 0U) {
            self->sync_connect_button_enabled();
            return;
        }
    }

    // Terminal result + empty field: do not revert label (duplicate LVGL events) / пустое поле + итог — не затирать.
    if (self->_pass_status_terminal && n == 0U) {
        self->sync_connect_button_enabled();
        return;
    }

    // User edited password (non-empty): resume validation / правка текста — снова валидация.
    if (self->_pass_status_terminal && n > 0U) {
        self->_pass_status_terminal = false;
    }

    if (n == 0U) {
        lv_label_set_text(self->_lbl_pass_status, "Enter password / введите пароль");
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    } else if (n < kMinPasswordLen) {
        lv_label_set_text(self->_lbl_pass_status, "Min 8 characters / минимум 8 символов");
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
    } else {
        lv_label_set_text(self->_lbl_pass_status, " ");
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    }
    self->sync_connect_button_enabled();
}

void LvglWifiFlowScreen::on_keyboard_event(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CANCEL) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    // Wi-Fi 6A: ignore keyboard cancel while saving/rebooting / Cancel клавиатуры заблокирован.
    if (self->_saving_in_progress) return;
    if (self->_await_connect_ui) {
        wifiOpsCancel();
    }
    self->show_networks_panel();
}

void LvglWifiFlowScreen::on_scan_row_click(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* tgt = lv_event_get_target(e);
    lv_obj_t* o   = tgt;
    while (o && lv_obj_get_user_data(o) == nullptr) {
        o = lv_obj_get_parent(o);
    }
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!o || !self || !self->_lbl_net_status) return;
    const uintptr_t stored = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(o));
    if (stored == 0U) return;
    const uint16_t idx = static_cast<uint16_t>(stored - 1U);
    WifiOpsScanRow  row{};
    if (!wifiOpsGetScanResult(idx, &row)) return;
    const YoRadioPalette& pal = yoradio_palette();
    const bool       is_open = (row.auth == WIFI_AUTH_OPEN);
    if (is_open) {
        lv_label_set_text(self->_lbl_net_status, "Open network: connect later / открытая сеть - позже");
        lv_obj_set_style_text_color(self->_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        self->show_networks_panel();
        return;
    }
    self->open_password_entry(row.ssid);
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
