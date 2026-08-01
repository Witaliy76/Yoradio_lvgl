/*
 * LvglInfoPage — LVGL Info page: network / system / display / memory.
 * LvglInfoPage — страница LVGL Info: сеть / система / дисплей / память.
 *
 * Layout: flex-column root with status chrome, INFO title, and a content area
 * holding four section-rail blocks separated by 1 px dividers.
 * Раскладка: flex-column root со status chrome, заголовком INFO и content из
 * четырёх секций с разделителями 1 px.
 *
 * All colors via LV_ACTIVE_PROFILE + yoradio_palette() — no hardcoded values.
 * Все цвета только через profile + palette — значения не зашиты.
 *
 * update() runs at nominally 1 Hz (throttled in display.cpp); Wi-Fi RSSI/channel
 * are sampled at most once every kWifiSamplePeriodMs (3 s).
 * update() вызывается nominally ~1 Гц (throttle в display.cpp); RSSI/канал Wi-Fi
 * сэмплируются не чаще одного раза в kWifiSamplePeriodMs (3 с).
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

// Author: Witaliy76 - https://github.com/Witaliy76
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
#include "../../core/options.h"

namespace lvgl_ui {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time resources / Ресурсы времени компиляции
// ─────────────────────────────────────────────────────────────────────────────

// Translation-unit build stamp — distinct from YOVERSION (the Firmware row shows YOVERSION).
// Build stamp единицы трансляции — отличается от YOVERSION (строка Firmware показывает YOVERSION).
static const char k_info_build_stamp[] = __DATE__ " " __TIME__;

// ─────────────────────────────────────────────────────────────────────────────
// UI string constants (l10n readiness) / Строки UI
// All user-visible strings are gathered here for future localization migration.
// INFOREF does not introduce runtime localization; kStr* are the single copy-point.
// Все видимые строки собраны здесь для будущей локализации.
// ─────────────────────────────────────────────────────────────────────────────

// Page title / Заголовок страницы
static constexpr char kStrInfoTitle[]       = "INFO";

// Section titles / Названия секций
static constexpr char kStrSectionNetwork[]  = "Network";
static constexpr char kStrSectionSystem[]   = "System";
static constexpr char kStrSectionDisplay[]  = "Display / UI";
static constexpr char kStrSectionMemory[]   = "Memory / SD";

// Key labels — Network / Ключи — Сеть
static constexpr char kStrKeySsid[]         = "SSID";
static constexpr char kStrKeyIp[]           = "IP";
static constexpr char kStrKeyWifi[]         = "Wi-Fi";
static constexpr char kStrKeyMac[]          = "MAC";

// Key labels — System / Ключи — Система
static constexpr char kStrKeyFirmware[]     = "Firmware";
static constexpr char kStrKeyBuild[]        = "Build";
static constexpr char kStrKeyChip[]         = "Chip";
static constexpr char kStrKeyCpu[]          = "CPU";
static constexpr char kStrKeyUptime[]       = "Uptime";

// Key labels — Display / UI / Ключи — Дисплей
static constexpr char kStrKeyDisplay[]      = "Display";
static constexpr char kStrKeyLvgl[]         = "LVGL";

// Key labels — Memory / SD / Ключи — Память
static constexpr char kStrKeyHeap[]         = "Heap";
static constexpr char kStrKeyPsram[]        = "PSRAM";
static constexpr char kStrKeySd[]           = "SD";

// State and placeholder values / Состояния и плейсхолдеры
static constexpr char kStrPlaceholder[]     = "--";
static constexpr char kStrWifiOffline[]     = "-- / ch -- / Offline";
static constexpr char kStrSdActive[]        = "Active";
static constexpr char kStrSdInactive[]      = "Inactive";

// ─────────────────────────────────────────────────────────────────────────────
// UI format-string constants / Форматные строки UI
// ─────────────────────────────────────────────────────────────────────────────

// Wi-Fi summary with primary channel / Строка Wi-Fi с каналом
static constexpr char kFmtWifiConnectedCh[]   = "%d dBm / ch %d / Connected";
// Wi-Fi summary — channel unknown / Строка Wi-Fi — канал неизвестен
static constexpr char kFmtWifiConnectedNoCh[] = "%d dBm / ch -- / Connected";
// Chip model + revision / Модель + ревизия чипа
static constexpr char kFmtChip[]              = "%s rev.%d";
// CPU frequency + core count / Частота + количество ядер
static constexpr char kFmtCpu[]               = "%u MHz / %u cores";
// Uptime HH:MM:SS / Время работы
static constexpr char kFmtUptime[]            = "%02lu:%02lu:%02lu";
// LVGL version x.y.z / Версия LVGL
static constexpr char kFmtLvglVersion[]       = "%d.%d.%d";
// Free heap in MB / Свободная heap в МБ
static constexpr char kFmtHeapMb[]            = "%lu MB";
// Free heap in KB / Свободная heap в КБ
static constexpr char kFmtHeapKb[]            = "%lu KB";
// PSRAM free / total with one decimal / PSRAM free/total с одним знаком
static constexpr char kFmtPsram[]             = "%u.%u / %u.%u MB";

// ─────────────────────────────────────────────────────────────────────────────
// Icon glyph resources / Ресурсы иконок секций
// UTF-8 codepoints for section-rail Tabler icons. Not localizable.
// UTF-8 codepoints для иконок секций. Не локализуются.
// In C++17, u8"" produces const char8_t* — reinterpret_cast to const char* required.
// В C++17 u8"" возвращает const char8_t* — необходимо приведение к const char*.
// ─────────────────────────────────────────────────────────────────────────────

static const char* const kIconNetwork = reinterpret_cast<const char*>(u8"\uEB18");
static const char* const kIconSystem  = reinterpret_cast<const char*>(u8"\uEF8E");
static const char* const kIconDisplay = reinterpret_cast<const char*>(u8"\uEA89");
static const char* const kIconMemory  = reinterpret_cast<const char*>(u8"\uEA88");

// ─────────────────────────────────────────────────────────────────────────────
// Font resources / Шрифты экрана
// ─────────────────────────────────────────────────────────────────────────────

// INFO page title: M18 — slightly larger than font_normal (≈16 px), less heavy than font_large (22 px).
// Шрифт заголовка INFO: M18 — чуть крупнее font_normal (≈16 px), не такой тяжёлый как font_large.
static const void* const kFontInfoTitle =
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr);

// Section-rail icon font — 36 px. Also used as the classification key in info_reapply_tree_colors()
// to identify icon labels (as opposed to key/value labels) during a theme walk.
// Шрифт иконок rail — 36 px. Также служит ключом классификации в info_reapply_tree_colors():
// по нему отличаем icon label от key/value labels при обходе дерева при смене темы.
static const void* const kFontSectionIcon =
    reinterpret_cast<const void*>(&lv_font_yora_info_section_icons_36);

// ─────────────────────────────────────────────────────────────────────────────
// Visual and layout constants / Визуальные и геометрические константы
// ─────────────────────────────────────────────────────────────────────────────

// Root flex-column pad_row / Зазор flex-строк корня
static constexpr lv_coord_t kRootRowGap       = 6;
// Content column pad_row between sections / Зазор секций в content
static constexpr lv_coord_t kContentRowGap    = 10;
// 1 px divider exact height / Точная высота 1 px разделителя
static constexpr lv_coord_t kDividerHeight    = 1;
// Data-column left inset (shifts KV block right relative to rail) / Отступ data-column от rail
static constexpr lv_coord_t kDataColumnInset  = 5;
// Display-width threshold: at or below this → compact layout / Порог compact/wide layout
static constexpr uint32_t   kCompactWidthMax  = 360u;
// Rail % width on compact (≤ kCompactWidthMax) displays / Ширина rail на compact
static constexpr int32_t    kRailWidthCompactPct = 24;
// Rail % width on wide (> kCompactWidthMax) displays / Ширина rail на wide
static constexpr int32_t    kRailWidthWidePct    = 22;
// Rail left inset on compact displays / Левый отступ rail на compact
static constexpr lv_coord_t kRailInsetCompact = 6;
// Rail left inset on wide displays / Левый отступ rail на wide
static constexpr lv_coord_t kRailInsetWide    = 10;
// KV row key-label width as % of row / Ширина key-label строки KV в % от строки
static constexpr int32_t    kKvKeyWidthPct    = 34;
// Minimum interval between Wi-Fi RSSI/channel samples / Минимальный интервал сэмплирования Wi-Fi
static constexpr uint32_t   kWifiSamplePeriodMs = 3000u;

// ─────────────────────────────────────────────────────────────────────────────
// Generic LVGL helpers / Вспомогательные функции LVGL
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Layout factories / Фабрики разметки
// ─────────────────────────────────────────────────────────────────────────────

// 1 px divider — matches Main status-divider geometry.
// lv_theme_default adds "card" padding to bare lv_obj; zeroed here so the line stays exactly 1 px.
// 1 px разделитель — геометрия как у status-divider на Main.
// Тема LVGL добавляет "card" padding к lv_obj; обнуляется для точной высоты 1 px.
static lv_obj_t* add_thin_divider(lv_obj_t* parent, const YoRadioPalette& pal) {
    lv_obj_t* d = lv_obj_create(parent);
    if (!d) return nullptr;
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_height(d, kDividerHeight);
    lv_obj_set_style_min_height(d, kDividerHeight, LV_PART_MAIN);
    lv_obj_set_style_max_height(d, kDividerHeight, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d, 0, LV_PART_MAIN);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(d, 0);
    return d;
}

// KV row long-mode selector / Режим отображения value в KV строке
enum class InfoKvValueLongMode : uint8_t {
    Clip,
    ScrollCircular,
};

// KV row: [key label kKvKeyWidthPct%] [value flex-grow].
// ScrollCircular wraps the value in a bounded flex slot so LVGL can scroll long text without
// affecting row or data-column dimensions (value width fixed at 100% of the slot).
// Clip variant attaches the value label directly to the row (no slot wrapper).
//
// Вариант ScrollCircular: value в flex-slot, который поглощает оставшееся место.
// LVGL может скроллить длинный текст без влияния на размеры строки или data-column.
// Вариант Clip: value прямо в строке, без wrapper.
static void add_kv_row(
    lv_obj_t*          data_col,
    const char*        key,
    const char*        initial,
    lv_obj_t**         out_val,
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
        lv_obj_set_width(k, LV_PCT(kKvKeyWidthPct));
    }

    lv_obj_t* v = nullptr;
    if (val_long_mode == InfoKvValueLongMode::ScrollCircular) {
        // Flex slot takes remainder; value width = 100% of slot so circular scroll is bounded.
        // Flex-slot поглощает оставшееся место; ширина value = 100% slot для ограниченного скролла.
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

// Section rail block: [rail (icon + section title)] [data column].
// Returns data column for the caller to add KV rows. Returns nullptr on failure.
// Rail width and left inset are responsive: narrower on wide displays for more KV room.
//
// Секция: [rail (иконка + название)] [data column (KV строки)].
// Возвращает data column для добавления KV строк. При ошибке — nullptr.
// Ширина и отступ rail адаптируются: на широких экранах rail уже для большего места под KV.
static lv_obj_t* add_section_rail_block(
    lv_obj_t*          content_column,
    const char*        section_title_en,
    const char*        icon_glyph_utf8,
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
    // Narrower rail on wide displays gives more room for KV data.
    // Slightly wider on compact (≤ kCompactWidthMax) so icon + title can breathe.
    // Узкий rail на широких дисплеях → больше места под KV.
    const uint32_t W = LV_ACTIVE_PROFILE.width;
    const lv_coord_t rail_w =
        (W <= kCompactWidthMax) ? LV_PCT(kRailWidthCompactPct) : LV_PCT(kRailWidthWidePct);
    lv_obj_set_width(rail, rail_w);
    lv_obj_set_height(rail, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    style_transparent_flex(rail);
    // Left inset prevents rail from hugging the screen edge without moving its position in the row.
    // Левый отступ: rail не прилипает к краю экрана, не смещая свою позицию в строке.
    const lv_coord_t rail_inset_l = (W <= kCompactWidthMax) ? kRailInsetCompact : kRailInsetWide;
    lv_obj_set_style_pad_left(rail, rail_inset_l, LV_PART_MAIN);
    lv_obj_set_style_pad_row(rail, 2, LV_PART_MAIN);

    lv_obj_t* lbl_icon = lv_label_create(rail);
    if (lbl_icon) {
        lv_label_set_text(lbl_icon, icon_glyph_utf8);
        lv_label_set_long_mode(lbl_icon, LV_LABEL_LONG_CLIP);
        info_set_font(lbl_icon, kFontSectionIcon);
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
    // Left inset shifts the KV block right of the rail without moving the rail itself.
    // Отступ слева сдвигает KV-блок правее rail, не перемещая rail.
    lv_obj_set_style_pad_left(data_col, kDataColumnInset, LV_PART_MAIN);

    return data_col;
}

// ─────────────────────────────────────────────────────────────────────────────
// Network helpers / Вспомогательные функции сети
// ─────────────────────────────────────────────────────────────────────────────

// Primary STA channel: esp_wifi_sta_get_ap_info preferred; fallback to WiFi.channel().
// Основной канал STA: приоритет esp_wifi_sta_get_ap_info; fallback WiFi.channel().
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

// ─────────────────────────────────────────────────────────────────────────────
// Math / formatting helpers / Математические помощники
// ─────────────────────────────────────────────────────────────────────────────

// Convert bytes to MB with one decimal place. / Байты → МБ с одним знаком после запятой.
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

// ─────────────────────────────────────────────────────────────────────────────
// Theme helpers / Вспомогательные функции темы
// ─────────────────────────────────────────────────────────────────────────────

// Recursive tree walk — recolors dividers and labels without rebuilding the screen.
//
// Classification rules (must match the object tree created by add_section_rail_block / add_kv_row):
//   label with font == kFontSectionIcon  → section icon    → text_secondary
//   first label-child of a ROW parent   → key label        → text_secondary
//   label-child of a COLUMN parent      → section title    → text_secondary
//   all other labels                    → value labels     → text_primary
//   object with height == 1 && OPA_COVER → divider        → pal.divider
//
// Рекурсивный обход дерева — перекрашивает разделители и labels без пересоздания экрана.
// Классификация должна соответствовать дереву, созданному add_section_rail_block / add_kv_row.
static void info_reapply_tree_colors(lv_obj_t* obj, const YoRadioPalette& pal, lv_obj_t* skip_subtree) {
    if (!obj) return;
    const uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; ++i) {
        lv_obj_t* ch = lv_obj_get_child(obj, i);
        if (!ch || ch == skip_subtree) continue;
        if (lv_obj_check_type(ch, &lv_label_class)) {
            const lv_font_t* f = lv_obj_get_style_text_font(ch, LV_PART_MAIN);
            if (f == static_cast<const lv_font_t*>(kFontSectionIcon)) {
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

// ─────────────────────────────────────────────────────────────────────────────
// Display formatting helpers / Вспомогательные функции форматирования дисплея
// ─────────────────────────────────────────────────────────────────────────────

// Compile-time display product-line string — determined by DSP_MODEL and panel resolution.
// Hardware identifiers ("ST7701", …) are not localizable user-facing text.
// Compile-time строка панели — определяется DSP_MODEL и разрешением.
static void info_format_display_product_line(char* buf, size_t cap) {
    if (!buf || cap == 0u) {
        return;
    }
    const unsigned rw = static_cast<unsigned>(LV_ACTIVE_PROFILE.width);
    const unsigned rh = static_cast<unsigned>(LV_ACTIVE_PROFILE.height);
#if DSP_MODEL == DSP_ST7701
    snprintf(buf, cap, "ST7701 %ux%u", rw, rh);
#else
    snprintf(buf, cap, "Panel %ux%u", rw, rh);
#endif
}

// Shared formatting scratch buffer — used by all _refresh*() methods sequentially.
// Info refresh runs sequentially in DspTask; methods never execute concurrently.
// Общий scratch buffer — используется всеми _refresh*() методами последовательно.
static char s_info_format_buf[80];

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout builders / Билдеры разметки
// ─────────────────────────────────────────────────────────────────────────────

// Status chrome: wgt_status_line + 1 px thin divider below it.
// If status-line creation fails (_status_line.root stays nullptr), the divider is not added.
// The caller (create) checks _status_line.root and performs lv_obj_del + early return on failure.
//
// Status chrome: wgt_status_line + 1 px разделитель ниже.
// Если status line не создан (_status_line.root == nullptr), разделитель не добавляется.
// Вызывающий код (create) проверяет _status_line.root и выполняет lv_obj_del + ранний выход.
void LvglInfoPage::create_status_chrome(LvglInfoPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    wgt_status_line::create(self._screen, self._status_line);
    if (!self._status_line.root) return;
    add_thin_divider(self._screen, pal);
}

// INFO title label: M18 (kFontInfoTitle), text_primary, left-aligned.
// Заголовок INFO: M18 (kFontInfoTitle), text_primary, по левому краю.
void LvglInfoPage::create_title(LvglInfoPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    self._lbl_info_title = lv_label_create(self._screen);
    if (self._lbl_info_title) {
        lv_label_set_text(self._lbl_info_title, kStrInfoTitle);
        info_set_font(self._lbl_info_title, kFontInfoTitle);
        lv_obj_set_style_text_color(self._lbl_info_title, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_info_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }
}

// Content area: four section-rail blocks separated by 1 px dividers + bottom divider + tail spacer.
// All 14 value-label handles are populated via add_kv_row out-parameters (stored as class members).
// The content container, section containers, rails, dividers, and tail spacer are local — not stored.
// Recoloring these objects is handled by the recursive info_reapply_tree_colors() tree walk.
//
// Content: четыре section-rail блока с разделителями + нижний разделитель + tail spacer.
// Все 14 value-label handle заполняются через out-параметры add_kv_row (хранятся как члены класса).
// Content container, секции, rails, разделители, tail spacer — локальные, не сохраняются.
// Перекраска этих объектов — через рекурсивный обход info_reapply_tree_colors().
void LvglInfoPage::create_content(LvglInfoPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;

    lv_obj_t* content = lv_obj_create(self._screen);
    if (!content) return;
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    style_transparent_flex(content);
    lv_obj_set_style_pad_row(content, kContentRowGap, LV_PART_MAIN);
    // Bottom padding = frame_padding so the lower divider aligns with the screen edge inset.
    // Нижний padding = frame_padding: нижний разделитель выравнивается с отступом экрана.
    lv_obj_set_style_pad_bottom(content,
        static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding), LV_PART_MAIN);

    lv_obj_t* dc_net = add_section_rail_block(content, kStrSectionNetwork, kIconNetwork, pal);
    if (dc_net) {
        add_kv_row(dc_net, kStrKeySsid,    kStrPlaceholder, &self._val_ssid,     pal);
        add_kv_row(dc_net, kStrKeyIp,      kStrPlaceholder, &self._val_ip,       pal);
        add_kv_row(dc_net, kStrKeyWifi,    kStrPlaceholder, &self._val_wifi,     pal,
                   InfoKvValueLongMode::ScrollCircular);
        add_kv_row(dc_net, kStrKeyMac,     kStrPlaceholder, &self._val_mac,      pal);
    }

    add_thin_divider(content, pal);

    lv_obj_t* dc_sys = add_section_rail_block(content, kStrSectionSystem, kIconSystem, pal);
    if (dc_sys) {
        add_kv_row(dc_sys, kStrKeyFirmware, kStrPlaceholder, &self._val_firmware, pal,
                   InfoKvValueLongMode::ScrollCircular);
        add_kv_row(dc_sys, kStrKeyBuild,    kStrPlaceholder, &self._val_build,    pal);
        add_kv_row(dc_sys, kStrKeyChip,     kStrPlaceholder, &self._val_chip,     pal);
        add_kv_row(dc_sys, kStrKeyCpu,      kStrPlaceholder, &self._val_cpu,      pal);
        add_kv_row(dc_sys, kStrKeyUptime,   kStrPlaceholder, &self._val_uptime,   pal);
    }

    add_thin_divider(content, pal);

    lv_obj_t* dc_disp = add_section_rail_block(content, kStrSectionDisplay, kIconDisplay, pal);
    if (dc_disp) {
        add_kv_row(dc_disp, kStrKeyDisplay, kStrPlaceholder, &self._val_display, pal,
                   InfoKvValueLongMode::ScrollCircular);
        add_kv_row(dc_disp, kStrKeyLvgl,    kStrPlaceholder, &self._val_lvgl,    pal);
    }

    add_thin_divider(content, pal);

    lv_obj_t* dc_mem = add_section_rail_block(content, kStrSectionMemory, kIconMemory, pal);
    if (dc_mem) {
        add_kv_row(dc_mem, kStrKeyHeap,  kStrPlaceholder, &self._val_heap,  pal);
        add_kv_row(dc_mem, kStrKeyPsram, kStrPlaceholder, &self._val_psram, pal);
        add_kv_row(dc_mem, kStrKeySd,    kStrPlaceholder, &self._val_sd,    pal);
    }

    add_thin_divider(content, pal);

    // Tail spacer absorbs remaining vertical space, keeping the lower divider at the bottom.
    // Tail spacer поглощает оставшееся вертикальное место, прижимая нижний разделитель к низу.
    lv_obj_t* content_tail_spacer = lv_obj_create(content);
    if (content_tail_spacer) {
        lv_obj_set_width(content_tail_spacer, LV_PCT(100));
        lv_obj_set_flex_grow(content_tail_spacer, 1);
        lv_obj_set_style_min_height(content_tail_spacer, 0, LV_PART_MAIN);
        style_transparent_flex(content_tail_spacer);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Class lifecycle and update methods / Методы класса: lifecycle и update
// ─────────────────────────────────────────────────────────────────────────────

ScreenType LvglInfoPage::screenType() const {
    return ScreenType::Page;
}

void LvglInfoPage::create() {
    if (_screen) return;

    const YoRadioPalette& pal = yoradio_palette();

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(_screen, LV_ACTIVE_PROFILE.frame_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, kRootRowGap, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_chrome(*this, pal);

    if (!_status_line.root) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    create_title(*this, pal);
    create_content(*this, pal);

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglInfoPage::enter() {}

void LvglInfoPage::exit() {}

void LvglInfoPage::update() {
    if (!_screen || !_status_line.root || !_val_ssid) return;

    wgt_status_line::update(_status_line);

    const uint32_t now_ms = millis();

    _refreshNetwork(now_ms);
    _refreshSystem(now_ms);
    _refreshDisplay();
    _refreshMemory();
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime refresh methods / Методы runtime-обновления данных
// ─────────────────────────────────────────────────────────────────────────────

// Network: RSSI/channel/SSID/IP/MAC.
// Wi-Fi cache is instance-owned (_wifi_*); reset only in _nullHandles() so throttle
// survives enter()/exit() but not destroy()/auto-delete + recreate.
// Сетевые данные: RSSI/канал/SSID/IP/MAC.
// Кэш Wi-Fi принадлежит экземпляру; сбрасывается только в _nullHandles(),
// поэтому throttle переживает enter()/exit(), но не destroy()/auto-delete + recreate.
void LvglInfoPage::_refreshNetwork(uint32_t now_ms) {
    const bool wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected != _wifi_was_connected) {
        _wifi_was_connected = wifi_connected;
        _wifi_sample_ms = 0;
    }
    if (wifi_connected) {
        if (_wifi_sample_ms == 0u || (now_ms - _wifi_sample_ms >= kWifiSamplePeriodMs)) {
            _wifi_sample_ms   = now_ms;
            _wifi_rssi_cached = WiFi.RSSI();
            _wifi_ch_cached   = info_sta_primary_channel();
        }
        info_set_text_if_changed(_val_ssid, WiFi.SSID().c_str());
        info_set_text_if_changed(_val_ip,   WiFi.localIP().toString().c_str());
        if (_wifi_ch_cached > 0) {
            snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtWifiConnectedCh,
                     _wifi_rssi_cached, _wifi_ch_cached);
        } else {
            snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtWifiConnectedNoCh,
                     _wifi_rssi_cached);
        }
        info_set_text_if_changed(_val_wifi, s_info_format_buf);
    } else {
        info_set_text_if_changed(_val_ssid, kStrPlaceholder);
        info_set_text_if_changed(_val_ip,   kStrPlaceholder);
        info_set_text_if_changed(_val_wifi, kStrWifiOffline);
    }
    info_set_text_if_changed(_val_mac, WiFi.macAddress().c_str());
}

// System: firmware version, build stamp, chip, CPU, uptime.
// Uptime derived from now_ms to avoid a second millis() call.
// Система: версия прошивки, build stamp, чип, CPU, uptime.
// Uptime вычисляется из now_ms без второго вызова millis().
void LvglInfoPage::_refreshSystem(uint32_t now_ms) {
    info_set_text_if_changed(_val_firmware, YOVERSION);
    info_set_text_if_changed(_val_build, k_info_build_stamp);

    snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtChip,
             ESP.getChipModel(), ESP.getChipRevision());
    info_set_text_if_changed(_val_chip, s_info_format_buf);

    snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtCpu,
             static_cast<unsigned>(ESP.getCpuFreqMHz()),
             static_cast<unsigned>(ESP.getChipCores()));
    info_set_text_if_changed(_val_cpu, s_info_format_buf);

    const uint32_t sec = now_ms / 1000u;
    const uint32_t h   = sec / 3600u;
    const uint32_t m   = (sec % 3600u) / 60u;
    const uint32_t s   = sec % 60u;
    snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtUptime,
             static_cast<unsigned long>(h),
             static_cast<unsigned long>(m),
             static_cast<unsigned long>(s));
    info_set_text_if_changed(_val_uptime, s_info_format_buf);
}

// Display/UI: compile-time panel product line (cached once) + LVGL version.
// s_display_line is function-static: compile-time/profile-derived, not Wi-Fi lifecycle.
// Дисплей: строка панели (инициализируется один раз) + версия LVGL.
// s_display_line — function-static: compile-time/profile, не зависит от lifecycle Wi-Fi.
void LvglInfoPage::_refreshDisplay() {
    static char s_display_line[48];
    static bool s_display_line_inited = false;
    if (!s_display_line_inited) {
        info_format_display_product_line(s_display_line, sizeof(s_display_line));
        s_display_line_inited = true;
    }
    info_set_text_if_changed(_val_display, s_display_line);

    snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtLvglVersion,
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    info_set_text_if_changed(_val_lvgl, s_info_format_buf);
}

// Memory/SD: free heap (MB or KB), PSRAM free/total, SD card state.
// Память / SD: свободная heap (МБ или КБ), PSRAM free/total, состояние SD.
void LvglInfoPage::_refreshMemory() {
    const uint32_t heap = ESP.getFreeHeap();
    if (heap >= 1024u * 1024u) {
        snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtHeapMb,
                 static_cast<unsigned long>(heap / (1024u * 1024u)));
    } else {
        snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtHeapKb,
                 static_cast<unsigned long>(heap / 1024u));
    }
    info_set_text_if_changed(_val_heap, s_info_format_buf);

    const uint32_t psram_total = ESP.getPsramSize();
    const uint32_t psram_free  = ESP.getFreePsram();
    if (psram_total == 0u) {
        info_set_text_if_changed(_val_psram, kStrPlaceholder);
    } else {
        unsigned fw = 0, ft = 0, tw = 0, tt = 0;
        info_mb_one_decimal(psram_free,  fw, ft);
        info_mb_one_decimal(psram_total, tw, tt);
        snprintf(s_info_format_buf, sizeof(s_info_format_buf), kFmtPsram, fw, ft, tw, tt);
        info_set_text_if_changed(_val_psram, s_info_format_buf);
    }

#if SDC_CS == 255
    info_set_text_if_changed(_val_sd, kStrPlaceholder);
#else
    if (config.getMode() == PM_SDCARD) {
        info_set_text_if_changed(_val_sd, kStrSdActive);
    } else {
        info_set_text_if_changed(_val_sd, kStrSdInactive);
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
    // Manual delete path: drop the LVGL root tree, then null handles.
    // Ручное удаление: удаляем дерево LVGL, затем обнуляем указатели.
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglInfoPage::releaseAfterAutoDelete() {
    // PageChain auto-delete: LVGL already freed the object tree via lv_scr_load_anim auto_del.
    // All Info values are runtime-derived — nothing to persist; only null handles here.
    // Never call lv_obj_del here — the tree is already gone.
    // PageChain auto-delete: LVGL уже освободил дерево объектов.
    // Все значения Info вычисляются в runtime — ничего хранить; только обнуляем указатели.
    _nullHandles();
}

void LvglInfoPage::_nullHandles() {
    _screen = nullptr;
    _status_line = {};
    _lbl_info_title = nullptr;
    _val_ssid = _val_ip = _val_wifi = _val_mac = nullptr;
    _val_firmware = _val_build = _val_chip = _val_cpu = _val_uptime = nullptr;
    _val_display = _val_lvgl = nullptr;
    _val_heap = _val_psram = _val_sd = nullptr;
    // Wi-Fi cache reset: next create + update will perform a fresh RSSI/channel sample.
    // Сброс кэша Wi-Fi: при следующем create + update будет свежий sample RSSI/канала.
    _wifi_sample_ms     = 0;
    _wifi_rssi_cached   = -100;
    _wifi_ch_cached     = -1;
    _wifi_was_connected = false;
}

lv_obj_t* LvglInfoPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
