/*
 * LvglWifiFlowScreen — Wi-Fi 3A service shell (PageChain RebootRequired, not carousel page).
 * Recovery Home + Available Networks; scan via wifi_ops_adapter; no connect / no keyboard.
 * Shell Wi‑Fi 3A: RebootRequired; Home + Networks; scan через ops; без connect/клавиатуры.
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

void LvglWifiFlowScreen::show_home_panel() {
    _home_visible = true;
    if (_panel_home) lv_obj_clear_flag(_panel_home, LV_OBJ_FLAG_HIDDEN);
    if (_panel_net) lv_obj_add_flag(_panel_net, LV_OBJ_FLAG_HIDDEN);
}

void LvglWifiFlowScreen::show_networks_panel() {
    _home_visible = false;
    if (_panel_net) lv_obj_clear_flag(_panel_net, LV_OBJ_FLAG_HIDDEN);
    if (_panel_home) lv_obj_add_flag(_panel_home, LV_OBJ_FLAG_HIDDEN);
}

void LvglWifiFlowScreen::start_scan_from_user() {
    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) &&
        (cur.phase == WifiOpsPhase::Scanning || (cur.busy && cur.currentOp == WifiOpsOp::Scan))) {
        const YoRadioPalette& pal = yoradio_palette();
        lv_label_set_text(_lbl_net_status, "Scan in progress… / уже идёт");
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
    lv_label_set_text(_lbl_net_status, "Starting scan… / запуск…");
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
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
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
    if (!_panel_home || !_panel_net) return;
    lv_obj_set_width(_panel_home, LV_PCT(100));
    lv_obj_set_flex_grow(_panel_home, 1);
    lv_obj_set_width(_panel_net, LV_PCT(100));
    lv_obj_set_flex_grow(_panel_net, 1);
    lv_obj_set_flex_flow(_panel_home, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(_panel_net, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_panel_home, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel_net, 8, LV_PART_MAIN);
    style_service_panel(_panel_home, pal);
    style_service_panel(_panel_net, pal);

    _hdr_home = lv_label_create(_panel_home);
    lv_label_set_text(_hdr_home, "Wi-Fi Setup");
    lv_obj_set_style_text_color(_hdr_home, pal.text_primary, LV_PART_MAIN);
    if (LV_ACTIVE_PROFILE.font_header) {
        lv_obj_set_style_text_font(_hdr_home, static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_header), LV_PART_MAIN);
    }

    _sub_home = lv_label_create(_panel_home);
    lv_label_set_text(_sub_home, "Saved networks (read-only, up to 5)");
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
    add_footer_button(rowh, "Back", pal, on_btn_back_home, this);

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

    show_home_panel();
    rebuild_saved_list();
}

void LvglWifiFlowScreen::enter() {
    (void)wifiOpsInit();
    _last_results_seq = 0;
    _await_scan_ui    = false;
    rebuild_saved_list();
    if (!_poll_timer) {
        _poll_timer = lv_timer_create(on_poll_timer, kPollMs, this);
    }
}

void LvglWifiFlowScreen::update() {}

void LvglWifiFlowScreen::exit() {
    if (_poll_timer) {
        lv_timer_del(_poll_timer);
        _poll_timer = nullptr;
    }
    wifiOpsCancel();
}

void LvglWifiFlowScreen::destroy() {
    exit();
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _panel_home = _panel_net = _hdr_home = _sub_home = _list_saved = nullptr;
    _hdr_net = _lbl_net_status = _list_scan = nullptr;
    _btn_scan = _btn_rescan = nullptr;
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

    if (snap.phase == WifiOpsPhase::Scanning || (snap.busy && snap.currentOp == WifiOpsOp::Scan)) {
        lv_label_set_text(_lbl_net_status, "Scanning…");
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
        if (_btn_scan) lv_obj_add_state(_btn_scan, LV_STATE_DISABLED);
        if (_btn_rescan) lv_obj_add_state(_btn_rescan, LV_STATE_DISABLED);
        return;
    }
    if (_btn_scan) lv_obj_clear_state(_btn_scan, LV_STATE_DISABLED);
    if (_btn_rescan) lv_obj_clear_state(_btn_rescan, LV_STATE_DISABLED);

    if (_await_scan_ui && snap.phase == WifiOpsPhase::Idle && !snap.busy) {
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
}

void LvglWifiFlowScreen::on_btn_scan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (self) self->start_scan_from_user();
}

void LvglWifiFlowScreen::on_btn_try_stub(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_lbl_net_status) return;
    const YoRadioPalette& pal = yoradio_palette();
    lv_label_set_text(self->_lbl_net_status, "Try saved: Wi-Fi 4+ / позже");
    lv_obj_set_style_text_color(self->_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
    self->show_networks_panel();
}

void LvglWifiFlowScreen::on_btn_hotspot_stub(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_lbl_net_status) return;
    const YoRadioPalette& pal = yoradio_palette();
    lv_label_set_text(self->_lbl_net_status, "Hotspot: later / позже");
    lv_obj_set_style_text_color(self->_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
    self->show_networks_panel();
}

void LvglWifiFlowScreen::on_btn_back_home(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (self) lvgl_ui::dismissWifiFlowReturnToPlayer();
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

void LvglWifiFlowScreen::on_scan_row_click(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* tgt = lv_event_get_target(e);
    lv_obj_t* o   = tgt;
    while (o && lv_obj_get_user_data(o) == nullptr) {
        o = lv_obj_get_parent(o);
    }
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!o || !self || !self->_lbl_net_status) return;
    const uintptr_t idx = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(o));
    WifiOpsScanRow  row{};
    if (!wifiOpsGetScanResult(static_cast<uint16_t>(idx), &row)) return;
    const YoRadioPalette& pal = yoradio_palette();
    const bool       is_open = (row.auth == WIFI_AUTH_OPEN);
    lv_label_set_text(self->_lbl_net_status,
                      is_open ? "Open: connect in Wi-Fi 4 / позже"
                               : "Password entry comes in Wi-Fi 4 / пароль — этап 4");
    lv_obj_set_style_text_color(self->_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
