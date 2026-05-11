/*
 * LvglWifiFlowScreen — Wi-Fi 3A–S6V9C service shell (PageChain RebootRequired, not carousel page).
 * Home + Networks + Password + SavedNetworkPanel (6B/6C) + Hotspot (S6V7A) + open network from Scan (S6V7B).
 * 4B: connect; 5F: status lock; 6A: save+reboot; 6B: saved panel; 6C: Remove; 6D: glitch helpers; S6V7A: Hotspot/idle AP.
 * S6V7B: open row; S6V9B: static notice; S6V9C: strict Hotspot-only SoftAP — AP starts on Hotspot page, stops on Back; S6V9H: NoNetwork UX text.
 * S6V11B-stylemem: shared lv_style_t for footer/list rows — cuts local-style heap pressure (LV_MEM_SIZE 48K).
 */

#include "scr_wifi_flow.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include <cstdio>
#include <cstring>

#include "../../core/display.h"
#include "../../core/network.h"
#include "../../core/wifi_credentials_store.h"
#include "../../core/wifi_ops_adapter.h"
#include "../fonts/lv_fonts.h"
#include "../wifi_flow_glyph_utf8.h"
#include "../lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

#include <WiFi.h>

namespace lvgl_ui {

namespace {

constexpr uint32_t kPollMs = 200;
// Wi-Fi S6V7A/S6V9B: Recovery Home idle — auto-open Hotspot panel after timeout (same 60s constant; AP backend unchanged).
// Wi-Fi S6V7A/S6V9B: простой на Recovery Home — авто Hotspot panel (тот же 60s; backend AP не трогаем).
constexpr uint32_t WIFI_RECOVERY_IDLE_TO_AP_TIMEOUT_MS = 60000U;
// Legacy credential field is 40 bytes; allow 39 typed chars + room / legacy поле 40 байт, ввод ≤39.
constexpr uint32_t kPasswordMaxInputChars = 39U;
constexpr size_t   kMinPasswordLen        = 8U;

// S6V9H: NoNetwork (e.g. phone hotspot not beaconing yet) — not wrong-password; prompt Rescan. / Не ошибка пароля.
static const char kWifiOpsNoNetworkUserMsg[] = "Network not found. Wait, then Rescan.";

// Wi-Fi S6V6D: compile-time glitch diagnostics (default off — zero Serial / minimal overhead).
// Wi-Fi S6V6D: диагностика глитча только по флагу (по умолчанию выкл).
#ifndef WIFI_FLOW_DIAG_GLITCH
#define WIFI_FLOW_DIAG_GLITCH 0
#endif

// Optional slot for counter attribution / слот для счётчиков (только при WIFI_FLOW_DIAG_GLITCH).
enum class WifiFlowDiagTextSlot : uint8_t { None = 0, SubHome, NetStatus, SavedStatus, PassStatus };
enum class WifiFlowDiagBtnSlot : uint8_t {
    None = 0,
    Scan,
    Rescan,
    PassConnect,
    SavedConnect,
    SavedChpwd,
    SavedRemove,
    SavedBack,
    PasswordTa,
    PasswordKbd,
    PasswordBack
};

#if WIFI_FLOW_DIAG_GLITCH
#include <Arduino.h>

static uint32_t g_diag_show_home_panel_calls             = 0;
static uint32_t g_diag_sync_home_boot_failure_ui_calls  = 0;
static uint32_t g_diag_rebuild_saved_list_calls         = 0;
static uint32_t g_diag_txt_sub_home_changes             = 0;
static uint32_t g_diag_txt_saved_status_changes         = 0;
static uint32_t g_diag_txt_net_status_changes           = 0;
static uint32_t g_diag_txt_pass_status_changes         = 0;
static uint32_t g_diag_state_scan_changes               = 0;
static uint32_t g_diag_state_rescan_changes             = 0;
static uint32_t g_diag_state_saved_btns_changes         = 0; // Connect / Chg pwd / Remove / Back
static uint32_t g_diag_state_pass_btns_changes          = 0; // TA / Kbd / Connect / Back pass
static uint32_t g_diag_last_summary_ms                 = 0;

static void wifi_flow_diag_bump_text_slot(WifiFlowDiagTextSlot s) {
    switch (s) {
    case WifiFlowDiagTextSlot::SubHome:
        ++g_diag_txt_sub_home_changes;
        break;
    case WifiFlowDiagTextSlot::NetStatus:
        ++g_diag_txt_net_status_changes;
        break;
    case WifiFlowDiagTextSlot::SavedStatus:
        ++g_diag_txt_saved_status_changes;
        break;
    case WifiFlowDiagTextSlot::PassStatus:
        ++g_diag_txt_pass_status_changes;
        break;
    default:
        break;
    }
}

static void wifi_flow_diag_bump_btn_slot(WifiFlowDiagBtnSlot who) {
    switch (who) {
    case WifiFlowDiagBtnSlot::Scan:
        ++g_diag_state_scan_changes;
        break;
    case WifiFlowDiagBtnSlot::Rescan:
        ++g_diag_state_rescan_changes;
        break;
    case WifiFlowDiagBtnSlot::SavedConnect:
    case WifiFlowDiagBtnSlot::SavedChpwd:
    case WifiFlowDiagBtnSlot::SavedRemove:
    case WifiFlowDiagBtnSlot::SavedBack:
        ++g_diag_state_saved_btns_changes;
        break;
    case WifiFlowDiagBtnSlot::PassConnect:
    case WifiFlowDiagBtnSlot::PasswordTa:
    case WifiFlowDiagBtnSlot::PasswordKbd:
    case WifiFlowDiagBtnSlot::PasswordBack:
        ++g_diag_state_pass_btns_changes;
        break;
    default:
        break;
    }
}

static void wifi_flow_diag_reset_for_enter() {
    g_diag_show_home_panel_calls            = 0;
    g_diag_sync_home_boot_failure_ui_calls  = 0;
    g_diag_rebuild_saved_list_calls         = 0;
    g_diag_txt_sub_home_changes             = 0;
    g_diag_txt_saved_status_changes         = 0;
    g_diag_txt_net_status_changes           = 0;
    g_diag_txt_pass_status_changes          = 0;
    g_diag_state_scan_changes               = 0;
    g_diag_state_rescan_changes             = 0;
    g_diag_state_saved_btns_changes         = 0;
    g_diag_state_pass_btns_changes          = 0;
    g_diag_last_summary_ms                  = millis();
}

static void wifi_flow_diag_print_summary(const char* tag) {
    Serial.printf(
        "[WiFiFlowDiag] %s: showHome=%lu syncHome=%lu rebuild=%lu txtSub=%lu txtSaved=%lu txtNet=%lu txtPass=%lu "
        "stScan=%lu stRescan=%lu stSavedBtns=%lu stPassBtns=%lu\n",
        tag,
        static_cast<unsigned long>(g_diag_show_home_panel_calls),
        static_cast<unsigned long>(g_diag_sync_home_boot_failure_ui_calls),
        static_cast<unsigned long>(g_diag_rebuild_saved_list_calls),
        static_cast<unsigned long>(g_diag_txt_sub_home_changes),
        static_cast<unsigned long>(g_diag_txt_saved_status_changes),
        static_cast<unsigned long>(g_diag_txt_net_status_changes),
        static_cast<unsigned long>(g_diag_txt_pass_status_changes),
        static_cast<unsigned long>(g_diag_state_scan_changes),
        static_cast<unsigned long>(g_diag_state_rescan_changes),
        static_cast<unsigned long>(g_diag_state_saved_btns_changes),
        static_cast<unsigned long>(g_diag_state_pass_btns_changes));
}

static void wifi_flow_diag_on_enter_flow() {
    wifi_flow_diag_reset_for_enter();
    Serial.println("[WiFiFlowDiag] enter Wi-Fi flow (counters reset)");
}

static void wifi_flow_diag_on_exit_flow() {
    wifi_flow_diag_print_summary("exit");
}

static void wifi_flow_diag_maybe_periodic_summary() {
    const uint32_t now = millis();
    if (now - g_diag_last_summary_ms < 5000U) return;
    g_diag_last_summary_ms = now;
    wifi_flow_diag_print_summary("periodic5s");
}
#endif // WIFI_FLOW_DIAG_GLITCH

// Only call lv_label_set_text when different — reduces full_refresh invalidation churn / меньше лишних invalidate.
static bool wifi_flow_set_text_if_changed(lv_obj_t* label, const char* text, WifiFlowDiagTextSlot slot = WifiFlowDiagTextSlot::None) {
    if (!label || !text) return false;
    const char* cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) return false;
    lv_label_set_text(label, text);
#if WIFI_FLOW_DIAG_GLITCH
    if (slot != WifiFlowDiagTextSlot::None) {
        wifi_flow_diag_bump_text_slot(slot);
    }
#endif
    return true;
}

// Only add/clear LV_STATE when needed / меняем состояние только при реальном отличии.
static bool wifi_flow_set_state_if_changed(lv_obj_t* obj, lv_state_t state, bool enabled, WifiFlowDiagBtnSlot who = WifiFlowDiagBtnSlot::None) {
    if (!obj) return false;
    const bool has = lv_obj_has_state(obj, state);
    if (has == enabled) return false;
    if (enabled) {
        lv_obj_add_state(obj, state);
    } else {
        lv_obj_clear_state(obj, state);
    }
#if WIFI_FLOW_DIAG_GLITCH
    if (who != WifiFlowDiagBtnSlot::None) {
        wifi_flow_diag_bump_btn_slot(who);
    }
#endif
    return true;
}

void style_service_panel(lv_obj_t* panel, const YoRadioPalette& pal) {
    if (!panel) return;
    lv_obj_set_style_bg_color(panel, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, pal.panel_border, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    // S6V11A-icons3: smaller radius → less rounded-rect mask pressure / меньше радиус — меньше lv_draw_mask OOM
    lv_obj_set_style_radius(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding), LV_PART_MAIN);
}

void wifi_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

const void* wifi_title_font_slot() {
    return (LV_ACTIVE_PROFILE.width <= 320u)
               ? reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr)
               : reinterpret_cast<const void*>(&lv_font_yora_montserrat_20_cyr);
}

const void* wifi_body_font_slot() {
    if (LV_ACTIVE_PROFILE.font_normal) return LV_ACTIVE_PROFILE.font_normal;
    return reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
}

// S6V11A-fix2: status/help lines — min 16px on 480+ (service-flow readability) / читаемость статуса
const void* wifi_status_font_slot() {
    if (LV_ACTIVE_PROFILE.width <= 320u) {
        return reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr);
    }
    return reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
}

// S6V11A-fix2: list rows on 480+ — 18px; narrow keeps profile body / строки списка на широких
const void* wifi_list_row_font_slot() {
    if (LV_ACTIVE_PROFILE.width <= 320u) {
        return wifi_body_font_slot();
    }
    return reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr);
}

// S6V11B: one header icon only (no per-row icons) / один акцентный icon в заголовке
static const lv_font_t* wifi_header_icon_font() {
    return (LV_ACTIVE_PROFILE.width <= 320u)
               ? &lv_font_yora_wifi_flow_icons_24
               : &lv_font_yora_wifi_flow_icons_36;
}

