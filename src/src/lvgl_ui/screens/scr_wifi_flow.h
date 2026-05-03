#ifndef SCR_WIFI_FLOW_H
#define SCR_WIFI_FLOW_H

#include "../lv_screen.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

// Wi-Fi 3A: LVGL service shell (RebootRequired) — Recovery Home + Available Networks; scan only, no connect.
// Wi-Fi 3A: сервисный shell Wi‑Fi (RebootRequired) — Home + Networks; только scan, без connect.

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

    void rebuild_saved_list();
    void rebuild_scan_list();
    void show_home_panel();
    void show_networks_panel();
    void start_scan_from_user();

    lv_obj_t*   _screen = nullptr;
    lv_obj_t*   _panel_home = nullptr;
    lv_obj_t*   _panel_net = nullptr;
    lv_obj_t*   _hdr_home = nullptr;
    lv_obj_t*   _sub_home = nullptr;
    lv_obj_t*   _list_saved = nullptr;
    lv_obj_t*   _hdr_net = nullptr;
    lv_obj_t*   _lbl_net_status = nullptr;
    lv_obj_t*   _list_scan = nullptr;
    lv_obj_t*   _btn_scan = nullptr;
    lv_obj_t*   _btn_rescan = nullptr;
    lv_timer_t* _poll_timer = nullptr;
    uint32_t    _last_results_seq = 0;
    bool        _await_scan_ui = false;
    bool        _home_visible = true;
};

} // namespace lvgl_ui

#endif
