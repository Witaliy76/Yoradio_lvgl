#ifndef SCR_WIFI_FLOW_H
#define SCR_WIFI_FLOW_H

#include "../lv_screen.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

// Wi-Fi 3A–6A: LVGL service shell — Home, Networks, PasswordEntry (4B connect + 5F status lock + 6A save/reboot).
// Wi-Fi 3A–6A: сервисный shell — Home, Networks, PasswordEntry (connect + save + reboot).

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
    static void on_btn_try_stub(lv_event_t* e);
    static void on_btn_hotspot_stub(lv_event_t* e);
    static void on_btn_back_home(lv_event_t* e);
    static void on_btn_back_net(lv_event_t* e);
    static void on_btn_rescan(lv_event_t* e);
    static void on_btn_cancel_scan(lv_event_t* e);
    static void on_scan_row_click(lv_event_t* e);
    static void on_btn_back_pass(lv_event_t* e);
    static void on_btn_connect(lv_event_t* e);
    static void on_ta_password_changed(lv_event_t* e);
    static void on_keyboard_event(lv_event_t* e);

    void rebuild_saved_list();
    void rebuild_scan_list();
    void show_home_panel();
    void sync_home_boot_failure_ui();
    void show_networks_panel();
    void show_password_panel();
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
    static void on_reboot_timer(lv_timer_t* t);

    lv_obj_t* _screen = nullptr;

    lv_obj_t* _panel_home = nullptr;
    lv_obj_t* _panel_net  = nullptr;
    lv_obj_t* _panel_pass = nullptr;

    lv_obj_t* _hdr_home = nullptr;
    lv_obj_t* _sub_home = nullptr;
    lv_obj_t* _list_saved = nullptr;

    lv_obj_t* _hdr_net = nullptr;
    lv_obj_t* _lbl_net_status = nullptr;
    lv_obj_t* _list_scan = nullptr;

    lv_obj_t* _hdr_pass = nullptr;
    lv_obj_t* _lbl_pass_ssid = nullptr;
    lv_obj_t* _lbl_pass_hint = nullptr;
    lv_obj_t* _ta_password = nullptr;
    lv_obj_t* _lbl_pass_status = nullptr;
    lv_obj_t* _btn_connect = nullptr;
    lv_obj_t* _btn_back_pass = nullptr;
    lv_obj_t* _kbd = nullptr;

    lv_obj_t* _btn_scan = nullptr;
    lv_obj_t* _btn_back_home = nullptr;
    lv_obj_t* _btn_rescan = nullptr;

    lv_timer_t* _poll_timer   = nullptr;
    lv_timer_t* _reboot_timer = nullptr;
    uint32_t    _last_results_seq = 0;
    bool        _await_scan_ui    = false;
    bool        _await_connect_ui = false;
    // Wi-Fi 5F: terminal connect result must not be overwritten by TA validation / итог connect не затирается валидацией.
    bool        _pass_status_terminal          = false;
    bool        _skip_next_ta_pass_status_sync = false;
    // Wi-Fi 6A: saving in progress; block UI edits until reboot / идёт сохранение, блок ввода до reboot.
    bool        _saving_in_progress = false;
    bool        _home_visible       = true;
    bool        _entered_from_boot_failure = false;

    // Ephemeral secrets — cleared on Back/exit; never logged / не в лог, не в snapshot.
    char _selectedSsid[33]{};
    char _passwordScratch[40]{};
    // Wi-Fi 6A: candidate password kept for post-Success persist only / кандидат для сохранения после Success.
    char _connectCandidatePass[40]{};
};

} // namespace lvgl_ui

#endif