static lv_obj_t* wifi_create_header_row(lv_obj_t* parent) {
    if (!parent) return nullptr;
    lv_obj_t* row = lv_obj_create(parent);
    if (!row) return nullptr;
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

enum class WifiBtnRole : uint8_t {
    Primary = 0,
    Secondary,
    Ghost,
    Destructive,
};

constexpr lv_style_selector_t wifi_sel(lv_part_t part, lv_state_t state) {
    return static_cast<lv_style_selector_t>(part) | static_cast<lv_style_selector_t>(state);
}

// S6V11B-stylemem: one init per Wi-Fi Flow screen lifetime — shared props avoid per-button local_style heap.
// S6V11B-stylemem: один init на жизненный цикл экрана — общие lv_style_t вместо десятков local props на кнопку.
static bool                     s_wf_flow_styles_ready = false;
static lv_style_t               s_wf_btn_base;
static lv_style_t               s_wf_btn_pri_d;
static lv_style_t               s_wf_btn_pri_p;
static lv_style_t               s_wf_btn_sec_d;
static lv_style_t               s_wf_btn_sec_p;
static lv_style_t               s_wf_btn_gho_d;
static lv_style_t               s_wf_btn_gho_p;
static lv_style_t               s_wf_btn_des_d;
static lv_style_t               s_wf_btn_des_p;
static lv_style_t               s_wf_btn_dis;
static lv_style_t               s_wf_lr_base;
static lv_style_t               s_wf_lr_row_d;
static lv_style_t               s_wf_lr_row_p;
static lv_style_t               s_wf_lr_empty;

static void wifi_flow_style_drop() {
    if (!s_wf_flow_styles_ready) return;
    lv_style_reset(&s_wf_btn_base);
    lv_style_reset(&s_wf_btn_pri_d);
    lv_style_reset(&s_wf_btn_pri_p);
    lv_style_reset(&s_wf_btn_sec_d);
    lv_style_reset(&s_wf_btn_sec_p);
    lv_style_reset(&s_wf_btn_gho_d);
    lv_style_reset(&s_wf_btn_gho_p);
    lv_style_reset(&s_wf_btn_des_d);
    lv_style_reset(&s_wf_btn_des_p);
    lv_style_reset(&s_wf_btn_dis);
    lv_style_reset(&s_wf_lr_base);
    lv_style_reset(&s_wf_lr_row_d);
    lv_style_reset(&s_wf_lr_row_p);
    lv_style_reset(&s_wf_lr_empty);
    s_wf_flow_styles_ready = false;
}

static void wifi_flow_style_ensure(const YoRadioPalette& pal) {
    if (s_wf_flow_styles_ready) return;

    const lv_coord_t btn_mh = (LV_ACTIVE_PROFILE.width <= 320u) ? 46 : 50;
    const lv_coord_t lr_mh  = (LV_ACTIVE_PROFILE.width <= 320u) ? 46 : 48;
    const lv_font_t* body   = static_cast<const lv_font_t*>(wifi_body_font_slot());
    const lv_font_t* lr_f   = static_cast<const lv_font_t*>(wifi_list_row_font_slot());

    lv_style_init(&s_wf_btn_base);
    lv_style_set_min_height(&s_wf_btn_base, btn_mh);
    lv_style_set_radius(&s_wf_btn_base, 6);
    lv_style_set_border_width(&s_wf_btn_base, 1);
    lv_style_set_pad_ver(&s_wf_btn_base, 8);
    lv_style_set_pad_hor(&s_wf_btn_base, 10);
    lv_style_set_text_font(&s_wf_btn_base, body);

    lv_style_init(&s_wf_btn_pri_d);
    lv_style_set_bg_color(&s_wf_btn_pri_d, pal.accent_soft);
    lv_style_set_bg_opa(&s_wf_btn_pri_d, LV_OPA_COVER);
    lv_style_set_border_color(&s_wf_btn_pri_d, pal.accent);
    lv_style_set_text_color(&s_wf_btn_pri_d, pal.text_primary);
    lv_style_init(&s_wf_btn_pri_p);
    lv_style_set_bg_color(&s_wf_btn_pri_p, pal.accent);
    lv_style_set_bg_opa(&s_wf_btn_pri_p, LV_OPA_70);
    lv_style_set_border_color(&s_wf_btn_pri_p, pal.accent);
    lv_style_set_text_color(&s_wf_btn_pri_p, pal.text_primary);

    lv_style_init(&s_wf_btn_sec_d);
    lv_style_set_bg_color(&s_wf_btn_sec_d, pal.panel_background);
    lv_style_set_bg_opa(&s_wf_btn_sec_d, LV_OPA_70);
    lv_style_set_border_color(&s_wf_btn_sec_d, pal.panel_border);
    lv_style_set_text_color(&s_wf_btn_sec_d, pal.text_primary);
    lv_style_init(&s_wf_btn_sec_p);
    lv_style_set_bg_color(&s_wf_btn_sec_p, pal.accent_soft);
    lv_style_set_bg_opa(&s_wf_btn_sec_p, LV_OPA_30);
    lv_style_set_border_color(&s_wf_btn_sec_p, pal.accent);
    lv_style_set_text_color(&s_wf_btn_sec_p, pal.text_primary);

    lv_style_init(&s_wf_btn_gho_d);
    lv_style_set_bg_opa(&s_wf_btn_gho_d, LV_OPA_TRANSP);
    lv_style_set_border_color(&s_wf_btn_gho_d, pal.divider);
    lv_style_set_text_color(&s_wf_btn_gho_d, pal.text_secondary);
    lv_style_init(&s_wf_btn_gho_p);
    lv_style_set_bg_color(&s_wf_btn_gho_p, pal.panel_background);
    lv_style_set_bg_opa(&s_wf_btn_gho_p, LV_OPA_20);
    lv_style_set_border_color(&s_wf_btn_gho_p, pal.panel_border);
    lv_style_set_text_color(&s_wf_btn_gho_p, pal.text_primary);

    lv_style_init(&s_wf_btn_des_d);
    lv_style_set_bg_color(&s_wf_btn_des_d, pal.panel_background);
    lv_style_set_bg_opa(&s_wf_btn_des_d, LV_OPA_70);
    lv_style_set_border_color(&s_wf_btn_des_d, pal.live_indicator_text);
    lv_style_set_text_color(&s_wf_btn_des_d, pal.live_indicator_text);
    lv_style_init(&s_wf_btn_des_p);
    lv_style_set_bg_color(&s_wf_btn_des_p, pal.live_indicator_text);
    lv_style_set_bg_opa(&s_wf_btn_des_p, LV_OPA_20);
    lv_style_set_border_color(&s_wf_btn_des_p, pal.live_indicator_text);
    lv_style_set_text_color(&s_wf_btn_des_p, pal.text_primary);

    lv_style_init(&s_wf_btn_dis);
    lv_style_set_bg_color(&s_wf_btn_dis, pal.panel_background);
    lv_style_set_bg_opa(&s_wf_btn_dis, LV_OPA_50);
    lv_style_set_border_color(&s_wf_btn_dis, pal.divider);
    lv_style_set_text_color(&s_wf_btn_dis, pal.text_secondary);

    lv_style_init(&s_wf_lr_base);
    lv_style_set_min_height(&s_wf_lr_base, lr_mh);
    lv_style_set_radius(&s_wf_lr_base, 4);
    lv_style_set_border_width(&s_wf_lr_base, 1);
    lv_style_set_border_color(&s_wf_lr_base, pal.panel_border);
    lv_style_set_text_font(&s_wf_lr_base, lr_f);

    lv_style_init(&s_wf_lr_row_d);
    lv_style_set_bg_color(&s_wf_lr_row_d, pal.panel_background);
    lv_style_set_bg_opa(&s_wf_lr_row_d, LV_OPA_40);
    lv_style_set_text_color(&s_wf_lr_row_d, pal.text_primary);

    lv_style_init(&s_wf_lr_row_p);
    lv_style_set_bg_color(&s_wf_lr_row_p, pal.accent_soft);
    lv_style_set_bg_opa(&s_wf_lr_row_p, LV_OPA_30);
    lv_style_set_border_color(&s_wf_lr_row_p, pal.accent);
    lv_style_set_text_color(&s_wf_lr_row_p, pal.text_primary);

    lv_style_init(&s_wf_lr_empty);
    lv_style_set_bg_color(&s_wf_lr_empty, pal.panel_background);
    lv_style_set_bg_opa(&s_wf_lr_empty, LV_OPA_40);
    lv_style_set_text_color(&s_wf_lr_empty, pal.text_secondary);

    s_wf_flow_styles_ready = true;
}

static void wifi_apply_list_row_normal(lv_obj_t* btn) {
    if (!btn || !s_wf_flow_styles_ready) return;
    lv_obj_add_style(btn, &s_wf_lr_base, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_wf_lr_row_d, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(btn, &s_wf_lr_row_p, LV_PART_MAIN | LV_STATE_PRESSED);
}

static void wifi_apply_list_row_empty(lv_obj_t* row) {
    if (!row || !s_wf_flow_styles_ready) return;
    lv_obj_add_style(row, &s_wf_lr_base, LV_PART_MAIN);
    lv_obj_add_style(row, &s_wf_lr_empty, LV_PART_MAIN);
}

void wifi_apply_button_role(lv_obj_t* btn, const YoRadioPalette& /*pal*/, WifiBtnRole role) {
    if (!btn || !s_wf_flow_styles_ready) return;

    lv_obj_add_style(btn, &s_wf_btn_base, LV_PART_MAIN);
    switch (role) {
        case WifiBtnRole::Primary:
            lv_obj_add_style(btn, &s_wf_btn_pri_d, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_style(btn, &s_wf_btn_pri_p, LV_PART_MAIN | LV_STATE_PRESSED);
            break;
        case WifiBtnRole::Secondary:
            lv_obj_add_style(btn, &s_wf_btn_sec_d, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_style(btn, &s_wf_btn_sec_p, LV_PART_MAIN | LV_STATE_PRESSED);
            break;
        case WifiBtnRole::Ghost:
            lv_obj_add_style(btn, &s_wf_btn_gho_d, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_style(btn, &s_wf_btn_gho_p, LV_PART_MAIN | LV_STATE_PRESSED);
            break;
        case WifiBtnRole::Destructive:
            lv_obj_add_style(btn, &s_wf_btn_des_d, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_style(btn, &s_wf_btn_des_p, LV_PART_MAIN | LV_STATE_PRESSED);
            break;
    }
    lv_obj_add_style(btn, &s_wf_btn_dis, LV_PART_MAIN | LV_STATE_DISABLED);
}

lv_obj_t* add_footer_button(lv_obj_t* row, const char* txt, const YoRadioPalette& pal, lv_event_cb_t cb, void* user) {
    (void)pal;
    if (!row) return nullptr;
    lv_obj_t* b = lv_btn_create(row);
    if (!b) return nullptr;
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    // min_height + colors: caller must wifi_apply_button_role(...) — avoids stacking Secondary+Primary styles.
    // Высота/цвета: вызывающий обязан вызвать wifi_apply_button_role — без двойного Secondary+Primary.
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
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
#if WIFI_FLOW_DIAG_GLITCH
    ++g_diag_show_home_panel_calls;
#endif
    cancel_open_connect_state();
    // Wi-Fi 6B: clear saved state before returning Home / сброс Saved panel при возврате на Home.
    clear_saved_state();
    clear_password_panel_state();
    _home_visible = true;
    if (_panel_home)  lv_obj_clear_flag(_panel_home,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_net)   lv_obj_add_flag(_panel_net,   LV_OBJ_FLAG_HIDDEN);
    if (_panel_pass)  lv_obj_add_flag(_panel_pass,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_saved) lv_obj_add_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);
    if (_panel_hotspot) lv_obj_add_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN);
    sync_home_boot_failure_ui();
    arm_recovery_idle_if_home_only();
}

bool LvglWifiFlowScreen::is_recovery_home_only_visible() const {
    if (!_panel_home || lv_obj_has_flag(_panel_home, LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_net && !lv_obj_has_flag(_panel_net, LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_pass && !lv_obj_has_flag(_panel_pass, LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_saved && !lv_obj_has_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_hotspot && !lv_obj_has_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN)) return false;
    return true;
}

void LvglWifiFlowScreen::disarm_boot_idle_timer() {
    _boot_idle_armed       = false;
    _boot_idle_deadline_ms = 0;
    if (_lbl_recovery_idle_countdown) {
        wifi_flow_set_text_if_changed(_lbl_recovery_idle_countdown, "", WifiFlowDiagTextSlot::None);
    }
}

bool LvglWifiFlowScreen::recovery_idle_ops_block() const {
    return _await_scan_ui || _await_connect_ui || _await_saved_connect_ui || _await_open_connect_ui || _saving_in_progress;
}

void LvglWifiFlowScreen::arm_recovery_idle_if_home_only() {
    if (!is_recovery_home_only_visible() || recovery_idle_ops_block()) {
        disarm_boot_idle_timer();
        return;
    }
    _boot_idle_deadline_ms = millis() + WIFI_RECOVERY_IDLE_TO_AP_TIMEOUT_MS;
    _boot_idle_armed       = true;
    // S6V9B: one LVGL write on arm only — avoids per-second full_refresh churn / один раз при арме, без 1 Hz.
    if (_lbl_recovery_idle_countdown) {
        wifi_flow_set_text_if_changed(
            _lbl_recovery_idle_countdown, "Hotspot starts automatically in 60s", WifiFlowDiagTextSlot::None);
    }
}

void LvglWifiFlowScreen::process_boot_idle_timer_tick() {
    if (!_boot_idle_armed) return;
    if (!is_recovery_home_only_visible()) {
        disarm_boot_idle_timer();
        return;
    }
    if (recovery_idle_ops_block()) {
        disarm_boot_idle_timer();
        return;
    }
    const uint32_t now = millis();
    const int32_t  remaining_ms = static_cast<int32_t>(_boot_idle_deadline_ms - now);
    if (remaining_ms <= 0) {
        disarm_boot_idle_timer();
        Serial.println("[Network] Wi-Fi Recovery idle timeout; opening Hotspot panel");
        show_hotspot_panel();
    }
}

void LvglWifiFlowScreen::sync_hotspot_panel_labels() {
    if (!_lbl_hotspot_ssid || !_lbl_hotspot_pwd || !_lbl_hotspot_ip || !_lbl_hotspot_help) return;
    wifi_flow_set_text_if_changed(_lbl_hotspot_ssid, "SSID: yoRadioAP", WifiFlowDiagTextSlot::None);
    wifi_flow_set_text_if_changed(_lbl_hotspot_pwd, "Password: open", WifiFlowDiagTextSlot::None);
    const IPAddress ap_ip = WiFi.softAPIP();
    char            ip_line[56];
    if (static_cast<uint32_t>(ap_ip) != 0U) {
        snprintf(ip_line, sizeof(ip_line), "IP: %s", ap_ip.toString().c_str());
    } else {
        snprintf(ip_line, sizeof(ip_line), "IP: 192.168.4.1 (expected if AP starting)");
    }
    wifi_flow_set_text_if_changed(_lbl_hotspot_ip, ip_line, WifiFlowDiagTextSlot::None);
    wifi_flow_set_text_if_changed(
        _lbl_hotspot_help,
        "Connect your phone or computer to this network.\nThen open http://192.168.4.1",
        WifiFlowDiagTextSlot::None);
}

void LvglWifiFlowScreen::show_hotspot_panel() {
    disarm_boot_idle_timer();
    cancel_open_connect_state();
    network.recoveryEnsureSoftAP();
    sync_hotspot_panel_labels();
    _home_visible = false;
    if (_panel_hotspot) lv_obj_clear_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN);
    if (_panel_home)  lv_obj_add_flag(_panel_home,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_net)   lv_obj_add_flag(_panel_net,   LV_OBJ_FLAG_HIDDEN);
    if (_panel_pass)  lv_obj_add_flag(_panel_pass,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_saved) lv_obj_add_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);
}

void LvglWifiFlowScreen::sync_home_boot_failure_ui() {
#if WIFI_FLOW_DIAG_GLITCH
    ++g_diag_sync_home_boot_failure_ui_calls;
#endif
    if (!_sub_home) return;
    const YoRadioPalette& pal = yoradio_palette();
    // Wi-Fi 6B: saved rows are interactive — remove "read-only" wording / строки кликабельны, убираем "read-only".
    if (_entered_from_boot_failure) {
        wifi_flow_set_text_if_changed(_sub_home,
                                      "Could not connect. Tap a saved network or Scan.",
                                      WifiFlowDiagTextSlot::SubHome);
        if (_btn_back_home) lv_obj_add_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    } else if (_entered_from_runtime_disconnect) {
        // S6V8A/S6V9B: runtime disconnect — Back visible; same static auto-Hotspot notice as other entries.
        // S6V8A/S6V9B: runtime disconnect — Back виден; то же статичное уведомление, что и для других входов.
        wifi_flow_set_text_if_changed(_sub_home,
                                      "Wi-Fi disconnected. Tap a saved network or Scan.",
                                      WifiFlowDiagTextSlot::SubHome);
        if (_btn_back_home) lv_obj_clear_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    } else {
        wifi_flow_set_text_if_changed(_sub_home,
                                      "Tap a saved network, or Scan to choose another.",
                                      WifiFlowDiagTextSlot::SubHome);
        if (_btn_back_home) lv_obj_clear_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(_sub_home, pal.text_secondary, LV_PART_MAIN);
}

void LvglWifiFlowScreen::show_networks_panel() {
    disarm_boot_idle_timer();
    clear_password_panel_state();
    _home_visible = false;
    if (_panel_net)   lv_obj_clear_flag(_panel_net,   LV_OBJ_FLAG_HIDDEN);
    if (_panel_home)  lv_obj_add_flag(_panel_home,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_pass)  lv_obj_add_flag(_panel_pass,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_saved) lv_obj_add_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);
    if (_panel_hotspot) lv_obj_add_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN);
}

void LvglWifiFlowScreen::show_password_panel() {
    disarm_boot_idle_timer();
    cancel_open_connect_state();
    _home_visible = false;
    if (_panel_pass)  lv_obj_clear_flag(_panel_pass,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_home)  lv_obj_add_flag(_panel_home,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_net)   lv_obj_add_flag(_panel_net,   LV_OBJ_FLAG_HIDDEN);
    if (_panel_saved) lv_obj_add_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);
    if (_panel_hotspot) lv_obj_add_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN);
}

// Wi-Fi 6B: show Saved Network panel (state set by open_saved_network) / показываем Saved panel.
void LvglWifiFlowScreen::show_saved_panel() {
    disarm_boot_idle_timer();
    cancel_open_connect_state();
    _home_visible = false;
    if (_panel_saved) lv_obj_clear_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);
    if (_panel_home)  lv_obj_add_flag(_panel_home,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_net)   lv_obj_add_flag(_panel_net,   LV_OBJ_FLAG_HIDDEN);
    if (_panel_pass)  lv_obj_add_flag(_panel_pass,  LV_OBJ_FLAG_HIDDEN);
    if (_panel_hotspot) lv_obj_add_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN);
}

