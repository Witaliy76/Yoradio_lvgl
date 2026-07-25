/*
 * LvglWifiFlowScreen — fixed-service Wi-Fi recovery workflow.
 * Wi-Fi сервисный экран: фиксированная тёмная палитра, 5 панелей, async backend, no liveReapplyTheme.
 *
 * Five panel surfaces: Home, Networks, Password, Saved Network and Hotspot.
 * Пять панелей: Home, Networks, Password, Saved Network и Hotspot.
 *
 * Uses a fixed service Dark palette and shared static LVGL styles (S6V11B-stylemem)
 * to limit local-style heap pressure (LV_MEM_SIZE 48K).
 * Backend operations are asynchronous and observed by polling every kPollIntervalMs.
 * AP starts on Hotspot page open and stops on Back (S6V9C strict Hotspot-only SoftAP).
 * All LVGL access is DspTask-only (PageChain RebootRequired, not carousel page).
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_wifi_flow.h"


#include "lvgl.h"
#include <cstdio>
#include <cstring>

#include "../../core/display.h"
#include "../../core/network.h"
#include "../../core/wifi_credentials_store.h"
#include "../../core/wifi_ops_adapter.h"
#include "../../i18n/i18n.h"
#include "../fonts/lv_fonts.h"
#include "../wifi_flow_glyph_utf8.h"
#include "../lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

#include <WiFi.h>

namespace lvgl_ui {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Typed UI strings are owned by the selected compile-time locale package.
// Типизированные UI-строки принадлежат выбранному compile-time locale package.
// ─────────────────────────────────────────────────────────────────────────────

// Intentional state placeholders — do not merge / Намеренные плейсхолдеры — не объединять.
// Empty string "" resets label to no content; single space " " is a visible placeholder slot.
// Пустая строка "" сбрасывает label; одиночный пробел " " — видимый placeholder-слот.
static constexpr char kStrEmpty[]             = "";
static constexpr char kStrStatusPlaceholder[] = " ";

// ─────────────────────────────────────────────────────────────────────────────
// Timing and input/buffer constants / Константы таймингов и буферов
// ─────────────────────────────────────────────────────────────────────────────

// Poll timer period / Период таймера опроса backend
static constexpr uint32_t kPollIntervalMs = 200U;

// Recovery Home idle → auto-open Hotspot panel after this timeout.
// Простой Recovery Home → авто-открытие Hotspot panel по таймеру.
static constexpr uint32_t kRecoveryIdleToHotspotTimeoutMs = 60000U;

// Reboot scheduling delay after successful save / Задержка перезагрузки после сохранения
static constexpr uint32_t kRebootDelayMs = 1800U;

// Legacy credential field is 40 bytes; allow 39 typed chars / legacy поле 40 байт, ввод ≤39.
static constexpr uint32_t kPasswordMaxInputChars = 39U;

// Minimum accepted password length / Минимальная длина пароля
static constexpr size_t   kMinPasswordLen = 8U;

// Profile width threshold: at or below this → compact layout / Порог compact layout
static constexpr uint16_t kCompactProfileMaxWidth = 320U;

// ─────────────────────────────────────────────────────────────────────────────
// Layout constants / Геометрические константы
// ─────────────────────────────────────────────────────────────────────────────

// Panel internal row gap (flex pad_row) / Зазор между элементами панели
static constexpr lv_coord_t kPanelRowGap             = 8;
// Saved and Scan list row gap / Зазор строк списка
static constexpr lv_coord_t kListRowGap              = 6;
// Primary action row gap (Home, Networks, Password) / Зазор кнопок основного ряда действий
static constexpr lv_coord_t kPrimaryActionRowColumnGap = 10;
// Compact action row gap (Saved, Hotspot) / Зазор кнопок компактного ряда
static constexpr lv_coord_t kCompactActionRowColumnGap = 8;
// Header row gap (icon + title) / Зазор в строке заголовка
static constexpr lv_coord_t kHeaderRowColumnGap      = 8;
// Panel border / Рамка панели
static constexpr lv_coord_t kPanelBorderWidth        = 1;
// Panel corner radius — small radius reduces rounded-rect mask pressure.
// Малый радиус снижает нагрузку lv_draw_mask при клиппинге.
static constexpr lv_coord_t kPanelRadius             = 6;
// Textarea corner radius / Радиус textarea
static constexpr lv_coord_t kTextAreaRadius          = 4;
// Textarea min-height by profile width / Мин высота textarea по ширине профиля
static constexpr lv_coord_t kTextAreaMinHeightSmall  = 46;
static constexpr lv_coord_t kTextAreaMinHeightLarge  = 50;
// Keyboard min-height by profile width / Мин высота клавиатуры по ширине профиля
static constexpr lv_coord_t kKeyboardMinHeightSmall  = 120;
static constexpr lv_coord_t kKeyboardMinHeightLarge  = 140;

// ─────────────────────────────────────────────────────────────────────────────
// Shared-style constants / Константы общих стилей
// Used inside wifi_flow_style_ensure(). / Используются внутри wifi_flow_style_ensure().
// ─────────────────────────────────────────────────────────────────────────────

static constexpr lv_coord_t kButtonMinHeightSmall     = 46;
static constexpr lv_coord_t kButtonMinHeightLarge     = 50;
static constexpr lv_coord_t kListRowMinHeightSmall    = 46;
static constexpr lv_coord_t kListRowMinHeightLarge    = 48;
static constexpr lv_coord_t kButtonRadius             = 6;
static constexpr lv_coord_t kButtonBorderWidth        = 1;
static constexpr lv_coord_t kButtonPaddingVertical    = 8;
static constexpr lv_coord_t kButtonPaddingHorizontal  = 10;
static constexpr lv_coord_t kListRowRadius            = 4;
static constexpr lv_coord_t kListRowBorderWidth       = 1;

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics resources and helpers (compile-time off by default, zero runtime overhead)
// Диагностика глитча (compile-time, по умолчанию выкл — нет Serial overhead).
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// Generic diff-update helpers / Вспомогательные функции diff-update
// ─────────────────────────────────────────────────────────────────────────────

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

// Formatted UI text is published only when complete; fallback copy never truncates UTF-8.
// Форматированный UI-текст публикуется только целиком; fallback не усекает UTF-8.
static bool wifi_copy_complete(char* out, size_t cap, const char* text) {
    if (!out || cap == 0U) return false;
    out[0] = '\0';
    if (!text) return false;
    const size_t bytes = strlen(text) + 1U;
    if (bytes > cap) return false;
    memcpy(out, text, bytes);
    return true;
}

template <typename... Args>
static bool wifi_format_checked(char* out, size_t cap, const char* fallback,
                                const char* format, Args... args) {
    if (!out || cap == 0U) return false;
    out[0] = '\0';
    if (format) {
        const int written = snprintf(out, cap, format, args...);
        if (written >= 0 && static_cast<size_t>(written) < cap) return true;
    }
    wifi_copy_complete(out, cap, fallback ? fallback : "");
    return false;
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

// ─────────────────────────────────────────────────────────────────────────────
// Static layout helpers / Вспомогательные функции static layout
// ─────────────────────────────────────────────────────────────────────────────

void style_service_panel(lv_obj_t* panel, const YoRadioPalette& pal) {
    if (!panel) return;
    lv_obj_set_style_bg_color(panel, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, pal.panel_border, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, kPanelBorderWidth, LV_PART_MAIN);
    // Smaller radius reduces rounded-rect mask pressure / малый радиус снижает нагрузку lv_draw_mask.
    lv_obj_set_style_radius(panel, kPanelRadius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding), LV_PART_MAIN);
}

void wifi_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

// ─────────────────────────────────────────────────────────────────────────────
// Font resources / Шрифты экрана
// Profile-based font selection — do not replace profile fonts with hardcoded fonts.
// Выбор шрифта по профилю — не заменять profile fonts на хардкод.
// ─────────────────────────────────────────────────────────────────────────────

// Title: M18 on narrow (≤320), M20 on wide / Заголовок
const void* wifi_title_font_slot() {
    return (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth)
               ? reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr)
               : reinterpret_cast<const void*>(&lv_font_yora_montserrat_20_cyr);
}

// Body: profile font_normal; fallback M16 / Текст тела
const void* wifi_body_font_slot() {
    if (LV_ACTIVE_PROFILE.font_normal) return LV_ACTIVE_PROFILE.font_normal;
    return reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
}

// Status/help lines: M14 on narrow, M16 on wide — service-flow readability.
// Статус: M14 на узких, M16 на широких — читаемость строк статуса.
const void* wifi_status_font_slot() {
    if (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth) {
        return reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr);
    }
    return reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
}

// List rows: body font on narrow, M18 on wide / Строки списка
const void* wifi_list_row_font_slot() {
    if (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth) {
        return wifi_body_font_slot();
    }
    return reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr);
}

// Header icon: one accent icon per panel header.
// Не использовать Montserrat на _kbd — ломает LVGL symbol glyphs.
static const lv_font_t* wifi_header_icon_font() {
    return (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth)
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

// ─────────────────────────────────────────────────────────────────────────────
// Shared Wi-Fi Flow style resources and lifecycle
// S6V11B-stylemem: shared lv_style_t avoids per-button local-style heap pressure.
// Общие style resources: один init на жизненный цикл экрана вместо heap на каждую кнопку.
// Invariant: styles must be dropped only after lv_obj_del(_screen).
// Invariant: сброс стилей только после удаления объектного дерева.
// ─────────────────────────────────────────────────────────────────────────────

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

    const lv_coord_t btn_mh = (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth)
                              ? kButtonMinHeightSmall : kButtonMinHeightLarge;
    const lv_coord_t lr_mh  = (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth)
                              ? kListRowMinHeightSmall : kListRowMinHeightLarge;
    const lv_font_t* body   = static_cast<const lv_font_t*>(wifi_body_font_slot());
    const lv_font_t* lr_f   = static_cast<const lv_font_t*>(wifi_list_row_font_slot());

    lv_style_init(&s_wf_btn_base);
    lv_style_set_min_height(&s_wf_btn_base, btn_mh);
    lv_style_set_radius(&s_wf_btn_base, kButtonRadius);
    lv_style_set_border_width(&s_wf_btn_base, kButtonBorderWidth);
    lv_style_set_pad_ver(&s_wf_btn_base, kButtonPaddingVertical);
    lv_style_set_pad_hor(&s_wf_btn_base, kButtonPaddingHorizontal);
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
    lv_style_set_radius(&s_wf_lr_base, kListRowRadius);
    lv_style_set_border_width(&s_wf_lr_base, kListRowBorderWidth);
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

// ─────────────────────────────────────────────────────────────────────────────
// Style application helpers / Применение стилей к объектам
// ─────────────────────────────────────────────────────────────────────────────

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

// WIFIREF-D2: Event adapter — screen instance from lv_event user_data (baseline cast only).
// WIFIREF-D2: адаптер события — instance экрана из user_data (только cast baseline).
static LvglWifiFlowScreen* wifi_flow_self_from_event(lv_event_t* e) {
    return static_cast<LvglWifiFlowScreen*>(lv_event_get_user_data(e));
}

// WIFIREF-D2: Timer adapter — screen instance from lv_timer user_data (baseline cast only).
// WIFIREF-D2: адаптер таймера — instance экрана из user_data таймера.
static LvglWifiFlowScreen* wifi_flow_self_from_timer(lv_timer_t* t) {
    return static_cast<LvglWifiFlowScreen*>(t->user_data);
}

// Local action-row baseline: transparent flex ROW, given column gap.
// Does not set scrollable, callbacks, child roles or visibility.
// Базовая строка действий: прозрачный flex ROW с заданным gap.
static void style_action_row(lv_obj_t* row, lv_coord_t column_gap) {
    if (!row) return;
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, column_gap, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout builders / Билдеры разметки
// Called only from create(), in order. Access to private members via `self`.
// Вызываются только из create(), в порядке. Доступ к members через `self`.
// ─────────────────────────────────────────────────────────────────────────────

ScreenType LvglWifiFlowScreen::screenType() const {
    return ScreenType::RebootRequired;
}

// ─────────────────────────────────────────────────────────────────────────────
// Local UI state and secret cleanup / Сброс локального UI state и секретов
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::clear_password_secrets() {
    // Keyboard must be detached before textarea is cleared; LVGL may fire VALUE_CHANGED otherwise.
    // Клавиатура должна быть отсоединена до очистки textarea — иначе LVGL может сгенерировать VALUE_CHANGED.
    if (_kbd) {
        lv_keyboard_set_textarea(_kbd, nullptr);
    }
    if (_ta_password) {
        lv_textarea_set_text(_ta_password, "");
    }
    memset(_passwordScratch, 0, sizeof(_passwordScratch));
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
}

void LvglWifiFlowScreen::clear_password_panel_state() {
    // Operation / await flags
    _await_connect_ui               = false;
    _saving_in_progress             = false;
    // Terminal / status lock flags
    _pass_status_terminal           = false;
    _skip_next_ta_pass_status_sync  = false;
    // Selected SSID and password-origin state
    memset(_selectedSsid, 0, sizeof(_selectedSsid));
    // Secret and textarea cleanup (keyboard detach must precede textarea clear)
    clear_password_secrets();
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel visibility and transitions / Видимость панелей и переходы
// ─────────────────────────────────────────────────────────────────────────────

// Show only the specified panel root; hide all others.
// Sets _home_visible to reflect whether the target is the Home panel.
// Does NOT cancel ops, clear state, arm idle or touch AP.
// Показывает только указанную панель. Не отменяет ops, не сбрасывает state, не трогает AP.
void LvglWifiFlowScreen::_showOnlyPanel(lv_obj_t* panel) {
    _home_visible = (panel == _panel_home);
    if (_panel_home)    { if (panel == _panel_home)    lv_obj_clear_flag(_panel_home,    LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_panel_home,    LV_OBJ_FLAG_HIDDEN); }
    if (_panel_net)     { if (panel == _panel_net)     lv_obj_clear_flag(_panel_net,     LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_panel_net,     LV_OBJ_FLAG_HIDDEN); }
    if (_panel_pass)    { if (panel == _panel_pass)    lv_obj_clear_flag(_panel_pass,    LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_panel_pass,    LV_OBJ_FLAG_HIDDEN); }
    if (_panel_saved)   { if (panel == _panel_saved)   lv_obj_clear_flag(_panel_saved,   LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_panel_saved,   LV_OBJ_FLAG_HIDDEN); }
    if (_panel_hotspot) { if (panel == _panel_hotspot) lv_obj_clear_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN); }
}

void LvglWifiFlowScreen::show_home_panel() {
#if WIFI_FLOW_DIAG_GLITCH
    ++g_diag_show_home_panel_calls;
#endif
    // Cleanup phase: cancel open connect, clear Saved and Password state.
    // Фаза очистки: отмена open connect, сброс Saved и Password state.
    cancel_open_connect_state();
    clear_saved_state();
    clear_password_panel_state();
    // Panel visibility.
    _showOnlyPanel(_panel_home);
    // Label and button synchronization based on entry context.
    // Синхронизация меток и кнопок на основе контекста входа.
    sync_home_boot_failure_ui();
    // Arm recovery idle if no operations are blocking.
    // Армировать recovery idle если нет блокирующих операций.
    arm_recovery_idle_if_home_only();
}

void LvglWifiFlowScreen::show_networks_panel() {
    // Cleanup: disarm idle and clear password state.
    // Очистка: disarm idle и сброс Password state.
    disarm_boot_idle_timer();
    clear_password_panel_state();
    _showOnlyPanel(_panel_net);
}

void LvglWifiFlowScreen::show_password_panel() {
    // Cleanup: disarm idle and cancel any in-flight open connect.
    // Очистка: disarm idle и отмена open connect.
    disarm_boot_idle_timer();
    cancel_open_connect_state();
    _showOnlyPanel(_panel_pass);
}

// Show Saved Network panel; state must already be set by open_saved_network().
// Показывает Saved panel; state должен быть установлен через open_saved_network().
void LvglWifiFlowScreen::show_saved_panel() {
    disarm_boot_idle_timer();
    cancel_open_connect_state();
    _showOnlyPanel(_panel_saved);
}

void LvglWifiFlowScreen::show_hotspot_panel() {
    // Cleanup and SoftAP start in existing order — do not reorder.
    // Очистка и запуск SoftAP в прежнем порядке — не переставлять.
    disarm_boot_idle_timer();
    cancel_open_connect_state();
    network.recoveryEnsureSoftAP();
    sync_hotspot_panel_labels();
    _showOnlyPanel(_panel_hotspot);
}

bool LvglWifiFlowScreen::is_recovery_home_only_visible() const {
    if (!_panel_home || lv_obj_has_flag(_panel_home, LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_net     && !lv_obj_has_flag(_panel_net,     LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_pass    && !lv_obj_has_flag(_panel_pass,    LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_saved   && !lv_obj_has_flag(_panel_saved,   LV_OBJ_FLAG_HIDDEN)) return false;
    if (_panel_hotspot && !lv_obj_has_flag(_panel_hotspot, LV_OBJ_FLAG_HIDDEN)) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recovery idle UI / UI recovery idle-таймера
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::disarm_boot_idle_timer() {
    _boot_idle_armed       = false;
    _boot_idle_deadline_ms = 0;
    if (_lbl_recovery_idle_countdown) {
        wifi_flow_set_text_if_changed(_lbl_recovery_idle_countdown, kStrEmpty, WifiFlowDiagTextSlot::None);
    }
}

bool LvglWifiFlowScreen::recovery_idle_ops_block() const {
    // All in-flight async operations block the idle auto-Hotspot timer.
    // Все async операции в полёте блокируют auto-Hotspot таймер.
    return _await_scan_ui || _await_connect_ui || _await_saved_connect_ui || _await_open_connect_ui || _saving_in_progress;
}

void LvglWifiFlowScreen::arm_recovery_idle_if_home_only() {
    if (!is_recovery_home_only_visible() || recovery_idle_ops_block()) {
        disarm_boot_idle_timer();
        return;
    }
    _boot_idle_deadline_ms = millis() + kRecoveryIdleToHotspotTimeoutMs;
    _boot_idle_armed       = true;
    // One LVGL write on arm only — avoids per-second full_refresh churn.
    // Один запрос к LVGL при арме — без периодических обновлений на каждый тик.
    if (_lbl_recovery_idle_countdown) {
        char countdown[96];
        wifi_format_checked(
            countdown, sizeof(countdown), i18n::text(i18n::TextId::WifiHotspotTitle),
            i18n::text(i18n::TextId::WifiHotspotAutoStartFormat),
            static_cast<unsigned>(kRecoveryIdleToHotspotTimeoutMs / 1000U));
        wifi_flow_set_text_if_changed(
            _lbl_recovery_idle_countdown, countdown, WifiFlowDiagTextSlot::None);
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
    char ssid_line[56];
    wifi_format_checked(ssid_line, sizeof(ssid_line), apSsid,
                        i18n::text(i18n::TextId::WifiHotspotSsidFormat), apSsid);
    wifi_flow_set_text_if_changed(_lbl_hotspot_ssid, ssid_line, WifiFlowDiagTextSlot::None);
    wifi_flow_set_text_if_changed(
        _lbl_hotspot_pwd, i18n::text(i18n::TextId::WifiHotspotPasswordOpen), WifiFlowDiagTextSlot::None);
    const IPAddress ap_ip = WiFi.softAPIP();
    char            ip_line[96];
    if (static_cast<uint32_t>(ap_ip) != 0U) {
        char ip_value[16];
        wifi_format_checked(ip_value, sizeof(ip_value), "",
                            "%u.%u.%u.%u",
                            static_cast<unsigned>(ap_ip[0]), static_cast<unsigned>(ap_ip[1]),
                            static_cast<unsigned>(ap_ip[2]), static_cast<unsigned>(ap_ip[3]));
        wifi_format_checked(ip_line, sizeof(ip_line), ip_value,
                            i18n::text(i18n::TextId::WifiHotspotIpFormat), ip_value);
    } else {
        static constexpr char kExpectedApIp[] = "192.168.4.1";
        wifi_format_checked(ip_line, sizeof(ip_line), kExpectedApIp,
                            i18n::text(i18n::TextId::WifiHotspotIpPendingFormat), kExpectedApIp);
    }
    wifi_flow_set_text_if_changed(_lbl_hotspot_ip, ip_line, WifiFlowDiagTextSlot::None);
    static constexpr char kHotspotSetupUrl[] = "http://192.168.4.1";
    char help_line[160];
    wifi_format_checked(help_line, sizeof(help_line), kHotspotSetupUrl,
                        i18n::text(i18n::TextId::WifiHotspotHelpFormat), kHotspotSetupUrl);
    wifi_flow_set_text_if_changed(
        _lbl_hotspot_help, help_line, WifiFlowDiagTextSlot::None);
}

// Home subtitle and Back visibility depend on how the user entered Wi-Fi Flow.
// Подзаголовок Home и видимость Back зависят от контекста входа в Wi-Fi Flow.
void LvglWifiFlowScreen::sync_home_boot_failure_ui() {
#if WIFI_FLOW_DIAG_GLITCH
    ++g_diag_sync_home_boot_failure_ui_calls;
#endif
    if (!_sub_home) return;
    const YoRadioPalette& pal = yoradio_palette_service();
    if (_entered_from_boot_failure) {
        // Boot-failure entry: no Back (device cannot return to Main without a working connection).
        // Вход при boot-failure: нет Back (без рабочего Wi-Fi вернуться на Main нельзя).
        wifi_flow_set_text_if_changed(_sub_home,
                                      i18n::text(i18n::TextId::WifiHomeSubtitleBootFailure),
                                      WifiFlowDiagTextSlot::SubHome);
        if (_btn_back_home) lv_obj_add_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    } else if (_entered_from_runtime_disconnect) {
        // Runtime-disconnect entry: Back visible; subtitle differs from manual entry.
        // Вход при runtime-disconnect: Back виден; subtitle отличается от manual.
        wifi_flow_set_text_if_changed(_sub_home,
                                      i18n::text(i18n::TextId::WifiHomeSubtitleDisconnected),
                                      WifiFlowDiagTextSlot::SubHome);
        if (_btn_back_home) lv_obj_clear_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Manual entry: Back visible; neutral subtitle.
        // Manual вход: Back виден; нейтральный subtitle.
        wifi_flow_set_text_if_changed(_sub_home,
                                      i18n::text(i18n::TextId::WifiHomeSubtitleManual),
                                      WifiFlowDiagTextSlot::SubHome);
        if (_btn_back_home) lv_obj_clear_flag(_btn_back_home, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(_sub_home, pal.text_secondary, LV_PART_MAIN);
}

// ─────────────────────────────────────────────────────────────────────────────
// Saved Network state ownership / Владение состоянием Saved Network
// ─────────────────────────────────────────────────────────────────────────────

// Reset Saved panel local state on navigation or after remove.
// Сброс локального состояния Saved panel при навигации или удалении.
void LvglWifiFlowScreen::clear_saved_state() {
    // Selected slot and SSID
    _selectedSavedSlot      = 255;
    memset(_selectedSavedSsid, 0, sizeof(_selectedSavedSsid));
    // Await and terminal flags
    _await_saved_connect_ui = false;
    _saved_status_terminal  = false;
    // Password and remove confirmation state
    _password_from_saved    = false;
    _remove_confirm_pending = false;
}

// Toggle the remove confirmation row and sync the _remove_confirm_pending flag.
// In all code paths, flag and row visibility change together.
// Переключает ряд подтверждения удаления; flag и видимость всегда меняются вместе.
void LvglWifiFlowScreen::_setSavedRemoveConfirmationVisible(bool visible) {
    _remove_confirm_pending = visible;
    if (visible) {
        if (_row_saved_normal)  lv_obj_add_flag(_row_saved_normal,   LV_OBJ_FLAG_HIDDEN);
        if (_row_saved_confirm) lv_obj_clear_flag(_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (_row_saved_confirm) lv_obj_add_flag(_row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
        if (_row_saved_normal)  lv_obj_clear_flag(_row_saved_normal, LV_OBJ_FLAG_HIDDEN);
    }
}

// Open Saved Network panel for a given slot: populate UI and transition to Saved panel.
// Открыть Saved Network panel для слота: заполнить UI и перейти на Saved panel.
void LvglWifiFlowScreen::open_saved_network(uint8_t slot) {
    if (slot >= WIFI_CRED_STORE_CAPACITY) return;
    WifiCredStoreEntryView v{};
    if (!wifiCredStoreGetEntry(slot, &v) || !v.ssid || !v.ssid[0]) return;

    // Reset Saved state before populating with new slot data.
    // Сброс Saved state перед заполнением данными нового слота.
    clear_saved_state();
    _selectedSavedSlot = slot;
    strlcpy(_selectedSavedSsid, v.ssid, sizeof(_selectedSavedSsid));

    if (_lbl_saved_ssid) {
        wifi_flow_set_text_if_changed(_lbl_saved_ssid, _selectedSavedSsid, WifiFlowDiagTextSlot::None);
    }
    if (_lbl_saved_sub) {
        wifi_flow_set_text_if_changed(
            _lbl_saved_sub, i18n::text(i18n::TextId::WifiSavedSubtitle), WifiFlowDiagTextSlot::None);
    }
    if (_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_saved_status, kStrStatusPlaceholder, WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
    }

    // Re-enable all buttons for a fresh panel session.
    // Включить все кнопки для нового сеанса панели.
    if (_btn_saved_connect) wifi_flow_set_state_if_changed(_btn_saved_connect, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedConnect);
    if (_btn_saved_chpwd)   wifi_flow_set_state_if_changed(_btn_saved_chpwd,   LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedChpwd);
    if (_btn_saved_back)    wifi_flow_set_state_if_changed(_btn_saved_back,    LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedBack);
    if (_btn_saved_remove)  wifi_flow_set_state_if_changed(_btn_saved_remove,  LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::SavedRemove);

    // Ensure normal row visible, confirm row hidden.
    // Нормальный ряд виден, ряд подтверждения скрыт.
    _setSavedRemoveConfirmationVisible(false);

    show_saved_panel();
}

// Open Password panel for a given SSID: reset state, attach keyboard, transition to panel.
// Открыть Password panel для SSID: сбросить state, привязать клавиатуру, перейти на panel.
void LvglWifiFlowScreen::open_password_entry(const char* ssid_utf8) {
    cancel_open_connect_state();
    if (!ssid_utf8 || !ssid_utf8[0] || !_lbl_pass_ssid || !_ta_password || !_kbd) return;

    // Clear secrets and operation state before populating new SSID.
    // Сброс секретов и состояния операции перед заполнением нового SSID.
    clear_password_secrets();
    _await_connect_ui               = false;
    _pass_status_terminal           = false;
    _skip_next_ta_pass_status_sync  = false;
    memset(_selectedSsid, 0, sizeof(_selectedSsid));
    strlcpy(_selectedSsid, ssid_utf8, sizeof(_selectedSsid));

    // Populate SSID label, clear textarea, then attach keyboard.
    // Keyboard must be attached after textarea is cleared to avoid spurious VALUE_CHANGED.
    // Заполнить SSID, очистить textarea, затем привязать клавиатуру.
    lv_label_set_text(_lbl_pass_ssid, _selectedSsid);
    lv_textarea_set_text(_ta_password, "");
    lv_keyboard_set_textarea(_kbd, _ta_password);

    if (_lbl_pass_status) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(
            _lbl_pass_status, i18n::text(i18n::TextId::WifiPasswordPrompt), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    }
    show_password_panel();
    sync_connect_button_enabled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel interaction-state helpers / Вспомогательные функции состояния взаимодействия
// ─────────────────────────────────────────────────────────────────────────────

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
    // Reboot timer adapter — one-shot ESP.restart(); self not used (baseline).
    // Адаптер reboot-таймера — однократный ESP.restart(); self не используется.
    (void)t;
    ESP.restart();
}

// ─────────────────────────────────────────────────────────────────────────────
// Credential persistence pipelines / Конвейеры сохранения credentials
// Cases A–E correspond to password-protected and open connect outcomes.
// Cases A–E — исходы подключения с паролем и к открытой сети.
//
// These handlers are called by result handlers after a successful Wi-Fi connect.
// They must not be called from operation start or poll paths.
// Вызываются из result handlers после успешного connect; не вызываются из start или poll.
// ─────────────────────────────────────────────────────────────────────────────

// Password-protected success persistence.
// Cases A–E: password ownership = _connectCandidatePass (never _passwordScratch or textarea).
// Password must be copied to _connectCandidatePass before textarea may clear.
// Cases A–E: источник пароля — _connectCandidatePass (не _passwordScratch и не textarea).
void LvglWifiFlowScreen::handle_successful_connect_persist() {
    if (!_lbl_pass_status) return;
    const YoRadioPalette& pal = yoradio_palette_service();

    // Case E: SSID or password rejects legacy csv format constraints.
    // Case E: SSID или пароль не проходят проверку legacy csv формата.
    if (!wifiCredStoreEntryFitsLegacyFile(_selectedSsid, _connectCandidatePass)) {
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiCannotSaveNetwork), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        return;
    }

    const int existingIdx = wifiCredStoreFindIndexBySsid(_selectedSsid);

    if (existingIdx < 0) {
        // Case D: store is full and SSID is new — cannot add.
        // Case D: хранилище заполнено, SSID новый — добавить нельзя.
        if (wifiCredStoreSavedCount() >= WIFI_CRED_STORE_CAPACITY) {
            wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiSavedNetworksFull), WifiFlowDiagTextSlot::PassStatus);
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }

        // Case A: new SSID, free slot available — insert and persist.
        // Case A: новый SSID, есть свободный слот — вставляем и сохраняем.
        if (!wifiCredStoreAddOrUpdate(_selectedSsid, _connectCandidatePass)) {
            wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiCouldNotSaveNetwork), WifiFlowDiagTextSlot::PassStatus);
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }
        const int newIdx = wifiCredStoreFindIndexBySsid(_selectedSsid);
        if (!wifiCredStorePersistToFs()) {
            // RAM mutated but file write failed: reload to restore consistency.
            // RAM изменён, запись файла не удалась: перезагружаем из файла для consistency.
            wifiCredStoreReloadFromFs();
            wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiCouldNotSaveNetwork), WifiFlowDiagTextSlot::PassStatus);
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
            _pass_status_terminal = true;
            return;
        }
        if (newIdx >= 0) {
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(newIdx));
        }

    } else {
        // SSID found in store: read current stored password for comparison.
        // SSID найден: читаем текущий сохранённый пароль для сравнения.
        const char* storedPass = "";
        WifiCredStoreEntryView ev{};
        if (wifiCredStoreGetEntry(static_cast<uint8_t>(existingIdx), &ev)) {
            storedPass = ev.password;
        }

        if (strcmp(storedPass, _connectCandidatePass) == 0) {
            // Case B: SSID exists, same password — no file write needed, only update last-success.
            // Case B: тот же пароль — файл не меняем, только обновляем last-success.
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        } else {
            // Case C: SSID exists, password changed — use staging API to safely overwrite.
            // Case C: пароль изменился — staging API для безопасной перезаписи.
            if (!wifiCredStagingBegin(static_cast<uint8_t>(existingIdx)) ||
                !wifiCredStagingSetCandidatePassword(_connectCandidatePass) ||
                !wifiCredStagingCommitToStoredPassword()) {
                wifiCredStagingDiscard();
                wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiCouldNotUpdatePassword), WifiFlowDiagTextSlot::PassStatus);
                lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
                _pass_status_terminal = true;
                return;
            }
            if (!wifiCredStorePersistToFs()) {
                // RAM mutated via staging; reload restores to last known good file state.
                // RAM изменён через staging; reload восстанавливает из последнего файла.
                wifiCredStoreReloadFromFs();
                wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiCouldNotSaveNetwork), WifiFlowDiagTextSlot::PassStatus);
                lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
                _pass_status_terminal = true;
                return;
            }
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        }
    }

    // All cases succeeded: zero candidate, lock UI, schedule reboot.
    // Все cases успешны: обнуляем кандидата, блокируем UI, планируем reboot.
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
    _saving_in_progress   = true;
    _pass_status_terminal = true;
    wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiSavedRestarting), WifiFlowDiagTextSlot::PassStatus);
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
    set_password_panel_saving_ui();

    // One-shot timer then ESP.restart() / однократный таймер, затем перезагрузка.
    if (!_reboot_timer) {
        _reboot_timer = lv_timer_create(on_reboot_timer, kRebootDelayMs, this);
        lv_timer_set_repeat_count(_reboot_timer, 1);
    }
}

// Open-network success persistence.
// Cases A–E mirror password flow but use empty password and _selectedOpenSsid ownership.
// Never routes through _connectCandidatePass or handle_successful_connect_persist().
// Cases A–E зеркалят password flow, но с пустым паролем и ownership _selectedOpenSsid.
void LvglWifiFlowScreen::handle_open_network_success_persist() {
    if (!_lbl_net_status) return;
    const YoRadioPalette& pal = yoradio_palette_service();
    static const char    kEmptyPass[] = "";

    // Case E: SSID rejects legacy csv format constraints.
    // Case E: SSID не проходит проверку legacy csv формата.
    if (!wifiCredStoreEntryFitsLegacyFile(_selectedOpenSsid, kEmptyPass)) {
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiCannotSaveNetwork), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        set_networks_panel_connecting_ui(false);
        return;
    }

    const int existingIdx = wifiCredStoreFindIndexBySsid(_selectedOpenSsid);

    if (existingIdx < 0) {
        // Case D: store is full and SSID is new — cannot add.
        // Case D: хранилище заполнено, SSID новый — добавить нельзя.
        if (wifiCredStoreSavedCount() >= WIFI_CRED_STORE_CAPACITY) {
            wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiSavedNetworksFull), WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            _open_status_terminal = true;
            set_networks_panel_connecting_ui(false);
            return;
        }

        // Case A: new SSID, free slot available — insert and persist.
        // Case A: новый SSID, есть свободный слот — вставляем и сохраняем.
        if (!wifiCredStoreAddOrUpdate(_selectedOpenSsid, kEmptyPass)) {
            wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiCouldNotSaveNetwork), WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            _open_status_terminal = true;
            set_networks_panel_connecting_ui(false);
            return;
        }
        const int newIdx = wifiCredStoreFindIndexBySsid(_selectedOpenSsid);
        if (!wifiCredStorePersistToFs()) {
            // RAM mutated but file write failed: reload to restore consistency.
            // RAM изменён, запись файла не удалась: перезагружаем из файла.
            wifiCredStoreReloadFromFs();
            wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiCouldNotSaveNetwork), WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            _open_status_terminal = true;
            set_networks_panel_connecting_ui(false);
            return;
        }
        if (newIdx >= 0) {
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(newIdx));
        }

    } else {
        // SSID found in store: read current stored password for comparison.
        // SSID найден: читаем текущий сохранённый пароль для сравнения.
        const char*              storedPass = "";
        WifiCredStoreEntryView ev{};
        if (wifiCredStoreGetEntry(static_cast<uint8_t>(existingIdx), &ev)) {
            storedPass = ev.password ? ev.password : "";
        }

        if (strcmp(storedPass, kEmptyPass) == 0) {
            // Case B: SSID exists, same empty password — no file write, only last-success.
            // Case B: тот же пустой пароль — файл не меняем, только last-success.
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        } else {
            // Case C: SSID exists, was password-protected — staging overwrite to empty.
            // Case C: был пароль — staging перезапись на пустой.
            if (!wifiCredStagingBegin(static_cast<uint8_t>(existingIdx)) ||
                !wifiCredStagingSetCandidatePassword(kEmptyPass) || !wifiCredStagingCommitToStoredPassword()) {
                wifiCredStagingDiscard();
                wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiCouldNotSaveNetwork), WifiFlowDiagTextSlot::NetStatus);
                lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
                _open_status_terminal = true;
                set_networks_panel_connecting_ui(false);
                return;
            }
            if (!wifiCredStorePersistToFs()) {
                // RAM mutated via staging; reload restores to last known good file state.
                // RAM изменён через staging; reload восстанавливает из последнего файла.
                wifiCredStoreReloadFromFs();
                wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiCouldNotSaveNetwork), WifiFlowDiagTextSlot::NetStatus);
                lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
                _open_status_terminal = true;
                set_networks_panel_connecting_ui(false);
                return;
            }
            wifiCredStoreSetLastSuccessFromSlot(static_cast<uint8_t>(existingIdx));
        }
    }

    // All cases succeeded: lock UI, schedule reboot.
    // Все cases успешны: блокируем UI, планируем reboot.
    _open_status_terminal = true;
    _saving_in_progress   = true;
    wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiSavedRestarting), WifiFlowDiagTextSlot::NetStatus);
    lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
    set_networks_panel_saving_ui();

    if (!_reboot_timer) {
        _reboot_timer = lv_timer_create(on_reboot_timer, kRebootDelayMs, this);
        lv_timer_set_repeat_count(_reboot_timer, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Saved-network removal persistence / Удаление сохранённой сети
// on_btn_saved_yes() performs store mutation; no reboot on success or failure.
// on_btn_saved_yes() мутирует store; reboot не планируется ни при успехе, ни при ошибке.
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Open-network connect pipeline / Конвейер подключения к открытой сети
// Open networks use empty credentials. This is a SEPARATE flow from password connect.
// Open сети используют пустые credentials — ОТДЕЛЬНЫЙ flow от password connect.
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::start_open_connect_from_user(const char* ssid) {
    if (!ssid || !ssid[0] || !_lbl_net_status) return;
    if (_await_open_connect_ui || _saving_in_progress) return;

    memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
    strlcpy(_selectedOpenSsid, ssid, sizeof(_selectedOpenSsid));
    _open_status_terminal = false;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) && cur.busy) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
        return;
    }

    // Open network: always use an empty password — never _connectCandidatePass.
    // Open сеть: всегда пустой пароль — никогда не _connectCandidatePass.
    static const char kEmptyPass[] = "";
    const bool started = wifiOpsRequestConnectWithPassword(_selectedOpenSsid, kEmptyPass, true);
    if (!started) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
        return;
    }

    _await_open_connect_ui = true;
    const YoRadioPalette& pal = yoradio_palette_service();
    wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiConnectingOpen), WifiFlowDiagTextSlot::NetStatus);
    lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
    set_networks_panel_connecting_ui(true);
}

// Open connect result routing — stays on Networks panel (no panel transition on failure).
// Роутинг результата open connect — остаётся на Networks panel при ошибке.
void LvglWifiFlowScreen::handle_open_connect_finished() {
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    _await_open_connect_ui = false;
    const YoRadioPalette& pal = yoradio_palette_service();

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
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiOpenConnectSignalFailed), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    case WifiOpsResult::NoNetwork:
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiNoNetwork), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    case WifiOpsResult::Cancelled:
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiCancelled), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    case WifiOpsResult::InternalError:
    case WifiOpsResult::Busy:
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    default:
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        _open_status_terminal = true;
        break;
    }
    set_networks_panel_connecting_ui(false);
}

// Password connect result routing. Each result maps to a specific UX contract.
// These branches must NOT be merged across the Password/Open/Saved flows.
// Роутинг результата password connect. Ветки не объединять с Open/Saved flows.
void LvglWifiFlowScreen::handle_connect_finished() {
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    _await_connect_ui = false;
    const YoRadioPalette& pal = yoradio_palette_service();
    set_password_panel_connecting_ui(false);

    if (!_lbl_pass_status) return;

    switch (snap.lastResult) {
    case WifiOpsResult::Success:
    case WifiOpsResult::AlreadyConnected:
        // Persist credentials and schedule reboot; textarea is cleared here so TA event fires clean.
        // Сохранить credentials и запланировать reboot; textarea очищается здесь (до callback TA).
        if (_ta_password) {
            _skip_next_ta_pass_status_sync = true;
            lv_textarea_set_text(_ta_password, "");
        }
        handle_successful_connect_persist();
        return; // persist fn owns further UI; do not fall through to sync_connect_button_enabled.
    case WifiOpsResult::AuthFailed:
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiWrongPassword), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Timeout:
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiConnectTimeout), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::NoNetwork:
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiNoNetwork), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Cancelled:
        // Cancelled behaves like Back: navigate to Networks and clear password state.
        // Cancelled — как Back: переход в Networks, сброс state пароля.
        clear_password_panel_state();
        show_networks_panel();
        return;
    case WifiOpsResult::InternalError:
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiInternalError), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    case WifiOpsResult::Busy:
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiBusy), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    default:
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiConnectFinished), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
        _pass_status_terminal = true;
        break;
    }
    sync_connect_button_enabled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Password-protected connect pipeline / Конвейер подключения с паролем
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::start_connect_from_user() {
    // Guards: require SSID, no in-flight op, no save pending.
    if (!_ta_password || !_lbl_pass_status || !_selectedSsid[0]) return;
    if (_await_connect_ui) return;
    if (_await_open_connect_ui) return;
    // Blocked while saving/rebooting — new connect attempt is not allowed.
    // Заблокировано во время saving/reboot.
    if (_saving_in_progress) return;

    // New attempt: reset terminal lock so status and TA validation respond again.
    // Новая попытка: сброс terminal lock — статус и TA снова реагируют.
    _pass_status_terminal           = false;
    _skip_next_ta_pass_status_sync  = false;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) && cur.busy) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiBusy), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    const char* pw_in = lv_textarea_get_text(_ta_password);
    const size_t plen = pw_in ? strlen(pw_in) : 0U;
    if (plen < kMinPasswordLen) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiPasswordMinChars), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    // Copy candidate password for post-Success persistence (must happen before textarea may clear).
    // Candidate must be filled before the textarea is cleared or the op is handed off.
    // Кандидат заполняется до очистки textarea — нужен для сохранения после Success.
    memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
    strlcpy(_connectCandidatePass, pw_in, sizeof(_connectCandidatePass));

    // Pass a separate on-stack copy to the backend; zero it immediately after.
    // Передаём отдельную копию в backend; обнуляем сразу после передачи.
    char tmp[40]{};
    strlcpy(tmp, pw_in, sizeof(tmp));
    const bool started = wifiOpsRequestConnectWithPassword(_selectedSsid, tmp, true);
    memset(tmp, 0, sizeof(tmp));

    const YoRadioPalette& pal = yoradio_palette_service();
    if (!started) {
        memset(_connectCandidatePass, 0, sizeof(_connectCandidatePass));
        WifiOpsSnapshot after{};
        (void)wifiOpsGetSnapshot(&after);
        if (after.busy) {
            wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiBusy), WifiFlowDiagTextSlot::PassStatus);
        } else {
            wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiConnectCannotStart), WifiFlowDiagTextSlot::PassStatus);
        }
        lv_obj_set_style_text_color(_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }

    _await_connect_ui = true;
    wifi_flow_set_text_if_changed(_lbl_pass_status, i18n::text(i18n::TextId::WifiConnectingWarning), WifiFlowDiagTextSlot::PassStatus);
    lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    set_password_panel_connecting_ui(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan operation pipeline / Конвейер операции сканирования
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::start_scan_from_user() {
    // Guards: open connect or save pending must not coexist with scan.
    // Guards: open connect или save не могут идти параллельно со сканом.
    if (_await_open_connect_ui || _saving_in_progress) return;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) &&
        (cur.phase == WifiOpsPhase::Scanning || (cur.busy && cur.currentOp == WifiOpsOp::Scan))) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiScanning), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }
    if (!wifiOpsRequestScan()) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiScanNotStarted), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
        return;
    }
    // Scan started: navigate to Networks and show starting status.
    // Скан запущен: переходим на Networks, статус «запуск».
    _await_scan_ui = true;
    show_networks_panel();
    wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiScanStarting), WifiFlowDiagTextSlot::NetStatus);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic list pipeline / Конвейер динамических списков
// ─────────────────────────────────────────────────────────────────────────────

// Rebuild the Home saved-network list from the credential store.
// user_data = slot + 1 (1-based; zero is never used). Never zero.
// user_data = slot + 1 (1-based; ноль никогда не используется).
void LvglWifiFlowScreen::rebuild_saved_list() {
#if WIFI_FLOW_DIAG_GLITCH
    ++g_diag_rebuild_saved_list_calls;
#endif
    if (!_list_saved) return;
    lv_obj_clean(_list_saved);
    const uint8_t n = wifiCredStoreSavedCount();
    if (n == 0) {
        // Montserrat cannot render LVGL built-in symbol glyphs (U+F00B → boxes); no icon here.
        // Montserrat не рендерит LVGL-символы (U+F00B → квадраты); иконки не используем.
        lv_obj_t* empty = lv_list_add_btn(
            _list_saved, nullptr, i18n::text(i18n::TextId::BootNoSavedWifiNetworks));
        if (empty) wifi_apply_list_row_empty(empty);
        return;
    }
    const uint8_t cap = (n > 5) ? 5 : n;
    for (uint8_t i = 0; i < cap; ++i) {
        WifiCredStoreEntryView v{};
        if (!wifiCredStoreGetEntry(i, &v) || !v.ssid) continue;
        char line[40];
        wifi_format_checked(line, sizeof(line), v.ssid, "%u  %s",
                            static_cast<unsigned>(i + 1U), v.ssid);
        lv_obj_t* btn = lv_list_add_btn(_list_saved, nullptr, line);
        if (!btn) {
            // Lightweight OOM guard: stop row creation gracefully / мягкий guard при нехватке памяти.
            if (_sub_home) {
                wifi_flow_set_text_if_changed(_sub_home, i18n::text(i18n::TextId::WifiUiMemoryLow), WifiFlowDiagTextSlot::SubHome);
            }
            break;
        }
        wifi_apply_list_row_normal(btn);
        // user_data = slot + 1 (1-based index; on_saved_row_click subtracts 1 before use).
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(i) + 1U)));
        lv_obj_add_event_cb(btn, on_saved_row_click, LV_EVENT_CLICKED, this);
    }
}

// Rebuild the scan result list from the current WifiOps snapshot.
// user_data = result_index + 1 (1-based; on_scan_row_click subtracts 1).
// user_data = result_index + 1 (1-based; on_scan_row_click вычитает 1).
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
        // U+2022 BULLET • matches Main meta row separator; wider spacing for scan readability.
        // U+2022 «•» — тот же разделитель что на Main; wider spacing для читаемости.
        wifi_format_checked(
            line, sizeof(line), scan_row.ssid,
            i18n::text(is_open ? i18n::TextId::WifiScanOpenRowFormat
                               : i18n::TextId::WifiScanSecuredRowFormat),
            scan_row.ssid,
            static_cast<int>(scan_row.rssi));

        lv_obj_t* btn = lv_list_add_btn(_list_scan, nullptr, line);
        if (!btn) {
            // Lightweight OOM guard / мягкий guard при нехватке памяти.
            if (_lbl_net_status) {
                const YoRadioPalette& pal = yoradio_palette_service();
                wifi_flow_set_text_if_changed(_lbl_net_status, i18n::text(i18n::TextId::WifiScanMemoryLow), WifiFlowDiagTextSlot::NetStatus);
                lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
            }
            break;
        }
        wifi_apply_list_row_normal(btn);
        // user_data = result index + 1 (1-based).
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(i) + 1U)));
        lv_obj_add_event_cb(btn, on_scan_row_click, LV_EVENT_CLICKED, this);
    }
    if (snap.scanCount == 0) {
        lv_obj_t* empty = lv_list_add_btn(
            _list_scan, nullptr, i18n::text(i18n::TextId::WifiNoNetworksFound));
        if (empty) wifi_apply_list_row_empty(empty);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel interaction-state helpers / Блокировка UI (panel-state)
// Saved panel UI-lock helpers / Блокировка UI Saved panel
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

// ─────────────────────────────────────────────────────────────────────────────
// Saved-network connect pipeline / Конвейер подключения к сохранённой сети
// Successful Saved connect does NOT rewrite wifi.csv — it only updates last-success.
// Успешный Saved connect НЕ перезаписывает wifi.csv — только обновляет last-success.
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::start_connect_from_saved() {
    // Guards: slot, no in-flight connect, no save pending, no open connect.
    if (_selectedSavedSlot == 255 || !_selectedSavedSsid[0]) return;
    if (_await_saved_connect_ui) return;
    if (_saving_in_progress) return;
    if (_await_open_connect_ui) return;

    WifiOpsSnapshot cur{};
    if (wifiOpsGetSnapshot(&cur) && cur.busy) {
        if (_lbl_saved_status) {
            const YoRadioPalette& pal = yoradio_palette_service();
            wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectBusyRetry), WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
        }
        return;
    }

    // Resolve stored PSK into a local stack buffer; memset immediately after.
    // PSK в стек; обнуляем сразу после передачи в backend.
    char tmpPass[40]{};
    if (!wifiCredStoreResolvePasswordForSlot(_selectedSavedSlot, tmpPass, sizeof(tmpPass))) {
        memset(tmpPass, 0, sizeof(tmpPass));
        if (_lbl_saved_status) {
            const YoRadioPalette& pal = yoradio_palette_service();
            wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
        }
        return;
    }

    const bool started = wifiOpsRequestConnectWithPassword(_selectedSavedSsid, tmpPass, true);
    memset(tmpPass, 0, sizeof(tmpPass)); // PSK off stack / PSK обнулён

    if (!started) {
        if (_lbl_saved_status) {
            const YoRadioPalette& pal = yoradio_palette_service();
            wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
        }
        return;
    }

    _await_saved_connect_ui = true;
    _saved_status_terminal  = false;
    if (_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiConnecting), WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
    }
    set_saved_panel_connecting_ui(true);
}

// Saved connect result routing. Saved Connect does NOT rewrite wifi.csv on success.
// Роутинг результата Saved connect. НЕ перезаписывает wifi.csv при успехе.
void LvglWifiFlowScreen::handle_saved_connect_finished() {
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    _await_saved_connect_ui = false;
    const YoRadioPalette& pal = yoradio_palette_service();
    set_saved_panel_connecting_ui(false);

    if (!_lbl_saved_status) return;

    switch (snap.lastResult) {
    case WifiOpsResult::Success:
    case WifiOpsResult::AlreadyConnected: {
        // Only update lastSSID; no wifi.csv rewrite (product contract). / Только lastSSID, без wifi.csv.
        const bool ok = wifiCredStoreSetLastSuccessFromSlot(_selectedSavedSlot);
        if (!ok) {
            wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            _saved_status_terminal = true;
            return;
        }
        wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectedRestarting), WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        _saving_in_progress    = true;
        set_saved_panel_saving_ui();
        // Reuse existing reboot timer; do not create duplicate / переиспользуем таймер, без дубликата.
        if (!_reboot_timer) {
            _reboot_timer = lv_timer_create(on_reboot_timer, kRebootDelayMs, this);
            lv_timer_set_repeat_count(_reboot_timer, 1);
        }
        return;
    }
    case WifiOpsResult::NoNetwork:
        wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiNoNetwork), WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
    case WifiOpsResult::Timeout:
    case WifiOpsResult::AuthFailed:
        // Auth/timeout — password mismatch or weak signal; NoNetwork is handled above.
        // Auth/timeout — неверный пароль или слабый сигнал; NoNetwork обрабатывается выше.
        wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiSavedAuthOrSignalFailed), WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
    case WifiOpsResult::Cancelled:
        wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiCancelled), WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
    default:
        wifi_flow_set_text_if_changed(_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        _saved_status_terminal = true;
        break;
    }
}

// Five panel roots: Home, Networks, Password, Saved, Hotspot.
// Returns false if any panel allocation fails — create() early returns in that case.
// Пять корневых панелей. false если любая не аллоцирована — create() делает ранний выход.
bool LvglWifiFlowScreen::create_panel_roots(LvglWifiFlowScreen& self, const YoRadioPalette& pal) {
    self._panel_home    = lv_obj_create(self._screen);
    self._panel_net     = lv_obj_create(self._screen);
    self._panel_pass    = lv_obj_create(self._screen);
    self._panel_saved   = lv_obj_create(self._screen);
    self._panel_hotspot = lv_obj_create(self._screen);
    if (!self._panel_home || !self._panel_net || !self._panel_pass ||
        !self._panel_saved || !self._panel_hotspot) {
        return false;
    }
    lv_obj_set_width(self._panel_home,    LV_PCT(100)); lv_obj_set_flex_grow(self._panel_home,    1);
    lv_obj_set_width(self._panel_net,     LV_PCT(100)); lv_obj_set_flex_grow(self._panel_net,     1);
    lv_obj_set_width(self._panel_pass,    LV_PCT(100)); lv_obj_set_flex_grow(self._panel_pass,    1);
    lv_obj_set_width(self._panel_saved,   LV_PCT(100)); lv_obj_set_flex_grow(self._panel_saved,   1);
    lv_obj_set_width(self._panel_hotspot, LV_PCT(100)); lv_obj_set_flex_grow(self._panel_hotspot, 1);
    lv_obj_set_flex_flow(self._panel_home,    LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(self._panel_net,     LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(self._panel_pass,    LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(self._panel_saved,   LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_flow(self._panel_hotspot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(self._panel_home,    kPanelRowGap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._panel_net,     kPanelRowGap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._panel_pass,    kPanelRowGap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._panel_saved,   kPanelRowGap, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._panel_hotspot, kPanelRowGap, LV_PART_MAIN);
    style_service_panel(self._panel_home,    pal);
    style_service_panel(self._panel_net,     pal);
    style_service_panel(self._panel_pass,    pal);
    style_service_panel(self._panel_saved,   pal);
    style_service_panel(self._panel_hotspot, pal);
    return true;
}

// Home panel: Wi-Fi icon + title, subtitle, idle countdown, saved list, action row (Scan / Hotspot / Back).
// Панель Home: иконка + заголовок, подзаголовок, idle уведомление, список saved, ряд кнопок.
void LvglWifiFlowScreen::create_home_panel(LvglWifiFlowScreen& self, const YoRadioPalette& pal) {
    lv_obj_t* hdr_home_row = wifi_create_header_row(self._panel_home);
    if (hdr_home_row) {
        lv_obj_t* ico_home = lv_label_create(hdr_home_row);
        if (ico_home) {
            lv_label_set_text(ico_home, wifi_flow_glyph_utf8_wifi_full());
            lv_obj_set_style_text_font(ico_home, wifi_header_icon_font(), LV_PART_MAIN);
            lv_obj_set_style_text_color(ico_home, pal.text_secondary, LV_PART_MAIN);
        }
        self._hdr_home = lv_label_create(hdr_home_row);
    } else {
        // Fallback: title directly on panel when header-row allocation fails / заголовок на панели без иконки.
        self._hdr_home = lv_label_create(self._panel_home);
    }
    if (self._hdr_home) {
        lv_label_set_text(self._hdr_home, i18n::text(i18n::TextId::WifiHomeTitle));
        lv_obj_set_style_text_color(self._hdr_home, pal.text_primary, LV_PART_MAIN);
        wifi_set_font(self._hdr_home, wifi_title_font_slot());
    }

    self._sub_home = lv_label_create(self._panel_home);
    // Initial text is immediately overwritten by sync_home_boot_failure_ui() on enter.
    // Перезаписывается при enter через sync_home_boot_failure_ui().
    wifi_flow_set_text_if_changed(
        self._sub_home, i18n::text(i18n::TextId::WifiHomeSubtitleManual), WifiFlowDiagTextSlot::SubHome);
    lv_obj_set_style_text_color(self._sub_home, pal.text_secondary, LV_PART_MAIN);
    wifi_set_font(self._sub_home, wifi_status_font_slot());
    lv_label_set_long_mode(self._sub_home, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(self._sub_home, LV_PCT(100));

    self._lbl_recovery_idle_countdown = lv_label_create(self._panel_home);
    // Static notice: written only on idle arm — no periodic text updates.
    // Статичное уведомление: только при arm, без периодических обновлений.
    wifi_flow_set_text_if_changed(self._lbl_recovery_idle_countdown, kStrEmpty, WifiFlowDiagTextSlot::None);
    lv_obj_set_style_text_color(self._lbl_recovery_idle_countdown, pal.text_meta, LV_PART_MAIN);
    wifi_set_font(self._lbl_recovery_idle_countdown, wifi_status_font_slot());
    lv_label_set_long_mode(self._lbl_recovery_idle_countdown, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(self._lbl_recovery_idle_countdown, LV_PCT(100));

    self._list_saved = lv_list_create(self._panel_home);
    lv_obj_set_width(self._list_saved, LV_PCT(100));
    lv_obj_set_flex_grow(self._list_saved, 1);
    lv_obj_set_style_bg_opa(self._list_saved, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(self._list_saved, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._list_saved, kListRowGap, LV_PART_MAIN);
    // Scrollbar hidden to reduce redraw artifacts; list remains scrollable when content overflows.
    // Scrollbar скрыт для уменьшения артефактов; прокрутка сохраняется при переполнении.
    lv_obj_set_scrollbar_mode(self._list_saved, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* rowh = lv_obj_create(self._panel_home);
    if (rowh) {
        style_action_row(rowh, kPrimaryActionRowColumnGap);
        self._btn_scan = add_footer_button(
            rowh, i18n::text(i18n::TextId::WifiActionScan), pal, on_btn_scan, &self);
        self._btn_hotspot = add_footer_button(
            rowh, i18n::text(i18n::TextId::WifiActionHotspot), pal, on_btn_hotspot, &self);
        self._btn_back_home = add_footer_button(
            rowh, i18n::text(i18n::TextId::WifiActionBack), pal, on_btn_back_home, &self);
        wifi_apply_button_role(self._btn_scan,      pal, WifiBtnRole::Primary);
        wifi_apply_button_role(self._btn_hotspot,   pal, WifiBtnRole::Secondary);
        wifi_apply_button_role(self._btn_back_home, pal, WifiBtnRole::Ghost);
    }
}

// Networks panel: Wi-Fi icon + title, status label, scan list, action row (Rescan / Cancel / Home).
// Панель Networks: иконка + заголовок, статус, список сканирования, ряд кнопок.
void LvglWifiFlowScreen::create_networks_panel(LvglWifiFlowScreen& self, const YoRadioPalette& pal) {
    lv_obj_t* hdr_net_row = wifi_create_header_row(self._panel_net);
    if (hdr_net_row) {
        lv_obj_t* ico_net = lv_label_create(hdr_net_row);
        if (ico_net) {
            lv_label_set_text(ico_net, wifi_flow_glyph_utf8_wifi_full());
            lv_obj_set_style_text_font(ico_net, wifi_header_icon_font(), LV_PART_MAIN);
            lv_obj_set_style_text_color(ico_net, pal.text_secondary, LV_PART_MAIN);
        }
        self._hdr_net = lv_label_create(hdr_net_row);
    } else {
        self._hdr_net = lv_label_create(self._panel_net);
    }
    if (self._hdr_net) {
        lv_label_set_text(self._hdr_net, i18n::text(i18n::TextId::WifiNetworksTitle));
        lv_obj_set_style_text_color(self._hdr_net, pal.text_primary, LV_PART_MAIN);
        wifi_set_font(self._hdr_net, wifi_title_font_slot());
    }

    self._lbl_net_status = lv_label_create(self._panel_net);
    wifi_flow_set_text_if_changed(self._lbl_net_status, kStrStatusPlaceholder, WifiFlowDiagTextSlot::NetStatus);
    lv_obj_set_style_text_color(self._lbl_net_status, pal.text_meta, LV_PART_MAIN);
    wifi_set_font(self._lbl_net_status, wifi_status_font_slot());
    lv_label_set_long_mode(self._lbl_net_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(self._lbl_net_status, LV_PCT(100));

    self._list_scan = lv_list_create(self._panel_net);
    lv_obj_set_width(self._list_scan, LV_PCT(100));
    lv_obj_set_flex_grow(self._list_scan, 1);
    lv_obj_set_style_bg_opa(self._list_scan, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(self._list_scan, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self._list_scan, kListRowGap, LV_PART_MAIN);

    lv_obj_t* rown = lv_obj_create(self._panel_net);
    if (rown) {
        style_action_row(rown, kPrimaryActionRowColumnGap);
        self._btn_rescan = add_footer_button(
            rown, i18n::text(i18n::TextId::WifiActionRescan), pal, on_btn_rescan, &self);
        self._btn_cancel_scan = add_footer_button(
            rown, i18n::text(i18n::TextId::WifiActionCancel), pal, on_btn_cancel_scan, &self);
        self._btn_back_net = add_footer_button(
            rown, i18n::text(i18n::TextId::WifiActionHome), pal, on_btn_back_net, &self);
        wifi_apply_button_role(self._btn_rescan,      pal, WifiBtnRole::Primary);
        wifi_apply_button_role(self._btn_cancel_scan, pal, WifiBtnRole::Secondary);
        wifi_apply_button_role(self._btn_back_net,    pal, WifiBtnRole::Ghost);
    }
}

// Password panel: title, selected SSID, hint, textarea, action row (Connect / Back), status, keyboard.
// Панель Password: заголовок, SSID, подсказка, поле ввода, ряд кнопок, статус, клавиатура.
void LvglWifiFlowScreen::create_password_panel(LvglWifiFlowScreen& self, const YoRadioPalette& pal) {
    self._hdr_pass = lv_label_create(self._panel_pass);
    lv_label_set_text(self._hdr_pass, i18n::text(i18n::TextId::WifiPasswordTitle));
    lv_obj_set_style_text_color(self._hdr_pass, pal.text_primary, LV_PART_MAIN);
    wifi_set_font(self._hdr_pass, wifi_title_font_slot());

    self._lbl_pass_ssid = lv_label_create(self._panel_pass);
    lv_label_set_text(self._lbl_pass_ssid, kStrStatusPlaceholder);
    lv_obj_set_style_text_color(self._lbl_pass_ssid, pal.text_primary, LV_PART_MAIN);
    wifi_set_font(self._lbl_pass_ssid, wifi_body_font_slot());
    lv_label_set_long_mode(self._lbl_pass_ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_width(self._lbl_pass_ssid, LV_PCT(100));

    self._lbl_pass_hint = lv_label_create(self._panel_pass);
    lv_label_set_text(self._lbl_pass_hint, i18n::text(i18n::TextId::WifiPasswordHint));
    lv_obj_set_style_text_color(self._lbl_pass_hint, pal.text_secondary, LV_PART_MAIN);
    wifi_set_font(self._lbl_pass_hint, wifi_status_font_slot());
    lv_label_set_long_mode(self._lbl_pass_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(self._lbl_pass_hint, LV_PCT(100));

    self._ta_password = lv_textarea_create(self._panel_pass);
    lv_obj_set_width(self._ta_password, LV_PCT(100));
    lv_obj_set_style_min_height(self._ta_password,
        (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth)
            ? kTextAreaMinHeightSmall : kTextAreaMinHeightLarge, LV_PART_MAIN);
    lv_textarea_set_one_line(self._ta_password, true);
    lv_textarea_set_max_length(self._ta_password, kPasswordMaxInputChars);
    lv_textarea_set_password_mode(self._ta_password, true);
    lv_obj_set_style_text_color(self._ta_password, pal.text_primary, wifi_sel(LV_PART_MAIN, LV_STATE_DEFAULT));
    wifi_set_font(self._ta_password, wifi_title_font_slot());
    // Smaller radius reduces rounded-rect mask pressure on textarea / малый радиус снижает маску TA.
    lv_obj_set_style_radius(self._ta_password, kTextAreaRadius, LV_PART_MAIN);
    lv_obj_add_event_cb(self._ta_password, on_ta_password_changed, LV_EVENT_VALUE_CHANGED, &self);

    lv_obj_t* rowp = lv_obj_create(self._panel_pass);
    if (rowp) {
        style_action_row(rowp, kPrimaryActionRowColumnGap);
        self._btn_connect = lv_btn_create(rowp);
        if (self._btn_connect) {
            lv_obj_set_flex_grow(self._btn_connect, 1);
            wifi_apply_button_role(self._btn_connect, pal, WifiBtnRole::Primary);
            wifi_flow_set_state_if_changed(self._btn_connect, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::PassConnect);
            lv_obj_add_event_cb(self._btn_connect, on_btn_connect, LV_EVENT_CLICKED, &self);
            lv_obj_t* lc = lv_label_create(self._btn_connect);
            lv_label_set_text(lc, i18n::text(i18n::TextId::WifiActionConnect));
            lv_obj_center(lc);
        }
        self._btn_back_pass = add_footer_button(
            rowp, i18n::text(i18n::TextId::WifiActionBack), pal, on_btn_back_pass, &self);
        wifi_apply_button_role(self._btn_back_pass, pal, WifiBtnRole::Secondary);
    }

    self._lbl_pass_status = lv_label_create(self._panel_pass);
    wifi_flow_set_text_if_changed(self._lbl_pass_status, kStrStatusPlaceholder, WifiFlowDiagTextSlot::PassStatus);
    lv_obj_set_style_text_color(self._lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    wifi_set_font(self._lbl_pass_status, wifi_status_font_slot());
    lv_label_set_long_mode(self._lbl_pass_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(self._lbl_pass_status, LV_PCT(100));

    self._kbd = lv_keyboard_create(self._panel_pass);
    if (self._kbd) {
        lv_obj_set_width(self._kbd, LV_PCT(100));
        lv_obj_set_flex_grow(self._kbd, 1);
        lv_obj_set_style_min_height(self._kbd,
            (LV_ACTIVE_PROFILE.width <= kCompactProfileMaxWidth)
                ? kKeyboardMinHeightSmall : kKeyboardMinHeightLarge, LV_PART_MAIN);
        lv_keyboard_set_mode(self._kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
        // Keyboard is initially detached; it is bound to _ta_password in open_password_entry().
        // Клавиатура отсоединена при create; привязывается к _ta_password в open_password_entry().
        lv_keyboard_set_textarea(self._kbd, nullptr);
        // Built-in LVGL Montserrat 18 on key labels only (LV_PART_ITEMS) — not YoRadio Cyrillic:
        // custom Montserrat lacks LVGL symbol glyphs (Shift / Backspace / OK). Device smoke required.
        // Встроенный LVGL Montserrat 18 только на подписи клавиш — не YoRadio Cyrillic (нет symbol glyphs).
        lv_obj_set_style_text_font(self._kbd, &lv_font_montserrat_18, LV_PART_ITEMS);
        lv_obj_add_event_cb(self._kbd, on_keyboard_event, LV_EVENT_ALL, &self);
    }

    lv_obj_add_flag(self._panel_pass, LV_OBJ_FLAG_HIDDEN);
}

// Saved Network panel: SSID title, subtitle, status, normal action row, confirm row.
// Панель Saved Network: SSID, подзаголовок, статус, нормальный ряд действий, ряд подтверждения.
void LvglWifiFlowScreen::create_saved_panel(LvglWifiFlowScreen& self, const YoRadioPalette& pal) {
    self._lbl_saved_ssid = lv_label_create(self._panel_saved);
    wifi_flow_set_text_if_changed(self._lbl_saved_ssid, kStrStatusPlaceholder, WifiFlowDiagTextSlot::None);
    lv_obj_set_style_text_color(self._lbl_saved_ssid, pal.text_primary, LV_PART_MAIN);
    wifi_set_font(self._lbl_saved_ssid, wifi_title_font_slot());
    lv_label_set_long_mode(self._lbl_saved_ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_width(self._lbl_saved_ssid, LV_PCT(100));

    self._lbl_saved_sub = lv_label_create(self._panel_saved);
    lv_label_set_text(self._lbl_saved_sub, i18n::text(i18n::TextId::WifiSavedSubtitle));
    lv_obj_set_style_text_color(self._lbl_saved_sub, pal.text_secondary, LV_PART_MAIN);
    wifi_set_font(self._lbl_saved_sub, wifi_status_font_slot());
    lv_label_set_long_mode(self._lbl_saved_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(self._lbl_saved_sub, LV_PCT(100));

    self._lbl_saved_status = lv_label_create(self._panel_saved);
    wifi_flow_set_text_if_changed(self._lbl_saved_status, kStrStatusPlaceholder, WifiFlowDiagTextSlot::SavedStatus);
    lv_obj_set_style_text_color(self._lbl_saved_status, pal.text_meta, LV_PART_MAIN);
    wifi_set_font(self._lbl_saved_status, wifi_status_font_slot());
    lv_label_set_long_mode(self._lbl_saved_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(self._lbl_saved_status, LV_PCT(100));

    // Normal row: Connect / Chg pwd / Remove / Back / нормальный ряд действий.
    self._row_saved_normal = lv_obj_create(self._panel_saved);
    if (self._row_saved_normal) {
        style_action_row(self._row_saved_normal, kCompactActionRowColumnGap);
        self._btn_saved_connect = add_footer_button(
            self._row_saved_normal, i18n::text(i18n::TextId::WifiActionConnect), pal, on_btn_saved_connect, &self);
        self._btn_saved_chpwd = add_footer_button(
            self._row_saved_normal, i18n::text(i18n::TextId::WifiActionChangePassword), pal, on_btn_saved_chpwd, &self);
        self._btn_saved_remove = add_footer_button(
            self._row_saved_normal, i18n::text(i18n::TextId::WifiActionRemove), pal, on_btn_saved_remove, &self);
        self._btn_saved_back = add_footer_button(
            self._row_saved_normal, i18n::text(i18n::TextId::WifiActionBack), pal, on_btn_saved_back, &self);
        // Give the longer RU Connect caption extra flex width without changing row geometry.
        // Длинной RU-подписи «Подключить» отдаём дополнительную flex-ширину без изменения геометрии ряда.
        if (self._btn_saved_connect) lv_obj_set_flex_grow(self._btn_saved_connect, 15);
        if (self._btn_saved_chpwd)   lv_obj_set_flex_grow(self._btn_saved_chpwd,   12);
        if (self._btn_saved_remove)  lv_obj_set_flex_grow(self._btn_saved_remove,  11);
        if (self._btn_saved_back)    lv_obj_set_flex_grow(self._btn_saved_back,    10);
        wifi_apply_button_role(self._btn_saved_connect, pal, WifiBtnRole::Primary);
        wifi_apply_button_role(self._btn_saved_chpwd,   pal, WifiBtnRole::Secondary);
        wifi_apply_button_role(self._btn_saved_remove,  pal, WifiBtnRole::Destructive);
        wifi_apply_button_role(self._btn_saved_back,    pal, WifiBtnRole::Ghost);
    }

    // Confirm row: Yes / No, hidden until Remove is tapped / ряд подтверждения, скрыт до тапа Remove.
    self._row_saved_confirm = lv_obj_create(self._panel_saved);
    if (self._row_saved_confirm) {
        style_action_row(self._row_saved_confirm, kCompactActionRowColumnGap);
        self._btn_saved_yes = add_footer_button(
            self._row_saved_confirm, i18n::text(i18n::TextId::WifiActionYes), pal, on_btn_saved_yes, &self);
        self._btn_saved_no = add_footer_button(
            self._row_saved_confirm, i18n::text(i18n::TextId::WifiActionNo), pal, on_btn_saved_no, &self);
        wifi_apply_button_role(self._btn_saved_yes, pal, WifiBtnRole::Destructive);
        wifi_apply_button_role(self._btn_saved_no,  pal, WifiBtnRole::Secondary);
        lv_obj_add_flag(self._row_saved_confirm, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_add_flag(self._panel_saved, LV_OBJ_FLAG_HIDDEN);
}

// Hotspot panel: title, AP info labels, Back button.
// This builder does not start or stop SoftAP — backend is controlled by show_hotspot_panel() / on_btn_back_hotspot.
// Панель Hotspot: заголовок, метки AP, кнопка Back.
// Builder не запускает и не останавливает SoftAP — backend управляется через show_hotspot_panel() / on_btn_back_hotspot.
void LvglWifiFlowScreen::create_hotspot_panel(LvglWifiFlowScreen& self, const YoRadioPalette& pal) {
    self._hdr_hotspot = lv_label_create(self._panel_hotspot);
    lv_label_set_text(self._hdr_hotspot, i18n::text(i18n::TextId::WifiHotspotTitle));
    lv_obj_set_style_text_color(self._hdr_hotspot, pal.text_primary, LV_PART_MAIN);
    wifi_set_font(self._hdr_hotspot, wifi_title_font_slot());

    self._lbl_hotspot_ssid = lv_label_create(self._panel_hotspot);
    self._lbl_hotspot_pwd  = lv_label_create(self._panel_hotspot);
    self._lbl_hotspot_ip   = lv_label_create(self._panel_hotspot);
    self._lbl_hotspot_help = lv_label_create(self._panel_hotspot);
    lv_obj_set_style_text_color(self._lbl_hotspot_ssid, pal.text_primary,   LV_PART_MAIN);
    lv_obj_set_style_text_color(self._lbl_hotspot_pwd,  pal.text_primary,   LV_PART_MAIN);
    lv_obj_set_style_text_color(self._lbl_hotspot_ip,   pal.text_primary,   LV_PART_MAIN);
    lv_obj_set_style_text_color(self._lbl_hotspot_help, pal.text_secondary, LV_PART_MAIN);
    wifi_set_font(self._lbl_hotspot_ssid, wifi_body_font_slot());
    wifi_set_font(self._lbl_hotspot_pwd,  wifi_body_font_slot());
    wifi_set_font(self._lbl_hotspot_ip,   wifi_body_font_slot());
    wifi_set_font(self._lbl_hotspot_help, wifi_status_font_slot());
    lv_label_set_long_mode(self._lbl_hotspot_help, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(self._lbl_hotspot_ssid, LV_PCT(100));
    lv_obj_set_width(self._lbl_hotspot_pwd,  LV_PCT(100));
    lv_obj_set_width(self._lbl_hotspot_ip,   LV_PCT(100));
    lv_obj_set_width(self._lbl_hotspot_help, LV_PCT(100));

    lv_obj_t* row_ap = lv_obj_create(self._panel_hotspot);
    if (row_ap) {
        style_action_row(row_ap, kCompactActionRowColumnGap);
        self._btn_hotspot_back = add_footer_button(
            row_ap, i18n::text(i18n::TextId::WifiActionBackToRecovery), pal, on_btn_back_hotspot, &self);
        wifi_apply_button_role(self._btn_hotspot_back, pal, WifiBtnRole::Secondary);
    }

    lv_obj_add_flag(self._panel_hotspot, LV_OBJ_FLAG_HIDDEN);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle — create → enter → active → exit → destroy
// WIFIREF-D2: timer helpers encapsulate exact baseline lv_timer_create/del/null.
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::lifecycle_start_poll_timer() {
    if (!_poll_timer) {
        _poll_timer = lv_timer_create(on_poll_timer, kPollIntervalMs, this);
    }
}

void LvglWifiFlowScreen::lifecycle_stop_poll_timer() {
    if (_poll_timer) {
        lv_timer_del(_poll_timer);
        _poll_timer = nullptr;
    }
}

void LvglWifiFlowScreen::lifecycle_cancel_reboot_timer() {
    if (_reboot_timer) {
        lv_timer_del(_reboot_timer);
        _reboot_timer = nullptr;
    }
}

void LvglWifiFlowScreen::create() {
    if (_screen) return;

    const YoRadioPalette& pal = yoradio_palette_service();
    const int32_t         pad = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding);

    // Phase 1: root screen allocation / корень экрана.
    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    // Root screen setup remains inline — panel builders depend on it.
    // Настройка корня остаётся inline — builders зависят от него.
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, pad, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_size(_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Phase 2: panel roots + shared styles / корни панелей и общие стили.
    if (!create_panel_roots(*this, pal)) {
        return;
    }

    // Shared styles must be initialized after panel roots exist; drop only after lv_obj_del in destroy().
    // Общие стили после корней панелей; сброс только после lv_obj_del в destroy().
    wifi_flow_style_ensure(pal);

    // Phase 3: static panel builders (callback registration inside builders) / статические панели.
    create_home_panel(*this, pal);
    create_networks_panel(*this, pal);
    create_password_panel(*this, pal);
    create_saved_panel(*this, pal);
    create_hotspot_panel(*this, pal);

    // Phase 4: initial sync and default panel / начальная синхронизация.
    sync_hotspot_panel_labels();

    show_home_panel();
    rebuild_saved_list();
}

void LvglWifiFlowScreen::enter() {
    // Phase 1: backend init and operation state reset / init backend и сброс ops state.
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

    // Phase 2: consume entry context (boot-failure vs runtime-disconnect vs settings-service vs manual).
    // Contexts are mutually exclusive: at most one is true per enter().
    // Контексты взаимно исключают друг друга: не более одного true за enter().
    _entered_from_boot_failure         = lvgl_ui::consumeWifiRecoveryEnteredFromBootFailure();
    // S6V8A: consume runtime disconnect entry context — mutually exclusive with boot-fail. / Контекст runtime эскалации.
    // If neither flag is set (e.g. manual entry via InvertDisplay), both remain false. / Если ни один флаг — оба false.
    _entered_from_runtime_disconnect   = lvgl_ui::consumeWifiRecoveryEnteredFromRuntimeDisconnect();
    // 6.7S5A-v4: consume settings-service context — Back→ESP.restart(), no return to carousel.
    // 6.7S5A-v4: consume settings-service context — Back→ESP.restart(), без возврата в карусель.
    _entered_from_settings_service     = lvgl_ui::consumeWifiEnteredFromSettingsService();
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

    // Phase 3: UI sync, idle arm, poll timer start / синхронизация UI, idle, старт poll timer.
    rebuild_saved_list();
    sync_home_boot_failure_ui();
    arm_recovery_idle_if_home_only();
    lifecycle_start_poll_timer();
}

void LvglWifiFlowScreen::update() {}

void LvglWifiFlowScreen::exit() {
    // Phase 1: idle disarm and password cleanup / disarm idle и очистка password.
    disarm_boot_idle_timer();
    clear_password_panel_state();
    _entered_from_boot_failure = false;
    _entered_from_settings_service = false;
    sync_home_boot_failure_ui();

    // Phase 2: timer release / освобождение таймеров.
    lifecycle_stop_poll_timer();
    lifecycle_cancel_reboot_timer();
#if WIFI_FLOW_DIAG_GLITCH
    wifi_flow_diag_on_exit_flow();
#endif

    // Phase 3: open-connect cancel and backend cancel / отмена open-connect и backend.
    _await_open_connect_ui = false;
    _open_status_terminal   = false;
    memset(_selectedOpenSsid, 0, sizeof(_selectedOpenSsid));
    set_networks_panel_connecting_ui(false);
    wifiOpsCancel();
}

void LvglWifiFlowScreen::destroy() {
    exit();
    clear_password_secrets();
    // Phase 1: LVGL object tree deletion / удаление дерева LVGL.
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    // Phase 2: shared style teardown after objects freed (baseline order: del then drop).
    // Phase 2: сброс стилей после lv_obj_del (порядок baseline: del, затем drop).
    wifi_flow_style_drop();
    // Phase 3: member handle nulling / обнуление handles.
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
    // Poll timer adapter — routes to pollOpsSnapshot() once per tick (baseline).
    // Адаптер poll timer — один вызов pollOpsSnapshot() за tick.
    LvglWifiFlowScreen* self = wifi_flow_self_from_timer(t);
    if (self) self->pollOpsSnapshot();
}

// ─────────────────────────────────────────────────────────────────────────────
// WIFIREF-D1: Polling dispatcher helpers / Helpers polling dispatcher
// Each bool helper returns true only when baseline pollOpsSnapshot() returned early.
// bool helper возвращает true только когда baseline pollOpsSnapshot() делал early return.
// ─────────────────────────────────────────────────────────────────────────────

bool LvglWifiFlowScreen::poll_handle_password_result(const WifiOpsSnapshot& snap, bool pass_visible,
                                                      const YoRadioPalette& pal) {
    // 4B: Connect polling for Password panel / опрос завершения connect для Password panel.
    if (!pass_visible || !_await_connect_ui) {
        return false;
    }
    if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
        if (_lbl_pass_status) {
            wifi_flow_set_text_if_changed(
                _lbl_pass_status,
                i18n::text(i18n::TextId::WifiConnectingWarning),
                WifiFlowDiagTextSlot::PassStatus);
            lv_obj_set_style_text_color(_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
        }
        set_password_panel_connecting_ui(true);
        return true;
    }
    if (!snap.busy && snap.phase == WifiOpsPhase::Idle) {
        handle_connect_finished();
        // Fall through: refresh scan buttons etc. / дальше — кнопки скана.
    }
    return false;
}

bool LvglWifiFlowScreen::poll_handle_saved_result(const WifiOpsSnapshot& snap, bool saved_visible,
                                                   const YoRadioPalette& pal) {
    // Wi-Fi 6B: Connect polling for Saved Network panel / опрос завершения connect для Saved panel.
    if (!saved_visible || !_await_saved_connect_ui) {
        return false;
    }
    if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
        if (_lbl_saved_status && !_saved_status_terminal) {
            wifi_flow_set_text_if_changed(
                _lbl_saved_status, i18n::text(i18n::TextId::WifiConnecting), WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
        }
        return true;
    }
    if (!snap.busy && snap.phase == WifiOpsPhase::Idle) {
        handle_saved_connect_finished();
        // Fall through: refresh scan buttons etc. / дальше — кнопки скана.
    }
    return false;
}

bool LvglWifiFlowScreen::poll_handle_open_result(const WifiOpsSnapshot& snap, bool net_visible,
                                                  const YoRadioPalette& pal) {
    // Wi-Fi S6V7B: open-network connect polling on Networks panel / опрос open connect на панели Networks.
    if (!net_visible || !_await_open_connect_ui) {
        return false;
    }
    if (snap.busy && snap.currentOp == WifiOpsOp::Connect) {
        if (_lbl_net_status && !_open_status_terminal) {
            wifi_flow_set_text_if_changed(
                _lbl_net_status, i18n::text(i18n::TextId::WifiConnectingOpen), WifiFlowDiagTextSlot::NetStatus);
            lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
        }
        set_networks_panel_connecting_ui(true);
        return true;
    }
    if (!snap.busy && snap.phase == WifiOpsPhase::Idle) {
        handle_open_connect_finished();
        // Fall through / дальше — общая логика скана.
    }
    return false;
}

bool LvglWifiFlowScreen::poll_scan_progress_blocks_ui(const WifiOpsSnapshot& snap, bool net_visible,
                                                         const YoRadioPalette& pal) {
    // S6V9F: only treat scan-in-progress as blocking UI when Networks is relevant or user is awaiting scan results.
    // S6V9F: иначе stale Scanning на Home после stop AP — вечный early-return и «мёртвый» Scan.
    const bool scan_progress_blocks_ui = net_visible || _await_scan_ui;
    if (!scan_progress_blocks_ui) {
        return false;
    }
    if (snap.phase != WifiOpsPhase::Scanning && !(snap.busy && snap.currentOp == WifiOpsOp::Scan)) {
        return false;
    }
    if (_lbl_net_status) {
        wifi_flow_set_text_if_changed(
            _lbl_net_status, i18n::text(i18n::TextId::WifiScanning), WifiFlowDiagTextSlot::NetStatus);
        lv_obj_set_style_text_color(_lbl_net_status, pal.text_meta, LV_PART_MAIN);
    }
    if (_btn_scan) {
        wifi_flow_set_state_if_changed(_btn_scan, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::Scan);
    }
    if (_btn_rescan) {
        wifi_flow_set_state_if_changed(_btn_rescan, LV_STATE_DISABLED, true, WifiFlowDiagBtnSlot::Rescan);
    }
    return true;
}

void LvglWifiFlowScreen::poll_restore_operation_buttons(bool net_visible) {
    if (_btn_scan) {
        wifi_flow_set_state_if_changed(_btn_scan, LV_STATE_DISABLED, false, WifiFlowDiagBtnSlot::Scan);
    }
    if (_btn_rescan) {
        // S6V7B: keep Networks Rescan disabled while open save → reboot pending / не включать Rescan до reboot после save open.
        const bool net_rescan_locked = net_visible && (_saving_in_progress || _await_open_connect_ui);
        wifi_flow_set_state_if_changed(_btn_rescan, LV_STATE_DISABLED, net_rescan_locked, WifiFlowDiagBtnSlot::Rescan);
    }
}

void LvglWifiFlowScreen::poll_handle_scan_completion(const WifiOpsSnapshot& snap, bool pass_visible,
                                                      bool saved_visible, const YoRadioPalette& pal) {
    // Completing scan await only when Networks (not Password or Saved) visible.
    // Завершение скана не обрабатывается на Password или Saved panel.
    if (!_await_scan_ui || pass_visible || saved_visible || _await_open_connect_ui ||
        snap.phase != WifiOpsPhase::Idle || snap.busy) {
        return;
    }
    _await_scan_ui = false;
    if (snap.lastResult == WifiOpsResult::Success) {
        rebuild_scan_list();
        wifi_flow_set_text_if_changed(
            _lbl_net_status, i18n::text(i18n::TextId::WifiScanComplete), WifiFlowDiagTextSlot::NetStatus);
    } else if (snap.lastResult == WifiOpsResult::Cancelled) {
        rebuild_scan_list();
        wifi_flow_set_text_if_changed(
            _lbl_net_status, i18n::text(i18n::TextId::WifiScanCancelled), WifiFlowDiagTextSlot::NetStatus);
    } else if (snap.lastResult == WifiOpsResult::Timeout) {
        rebuild_scan_list();
        wifi_flow_set_text_if_changed(
            _lbl_net_status, i18n::text(i18n::TextId::WifiScanTimeout), WifiFlowDiagTextSlot::NetStatus);
    } else if (snap.lastResult == WifiOpsResult::Busy) {
        rebuild_scan_list();
        wifi_flow_set_text_if_changed(
            _lbl_net_status, i18n::text(i18n::TextId::WifiBusy), WifiFlowDiagTextSlot::NetStatus);
    } else {
        rebuild_scan_list();
        wifi_flow_set_text_if_changed(
            _lbl_net_status, i18n::text(i18n::TextId::WifiScanComplete), WifiFlowDiagTextSlot::NetStatus);
    }
    lv_obj_set_style_text_color(_lbl_net_status, pal.text_secondary, LV_PART_MAIN);
    (void)snap.resultsSeq;
    _last_results_seq = snap.resultsSeq;
}

void LvglWifiFlowScreen::poll_emit_diagnostics() {
#if WIFI_FLOW_DIAG_GLITCH
    wifi_flow_diag_maybe_periodic_summary();
#endif
}

// WIFIREF-D1: top-level polling dispatcher — phase order is load-bearing.
// WIFIREF-D1: верхнеуровневый polling dispatcher — порядок фаз критичен.
void LvglWifiFlowScreen::pollOpsSnapshot() {
    // Phase 1: single snapshot acquisition / один snapshot за tick.
    WifiOpsSnapshot snap{};
    if (!wifiOpsGetSnapshot(&snap)) return;

    // Phase 2: recovery idle tick (Home countdown / auto-Hotspot).
    process_boot_idle_timer_tick();

    const YoRadioPalette& pal = yoradio_palette_service();

    // Phase 3: panel visibility flags (no early return — used by later phases).
    const bool pass_visible  = _panel_pass  && !lv_obj_has_flag(_panel_pass,  LV_OBJ_FLAG_HIDDEN);
    const bool saved_visible = _panel_saved && !lv_obj_has_flag(_panel_saved, LV_OBJ_FLAG_HIDDEN);
    const bool net_visible   = _panel_net   && !lv_obj_has_flag(_panel_net,   LV_OBJ_FLAG_HIDDEN);

    // Phase 4–6: connect result routing (dispatch only — handlers own semantics).
    if (poll_handle_password_result(snap, pass_visible, pal)) {
        return;
    }
    if (poll_handle_saved_result(snap, saved_visible, pal)) {
        return;
    }
    if (poll_handle_open_result(snap, net_visible, pal)) {
        return;
    }

    // Phase 7: scan-progress blocking guard (S6V9F stale-Scanning workaround).
    if (poll_scan_progress_blocks_ui(snap, net_visible, pal)) {
        return;
    }

    // Phase 8: restore operation buttons.
    poll_restore_operation_buttons(net_visible);

    // Phase 9: scan completion.
    poll_handle_scan_completion(snap, pass_visible, saved_visible, pal);

    // Phase 10: Password Connect button sync.
    if (pass_visible && !_await_connect_ui) {
        sync_connect_button_enabled();
    }

    // Phase 11: diagnostics.
    poll_emit_diagnostics();
}

// ─────────────────────────────────────────────────────────────────────────────
// Static button event adapters / Адаптеры static button callbacks
// Thin LVGL adapters: event code → instance → screen-owned method.
// Тонкие адаптеры: event code → instance → screen-owned method.
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::on_btn_scan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (self) self->start_scan_from_user();
}

void LvglWifiFlowScreen::on_btn_hotspot(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self) return;
    self->show_hotspot_panel();
}

void LvglWifiFlowScreen::on_btn_back_hotspot(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
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
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self || self->_entered_from_boot_failure) return;

    // 6.7S5A-v4: Settings service mode — no return to carousel; reboot into normal Main boot.
    // Does not call dismissRebootRequired() or dismissWifiFlowReturnToPlayer().
    // 6.7S5A-v4: сервисный режим Settings — нет возврата в карусель; reboot в нормальный Main boot.
    // Не вызывает dismissRebootRequired() и dismissWifiFlowReturnToPlayer().
    if (self->_entered_from_settings_service) {
        Serial.println("[SETTINGS_WIFI] back-reboot");
        ESP.restart();
        return;
    }

    lvgl_ui::dismissWifiFlowReturnToPlayer();
}

void LvglWifiFlowScreen::on_btn_back_net(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self) return;
    if (self->_await_open_connect_ui || self->_saving_in_progress) return;
    wifiOpsCancel();
    self->show_home_panel();
}

void LvglWifiFlowScreen::on_btn_rescan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self) return;
    if (self->_await_open_connect_ui || self->_saving_in_progress) return;
    self->start_scan_from_user();
}

void LvglWifiFlowScreen::on_btn_cancel_scan(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self) return;
    if (self->_await_open_connect_ui || self->_saving_in_progress) return;
    wifiOpsCancel();
}

void LvglWifiFlowScreen::on_btn_back_pass(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
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
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (self) self->start_connect_from_user();
}

// ─────────────────────────────────────────────────────────────────────────────
// Password and keyboard event adapters / Адаптеры password и keyboard
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::on_ta_password_changed(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self || !self->_ta_password || !self->_lbl_pass_status) return;
    if (self->_await_connect_ui) return;
    // Wi-Fi 6A: saving in progress — ignore all TA events / идёт сохранение — игнорируем TA события.
    if (self->_saving_in_progress) return;

    const char* t = lv_textarea_get_text(self->_ta_password);
    const size_t  n = t ? strlen(t) : 0U;
    const YoRadioPalette& pal = yoradio_palette_service();

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
        wifi_flow_set_text_if_changed(
            self->_lbl_pass_status, i18n::text(i18n::TextId::WifiPasswordPrompt), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    } else if (n < kMinPasswordLen) {
        wifi_flow_set_text_if_changed(
            self->_lbl_pass_status, i18n::text(i18n::TextId::WifiPasswordMinChars), WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_secondary, LV_PART_MAIN);
    } else {
        wifi_flow_set_text_if_changed(self->_lbl_pass_status, " ", WifiFlowDiagTextSlot::PassStatus);
        lv_obj_set_style_text_color(self->_lbl_pass_status, pal.text_meta, LV_PART_MAIN);
    }
    self->sync_connect_button_enabled();
}

void LvglWifiFlowScreen::on_keyboard_event(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CANCEL) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
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

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic row event adapters / Адаптеры dynamic row callbacks
// user_data encoding: index + 1 (one-based); zero rejected.
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::on_scan_row_click(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* tgt = lv_event_get_target(e);
    lv_obj_t* o   = tgt;
    while (o && lv_obj_get_user_data(o) == nullptr) {
        o = lv_obj_get_parent(o);
    }
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
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
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!o || !self) return;
    const uintptr_t stored = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(o));
    if (stored == 0U) return; // sentinel: not a valid slot / не слот
    const uint8_t slot = static_cast<uint8_t>(stored - 1U);
    if (slot >= wifiCredStoreSavedCount()) return;
    self->open_saved_network(slot);
}

// ─────────────────────────────────────────────────────────────────────────────
// Saved panel button adapters / Адаптеры кнопок Saved panel
// ─────────────────────────────────────────────────────────────────────────────

void LvglWifiFlowScreen::on_btn_saved_connect(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (self) self->start_connect_from_saved();
}

// Wi-Fi 6B: Change password — open Password panel with empty TA; Back from there returns here.
// Wi-Fi 6B: смена пароля — Password panel с пустым TA; Back возвращает сюда.
void LvglWifiFlowScreen::on_btn_saved_chpwd(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self || !self->_selectedSavedSsid[0]) return;
    self->_password_from_saved = true;
    self->open_password_entry(self->_selectedSavedSsid);
    // Update hint to indicate context / уточняем подсказку для смены пароля.
    if (self->_lbl_pass_hint) {
        lv_label_set_text(
            self->_lbl_pass_hint, i18n::text(i18n::TextId::WifiPasswordChangeHint));
    }
}

// Wi-Fi 6B: Back on Saved panel: cancel if connecting, then return Home.
// Wi-Fi 6B: Back на Saved panel: Cancel если идёт connect, затем возврат Home.
void LvglWifiFlowScreen::on_btn_saved_back(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
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
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self) return;
    // Block if connect or reboot is in progress / блокировать при connect/reboot.
    if (self->_await_saved_connect_ui || self->_saving_in_progress) return;
    if (self->_selectedSavedSlot == 255 || !self->_selectedSavedSsid[0]) return;

    if (self->_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette_service();
        char msg[80];
        wifi_format_checked(msg, sizeof(msg), i18n::text(i18n::TextId::WifiRemoveConfirm),
                            i18n::text(i18n::TextId::WifiRemoveConfirmFormat),
                            self->_selectedSavedSsid);
        wifi_flow_set_text_if_changed(self->_lbl_saved_status, msg, WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
        self->_saved_status_terminal = true;
    }
    self->_setSavedRemoveConfirmationVisible(true);
}

// Wi-Fi 6C: No — cancel confirmation, restore normal row / No — отмена, возврат к нормальному ряду.
void LvglWifiFlowScreen::on_btn_saved_no(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self) return;
    self->_saved_status_terminal  = false;
    if (self->_lbl_saved_status) {
        const YoRadioPalette& pal = yoradio_palette_service();
        wifi_flow_set_text_if_changed(self->_lbl_saved_status, kStrStatusPlaceholder, WifiFlowDiagTextSlot::SavedStatus);
        lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_meta, LV_PART_MAIN);
    }
    self->_setSavedRemoveConfirmationVisible(false);
}

// Yes — confirm Remove; mutate store, persist, return Home. No reboot.
// Yes — выполнить удаление; store+persist, затем Home. Без reboot.
void LvglWifiFlowScreen::on_btn_saved_yes(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglWifiFlowScreen* self = wifi_flow_self_from_event(e);
    if (!self) return;
    const YoRadioPalette& pal = yoradio_palette_service();

    // Phase 1: validate selected slot before any mutation.
    // Фаза 1: проверка выбранного слота перед мутацией.
    if (self->_selectedSavedSlot == 255 ||
        self->_selectedSavedSlot >= wifiCredStoreSavedCount()) {
        if (self->_lbl_saved_status) {
            wifi_flow_set_text_if_changed(
                self->_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            self->_saved_status_terminal = true;
        }
        self->_setSavedRemoveConfirmationVisible(false);
        return;
    }

    // Phase 2: remove entry from RAM store (RemoveAt adjusts lastSSID internally).
    // Фаза 2: удаление из RAM store (RemoveAt корректирует lastSSID внутри).
    const bool removed = wifiCredStoreRemoveAt(self->_selectedSavedSlot);
    if (!removed) {
        if (self->_lbl_saved_status) {
            wifi_flow_set_text_if_changed(
                self->_lbl_saved_status, i18n::text(i18n::TextId::WifiConnectErrorRetry), WifiFlowDiagTextSlot::SavedStatus);
            lv_obj_set_style_text_color(self->_lbl_saved_status, pal.text_secondary, LV_PART_MAIN);
            self->_saved_status_terminal = true;
        }
        self->_setSavedRemoveConfirmationVisible(false);
        return;
    }

    // Phase 3: persist updated list; reload on failure to restore consistency.
    // Фаза 3: запись на диск; при ошибке reload для consistency.
    const bool persisted = wifiCredStorePersistToFs();
    if (!persisted) {
        wifiCredStoreReloadFromFs();
    }

    // Phase 4: rebuild Saved list, return Home, clear saved state via show_home_panel().
    // Фаза 4: обновить список, вернуться на Home; show_home_panel() сбрасывает saved state.
    // No reboot regardless of persist outcome / reboot не планируется в любом случае.
    self->rebuild_saved_list();
    self->show_home_panel();
    // persist-fail UX is minimal (per spec): show_home_panel clears saved state;
    // no dedicated Home status label exists; list reflects reloaded state.
}

} // namespace lvgl_ui

