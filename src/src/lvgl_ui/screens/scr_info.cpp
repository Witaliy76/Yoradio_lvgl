/*
 * LvglInfoPage — LVGL «Info» page (device / network / memory). Not stream metadata.
 * LvglInfoPage — страница LVGL «Info» (устройство / сеть / память). Не метаданные потока.
 *
 * Stage 6.2 Patch C: flex skeleton + section rail (status line + EN labels + Tabler rail icons).
 * Stage 6.2 Patch C2: rail/KV alignment, divider visibility, typography rhythm (layout only).
 * Stage 6.2 Patch C3: divider bg was wiped by style_transparent_flex (opa TRANSP) — fix + rail inset + INFO 18 px.
 * Stage 6.2 Patch C4: 1 px dividers (Main law); bottom divider inside content + tail spacer + profile pad_bottom.
 * Stage 6.2 Patch D: final Info fields (network/system/display/memory) + ~5 px data-column inset.
 * Stage 6.2 Patch E: Wi-Fi + Display value labels — LV_LABEL_LONG_SCROLL_CIRCULAR when text overflows (bounded flex slot).
 *
 * - Implements ILvglScreen: create → enter → update → exit → destroy.
 * - DspTask-only lv_* via Display::loop → lvgl_ui::taskHandler.
 * - Layout: LV_ACTIVE_PROFILE + yoradio_palette() only — no hardcoded colors.
 * - update() ~1 Hz; Wi-Fi RSSI/channel cached ~3 s (same spirit as wgt_status_line).
 */

#include "scr_info.h"


#include "lvgl.h"
#include "Arduino.h"
#include <cstdio>
#include <cstring>
#include "WiFi.h"
#include "Esp.h"
#include "esp_wifi.h"

#include "../fonts/lv_fonts.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "lvgl_ui.h"
#include "../../core/config.h"
#include "../../core/network.h"
#include "../../core/options.h"