// Wi-Fi 6B/6C: reset Saved panel state on navigation / сброс состояния Saved panel при навигации.
void LvglWifiFlowScreen::clear_saved_state() {
    _selectedSavedSlot      = 255;
    _await_saved_connect_ui = false;
    _saved_status_terminal  = false;
    _password_from_saved    = false;
    // Wi-Fi 6C: clear pending confirmation / сброс ожидающего подтверждения.
    _remove_confirm_pending = false;
    memset(_selectedSavedSsid, 0, sizeof(_selectedSavedSsid));
}

// Wi-Fi 6B: open Saved Network panel for a given slot / открытие Saved panel по тапу на строке.
void LvglWifiFlowScreen::open_saved_network(uint8_t slot) {
    if (slot >= WIFI_CRED_STORE_CAPACITY) return;
    WifiCredStoreEntryView v{};
    if (!wifiCredStoreGetEntry(slot, &v) || !v.ssid || !v.ssid[0]) return;
    clear_saved_state();
    _selectedSavedSlot = slot;
    strlcpy(_selectedSavedSsid, v.ssid, sizeof(_selectedSavedSsid));
    if (_lbl_saved_ssid) {
        wifi_flow_set_text_if_changed(_lbl_saved_ssid, _selectedSavedSsid, WifiFlowDiagTextSlot::None);
    }
    if (_lbl_saved_sub) {
        wifi_flow_set_text_if_changed(_lbl_saved_sub,
                                      "Saved network. Use saved password or change it.",
                                      WifiFlowDiagTextSlot::None);
    }
    if (_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_saved_status, " ", WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
    }
    // Re-enable buttons for fresh panel open / кнопки включены при каждом открытии.
    if (_btn_saved_connect) {
        wifi_flow_set_state_if_changed(_btn_saved_connect, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedConnect);
    }
    if (_btn_saved_chpwd) {
        wifi_flow_set_state_if_changed(_btn_saved_chpwd, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedChpwd);
    }
    if (_btn_saved_back) {
        wifi_flow_set_state_if_changed(_btn_saved_back, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedBack);
    }
    // Wi-Fi 6C: ensure normal row visible, confirm hidden, Remove enabled / нормальный ряд, Remove активен.
    if (_btn_saved_remove) {
        wifi_flow_set_state_if_changed(_btn_saved_remove, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedRemove);
    }
    if (_row_saved_confirm) lv_obj_add_flag(_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
    if (_row_saved_normal)  lv_obj_clear_flag(_row_saved_normal, LV_OBJ_FLAG_HIDDEN);
    _remove_confirm_pending = false;
    show_saved_panel();
}

void LvglWifiFlowScreen::open_password_entry(const char* ssid_utf8) {
    cancel_open_connect_state();
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
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Enter password / введите пароль", WifiFlowDiagTextSlot::PassStatus);
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
        wifi_flow_set_state_if_changed(_btn_connect, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PassConnect);
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
    wifi_flow_set_state_if_changed(_btn_connect, LV_STATE_DISABLED, !enable, WifiFlowDiagBtnSlot::PassConnect);
}

void LvglWifiFlowScreen::set_password_panel_connecting_ui(bool connecting) {
    if (!_ta_password || !_kbd || !_btn_connect) return;
    if (connecting) {
        wifi_flow_set_state_if_changed(_ta_password, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PasswordTa);
        wifi_flow_set_state_if_changed(_kbd, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PasswordKbd);
        wifi_flow_set_state_if_changed(_btn_connect, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PassConnect);
    } else {
        wifi_flow_set_state_if_changed(_ta_password, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::PasswordTa);
        wifi_flow_set_state_if_changed(_kbd, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::PasswordKbd);
    }
    sync_connect_button_enabled();
}

void LvglWifiFlowScreen::set_password_panel_saving_ui() {
    // Disable all input while saving/rebooting; no re-enable expected / блок ввода до reboot.
    if (_ta_password) {
        wifi_flow_set_state_if_changed(_ta_password, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PasswordTa);
    }
    if (_kbd) {
        wifi_flow_set_state_if_changed(_kbd, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PasswordKbd);
    }
    if (_btn_connect) {
        wifi_flow_set_state_if_changed(_btn_connect, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PassConnect);
    }
    if (_btn_back_pass) {
        wifi_flow_set_state_if_changed(_btn_back_pass, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PasswordBack);
    }
}

// Wi-Fi S6V7B: cancel in-flight open connect + clear flags / отмена open connect и сброс флагов.
void LvglWifiFlowScreen::cancel_open_connect_state() {
    if (_await_open_connect_ui) {
        wifiOpsCancel();
    }
    _await_open_connect_ui = false;
    _open_status_terminal   = false;
    memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
    set_networks_panel_connecting_ui(false);
}

// Wi-Fi S6V7B: Networks footer during open connect / футер Networks во время open connect.
void LvglWifiFlowScreen::set_networks_panel_connecting_ui(bool connecting) {
    const bool disable = connecting || _saving_in_progress;
    if (_btn_rescan) {
        wifi_flow_set_state_if_changed(_btn_rescan, LV_STATE_DISABLED, disable, WifiFlowDiagBtnSlot::Rescan);
    }
    if (_btn_back_net) {
        wifi_flow_set_state_if_changed(_btn_back_net, LV_STATE_DISABLED, disable, WifiFlowDiagBtnSlot::None);
    }
    if (_btn_cancel_scan) {
        wifi_flow_set_state_if_changed(_btn_cancel_scan, LV_STATE_DISABLED, disable, WifiFlowDiagBtnSlot::None);
    }
}

// Wi-Fi S6V7B: lock Networks footer before reboot after open save / блок футера до reboot после save open.
void LvglWifiFlowScreen::set_networks_panel_saving_ui() {
    if (_btn_rescan) {
        wifi_flow_set_state_if_changed(_btn_rescan, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::Rescan);
    }
    if (_btn_back_net) {
        wifi_flow_set_state_if_changed(_btn_back_net, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::None);
    }
    if (_btn_cancel_scan) {
        wifi_flow_set_state_if_changed(_btn_cancel_scan, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::None);
    }
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
        wifi_flow_set_text_if_changed(_lbl_pass_status,
                                     "Cannot save this network / нельзя сохранить эту сеть",
                                     WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        return;
    }

    const int existingIdx = wifiCredStoreFindIndexBySsid(_selectedSsid);

    if (existingIdx < 0) {
        // Case D: store full and SSID is new.
        // Case D: нет свободных слотов, новый SSID.
        if (wifiCredStoreSavedCount() >= WIFI_CRED_STORE_CAPACITY) {
            wifi_flow_set_text_if_changed(_lbl_pass_status,
                                          "Saved networks full. Remove one first. / хранилище заполнено - удалите сеть",
                                          WifiFlowDiagTextSlot::PassStatus);
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }

        // Case A: new SSID, free slot / новый SSID, есть слот.
        if (!wifiCredStoreAddOrUpdate(_selectedSsid, _connectCandidatePass)) {
            wifi_flow_set_text_if_changed(_lbl_pass_status,
                                          "Could not save network / ошибка сохранения сети",
                                          WifiFlowDiagTextSlot::PassStatus);
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }
        const int newIdx = wifiCredStoreFindIndexBySsid(_selectedSsid);
        if (!wifiCredStorePersistToFs()) {
            // RAM mutated but file failed: reload to restore consistency / RAM изменён, но файл не записан.
            wifiCredStoreReloadFromFs();
            wifi_flow_set_text_if_changed(_lbl_pass_status,
                                          "Could not save network / ошибка записи на диск",
                                          WifiFlowDiagTextSlot::PassStatus);
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
                wifi_flow_set_text_if_changed(_lbl_pass_status,
                                              "Could not update password / ошибка обновления пароля",
                                              WifiFlowDiagTextSlot::PassStatus);
                lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
                _pass_status_terminal = true;
                return;
            }
            if (!wifiCredStorePersistToFs()) {
                wifiCredStoreReloadFromFs();
                wifi_flow_set_text_if_changed(_lbl_pass_status,
                                          "Could not save network / ошибка записи на диск",
                                          WifiFlowDiagTextSlot::PassStatus);
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
    wifi_flow_set_text_if_changed(_lbl_pass_status,
                                  "Saved. Restarting... / сохранено, перезагрузка...",
                                  WifiFlowDiagTextSlot::PassStatus);
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
    set_password_panel_saving_ui();

    // One-shot timer ~1800 ms then ESP.restart() / однократный таймер ~1800 мс, затем перезагрузка.
    if (!_reboot_timer) {
        _reboot_timer = lv_timer_create(on_reboot_timer, 1800, this);
        lv_timer_set_repeat_count(_reboot_timer, 1);
    }
}

// Wi-Fi S6V7B: start connect to open AP from scan row (explicit tap) / старт connect к open AP из Scan.
void LvglWifiFlowScreen::start_open_connect_from_user(const char* ssid) {
    if (!ssid || !ssid[0] || !_lbl_net_status) return;
    if (_await_open_connect_ui || _saving_in_progress) return;

    memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
    strlcpy(_selectedOpenSsid, ssid, sizeof(_selectedOpenSsid));
    _open_status_terminal = false;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) && cur.busy) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_net_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
        return;
    }

    static const char kEmptyPass[] = "";
    const bool       started = wifiOpsRequestConnectWithPassword(_selectedOpenSsid, kEmptyPass, true);
    if (!started) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_net_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
        return;
    }

    _await_open_connect_ui = true;
    const YoRadioPalette& pal = yoradio_palette();
    wifi_flow_set_text_if_changed(_lbl_net_status, "Connecting to open network...", WifiFlowDiagTextSlot::NetStatus);
    lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
    set_networks_panel_connecting_ui(true);
}

