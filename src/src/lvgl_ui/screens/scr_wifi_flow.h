#ifndef SCR_WIFI_FLOW_H
#define SCR_WIFI_FLOW_H

#include "../lv_screen.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

// Wi-Fi 3A–6D + S6V7A–S6V9C: LVGL service shell — S6V9C = strict Hotspot-only SoftAP (AP starts on Hotspot page, stops on Back via recoveryStopSoftAP).
// Wi-Fi 3A–6D + S6V7A–S6V9C: тот же shell; S6V9C — AP только при Hotspot page, гасится через recoveryStopSoftAP() при Back.

class LvglWifiFlowScreen final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void        create() override;
    void        enter() override;
    void        update() override;
    void        exit() override;
    void        destroy() override;
    lv_obj_t*   screen() override;

private:
    static void on_poll_timer(lv_timer_t* t);
    void        pollOpsSnapshot();
    static void on_btn_scan(lv_event_t* e);
    static void on_btn_hotspot(lv_event_t* e);
    static void on_btn_back_hotspot(lv_event_t* e);
    static void on_btn_back_home(lv_event_t* e);
    static void on_btn_back_net(lv_event_t* e);
    static void on_btn_rescan(lv_event_t* e);
    static void on_btn_cancel_scan(lv_event_t* e);
    static void on_scan_row_click(lv_event_t* e);
    static void on_btn_back_pass(lv_event_t* e);
    static void on_btn_connect(lv_event_t* e);
    static void on_ta_password_changed(lv_event_t* e);
    static void on_keyboard_event(lv_event_t* e);
    // Wi-Fi 6B: Saved Network panel callbacks / обработчики Saved Network panel.
    static void on_saved_row_click(lv_event_t* e);
    static void on_btn_saved_connect(lv_event_t* e);
    static void on_btn_saved_chpwd(lv_event_t* e);
    static void on_btn_saved_back(lv_event_t* e);
    // Wi-Fi 6C: Remove inline confirmation callbacks / обработчики inline-подтверждения Remove.
    static void on_btn_saved_remove(lv_event_t* e);
    static void on_btn_saved_yes(lv_event_t* e);
    static void on_btn_saved_no(lv_event_t* e);

    void rebuild_saved_list();
    void rebuild_scan_list();
    void show_home_panel();
    void sync_home_boot_failure_ui();
    void show_networks_panel();
    void show_password_panel();
    // Wi-Fi 6B: Saved Network panel navigation / навигация Saved Network panel.
    void show_saved_panel();
    // Wi-Fi S6V7A: LVGL Hotspot panel (softAP info); legacy _apScreen stays emergency fallback only.
    void show_hotspot_panel();
    void sync_hotspot_panel_labels();
    void clear_saved_state();
    void open_saved_network(uint8_t slot);
    void start_connect_from_saved();
    void handle_saved_connect_finished();
    void set_saved_panel_connecting_ui(bool connecting);
    void set_saved_panel_saving_ui();
    void start_scan_from_user();
    void clear_password_secrets();
    void clear_password_panel_state();
    void open_password_entry(const char* ssid_utf8);
    void sync_connect_button_enabled();
    void set_password_panel_connecting_ui(bool connecting);
    void set_password_panel_saving_ui();
    void handle_connect_finished();
    void handle_successful_connect_persist();
    void start_connect_from_user();
    // Wi-Fi S6V7B: open network (WIFI_AUTH_OPEN) connect from Networks panel / open network из Scan.
    void start_open_connect_from_user(const char* ssid);
    void handle_open_connect_finished();
    void handle_open_network_success_persist();
    void set_networks_panel_connecting_ui(bool connecting);
    void set_networks_panel_saving_ui();
    void cancel_open_connect_state();
    static void on_reboot_timer(lv_timer_t* t);

    void disarm_boot_idle_timer();
    // S6V9A/S6V9B: arm 60s idle→Hotspot when Recovery Home is the only visible panel (any entry path).
    // S6V9A/S6V9B: армируем 60s простоя на Home Recovery, пока виден только Home (любой вход).
    void arm_recovery_idle_if_home_only();
    bool recovery_idle_ops_block() const;
    void process_boot_idle_timer_tick();
    bool is_recovery_home_only_visible() const;

    lv_obj_t* _screen = nullptr;

    lv_obj_t* _panel_home  = nullptr;
    lv_obj_t* _panel_net   = nullptr;
    lv_obj_t* _panel_pass  = nullptr;
    // Wi-Fi 6B: fourth internal panel for saved network actions / четвёртая панель для действий с сохранённой сетью.
    lv_obj_t* _panel_saved = nullptr;
    // Wi-Fi S6V7A: fifth panel — read-only softAP instructions / пятая панель — только текст и Back.
    lv_obj_t* _panel_hotspot = nullptr;

    lv_obj_t* _hdr_home   = nullptr;
    lv_obj_t* _sub_home   = nullptr;
    // S6V9B: static auto-Hotspot notice label on Home (not on Hotspot button) / статичное уведомление на Home.
    lv_obj_t* _lbl_recovery_idle_countdown = nullptr;
    lv_obj_t* _list_saved = nullptr;

    lv_obj_t* _hdr_net        = nullptr;
    lv_obj_t* _lbl_net_status = nullptr;
    lv_obj_t* _list_scan      = nullptr;

    lv_obj_t* _hdr_pass        = nullptr;
    lv_obj_t* _lbl_pass_ssid   = nullptr;
    lv_obj_t* _lbl_pass_hint   = nullptr;
    lv_obj_t* _ta_password     = nullptr;
    lv_obj_t* _lbl_pass_status = nullptr;
    lv_obj_t* _btn_connect     = nullptr;
    lv_obj_t* _btn_back_pass   = nullptr;
    lv_obj_t* _kbd             = nullptr;

    // Wi-Fi 6B: Saved Network panel widgets / виджеты Saved Network panel.
    lv_obj_t* _lbl_saved_ssid    = nullptr;
    lv_obj_t* _lbl_saved_sub     = nullptr;
    lv_obj_t* _lbl_saved_status  = nullptr;
    lv_obj_t* _btn_saved_connect = nullptr;
    lv_obj_t* _btn_saved_chpwd   = nullptr;
    lv_obj_t* _btn_saved_back    = nullptr;
    // Wi-Fi 6C: two-row button layout (normal / confirm) / два ряда кнопок (нормальный / подтверждение).
    lv_obj_t* _row_saved_normal  = nullptr;
    lv_obj_t* _row_saved_confirm = nullptr;
    lv_obj_t* _btn_saved_remove  = nullptr;
    lv_obj_t* _btn_saved_yes     = nullptr;
    lv_obj_t* _btn_saved_no      = nullptr;

    lv_obj_t* _hdr_hotspot      = nullptr;
    lv_obj_t* _lbl_hotspot_ssid = nullptr;
    lv_obj_t* _lbl_hotspot_pwd  = nullptr;
    lv_obj_t* _lbl_hotspot_ip   = nullptr;
    lv_obj_t* _lbl_hotspot_help = nullptr;
    lv_obj_t* _btn_hotspot_back = nullptr;

    lv_obj_t* _btn_scan         = nullptr;
    lv_obj_t* _btn_hotspot      = nullptr;
    lv_obj_t* _btn_back_home    = nullptr;
    lv_obj_t* _btn_rescan       = nullptr;
    lv_obj_t* _btn_cancel_scan  = nullptr;
    lv_obj_t* _btn_back_net     = nullptr;

    lv_timer_t* _poll_timer   = nullptr;
    lv_timer_t* _reboot_timer = nullptr;
    uint32_t    _last_results_seq = 0;
    bool        _await_scan_ui           = false;
    bool        _await_connect_ui        = false;
    // Wi-Fi S6V7B: open-network connect from Scan (password "") / коннект к open AP из списка скана.
    bool        _await_open_connect_ui  = false;
    bool        _open_status_terminal    = false;
    // Wi-Fi 5F: terminal connect result must not be overwritten by TA validation / итог connect не затирается валидацией.
    bool        _pass_status_terminal          = false;
    bool        _skip_next_ta_pass_status_sync = false;
    // Wi-Fi 6A: saving in progress; block UI edits until reboot / идёт сохранение, блок ввода до reboot.
    bool        _saving_in_progress = false;
    bool        _home_visible       = true;
    bool        _entered_from_boot_failure = false;
    // S6V8A: entered from runtime disconnect escalation (Back visible; subtitle differs from manual).
    // S6V8A: вход через runtime disconnect escalation (Back виден; подзаголовок отличается от manual).
    bool        _entered_from_runtime_disconnect = false;
    // S6V7A/S6V9B: Recovery Home idle → Hotspot panel after WIFI_RECOVERY_IDLE_TO_AP_TIMEOUT_MS.
    // S6V9B: arms on any Recovery Home idle / армируется на любом простое Home.
    bool        _boot_idle_armed         = false;
    uint32_t    _boot_idle_deadline_ms   = 0;
    // Wi-Fi 6B: saved network panel state / состояние Saved Network panel.
    uint8_t     _selectedSavedSlot      = 255;
    bool        _await_saved_connect_ui = false;
    bool        _saved_status_terminal  = false;
    // Wi-Fi 6B: Password opened from Saved panel (Change password); Back returns to Saved.
    // Wi-Fi 6B: пароль открыт из Saved panel; Back возвращает в Saved.
    bool        _password_from_saved    = false;
    // Wi-Fi 6C: inline Remove confirmation active / активно inline-подтверждение удаления.
    bool        _remove_confirm_pending = false;

    // Ephemeral secrets — cleared on Back/exit; never logged / не в лог, не в snapshot.
    char _selectedSsid[33]{};
    char _passwordScratch[40]{};
    // Wi-Fi 6A: candidate password kept for post-Success persist only / кандидат для сохранения после Success.
    char _connectCandidatePass[40]{};
    // Wi-Fi 6B: selected saved network SSID copy — not a pointer into store / копия SSID, не указатель в store.
    char _selectedSavedSsid[33]{};
    // Wi-Fi S6V7B: SSID for open-network connect (copy from scan row) / SSID для open connect.
    char _selectedOpenSsid[33]{};
};

} // namespace lvgl_ui

#endif