namespace lvgl_ui {

namespace {

// Compile stamp for this TU — distinct from YOVERSION (Firmware row).
static const char k_info_build_stamp[] = __DATE__ " " __TIME__;

enum class InfoKvValueLongMode : uint8_t {
    Clip,
    ScrollCircular,
};

// Easy to swap for visual tuning / легко сменить размер иконок секций.
static const void* k_info_section_icon_font = reinterpret_cast<const void*>(&lv_font_yora_info_section_icons_36);

static void info_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void info_set_text_if_changed(lv_obj_t* lbl, const char* s) {
    if (!lbl || !s) return;
    const char* cur = lv_label_get_text(lbl);
    if (cur != nullptr && strcmp(cur, s) == 0) return;
    lv_label_set_text(lbl, s);
}

static void style_transparent_flex(lv_obj_t* o) {
    if (!o) return;
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

// Same law as scr_main status_divider / _bar_buffer: 1 px, pal.divider, opaque — NOT style_transparent_flex (C3).
static lv_obj_t* add_thin_divider(lv_obj_t* parent, const YoRadioPalette& pal) {
    lv_obj_t* d = lv_obj_create(parent);
    if (!d) return nullptr;
    const lv_coord_t h = 1;
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_height(d, h);
    lv_obj_set_style_min_height(d, h, LV_PART_MAIN);
    lv_obj_set_style_max_height(d, h, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d, 0, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(d, 0);
    return d;
}

static void add_kv_row(
    lv_obj_t* data_col,
    const char* key,
    const char* initial,
    lv_obj_t** out_val,
    const YoRadioPalette& pal,
    InfoKvValueLongMode val_long_mode = InfoKvValueLongMode::Clip) {
    if (!data_col) return;

    lv_obj_t* row = lv_obj_create(data_col);
    if (!row) return;
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    style_transparent_flex(row);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);

    lv_obj_t* k = lv_label_create(row);
    if (k) {
        lv_label_set_text(k, key);
        lv_label_set_long_mode(k, LV_LABEL_LONG_WRAP);
        info_set_font(k, LV_ACTIVE_PROFILE.font_normal);
        lv_obj_set_style_text_color(k, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(k, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(k, LV_PCT(34));
    }

    lv_obj_t* v = nullptr;
    if (val_long_mode == InfoKvValueLongMode::ScrollCircular) {
        // Flex slot takes remainder; label width = 100% of slot — same idea as scr_main cont_text labels (Patch E).
        lv_obj_t* slot = lv_obj_create(row);
        if (slot) {
            lv_obj_set_flex_grow(slot, 1);
            lv_obj_set_height(slot, LV_SIZE_CONTENT);
            style_transparent_flex(slot);
            lv_obj_set_style_min_width(slot, 0, LV_PART_MAIN);
            v = lv_label_create(slot);
            if (v) {
                lv_label_set_long_mode(v, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_obj_set_width(v, LV_PCT(100));
            }
        }
    } else {
        v = lv_label_create(row);
        if (v) {
            lv_label_set_long_mode(v, LV_LABEL_LONG_CLIP);
            lv_obj_set_flex_grow(v, 1);
        }
    }

    if (v) {
        lv_label_set_text(v, initial);
        info_set_font(v, LV_ACTIVE_PROFILE.font_normal);
        lv_obj_set_style_text_color(v, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }
    if (out_val) {
        *out_val = v;
    }
}

// Section: horizontal row [rail | data column]. Returns data column for KV rows.
// Секция: [rail | колонка данных].
static lv_obj_t* add_section_rail_block(
    lv_obj_t* content_column,
    const char* section_title_en,
    const char* icon_glyph_utf8,
    const YoRadioPalette& pal) {
    if (!content_column) return nullptr;

    lv_obj_t* section = lv_obj_create(content_column);
    if (!section) return nullptr;
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    style_transparent_flex(section);
    lv_obj_set_style_pad_column(section, 8, LV_PART_MAIN);

    lv_obj_t* rail = lv_obj_create(section);
    if (!rail) return nullptr;
    // Narrower rail → more room for KV; slightly wider on 320 px so icon+label still breathe.
    // Узкий rail → больше места под KV; на 320 px чуть шире процент.
    const uint32_t W = LV_ACTIVE_PROFILE.width;
    const lv_coord_t rail_w = (W <= 360u) ? LV_PCT(24) : LV_PCT(22);
    lv_obj_set_width(rail, rail_w);
    lv_obj_set_height(rail, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    style_transparent_flex(rail);
    // Small left inset so rail does not hug screen edge (Patch C3).
    const lv_coord_t rail_inset_l = (W <= 360u) ? 6 : 10;
    lv_obj_set_style_pad_left(rail, rail_inset_l, LV_PART_MAIN);
    lv_obj_set_style_pad_row(rail, 2, LV_PART_MAIN);

    lv_obj_t* lbl_icon = lv_label_create(rail);
    if (lbl_icon) {
        lv_label_set_text(lbl_icon, icon_glyph_utf8);
        lv_label_set_long_mode(lbl_icon, LV_LABEL_LONG_CLIP);
        info_set_font(lbl_icon, k_info_section_icon_font);
        lv_obj_set_style_text_color(lbl_icon, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl_icon, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(lbl_icon, LV_PCT(100));
    }

    lv_obj_t* lbl_sec = lv_label_create(rail);
    if (lbl_sec) {
        lv_label_set_text(lbl_sec, section_title_en);
        lv_label_set_long_mode(lbl_sec, LV_LABEL_LONG_WRAP);
        info_set_font(lbl_sec, LV_ACTIVE_PROFILE.font_normal);
        lv_obj_set_style_text_color(lbl_sec, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl_sec, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(lbl_sec, LV_PCT(100));
    }

    lv_obj_t* data_col = lv_obj_create(section);
    if (!data_col) return nullptr;
    lv_obj_set_flex_grow(data_col, 1);
    lv_obj_set_height(data_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(data_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(data_col, 2, LV_PART_MAIN);
    style_transparent_flex(data_col);
    // Patch D: shift KV block slightly right without moving rail / слегка вправо, rail не трогаем.
    lv_obj_set_style_pad_left(data_col, 5, LV_PART_MAIN);

    return data_col;
}

// STA primary channel: esp_wifi_sta_get_ap_info first; fallback WiFi.channel() (Patch D mini-audit).
static int info_sta_primary_channel() {
    if (WiFi.status() != WL_CONNECTED) {
        return -1;
    }
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.primary > 0) {
        return static_cast<int>(ap.primary);
    }
    const int ch = static_cast<int>(WiFi.channel());
    if (ch > 0 && ch <= 14) {
        return ch;
    }
    return -1;
}

static void info_mb_one_decimal(uint32_t bytes, unsigned& out_whole, unsigned& out_tenth) {
    if (bytes == 0u) {
        out_whole = 0u;
        out_tenth = 0u;
        return;
    }
    const uint64_t tenths = (static_cast<uint64_t>(bytes) * 10ull) / (1024u * 1024u);
    out_whole = static_cast<unsigned>(tenths / 10u);
    out_tenth = static_cast<unsigned>(tenths % 10u);
}

// Stage 6.6R-C: walk Info tree — dividers + KV/rail labels (tokenized at create; no layout change).
// Этап 6.6R-C: обход дерева Info — dividers и label-цвета без пересоздания экрана.
static void info_reapply_tree_colors(lv_obj_t* obj, const YoRadioPalette& pal, lv_obj_t* skip_subtree) {
    if (!obj) return;
    const uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; ++i) {
        lv_obj_t* ch = lv_obj_get_child(obj, i);
        if (!ch || ch == skip_subtree) continue;
        if (lv_obj_check_type(ch, &lv_label_class)) {
            const lv_font_t* f = lv_obj_get_style_text_font(ch, LV_PART_MAIN);
            if (f == static_cast<const lv_font_t*>(k_info_section_icon_font)) {
                lv_obj_set_style_text_color(ch, pal.text_secondary, LV_PART_MAIN);
            } else {
                lv_obj_t* parent = lv_obj_get_parent(ch);
                const lv_flex_flow_t flow =
                    parent ? lv_obj_get_style_flex_flow(parent, LV_PART_MAIN) : LV_FLEX_FLOW_ROW;
                if (parent && flow == LV_FLEX_FLOW_ROW && lv_obj_get_child(parent, 0) == ch) {
                    lv_obj_set_style_text_color(ch, pal.text_secondary, LV_PART_MAIN);
                } else if (parent && flow == LV_FLEX_FLOW_COLUMN) {
                    lv_obj_set_style_text_color(ch, pal.text_secondary, LV_PART_MAIN);
                } else {
                    lv_obj_set_style_text_color(ch, pal.text_primary, LV_PART_MAIN);
                }
            }
        } else if (lv_obj_get_height(ch) == 1) {
            if (lv_obj_get_style_bg_opa(ch, LV_PART_MAIN) == LV_OPA_COVER) {
                lv_obj_set_style_bg_color(ch, pal.divider, LV_PART_MAIN);
            }
        }
        info_reapply_tree_colors(ch, pal, skip_subtree);
    }
}

static void info_format_display_product_line(char* buf, size_t cap) {
    if (!buf || cap == 0u) {
        return;
    }
    const unsigned rw = static_cast<unsigned>(LV_ACTIVE_PROFILE.width);
    const unsigned rh = static_cast<unsigned>(LV_ACTIVE_PROFILE.height);
#if DSP_MODEL == DSP_ST7701
    snprintf(buf, cap, "ST7701 %ux%u", rw, rh);
#elif DSP_MODEL == DSP_AXS15231B
    snprintf(buf, cap, "AXS15231B %ux%u", rw, rh);
#elif DSP_MODEL == DSP_UEDX48480021
    snprintf(buf, cap, "UEDX %ux%u", rw, rh);
#else
    snprintf(buf, cap, "Panel %ux%u", rw, rh);
#endif
}

} // namespace

ScreenType LvglInfoPage::screenType() const {
    return ScreenType::Page;
}

void LvglInfoPage::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(_screen, LV_ACTIVE_PROFILE.frame_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, 6, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (!wgt_status_line::create(_screen, _status_line)) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    add_thin_divider(_screen, pal);

    _lbl_info_title = lv_label_create(_screen);
    if (_lbl_info_title) {
        lv_label_set_text(_lbl_info_title, "INFO");
        // ~+2 px vs font_normal (16): existing bundled 18 cyr, calmer than font_large (22).
        info_set_font(_lbl_info_title, reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr));
        lv_obj_set_style_text_color(_lbl_info_title, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_align(_lbl_info_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }

    lv_obj_t* content = lv_obj_create(_screen);
    if (content) {
        lv_obj_set_width(content, LV_PCT(100));
        lv_obj_set_flex_grow(content, 1);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        style_transparent_flex(content);
        lv_obj_set_style_pad_row(content, 10, LV_PART_MAIN);
        // After transparent base (pad_all 0): row gap + bottom inset — lower divider is content boundary (C4).
        lv_obj_set_style_pad_bottom(content, static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding), LV_PART_MAIN);

        lv_obj_t* dc_net = add_section_rail_block(
            content,
            "Network",
            reinterpret_cast<const char*>(u8"\uEB18"),
            pal);
        if (dc_net) {
            add_kv_row(dc_net, "SSID", "--", &_val_ssid, pal);
            add_kv_row(dc_net, "IP", "--", &_val_ip, pal);
            add_kv_row(dc_net, "Wi-Fi", "--", &_val_wifi, pal, InfoKvValueLongMode::ScrollCircular);
            add_kv_row(dc_net, "MAC", "--", &_val_mac, pal);
        }

        add_thin_divider(content, pal);

        lv_obj_t* dc_sys = add_section_rail_block(
            content,
            "System",
            reinterpret_cast<const char*>(u8"\uEF8E"),
            pal);
        if (dc_sys) {
            add_kv_row(dc_sys, "Firmware", "--", &_val_firmware, pal, InfoKvValueLongMode::ScrollCircular);
            add_kv_row(dc_sys, "Build", "--", &_val_build, pal);
            add_kv_row(dc_sys, "Chip", "--", &_val_chip, pal);
            add_kv_row(dc_sys, "CPU", "--", &_val_cpu, pal);
            add_kv_row(dc_sys, "Uptime", "--", &_val_uptime, pal);
        }

        add_thin_divider(content, pal);

        lv_obj_t* dc_disp = add_section_rail_block(
            content,
            "Display / UI",
            reinterpret_cast<const char*>(u8"\uEA89"),
            pal);
        if (dc_disp) {
            add_kv_row(dc_disp, "Display", "--", &_val_display, pal, InfoKvValueLongMode::ScrollCircular);
            add_kv_row(dc_disp, "LVGL", "--", &_val_lvgl, pal);
        }

        add_thin_divider(content, pal);

        lv_obj_t* dc_mem = add_section_rail_block(
            content,
            "Memory / SD",
            reinterpret_cast<const char*>(u8"\uEA88"),
            pal);
        if (dc_mem) {
            add_kv_row(dc_mem, "Heap", "--", &_val_heap, pal);
            add_kv_row(dc_mem, "PSRAM", "--", &_val_psram, pal);
            add_kv_row(dc_mem, "SD", "--", &_val_sd, pal);
        }

        add_thin_divider(content, pal);

        lv_obj_t* content_tail_spacer = lv_obj_create(content);
        if (content_tail_spacer) {
            lv_obj_set_width(content_tail_spacer, LV_PCT(100));
            lv_obj_set_flex_grow(content_tail_spacer, 1);
            lv_obj_set_style_min_height(content_tail_spacer, 0, LV_PART_MAIN);
            style_transparent_flex(content_tail_spacer);
        }
    }

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglInfoPage::enter() {}

void LvglInfoPage::exit() {}

void LvglInfoPage::update() {
    if (!_screen || !_status_line.root || !_val_ssid) return;

    wgt_status_line::update(_status_line);

    static char buf[80];

    // Wi-Fi summary: RSSI + channel + state; sample RSSI/channel at most every 3 s when connected.
    static uint32_t s_wifi_sample_ms = 0;
    static int      s_rssi_cached = -100;
    static int      s_ch_cached = -1;
    static bool     s_wifi_was_connected = false;
    const bool      wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected != s_wifi_was_connected) {
        s_wifi_was_connected = wifi_connected;
        s_wifi_sample_ms = 0;
    }
    const uint32_t now_ms = millis();
    if (wifi_connected) {
        if (s_wifi_sample_ms == 0u || (now_ms - s_wifi_sample_ms >= 3000u)) {
            s_wifi_sample_ms = now_ms;
            s_rssi_cached = WiFi.RSSI();
            s_ch_cached = info_sta_primary_channel();
        }
        info_set_text_if_changed(_val_ssid, WiFi.SSID().c_str());
        info_set_text_if_changed(_val_ip, WiFi.localIP().toString().c_str());
        if (s_ch_cached > 0) {
            snprintf(
                buf,
                sizeof(buf),
                "%d dBm / ch %d / Connected",
                s_rssi_cached,
                s_ch_cached);
        } else {
            snprintf(buf, sizeof(buf), "%d dBm / ch -- / Connected", s_rssi_cached);
        }
        info_set_text_if_changed(_val_wifi, buf);
    } else {
        info_set_text_if_changed(_val_ssid, "--");
        info_set_text_if_changed(_val_ip, "--");
        info_set_text_if_changed(_val_wifi, "-- / ch -- / Offline");
    }

    info_set_text_if_changed(_val_mac, WiFi.macAddress().c_str());

    info_set_text_if_changed(_val_firmware, YOVERSION);
    info_set_text_if_changed(_val_build, k_info_build_stamp);

    snprintf(buf, sizeof(buf), "%s rev.%d", ESP.getChipModel(), ESP.getChipRevision());
    info_set_text_if_changed(_val_chip, buf);

    snprintf(
        buf,
        sizeof(buf),
        "%u MHz / %u cores",
        static_cast<unsigned>(ESP.getCpuFreqMHz()),
        static_cast<unsigned>(ESP.getChipCores()));
    info_set_text_if_changed(_val_cpu, buf);

    uint32_t sec = static_cast<uint32_t>(millis() / 1000u);
    uint32_t h = sec / 3600u;
    uint32_t m = (sec % 3600u) / 60u;
    uint32_t s = sec % 60u;
    snprintf(
        buf,
        sizeof(buf),
        "%02lu:%02lu:%02lu",
        static_cast<unsigned long>(h),
        static_cast<unsigned long>(m),
        static_cast<unsigned long>(s));
    info_set_text_if_changed(_val_uptime, buf);

    static char s_display_line[48];
    static bool s_display_line_inited = false;
    if (!s_display_line_inited) {
        info_format_display_product_line(s_display_line, sizeof(s_display_line));
        s_display_line_inited = true;
    }
    info_set_text_if_changed(_val_display, s_display_line);

    snprintf(
        buf,
        sizeof(buf),
        "%d.%d.%d",
        LVGL_VERSION_MAJOR,
        LVGL_VERSION_MINOR,
        LVGL_VERSION_PATCH);
    info_set_text_if_changed(_val_lvgl, buf);

    uint32_t heap = ESP.getFreeHeap();
    if (heap >= 1024u * 1024u) {
        snprintf(buf, sizeof(buf), "%lu MB", static_cast<unsigned long>(heap / (1024u * 1024u)));
    } else {
        snprintf(buf, sizeof(buf), "%lu KB", static_cast<unsigned long>(heap / 1024u));
    }
    info_set_text_if_changed(_val_heap, buf);

    const uint32_t psram_total = ESP.getPsramSize();
    const uint32_t psram_free = ESP.getFreePsram();
    if (psram_total == 0u) {
        info_set_text_if_changed(_val_psram, "--");
    } else {
        unsigned fw = 0;
        unsigned ft = 0;
        unsigned tw = 0;
        unsigned tt = 0;
        info_mb_one_decimal(psram_free, fw, ft);
        info_mb_one_decimal(psram_total, tw, tt);
        snprintf(buf, sizeof(buf), "%u.%u / %u.%u MB", fw, ft, tw, tt);
        info_set_text_if_changed(_val_psram, buf);
    }

#if SDC_CS == 255
    info_set_text_if_changed(_val_sd, "--");
#else
    if (config.getMode() == PM_SDCARD) {
        info_set_text_if_changed(_val_sd, "Active");
    } else {
        info_set_text_if_changed(_val_sd, "Inactive");
    }
#endif
}

void LvglInfoPage::liveReapplyTheme() {
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    wgt_status_line::reapplyTheme(_status_line);
    if (_lbl_info_title) {
        lv_obj_set_style_text_color(_lbl_info_title, pal.text_primary, LV_PART_MAIN);
    }
    info_reapply_tree_colors(_screen, pal, _status_line.root);
    lv_obj_invalidate(_screen);
}

void LvglInfoPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _status_line = {};
    _lbl_info_title = nullptr;
    _val_ssid = _val_ip = _val_wifi = _val_mac = nullptr;
    _val_firmware = _val_build = _val_chip = _val_cpu = _val_uptime = nullptr;
    _val_display = _val_lvgl = nullptr;
    _val_heap = _val_psram = _val_sd = nullptr;
}

lv_obj_t* LvglInfoPage::screen() {
    return _screen;
}

} // namespace lvgl_ui