// Wi-Fi S6V7B: persist open network after Success (Cases A–E) / сохранение open сети после Success.
void LvglWifiFlowScreen::handle_open_network_success_persist() {
    if (!_lbl_net_status) return;
    const YoRadioPalette& pal = yoradio_palette();
    static const char    kEmptyPass[] = "";

    if (!wifiCredStoreEntryFitsLegacyFile(_selectedOpenSsid, kEmptyPass)) {
        wifi_flow_set_text_if_changed(_lbl_net_status, "Cannot save this network.", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        set_networks_panel_connecting_ui(false);
        return;
    }

    const int existingIdx = wifiCredStoreFindIndexBySsid(_selectedOpenSsid);

    if (existingIdx < 0) {
        if (wifiCredStoreSavedCount() >= WIFI_CRED_STORE_CAPACITY) {
            wifi_flow_set_text_if_changed(_lbl_net_status, "Saved networks full. Remove one first.", WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            _open_status_terminal = true;
            set_networks_panel_connecting_ui(false);
            return;
        }
        if (!wifiCredStoreAddOrUpdate(_selectedOpenSsid, kEmptyPass)) {
            wifi_flow_set_text_if_changed(_lbl_net_status, "Could not save network.", WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            _open_status_terminal = true;
            set_networks_panel_connecting_ui(false);
            return;
        }
        const int newIdx = wifiCredStoreFindIndexBySsid(_selectedOpenSsid);
        if (!wifiCredStorePersistToFs()) {
            wifiCredStoreReloadFromFs();
            wifi_flow_set_text_if_changed(_lbl_net_status, "Could not save network.", WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            _open_status_terminal = true;
            set_networks_panel_connecting_ui(false);
            return;
        }
        if (newIdx >= 0) {
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(newIdx));
        }
    } else {
        const char*              storedPass = "";
        WifiCredStoreEntryView ev{};
        if (wifiCredStoreGetEntry(static_cast<uint8_t>(existingIdx), &ev)) {
            storedPass = ev.password ? ev.password : "";
        }
        if (strcmp(storedPass, kEmptyPass) == 0) {
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        } else {
            if (!wifiCredStagingBegin(static_cast<uint8_t>(existingIdx)) ||
                !wifiCredStagingSetCandidatePassword(kEmptyPass) || !wifiCredStagingCommitToStoredPassword()) {
                wifiCredStagingDiscard();
                wifi_flow_set_text_if_changed(_lbl_net_status, "Could not save network.", WifiFlowDiagTextSlot::NetStatus);
                lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
                _open_status_terminal = true;
                set_networks_panel_connecting_ui(false);
                return;
            }
            if (!wifiCredStorePersistToFs()) {
                wifiCredStoreReloadFromFs();
                wifi_flow_set_text_if_changed(_lbl_net_status, "Could not save network.", WifiFlowDiagTextSlot::NetStatus);
                lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
                _open_status_terminal = true;
                set_networks_panel_connecting_ui(false);
                return;
            }
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        }
    }

    _open_status_terminal = true;
    _saving_in_progress   = true;
    wifi_flow_set_text_if_changed(_lbl_net_status, "Saved. Restarting...", WifiFlowDiagTextSlot::NetStatus);
    lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
    set_networks_panel_saving_ui();

    if (!_reboot_timer) {
        _reboot_timer = lv_timer_create(on_reboot_timer, 1800, this);
        lv_timer_set_repeat_count(_reboot_timer, 1);
    }
}

void LvglWifiFlowScreen::handle_open_connect_finished() {
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    _await_open_connect_ui = false;
    const YoRadioPalette& pal = yoradio_palette();

    if (!_lbl_net_status) {
        set_networks_panel_connecting_ui(false);
        return;
    }

    switch (snap.lastResult) {
    case WifiOpsResult::Success:
    case WifiOpsResult::AlreadyConnected:
        handle_open_network_success_persist();
        return;
    case WifiOpsResult::AuthFailed:
    case WifiOpsResult::Timeout:
        wifi_flow_set_text_if_changed(_lbl_net_status, "Could not connect. Check signal.", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    case WifiOpsResult::NoNetwork:
        wifi_flow_set_text_if_changed(_lbl_net_status, kWifiOpsNoNetworkUserMsg, WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    case WifiOpsResult::Cancelled:
        wifi_flow_set_text_if_changed(_lbl_net_status, "Cancelled.", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    case WifiOpsResult::InternalError:
    case WifiOpsResult::Busy:
        wifi_flow_set_text_if_changed(_lbl_net_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    default:
        wifi_flow_set_text_if_changed(_lbl_net_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    }
    set_networks_panel_connecting_ui(false);
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
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Wrong password / неверный пароль", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Timeout:
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Timeout / таймаут", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::NoNetwork:
        wifi_flow_set_text_if_changed(_lbl_pass_status, kWifiOpsNoNetworkUserMsg, WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Cancelled:
        // Same policy as Back: leave password panel via Networks + clear / как Back — сети + очистка.
        clear_password_panel_state();
        show_networks_panel();
        return;
    case WifiOpsResult::InternalError:
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Wi-Fi error / ошибка Wi-Fi", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Busy:
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Busy / занято", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    default:
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Connect finished / подключение завершено", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    }
    sync_connect_button_enabled();
}

void LvglWifiFlowScreen::start_connect_from_user() {
    if (!_ta_password || !_lbl_pass_status || !_selectedSsid[0]) return;
    if (_await_connect_ui) return;
    if (_await_open_connect_ui) return;
    // Wi-Fi 6A: do not allow new connect while saving/rebooting / блок нового connect при сохранении.
    if (_saving_in_progress) return;

    // New Connect attempt: allow status + TA validation again / новая попытка — снова валидация и статусы.
    _pass_status_terminal            = false;
    _skip_next_ta_pass_status_sync = false;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) && cur.busy) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Wi-Fi busy / занято", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    const char* pw_in = lv_textarea_get_text(_ta_password);
    const size_t plen = pw_in ? strlen(pw_in) : 0U;
    if (plen < kMinPasswordLen) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_pass_status, "Min 8 characters / минимум 8 символов", WifiFlowDiagTextSlot::PassStatus);
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
            wifi_flow_set_text_if_changed(_lbl_pass_status, "Busy - cannot start / занято", WifiFlowDiagTextSlot::PassStatus);
        } else {
            wifi_flow_set_text_if_changed(_lbl_pass_status, "Cannot start connect / не удалось запустить", WifiFlowDiagTextSlot::PassStatus);
        }
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    _await_connect_ui = true;
    wifi_flow_set_text_if_changed(
        _lbl_pass_status,
        "Connecting... current Wi-Fi may disconnect / подключение... возможен разрыв Wi-Fi",
        WifiFlowDiagTextSlot::PassStatus);
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    set_password_panel_connecting_ui(true);
}

void LvglWifiFlowScreen::start_scan_from_user() {
    if (_await_open_connect_ui || _saving_in_progress) return;
    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) &&
        (cur.phase == WifiOpsPhase::Scanning || (cur.busy && cur.currentOp == WifiOpsOp::Scan))) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_net_status, "Scan in progress... / уже идёт", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }
    if (!wifiOpsRequestScan()) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_net_status, "Scan not started (busy?) / не стартовало", WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }
    _await_scan_ui = true;
    show_networks_panel();
    wifi_flow_set_text_if_changed(_lbl_net_status, "Starting scan... / запуск...", WifiFlowDiagTextSlot::NetStatus);
}

void LvglWifiFlowScreen::rebuild_saved_list() {
#if WIFI_FLOW_DIAG_GLITCH
    ++g_diag_rebuild_saved_list_calls;
#endif
    if (!_list_saved) return;
    lv_obj_clean(_list_saved);
    const uint8_t n = wifiCredStoreSavedCount();
    if (n == 0) {
        // No LV_SYMBOL_* — Montserrat cannot render built-in symbol glyphs (U+F00B boxes) / без символов LVGL
        lv_obj_t* empty = lv_list_add_btn(_list_saved, nullptr, "No saved networks");
        if (empty) {
            wifi_apply_list_row_empty(empty);
        }
        return;
    }
    const uint8_t cap = (n > 5) ? 5 : n;
    for (uint8_t i = 0; i < cap; ++i) {
        WifiCredStoreEntryView v{};
        if (!wifiCredStoreGetEntry(i, &v) || !v.ssid) continue;
        char line[40];
        snprintf(line, sizeof(line), "%u  %s", static_cast<unsigned>(i + 1U), v.ssid);
        lv_obj_t* btn = lv_list_add_btn(_list_saved, nullptr, line);
        if (!btn) {
            // Lightweight OOM guard: stop row creation gracefully / мягкий guard при нехватке памяти
            if (_sub_home) {
                wifi_flow_set_text_if_changed(_sub_home, "UI memory low. Reopen Wi-Fi screen.", WifiFlowDiagTextSlot::SubHome);
            }
            break;
        }
        wifi_apply_list_row_normal(btn);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(i) + 1U)));
        lv_obj_add_event_cb(btn, on_saved_row_click, LV_EVENT_CLICKED, this);
    }
}

void LvglWifiFlowScreen::rebuild_scan_list() {
    if (!_list_scan) return;
    lv_obj_clean(_list_scan);
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;
    for (uint16_t i = 0; i < snap.scanCount; ++i) {
        WifiOpsScanRow scan_row{};
        if (!wifiOpsGetScanResult(i, &scan_row)) continue;
        const bool is_open = (scan_row.auth == WIFI_AUTH_OPEN);
        char line[96];
        // One-line row only; grey RSSI needs multi-label — postponed (S6V11B-stylemem note).
        // Одна строка; серый RSSI — отдельные label, откладываем до memory-safe row layout.
        snprintf(
            line,
            sizeof(line),
            is_open ? "%s  %d dBm  Open" : "%s  %d dBm  Lock",
            scan_row.ssid,
            static_cast<int>(scan_row.rssi));

        lv_obj_t* btn = lv_list_add_btn(_list_scan, nullptr, line);
        if (!btn) {
            // Lightweight OOM guard: stop row creation gracefully / мягкий guard при нехватке памяти
            if (_lbl_net_status) {
                const YoRadioPalette& pal = yoradio_palette();
                wifi_flow_set_text_if_changed(_lbl_net_status, "UI memory low. Try Rescan.", WifiFlowDiagTextSlot::NetStatus);
                lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            }
            break;
        }
        wifi_apply_list_row_normal(btn);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(i) + 1U)));
        lv_obj_add_event_cb(btn, on_scan_row_click, LV_EVENT_CLICKED, this);
    }
    if (snap.scanCount == 0) {
        lv_obj_t* empty = lv_list_add_btn(_list_scan, nullptr, "No networks found");
        if (empty) {
            wifi_apply_list_row_empty(empty);
        }
    }
}

// Wi-Fi 6B/6C: disable/enable Saved panel action buttons during connect / блок кнопок во время connect.
void LvglWifiFlowScreen::set_saved_panel_connecting_ui(bool connecting) {
    if (connecting) {
        if (_btn_saved_connect) {
            wifi_flow_set_state_if_changed(_btn_saved_connect, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::SavedConnect);
        }
        if (_btn_saved_chpwd) {
            wifi_flow_set_state_if_changed(_btn_saved_chpwd, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::SavedChpwd);
        }
        // Wi-Fi 6C: disable Remove while connect is running / Remove выкл во время connect.
        if (_btn_saved_remove) {
            wifi_flow_set_state_if_changed(_btn_saved_remove, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::SavedRemove);
        }
    } else {
        if (_btn_saved_connect) {
            wifi_flow_set_state_if_changed(_btn_saved_connect, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedConnect);
        }
        if (_btn_saved_chpwd) {
            wifi_flow_set_state_if_changed(_btn_saved_chpwd, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedChpwd);
        }
        if (_btn_saved_remove) {
            wifi_flow_set_state_if_changed(_btn_saved_remove, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedRemove);
        }
    }
}

// Wi-Fi 6B/6C: lock Saved panel permanently before reboot / блок панели до reboot.
void LvglWifiFlowScreen::set_saved_panel_saving_ui() {
    if (_btn_saved_connect) {
        wifi_flow_set_state_if_changed(_btn_saved_connect, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::SavedConnect);
    }
    if (_btn_saved_chpwd) {
        wifi_flow_set_state_if_changed(_btn_saved_chpwd, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::SavedChpwd);
    }
    if (_btn_saved_back) {
        wifi_flow_set_state_if_changed(_btn_saved_back, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::SavedBack);
    }
    // Wi-Fi 6C: also lock Remove while reboot is pending / Remove тоже заблокирован до reboot.
    if (_btn_saved_remove) {
        wifi_flow_set_state_if_changed(_btn_saved_remove, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::SavedRemove);
    }
}

// Wi-Fi 6B: connect using stored password for selected saved slot.
// Wi-Fi 6B: подключение по сохранённому паролю выбранного слота.
void LvglWifiFlowScreen::start_connect_from_saved() {
    if (_selectedSavedSlot == 255 || !_selectedSavedSsid[0]) return;
    if (_await_saved_connect_ui) return;
    if (_saving_in_progress) return;
    if (_await_open_connect_ui) return;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) && cur.busy) {
        if (_lbl_saved_status) {
            const YoRadioPalette& pal = yoradio_palette();
            wifi_flow_set_text_if_changed(_lbl_saved_status, "Wi-Fi busy. Try again.", WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
        }
        return;
    }

    // Resolve stored PSK into a local stack buffer; memset immediately after use / PSK в стек, затем обнуление.
    char tmpPass[40]{};
    if (!wifiCredStoreResolvePasswordForSlot(_selectedSavedSlot, tmpPass, sizeof(tmpPass))) {
        memset(tmpPass, 0, sizeof(tmpPass));
        if (_lbl_saved_status) {
            const YoRadioPalette& pal = yoradio_palette();
            wifi_flow_set_text_if_changed(_lbl_saved_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
        }
        return;
    }

    const bool started = wifiOpsRequestConnectWithPassword(_selectedSavedSsid, tmpPass, true);
    memset(tmpPass, 0, sizeof(tmpPass)); // PSK off stack / PSK обнулён

    if (!started) {
        if (_lbl_saved_status) {
            const YoRadioPalette& pal = yoradio_palette();
            wifi_flow_set_text_if_changed(_lbl_saved_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
        }
        return;
    }

    _await_saved_connect_ui = true;
    _saved_status_terminal  = false;
    if (_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(_lbl_saved_status, "Connecting...", WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
    }
    set_saved_panel_connecting_ui(true);
}

// Wi-Fi 6B: called by pollOpsSnapshot when connect op finishes on Saved panel.
// Wi-Fi 6B: вызывается из pollOpsSnapshot при завершении connect на Saved panel.
void LvglWifiFlowScreen::handle_saved_connect_finished() {
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    _await_saved_connect_ui = false;
    const YoRadioPalette& pal = yoradio_palette();
    set_saved_panel_connecting_ui(false);

    if (!_lbl_saved_status) return;

    switch (snap.lastResult) {
    case WifiOpsResult::Success:
    case WifiOpsResult::AlreadyConnected: {
        // Only update lastSSID; no wifi.csv write for saved Connect / только lastSSID, wifi.csv не трогаем.
        const bool ok = wifiCredStoreSetLastSuccessFromSlot(_selectedSavedSlot);
        if (!ok) {
            wifi_flow_set_text_if_changed(_lbl_saved_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
            return;
        }
        wifi_flow_set_text_if_changed(_lbl_saved_status, "Connected. Restarting...", WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        _saving_in_progress    = true;
        set_saved_panel_saving_ui();
        // Reuse existing reboot timer; do not create duplicate / переиспользуем таймер, без дубликата.
        if (!_reboot_timer) {
            _reboot_timer = lv_timer_create(on_reboot_timer, 1800, this);
            lv_timer_set_repeat_count(_reboot_timer, 1);
        }
        return;
    }
    case WifiOpsResult::NoNetwork:
        wifi_flow_set_text_if_changed(_lbl_saved_status, kWifiOpsNoNetworkUserMsg, WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
    case WifiOpsResult::Timeout:
    case WifiOpsResult::AuthFailed:
        // Auth/timeout — password or signal; NoNetwork handled above / пароль или сигнал; NoNetwork отдельно.
        wifi_flow_set_text_if_changed(_lbl_saved_status,
                                      "Could not connect. Check password or signal.",
                                      WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
    case WifiOpsResult::Cancelled:
        wifi_flow_set_text_if_changed(_lbl_saved_status, "Cancelled.", WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
    default:
        wifi_flow_set_text_if_changed(_lbl_saved_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
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

    _panel_home  = lv_obj_create(_screen);
    _panel_net   = lv_obj_create(_screen);
    _panel_pass  = lv_obj_create(_screen);
    _panel_saved = lv_obj_create(_screen);
    _panel_hotspot = lv_obj_create(_screen);
    if (!_panel_home || !_panel_net || !_panel_pass || !_panel_saved || !_panel_hotspot) return;
    lv_obj_set_width(_panel_home,  LV_PCT(100));
    lv_obj_set_flex_grow(_panel_home,  1);
    lv_obj_set_width(_panel_net,   LV_PCT(100));
    lv_obj_set_flex_grow(_panel_net,   1);
    lv_obj_set_width(_panel_pass,  LV_PCT(100));
    lv_obj_set_flex_grow(_panel_pass,  1);
    lv_obj_set_width(_panel_saved, LV_PCT(100));
    lv_obj_set_flex_grow(_panel_saved, 1);
    lv_obj_set_width(_panel_hotspot, LV_PCT(100));
    lv_obj_set_flex_grow(_panel_hotspot, 1);
    lv_obj_set_flex_flow(_panel_home,  LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(_panel_net,   LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(_panel_pass,  LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(_panel_saved, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(_panel_hotspot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_panel_home,  8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel_net,   8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel_pass,  8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel_saved, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_panel_hotspot, 8, LV_PART_MAIN);
    style_service_panel(_panel_home,  pal);
    style_service_panel(_panel_net,   pal);
    style_service_panel(_panel_pass,  pal);
    style_service_panel(_panel_saved, pal);
    style_service_panel(_panel_hotspot, pal);

    // S6V11B-stylemem: init shared styles once widgets exist; drop in destroy after lv_obj_del.
    // S6V11B-stylemem: общие стили после панелей; сброс в destroy после удаления дерева.
    wifi_flow_style_ensure(pal);

    lv_obj_t* hdr_home_row = wifi_create_header_row(_panel_home);
    if (hdr_home_row) {
        lv_obj_t* ico_home = lv_label_create(hdr_home_row);
        if (ico_home) {
            lv_label_set_text(ico_home, wifi_flow_glyph_utf8_wifi_full());
            lv_obj_set_style_text_font(ico_home, wifi_header_icon_font(), LV_PART_MAIN);
            lv_obj_set_style_text_color(ico_home, pal.text_secondary, LV_PART_MAIN);
        }
        _hdr_home = lv_label_create(hdr_home_row);
    } else {
        // Fallback: keep title without icon when allocation is low / fallback: заголовок без иконки
        _hdr_home = lv_label_create(_panel_home);
    }
    if (_hdr_home) {
        lv_label_set_text(_hdr_home, "Wi-Fi Recovery");
        lv_obj_set_style_text_color(_hdr_home, pal.text_primary, LV_PART_MAIN);
        wifi_set_font(_hdr_home, wifi_title_font_slot());
    }

    _sub_home = lv_label_create(_panel_home);
    // Initial text is immediately overwritten by sync_home_boot_failure_ui() / перезаписывается при enter.
    wifi_flow_set_text_if_changed(_sub_home, "Tap a saved network, or Scan to choose another.", WifiFlowDiagTextSlot::SubHome);
    lv_obj_set_style_text_color(_sub_home, pal.text_secondary, LV_PART_MAIN);
    wifi_set_font(_sub_home, wifi_status_font_slot());
    lv_label_set_long_mode(_sub_home, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_sub_home, LV_PCT(100));

    _lbl_recovery_idle_countdown = lv_label_create(_panel_home);
    // S6V9B: static notice only on arm — no periodic text updates / только при arm, без периодических set_text.
    wifi_flow_set_text_if_changed(_lbl_recovery_idle_countdown, "", WifiFlowDiagTextSlot::None);
    lv_obj_set_style_text_color(_lbl_recovery_idle_countdown, pal.text_meta, LV_PART_MAIN);
    wifi_set_font(_lbl_recovery_idle_countdown, wifi_status_font_slot());
    lv_label_set_long_mode(_lbl_recovery_idle_countdown, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(_lbl_recovery_idle_countdown, LV_PCT(100));

    _list_saved = lv_list_create(_panel_home);
    lv_obj_set_width(_list_saved, LV_PCT(100));
    lv_obj_set_flex_grow(_list_saved, 1);
    lv_obj_set_style_bg_opa(_list_saved, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list_saved, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_list_saved, 6, LV_PART_MAIN);
    // S6V6D: hide scrollbar redraw on Home saved list; list stays scrollable if content exceeds viewport.
    // S6V6D: убираем полосу прокрутки (меньше мерцания), прокрутка списка сохраняется.
    lv_obj_set_scrollbar_mode(_list_saved, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* rowh = lv_obj_create(_panel_home);
    if (rowh) {
        lv_obj_set_width(rowh, LV_PCT(100));
        lv_obj_set_height(rowh, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(rowh, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(rowh, 10, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rowh, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(rowh, 0, LV_PART_MAIN);
        _btn_scan = add_footer_button(rowh, "Scan", pal, on_btn_scan, this);
        _btn_hotspot = add_footer_button(rowh, "Hotspot", pal, on_btn_hotspot, this);
        _btn_back_home = add_footer_button(rowh, "Back", pal, on_btn_back_home, this);
        wifi_apply_button_role(_btn_scan, pal, WifiBtnRole::Primary);
        wifi_apply_button_role(_btn_hotspot, pal, WifiBtnRole::Secondary);
        wifi_apply_button_role(_btn_back_home, pal, WifiBtnRole::Ghost);
    }

    lv_obj_t* hdr_net_row = wifi_create_header_row(_panel_net);
    if (hdr_net_row) {
        lv_obj_t* ico_net = lv_label_create(hdr_net_row);
        if (ico_net) {
            lv_label_set_text(ico_net, wifi_flow_glyph_utf8_wifi_full());
            lv_obj_set_style_text_font(ico_net, wifi_header_icon_font(), LV_PART_MAIN);
            lv_obj_set_style_text_color(ico_net, pal.text_secondary, LV_PART_MAIN);
        }
        _hdr_net = lv_label_create(hdr_net_row);
    } else {
        _hdr_net = lv_label_create(_panel_net);
    }
    if (_hdr_net) {
        lv_label_set_text(_hdr_net, "Available networks");
        lv_obj_set_style_text_color(_hdr_net, pal.text_primary, LV_PART_MAIN);
        wifi_set_font(_hdr_net, wifi_title_font_slot());
    }

    _lbl_net_status = lv_label_create(_panel_net);
    wifi_flow_set_text_if_changed(_lbl_net_status, " ", WifiFlowDiagTextSlot::NetStatus);
    lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
    wifi_set_font(_lbl_net_status, wifi_status_font_slot());
    lv_label_set_long_mode(_lbl_net_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lbl_net_status, LV_PCT(100));

    _list_scan = lv_list_create(_panel_net);
    lv_obj_set_width(_list_scan, LV_PCT(100));
    lv_obj_set_flex_grow(_list_scan, 1);
    lv_obj_set_style_bg_opa(_list_scan, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list_scan, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_list_scan, 6, LV_PART_MAIN);

    lv_obj_t* rown = lv_obj_create(_panel_net);
    if (rown) {
        lv_obj_set_width(rown, LV_PCT(100));
        lv_obj_set_height(rown, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(rown, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(rown, 10, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rown, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(rown, 0, LV_PART_MAIN);
        _btn_rescan = add_footer_button(rown, "Rescan", pal, on_btn_rescan, this);
        _btn_cancel_scan = add_footer_button(rown, "Cancel", pal, on_btn_cancel_scan, this);
        _btn_back_net    = add_footer_button(rown, "Home", pal, on_btn_back_net, this);
        wifi_apply_button_role(_btn_rescan, pal, WifiBtnRole::Primary);
        wifi_apply_button_role(_btn_cancel_scan, pal, WifiBtnRole::Secondary);
        wifi_apply_button_role(_btn_back_net, pal, WifiBtnRole::Ghost);
    }

    // Wi-Fi 4A: Password panel / панель пароля (UI only).
    _hdr_pass = lv_label_create(_panel_pass);
    lv_label_set_text(_hdr_pass, "Wi-Fi Setup");
    lv_obj_set_style_text_color(_hdr_pass, pal.text_primary, LV_PART_MAIN);
    wifi_set_font(_hdr_pass, wifi_title_font_slot());

    _lbl_pass_ssid = lv_label_create(_panel_pass);
    lv_label_set_text(_lbl_pass_ssid, " ");
    lv_obj_set_style_text_color(_lbl_pass_ssid, pal.text_primary, LV_PART_MAIN);
    wifi_set_font(_lbl_pass_ssid, wifi_body_font_slot());
    lv_label_set_long_mode(_lbl_pass_ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_lbl_pass_ssid, LV_PCT(100));

    _lbl_pass_hint = lv_label_create(_panel_pass);
    lv_label_set_text(
        _lbl_pass_hint,
        "Enter the password for the selected network.");
    lv_obj_set_style_text_color(_lbl_pass_hint, pal.text_secondary, LV_PART_MAIN);
    wifi_set_font(_lbl_pass_hint, wifi_status_font_slot());
    lv_label_set_long_mode(_lbl_pass_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lbl_pass_hint, LV_PCT(100));

    _ta_password = lv_textarea_create(_panel_pass);
    lv_obj_set_width(_ta_password, LV_PCT(100));
    lv_obj_set_style_min_height(_ta_password, (LV_ACTIVE_PROFILE.width <= 320u) ? 46 : 50, LV_PART_MAIN);
    lv_textarea_set_one_line(_ta_password, true);
    lv_textarea_set_max_length(_ta_password, kPasswordMaxInputChars);
    lv_textarea_set_password_mode(_ta_password, true);
    lv_obj_set_style_text_color(_ta_password, pal.text_primary, wifi_sel(LV_PART_MAIN, LV_STATE_DEFAULT));
    wifi_set_font(_ta_password, wifi_body_font_slot());
    // S6V11A-icons3: small radius on TA — cheaper rounded masks than theme default / меньше маска скругления
    lv_obj_set_style_radius(_ta_password, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(_ta_password, on_ta_password_changed, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* rowp = lv_obj_create(_panel_pass);
    if (rowp) {
        lv_obj_set_width(rowp, LV_PCT(100));
        lv_obj_set_height(rowp, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(rowp, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(rowp, 10, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rowp, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(rowp, 0, LV_PART_MAIN);

        _btn_connect = lv_btn_create(rowp);
        if (_btn_connect) {
            lv_obj_set_flex_grow(_btn_connect, 1);
            wifi_apply_button_role(_btn_connect, pal, WifiBtnRole::Primary);
            wifi_flow_set_state_if_changed(_btn_connect, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PassConnect);
            lv_obj_add_event_cb(_btn_connect, on_btn_connect, LV_EVENT_CLICKED, this);
            lv_obj_t* lc = lv_label_create(_btn_connect);
            lv_label_set_text(lc, "Connect");
            lv_obj_center(lc);
        }
        _btn_back_pass = add_footer_button(rowp, "Back", pal, on_btn_back_pass, this);
        wifi_apply_button_role(_btn_back_pass, pal, WifiBtnRole::Secondary);
    }

    _lbl_pass_status = lv_label_create(_panel_pass);
    wifi_flow_set_text_if_changed(_lbl_pass_status, " ", WifiFlowDiagTextSlot::PassStatus);
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    wifi_set_font(_lbl_pass_status, wifi_status_font_slot());
    lv_label_set_long_mode(_lbl_pass_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lbl_pass_status, LV_PCT(100));

    _kbd = lv_keyboard_create(_panel_pass);
    if (_kbd) {
        lv_obj_set_width(_kbd, LV_PCT(100));
        lv_obj_set_flex_grow(_kbd, 1);
        lv_obj_set_style_min_height(_kbd, (LV_ACTIVE_PROFILE.width <= 320u) ? 120 : 140, LV_PART_MAIN);
        lv_keyboard_set_mode(_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(_kbd, nullptr);
        // Do not set Montserrat on _kbd — breaks LVGL symbol font on special keys / без смены шрифта клавиш
        lv_obj_add_event_cb(_kbd, on_keyboard_event, LV_EVENT_ALL, this);
    }

    lv_obj_add_flag(_panel_pass, LV_OBJ_FLAG_HIDDEN);

    // Wi-Fi 6B: Saved Network panel — title, subtitle, status, buttons / Saved Network panel.
    {
        _lbl_saved_ssid = lv_label_create(_panel_saved);
        wifi_flow_set_text_if_changed(_lbl_saved_ssid, " ", WifiFlowDiagTextSlot::None);
        lv_obj_set_style_text_color(_lbl_saved_ssid, pal.text_primary, LV_PART_MAIN);
        wifi_set_font(_lbl_saved_ssid, wifi_title_font_slot());
        lv_label_set_long_mode(_lbl_saved_ssid, LV_LABEL_LONG_DOT);
        lv_obj_set_width(_lbl_saved_ssid, LV_PCT(100));

        _lbl_saved_sub = lv_label_create(_panel_saved);
        lv_label_set_text(_lbl_saved_sub, "Saved network. Use saved password or change it.");
        lv_obj_set_style_text_color(_lbl_saved_sub, pal.text_secondary, LV_PART_MAIN);
        wifi_set_font(_lbl_saved_sub, wifi_status_font_slot());
        lv_label_set_long_mode(_lbl_saved_sub, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_lbl_saved_sub, LV_PCT(100));

        _lbl_saved_status = lv_label_create(_panel_saved);
        wifi_flow_set_text_if_changed(_lbl_saved_status, " ", WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
        wifi_set_font(_lbl_saved_status, wifi_status_font_slot());
        lv_label_set_long_mode(_lbl_saved_status, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_lbl_saved_status, LV_PCT(100));

        // Wi-Fi 6C: normal row — Connect / Chg pwd / Remove / Back / нормальный ряд действий.
        _row_saved_normal = lv_obj_create(_panel_saved);
        if (_row_saved_normal) {
            lv_obj_set_width(_row_saved_normal, LV_PCT(100));
            lv_obj_set_height(_row_saved_normal, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_row_saved_normal, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(_row_saved_normal, 8, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_row_saved_normal, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(_row_saved_normal, 0, LV_PART_MAIN);

            _btn_saved_connect = add_footer_button(_row_saved_normal, "Connect", pal, on_btn_saved_connect, this);
            _btn_saved_chpwd   = add_footer_button(_row_saved_normal, "Chg pwd", pal, on_btn_saved_chpwd,   this);
            _btn_saved_remove  = add_footer_button(_row_saved_normal, "Remove",  pal, on_btn_saved_remove,  this);
            _btn_saved_back    = add_footer_button(_row_saved_normal, "Back",    pal, on_btn_saved_back,    this);
            wifi_apply_button_role(_btn_saved_connect, pal, WifiBtnRole::Primary);
            wifi_apply_button_role(_btn_saved_chpwd, pal, WifiBtnRole::Secondary);
            wifi_apply_button_role(_btn_saved_remove, pal, WifiBtnRole::Destructive);
            wifi_apply_button_role(_btn_saved_back, pal, WifiBtnRole::Ghost);
        }

        // Wi-Fi 6C: confirm row — Yes / No, hidden until Remove tapped / ряд подтверждения, скрыт по умолчанию.
        _row_saved_confirm = lv_obj_create(_panel_saved);
        if (_row_saved_confirm) {
            lv_obj_set_width(_row_saved_confirm, LV_PCT(100));
            lv_obj_set_height(_row_saved_confirm, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(_row_saved_confirm, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(_row_saved_confirm, 8, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_row_saved_confirm, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(_row_saved_confirm, 0, LV_PART_MAIN);

            _btn_saved_yes = add_footer_button(_row_saved_confirm, "Yes", pal, on_btn_saved_yes, this);
            _btn_saved_no  = add_footer_button(_row_saved_confirm, "No",  pal, on_btn_saved_no,  this);
            wifi_apply_button_role(_btn_saved_yes, pal, WifiBtnRole::Destructive);
            wifi_apply_button_role(_btn_saved_no, pal, WifiBtnRole::Secondary);
            lv_obj_add_flag(_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_add_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);

    // Wi-Fi S6V7A: Hotspot panel — yoRadioAP / open / help; Back → Recovery Home; no legacy _apScreen.
    {
        _hdr_hotspot = lv_label_create(_panel_hotspot);
        lv_label_set_text(_hdr_hotspot, "Open Hotspot Mode");
        lv_obj_set_style_text_color(_hdr_hotspot, pal.text_primary, LV_PART_MAIN);
        wifi_set_font(_hdr_hotspot, wifi_title_font_slot());

        _lbl_hotspot_ssid = lv_label_create(_panel_hotspot);
        _lbl_hotspot_pwd  = lv_label_create(_panel_hotspot);
        _lbl_hotspot_ip   = lv_label_create(_panel_hotspot);
        _lbl_hotspot_help = lv_label_create(_panel_hotspot);
        lv_obj_set_style_text_color(_lbl_hotspot_ssid, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_color(_lbl_hotspot_pwd, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_color(_lbl_hotspot_ip, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_color(_lbl_hotspot_help, pal.text_secondary, LV_PART_MAIN);
        wifi_set_font(_lbl_hotspot_ssid, wifi_body_font_slot());
        wifi_set_font(_lbl_hotspot_pwd, wifi_body_font_slot());
        wifi_set_font(_lbl_hotspot_ip, wifi_body_font_slot());
        wifi_set_font(_lbl_hotspot_help, wifi_status_font_slot());
        lv_label_set_long_mode(_lbl_hotspot_help, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_lbl_hotspot_ssid, LV_PCT(100));
        lv_obj_set_width(_lbl_hotspot_pwd, LV_PCT(100));
        lv_obj_set_width(_lbl_hotspot_ip, LV_PCT(100));
        lv_obj_set_width(_lbl_hotspot_help, LV_PCT(100));

        lv_obj_t* row_ap = lv_obj_create(_panel_hotspot);
        if (row_ap) {
            lv_obj_set_width(row_ap, LV_PCT(100));
            lv_obj_set_height(row_ap, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row_ap, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(row_ap, 8, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row_ap, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row_ap, 0, LV_PART_MAIN);
            _btn_hotspot_back = add_footer_button(row_ap, "Back to Recovery", pal, on_btn_back_hotspot, this);
            wifi_apply_button_role(_btn_hotspot_back, pal, WifiBtnRole::Secondary);
        }
    }

    sync_hotspot_panel_labels();

    lv_obj_add_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN);

    show_home_panel();
    rebuild_saved_list();
}

void LvglWifiFlowScreen::enter() {
    (void)wifiOpsInit();
    cancel_open_connect_state();
    _last_results_seq              = 0;
    _await_scan_ui                 = false;
    _await_connect_ui              = false;
    _pass_status_terminal          = false;
    _skip_next_ta_pass_status_sync = false;
    _saving_in_progress            = false;
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
    // Wi-Fi 6B/6C: reset Saved panel state on each enter / сброс состояния Saved panel при каждом входе.
    _await_saved_connect_ui = false;
    _saved_status_terminal  = false;
    _password_from_saved    = false;
    _remove_confirm_pending = false;
    _selectedSavedSlot      = 255;
    memset(_selectedSavedSsid, 0, sizeof(_selectedSavedSsid));
    _entered_from_boot_failure         = lvgl_ui::consumeWifiRecoveryEnteredFromBootFailure();
    // S6V8A: consume runtime disconnect entry context — mutually exclusive with boot-fail. / Контекст runtime эскалации.
    // If neither flag is set (e.g. manual entry via InvertDisplay), both remain false. / Если ни один флаг — оба false.
    _entered_from_runtime_disconnect   = lvgl_ui::consumeWifiRecoveryEnteredFromRuntimeDisconnect();
    // S6V9C: in LVGL path boot-fail no longer raises AP immediately; AP starts only on Hotspot page.
    // S6V9C: в LVGL path AP при boot-fail больше не поднимается; лог только если AP всё же активен (non-LVGL fallback).
    if (_entered_from_boot_failure && network.status == SOFT_AP) {
        const IPAddress ip = WiFi.softAPIP();
        if (static_cast<uint32_t>(ip) != 0U) {
            Serial.println("[Network] Wi-Fi Recovery active; AP was already active (unexpected in LVGL path)");
        }
    }
#if WIFI_FLOW_DIAG_GLITCH
    wifi_flow_diag_on_enter_flow();
#endif
    rebuild_saved_list();
    sync_home_boot_failure_ui();
    arm_recovery_idle_if_home_only();
    if (!_poll_timer) {
        _poll_timer = lv_timer_create(on_poll_timer, kPollMs, this);
    }
}

void LvglWifiFlowScreen::update() {}

void LvglWifiFlowScreen::exit() {
    disarm_boot_idle_timer();
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
#if WIFI_FLOW_DIAG_GLITCH
    wifi_flow_diag_on_exit_flow();
#endif
    _await_open_connect_ui = false;
    _open_status_terminal   = false;
    memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
    set_networks_panel_connecting_ui(false);
    wifiOpsCancel();
}

void LvglWifiFlowScreen::destroy() {
    exit();
    clear_password_secrets();
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    // Reset shared styles after objects freed — safe before next create().
    // Сброс общих стилей после удаления объектов — безопасно перед следующим create().
    wifi_flow_style_drop();
    _panel_home = _panel_net = _panel_pass = _panel_saved = _panel_hotspot = nullptr;
    _hdr_home = _sub_home = _lbl_recovery_idle_countdown = _list_saved = nullptr;
    _hdr_net = _lbl_net_status = _list_scan = nullptr;
    _hdr_pass = _lbl_pass_ssid = _lbl_pass_hint = _ta_password = _lbl_pass_status = nullptr;
    _btn_connect = _btn_back_pass = _kbd = nullptr;
    _btn_scan = _btn_hotspot = _btn_back_home = _btn_rescan = _btn_cancel_scan = _btn_back_net = nullptr;
    // Wi-Fi 6B: null Saved Network panel widgets / обнуляем виджеты Saved Network panel.
    _lbl_saved_ssid = _lbl_saved_sub = _lbl_saved_status = nullptr;
    _btn_saved_connect = _btn_saved_chpwd = _btn_saved_back = nullptr;
    // Wi-Fi 6C: null Remove confirmation widgets / обнуляем виджеты Remove.
    _row_saved_normal = _row_saved_confirm = nullptr;
    _btn_saved_remove = _btn_saved_yes = _btn_saved_no = nullptr;
    _hdr_hotspot = _lbl_hotspot_ssid = _lbl_hotspot_pwd = _lbl_hotspot_ip = _lbl_hotspot_help = nullptr;
    _btn_hotspot_back = nullptr;
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

    process_boot_idle_timer_tick();

    const YoRadioPalette& pal = yoradio_palette();

    const bool pass_visible  = _panel_pass  && !lv_obj_has_flag(_panel_pass,  LV_OBJ_FLAG_HIDDEN);
    // Wi-Fi 6B: track saved panel visibility for connect polling / видимость Saved panel для polling.
    const bool saved_visible = _panel_saved && !lv_obj_has_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);
    const bool net_visible   = _panel_net && !lv_obj_has_flag(_panel_net, LV_OBJ_FLAG_HIDDEN);

    // 4B: Connect polling for Password panel / опрос завершения connect для Password panel.
    if (pass_visible && _await_connect_ui) {
        if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
            if (_lbl_pass_status) {
                wifi_flow_set_text_if_changed(
                    _lbl_pass_status,
                    "Connecting... current Wi-Fi may disconnect / подключение... возможен разрыв Wi-Fi",
                    WifiFlowDiagTextSlot::PassStatus);
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

    // Wi-Fi 6B: Connect polling for Saved Network panel / опрос завершения connect для Saved panel.
    if (saved_visible && _await_saved_connect_ui) {
        if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
            if (_lbl_saved_status && !_saved_status_terminal) {
                wifi_flow_set_text_if_changed(_lbl_saved_status, "Connecting...", WifiFlowDiagTextSlot::SavedStatus);
                lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
            }
            return;
        }
        if (!snap.busy && snap.phase == WifiOpsPhase::Idle) {
            handle_saved_connect_finished();
            // Fall through: refresh scan buttons etc. / дальше — кнопки скана.
        }
    }

    // Wi-Fi S6V7B: open-network connect polling on Networks panel / опрос open connect на панели Networks.
    if (net_visible && _await_open_connect_ui) {
        if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
            if (_lbl_net_status && !_open_status_terminal) {
                wifi_flow_set_text_if_changed(_lbl_net_status, "Connecting to open network...", WifiFlowDiagTextSlot::NetStatus);
                lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
            }
            set_networks_panel_connecting_ui(true);
            return;
        }
        if (!snap.busy && snap.phase == WifiOpsPhase::Idle) {
            handle_open_connect_finished();
            // Fall through / дальше — общая логика скана.
        }
    }

    // S6V9F: only treat scan-in-progress as blocking UI when Networks is relevant or user is awaiting scan results.
    // S6V9F: иначе stale Scanning на Home после stop AP — вечный early-return и «мёртвый» Scan.
    const bool scan_progress_blocks_ui =
        net_visible || _await_scan_ui;
    if (scan_progress_blocks_ui &&
        (snap.phase == WifiOpsPhase::Scanning || (snap.busy && snap.currentOp == WifiOpsOp::Scan))) {
        if (_lbl_net_status) {
            wifi_flow_set_text_if_changed(_lbl_net_status, "Scanning...", WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
        }
        if (_btn_scan) {
            wifi_flow_set_state_if_changed(_btn_scan, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::Scan);
        }
        if (_btn_rescan) {
            wifi_flow_set_state_if_changed(_btn_rescan, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::Rescan);
        }
        return;
    }
    if (_btn_scan) {
        wifi_flow_set_state_if_changed(_btn_scan, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::Scan);
    }
    if (_btn_rescan) {
        // S6V7B: keep Networks Rescan disabled while open save → reboot pending / не включать Rescan до reboot после save open.
        const bool net_rescan_locked = net_visible && (_saving_in_progress || _await_open_connect_ui);
        wifi_flow_set_state_if_changed(_btn_rescan, LV_STATE_DISABLED, net_rescan_locked, WifiFlowDiagBtnSlot::Rescan);
    }

    // Completing scan await only when Networks (not Password or Saved) visible.
    // Завершение скана не обрабатывается на Password или Saved panel.
    if (_await_scan_ui && !pass_visible && !saved_visible && !_await_open_connect_ui && snap.phase == WifiOpsPhase::Idle &&
        !snap.busy) {
        _await_scan_ui = false;
        if (snap.lastResult == WifiOpsResult::Success) {
            rebuild_scan_list();
            wifi_flow_set_text_if_changed(_lbl_net_status, "Scan complete", WifiFlowDiagTextSlot::NetStatus);
        } else if (snap.lastResult == WifiOpsResult::Cancelled) {
            rebuild_scan_list();
            wifi_flow_set_text_if_changed(_lbl_net_status, "Scan cancelled", WifiFlowDiagTextSlot::NetStatus);
        } else if (snap.lastResult == WifiOpsResult::Timeout) {
            rebuild_scan_list();
            wifi_flow_set_text_if_changed(_lbl_net_status, "Scan timeout", WifiFlowDiagTextSlot::NetStatus);
        } else if (snap.lastResult == WifiOpsResult::Busy) {
            rebuild_scan_list();
            wifi_flow_set_text_if_changed(_lbl_net_status, "Busy", WifiFlowDiagTextSlot::NetStatus);
        } else {
            rebuild_scan_list();
            wifi_flow_set_text_if_changed(_lbl_net_status, "Scan finished", WifiFlowDiagTextSlot::NetStatus);
        }
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        (void)snap.resultsSeq;
        _last_results_seq = snap.resultsSeq;
    }

    if (pass_visible && !_await_connect_ui) {
        sync_connect_button_enabled();
    }

#if WIFI_FLOW_DIAG_GLITCH
    wifi_flow_diag_maybe_periodic_summary();
#endif
}

void LvglWifiFlowScreen::on_btn_scan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (self) self->start_scan_from_user();
}

void LvglWifiFlowScreen::on_btn_hotspot(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->show_hotspot_panel();
}

void LvglWifiFlowScreen::on_btn_back_hotspot(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    // S6V9C: stop SoftAP before returning Home (stops yoRadioAP, cancels softapdelay) / гасим AP до возврата Home.
    network.recoveryStopSoftAP();
    // S6V9F: radio/netif change can strand WiFiOps snapshot in Scanning — poll used to early-return and freeze Home UI.
    // S6V9F: смена радио может оставить phase=Scanning — ранний return в poll блокировал кнопки на Home.
    wifiOpsCancel();
    self->_await_scan_ui = false;
    self->show_home_panel();
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
    if (self->_await_open_connect_ui || self->_saving_in_progress) return;
    wifiOpsCancel();
    self->show_home_panel();
}

void LvglWifiFlowScreen::on_btn_rescan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->_await_open_connect_ui || self->_saving_in_progress) return;
    self->start_scan_from_user();
}

void LvglWifiFlowScreen::on_btn_cancel_scan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->_await_open_connect_ui || self->_saving_in_progress) return;
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
    // Wi-Fi 6B: return to Saved panel if Password was opened via Change password / Back на Saved при смене пароля.
    if (self->_password_from_saved) {
        self->_password_from_saved = false;
        self->clear_password_panel_state();
        self->show_saved_panel();
        return;
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
        wifi_flow_set_text_if_changed(self->_lbl_pass_status, "Enter password / введите пароль", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    } else if (n < kMinPasswordLen) {
        wifi_flow_set_text_if_changed(self->_lbl_pass_status, "Min 8 characters / минимум 8 символов", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
    } else {
        wifi_flow_set_text_if_changed(self->_lbl_pass_status, " ", WifiFlowDiagTextSlot::PassStatus);
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
    // Wi-Fi 6B: return to Saved panel if keyboard was opened via Change password / Cancel с клавиатуры → Saved.
    if (self->_password_from_saved) {
        self->_password_from_saved = false;
        self->clear_password_panel_state();
        self->show_saved_panel();
        return;
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
    if (self->_await_open_connect_ui || self->_saving_in_progress) return;
    const uintptr_t stored = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(o));
    if (stored == 0U) return;
    const uint16_t idx = static_cast<uint16_t>(stored - 1U);
    WifiOpsScanRow  row{};
    if (!wifiOpsGetScanResult(idx, &row)) return;
    const bool is_open = (row.auth == WIFI_AUTH_OPEN);
    if (is_open) {
        self->start_open_connect_from_user(row.ssid);
        return;
    }
    self->open_password_entry(row.ssid);
}

// Wi-Fi 6B: tap on saved network row — open Saved Network panel.
// Wi-Fi 6B: тап по строке сохранённой сети — открыть Saved Network panel.
void LvglWifiFlowScreen::on_saved_row_click(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* tgt = lv_event_get_target(e);
    lv_obj_t* o   = tgt;
    // Climb from child label to the list row button that holds user_data / поднимаемся до кнопки со slot+1.
    while (o && lv_obj_get_user_data(o) == nullptr) {
        o = lv_obj_get_parent(o);
    }
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!o || !self) return;
    const uintptr_t stored = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(o));
    if (stored == 0U) return; // sentinel: not a valid slot / не слот
    const uint8_t slot = static_cast<uint8_t>(stored - 1U);
    if (slot >= wifiCredStoreSavedCount()) return;
    self->open_saved_network(slot);
}

void LvglWifiFlowScreen::on_btn_saved_connect(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (self) self->start_connect_from_saved();
}

// Wi-Fi 6B: Change password — open Password panel with empty TA; Back from there returns here.
// Wi-Fi 6B: смена пароля — Password panel с пустым TA; Back возвращает сюда.
void LvglWifiFlowScreen::on_btn_saved_chpwd(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self || !self->_selectedSavedSsid[0]) return;
    self->_password_from_saved = true;
    self->open_password_entry(self->_selectedSavedSsid);
    // Update hint to indicate context / уточняем подсказку для смены пароля.
    if (self->_lbl_pass_hint) {
        lv_label_set_text(self->_lbl_pass_hint, "Enter new password. On success it will be saved.");
    }
}

// Wi-Fi 6B: Back on Saved panel: cancel if connecting, then return Home.
// Wi-Fi 6B: Back на Saved panel: Cancel если идёт connect, затем возврат Home.
void LvglWifiFlowScreen::on_btn_saved_back(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    if (self->_saving_in_progress) return; // locked until reboot / заблокировано до reboot
    if (self->_await_saved_connect_ui) {
        wifiOpsCancel();
        self->_await_saved_connect_ui = false;
    }
    self->show_home_panel();
}

// Wi-Fi 6C: Remove tapped — show inline confirmation / тап Remove — показываем подтверждение.
void LvglWifiFlowScreen::on_btn_saved_remove(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    // Block if connect or reboot is in progress / блокировать при connect/reboot.
    if (self->_await_saved_connect_ui || self->_saving_in_progress) return;
    if (self->_selectedSavedSlot == 255 || !self->_selectedSavedSsid[0]) return;

    self->_remove_confirm_pending = true;
    if (self->_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette();
        char msg[80];
        snprintf(msg, sizeof(msg), "Remove \"%s\"? Cannot be undone.", self->_selectedSavedSsid);
        wifi_flow_set_text_if_changed(self->_lbl_saved_status, msg, WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        self->_saved_status_terminal = true;
    }
    if (self->_row_saved_normal)  lv_obj_add_flag(self->_row_saved_normal,   LV_OBJ_FLAG_HIDDEN);
    if (self->_row_saved_confirm) lv_obj_clear_flag(self->_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
}

// Wi-Fi 6C: No — cancel confirmation, restore normal row / No — отмена, возврат к нормальному ряду.
void LvglWifiFlowScreen::on_btn_saved_no(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->_remove_confirm_pending = false;
    self->_saved_status_terminal  = false;
    if (self->_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette();
        wifi_flow_set_text_if_changed(self->_lbl_saved_status, " ", WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
    }
    if (self->_row_saved_confirm) lv_obj_add_flag(self->_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
    if (self->_row_saved_normal)  lv_obj_clear_flag(self->_row_saved_normal, LV_OBJ_FLAG_HIDDEN);
}

// Wi-Fi 6C: Yes — confirm Remove; mutate store, persist, return Home. No reboot.
// Wi-Fi 6C: Yes — выполнить удаление; store+persist, затем Home. Без reboot.
void LvglWifiFlowScreen::on_btn_saved_yes(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    const YoRadioPalette& pal = yoradio_palette();

    // Guard: validate slot before any mutation / проверка слота перед мутацией.
    if (self->_selectedSavedSlot == 255 ||
        self->_selectedSavedSlot >= wifiCredStoreSavedCount()) {
        self->_remove_confirm_pending = false;
        if (self->_lbl_saved_status) {
            wifi_flow_set_text_if_changed(self->_lbl_saved_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            self->_saved_status_terminal = true;
        }
        if (self->_row_saved_confirm) lv_obj_add_flag(self->_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
        if (self->_row_saved_normal)  lv_obj_clear_flag(self->_row_saved_normal, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // RemoveAt adjusts lastSSID internally; no extra ClearLastSuccess needed.
    // RemoveAt корректирует lastSSID внутри; дополнительный ClearLastSuccess не нужен.
    const bool removed = wifiCredStoreRemoveAt(self->_selectedSavedSlot);
    if (!removed) {
        self->_remove_confirm_pending = false;
        if (self->_lbl_saved_status) {
            wifi_flow_set_text_if_changed(self->_lbl_saved_status, "Wi-Fi error. Try again.", WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            self->_saved_status_terminal = true;
        }
        if (self->_row_saved_confirm) lv_obj_add_flag(self->_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
        if (self->_row_saved_normal)  lv_obj_clear_flag(self->_row_saved_normal, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const bool persisted = wifiCredStorePersistToFs();
    if (!persisted) {
        // Persist failed: reload RAM from file to restore consistency / RAM восстанавливается из файла.
        wifiCredStoreReloadFromFs();
    }

    // Regardless of persist outcome: rebuild list and return Home. No reboot.
    // В любом случае: обновляем список и возвращаемся на Home. Без reboot.
    self->_remove_confirm_pending = false;
    self->rebuild_saved_list();
    self->show_home_panel();
    // persist-fail UX is minimal (per spec): show_home_panel clears saved state;
    // no dedicated Home status label exists; list reflects reloaded state.
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
