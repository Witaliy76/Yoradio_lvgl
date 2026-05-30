/*
 * LvglStationPage — Station list (6.3F9 + 6.3G play + 6.3H enter scroll): left gutter marker; play on accepted SHORT_CLICKED only.
 * Страница Station: ввод 6.3F9; визуал — маркер текущей слева от номера/имени, pad лейбла уводит длинные имена.
 *
 * DspTask-only lv_*; one label scrolls natively, text buffer lives outside LVGL heap.
 * Только DspTask для lv_*; один label скроллится native, буфер текста вне LVGL heap.
 */

#include "scr_station.h"


#include <cstdio>

#include "Arduino.h"
#include <cstdint>

#include "lvgl.h"
#include "../../core/options.h"
#include "../adapters/station_list_adapter.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

namespace {

constexpr size_t kStationLineBytes = 112;  // Max UTF-8 bytes budget per station line in list buffer / лимит байт на строку буфера
constexpr size_t kStationListExtraBytes = 96;  // Extra pool for list realloc headroom / запас памяти буфера под список
// List font: M22 (line_height 25) + line_space — kStationLinePitch must stay in sync w/ _lbl_list.
// Шрифт списка: M22; шаг строки = line_height(25) + kStationListLineSpace (тест +2 к M20).
constexpr lv_coord_t kStationListFontLineHeight = 25;  // List row font metrics baseline height / высота строки шрифта M22 для списка
constexpr lv_coord_t kStationListLineSpace = 16;  // LVGL line_space between rows (gap under glyphs) / межстрочный зазор LVGL
constexpr lv_coord_t kStationLinePitch = kStationListFontLineHeight + kStationListLineSpace;  // Row step for overlay math / шаг строки для Y оверлеев
// Overlays align to the *text* line (first kStationListFontLineHeight px), not the line_space gap below.
// Оверлей и маркер — по визуальной строке текста, не по полному шагу (после текста — пустой line_space).
// Row highlight: +5px padding top and bottom vs previous (pad 3→8); total h=41, bleeds into line_space a bit.
// Подсветка строки: +5px сверху и +5px снизу к прежнему; часть уходит в межстрочный зазор.
constexpr lv_coord_t kRowOverlayTopPad = 8;  // Highlight extends this many px above/below text / паддинг подсветки вокруг текста
constexpr lv_coord_t kRowOverlayHeight = kStationListFontLineHeight + 2 * kRowOverlayTopPad;  // Total highlight band height (=41) / полная высота полосы подсветки
constexpr lv_coord_t kRowAccentY = -2;  // Vertical offset of left accent strip vs overlay / смещение левой полоски фокуса по Y
constexpr lv_coord_t kRowAccentH = 29;  // Height of left accent strip / высота вертикальной полоски акцента
constexpr lv_coord_t kMarkerIconLineHeight = 17;  // Speaker glyph font cap-height used for centering / высота глифа station_icons для выравнивания
constexpr lv_coord_t kMarkerY =
    (kStationListFontLineHeight - kMarkerIconLineHeight) / 2;  // Center marker in text band (4 px) / вертикальное центрирование в строке текста
constexpr lv_coord_t kListPadLeft = 16;  // List area inner left padding from profile edge / левый padding контейнера списка
// Stage 6.3 polish: left gutter — accent | gap | marker | gap | list text (pad on _lbl_list).
// Tune spacing only here / поджать только зазоры; слот маркера и шрифт без ужима.
constexpr lv_coord_t kFocusAccentStripW = 3;  // Focus rail width at row left edge / ширина левой полоски «фокусная строка»
constexpr lv_coord_t kMarkerGutterAfterAccent = 5;  // Horizontal gap accent→speaker glyph / зазор акцент → иконка маркера
constexpr lv_coord_t kMarkerBoxW = 32;  // Reserved width so glyph never collides with number / ширина слота под маркер слева от номера
constexpr lv_coord_t kMarkerToNameGap = 5;  // Gap after marker slot before list label text (number column) / зазор после слота → начало текста лейбла

static lv_coord_t marker_left_x_in_list() {
    return -kListPadLeft + 1 + kFocusAccentStripW + kMarkerGutterAfterAccent;
}

// Indent list label so long names never run under the marker / отступ текста под колонку маркера.
static lv_coord_t list_label_pad_left_for_marker_gutter() {
    const int32_t mleft = static_cast<int32_t>(marker_left_x_in_list());
    const int32_t mright = mleft + static_cast<int32_t>(kMarkerBoxW);
    const int32_t pad = mright + static_cast<int32_t>(kMarkerToNameGap);
    return static_cast<lv_coord_t>(pad > 0 ? pad : 0);
}

// Same UTF-8 chunk as scr_main `k_meta_field_sep` — U+2022 • in lv_font_yora_montserrat_16_cyr (not U+00B7).
// Тот же разделитель, что в meta row Main — см. scr_main.cpp рядом с k_meta_field_sep.
static constexpr char kMetaFieldSepUtf8[] = " \xE2\x80\xA2 ";

constexpr lv_coord_t kListPadTop = 8; // must match _list_area pad_top / как у отступа списка
// Ignore taps in right strip where vertical scrollbar sits (SHORT_CLICKED belt-and suspenders vs old F2–F4).
constexpr int32_t kScrollbarRightIgnorePx = 26;
// LVGL releases with scroll_obj==NULL if scroll never latched — reject by Δscroll_y / finger travel vs PRESSED.
// Endpoints lie: finger may return near press; scroll_y may bounce — track peaks during LV_EVENT_PRESSING.
// scroll_obj==NULL или отскок: конец жеста может совпасть с началом — копим пики по PRESSING.
constexpr lv_coord_t kListTapMaxScrollYDeltaPx = 14;
constexpr lv_coord_t kListTapMaxFingerTravelPx = 24;

static lv_coord_t row_y_for_station_row(uint16_t station_one_based) {
    return static_cast<lv_coord_t>(
        (static_cast<uint32_t>(station_one_based) - 1u) * static_cast<uint32_t>(kStationLinePitch));
}

static void station_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

static void style_transparent(lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void add_thin_divider(lv_obj_t* parent, const YoRadioPalette& pal) {
    lv_obj_t* div = lv_obj_create(parent);
    if (!div) return;
    lv_obj_set_width(div, LV_PCT(100));
    lv_obj_set_height(div, 1);
    lv_obj_set_style_bg_color(div, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
}

static void station_reapply_dividers(lv_obj_t* obj, const YoRadioPalette& pal) {
    if (!obj) return;
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* ch = lv_obj_get_child(obj, i);
        if (!ch) continue;
        if (lv_obj_get_height(ch) == 1 && lv_obj_get_style_bg_opa(ch, LV_PART_MAIN) == LV_OPA_COVER) {
            lv_obj_set_style_bg_color(ch, pal.divider, LV_PART_MAIN);
        }
        station_reapply_dividers(ch, pal);
    }
}

} // namespace

ScreenType LvglStationPage::screenType() const {
    return ScreenType::Page;
}

void LvglStationPage::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(_screen, LV_ACTIVE_PROFILE.frame_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, 8, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (!wgt_status_line::create(_screen, _status_line)) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    add_thin_divider(_screen, pal);

    lv_obj_t* header = lv_obj_create(_screen);
    if (header) {
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_height(header, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        style_transparent(header);

        _lbl_title = lv_label_create(header);
        if (_lbl_title) {
            lv_label_set_text(_lbl_title, "STATIONS");
            station_set_font(_lbl_title, reinterpret_cast<const void*>(&lv_font_yora_montserrat_20_cyr));
            lv_obj_set_style_text_color(_lbl_title, pal.text_primary, LV_PART_MAIN);
        }

        _lbl_count = lv_label_create(header);
        if (_lbl_count) {
            lv_label_set_text(_lbl_count, "-- / --");
            station_set_font(_lbl_count, reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr));
            lv_obj_set_style_text_color(_lbl_count, pal.text_secondary, LV_PART_MAIN);
        }
    }

    _list_area = lv_obj_create(_screen);
    if (_list_area) {
        lv_obj_set_width(_list_area, LV_PCT(100));
        lv_obj_set_flex_grow(_list_area, 1);
        lv_obj_set_style_bg_opa(_list_area, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(_list_area, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_list_area, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(_list_area, kListPadLeft, LV_PART_MAIN);
        lv_obj_set_style_pad_right(_list_area, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_top(_list_area, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(_list_area, 8, LV_PART_MAIN);
        lv_obj_add_flag(_list_area, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(_list_area, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        // Needed so indev hits the scroll widget (tap routes here, not invisible label handlers). / Индеф → скролл.
        lv_obj_add_flag(_list_area, LV_OBJ_FLAG_CLICKABLE);
        // Bubble horizontal gestures to page root carousel (GESTURE handler on _screen).
        // Пузырить горизонтальные жесты на корень — обработчик карусели на _screen.
        lv_obj_add_flag(_list_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_scroll_dir(_list_area, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(_list_area, LV_SCROLLBAR_MODE_AUTO);
        // F7/F8: PRESSED captures scroll_y + point; SHORT_CLICKED filtered if stroke looked like scroll (no latch).
        // F7/F8: PRESSED фиксирует scroll_y и точку; SHORT_CLICKED отбрасываем, если жест был как прокрутка.
        lv_obj_add_event_cb(_list_area, _listAreaPressedEvt, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(_list_area, _listAreaPressingEvt, LV_EVENT_PRESSING, this);
        lv_obj_add_event_cb(_list_area, _listAreaReleasedEvt, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(_list_area, _listAreaShortClickedEvt, LV_EVENT_SHORT_CLICKED, this);

        _populateStationList();
    }

    _hint_area = lv_obj_create(_screen);
    if (_hint_area) {
        lv_obj_set_width(_hint_area, LV_PCT(100));
        lv_obj_set_height(_hint_area, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(_hint_area, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(_hint_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        // Quiet band: not a button; border = same token as thin dividers (not panel_border @ low opa — was nearly invisible).
        // Тихая полоса: рамка как у 1px divider, иначе panel_border+30% «пропадает» на тёмном фоне.
        lv_obj_set_style_bg_color(_hint_area, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_hint_area, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_border_color(_hint_area, pal.divider, LV_PART_MAIN);
        lv_obj_set_style_border_opa(_hint_area, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(_hint_area, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(_hint_area, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_left(_hint_area, 16, LV_PART_MAIN);
        lv_obj_set_style_pad_right(_hint_area, 16, LV_PART_MAIN);
        lv_obj_set_style_pad_top(_hint_area, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(_hint_area, 10, LV_PART_MAIN);
        lv_obj_clear_flag(_hint_area, LV_OBJ_FLAG_SCROLLABLE);

        // Inner row: icon + text as one unit, centered in the band / связка по центру полосы, не растянута на ширину.
        lv_obj_t* hint_row = lv_obj_create(_hint_area);
        if (hint_row) {
            lv_obj_set_width(hint_row, LV_SIZE_CONTENT);
            lv_obj_set_height(hint_row, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(hint_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(hint_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(hint_row, 10, LV_PART_MAIN);
            style_transparent(hint_row);

            _lbl_hint_icon = lv_label_create(hint_row);
            if (_lbl_hint_icon) {
                lv_label_set_text(_lbl_hint_icon, station_glyph_utf8_hand_click());
                station_set_font(_lbl_hint_icon, reinterpret_cast<const void*>(&lv_font_yora_station_icons_20));
                lv_obj_set_style_text_color(_lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
                lv_obj_set_style_text_align(_lbl_hint_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            }

            _lbl_hint_text = lv_label_create(hint_row);
            if (_lbl_hint_text) {
                {
                    // One buffer in create() — kMetaFieldSepUtf8 matches scr_main k_meta_field_sep byte-for-byte.
                    char hint_buf[80];
                    snprintf(
                        hint_buf,
                        sizeof(hint_buf),
                        "Swipe up/down to scroll%sTap a station to play",
                        kMetaFieldSepUtf8);
                    lv_label_set_text(_lbl_hint_text, hint_buf);
                }
                station_set_font(_lbl_hint_text, reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr));
                lv_obj_set_style_text_color(_lbl_hint_text, pal.text_secondary, LV_PART_MAIN);
                lv_obj_set_style_text_align(_lbl_hint_text, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                // Single line: clip if band too narrow / одна строка, без переноса.
                lv_label_set_long_mode(_lbl_hint_text, LV_LABEL_LONG_CLIP);
                const lv_coord_t max_w = static_cast<lv_coord_t>(
                    LV_ACTIVE_PROFILE.width - 2u * static_cast<uint32_t>(LV_ACTIVE_PROFILE.frame_padding) - 32 - 20 - 40);
                if (max_w > 80) {
                    lv_obj_set_width(_lbl_hint_text, max_w);
                }
            }
        }
    }

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglStationPage::enter() {
    _refreshOnPageActivate();
    _scrollListToCurrentOnEnter();
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
}

void LvglStationPage::update() {
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
}

void LvglStationPage::_refreshOnPageActivate() {
    if (!_list_area) return;
    station_list_adapter::StationListSignature now{};
    if (!station_list_adapter::list_signature(&now)) {
        refreshCurrentStationVisuals();
        return;
    }
    if (_list_sig_cache_valid && station_list_adapter::list_signature_equal(now, _list_sig_cache)) {
        refreshCurrentStationVisuals();
        return;
    }
    _populateStationList();
}

void LvglStationPage::refreshCurrentStationVisuals() {
    if (!_screen) return;
    const uint16_t total = station_list_adapter::station_count();
    const uint16_t current = station_list_adapter::current_station_num();
    _updateCountLabel(current, total);
    if (!_list_area || !_lbl_list) return;

    // Only playback "current" marker + header refresh here — focus overlays stay UI-local.
    // Обновление только маркера «текущая» из адаптера; фокус не принудительно к current.
    if (!station_list_adapter::is_valid_station_num(current)) {
        _destroyCurrentMarkerOverlay();
        return;
    }

    const lv_coord_t y_marker = row_y_for_station_row(current);
    const lv_coord_t mx = marker_left_x_in_list();
    if (_current_marker) {
        lv_obj_set_pos(_current_marker, mx, y_marker + kMarkerY);
        return;
    }
    _layoutMarkerForCurrentStation(current);
}

void LvglStationPage::_updateCountLabel(uint16_t current, uint16_t total) {
    if (!_lbl_count) return;
    _station_total = total;
    char count_buf[24];
    if (total == 0u) {
        lv_label_set_text(_lbl_count, "-- / --");
        return;
    }
    if (station_list_adapter::is_valid_station_num(current)) {
        snprintf(count_buf, sizeof(count_buf), "%u / %u", static_cast<unsigned>(current), static_cast<unsigned>(total));
    } else {
        snprintf(count_buf, sizeof(count_buf), "-- / %u", static_cast<unsigned>(total));
    }
    lv_label_set_text(_lbl_count, count_buf);
}

void LvglStationPage::_cacheListSignature() {
    (void)station_list_adapter::list_signature(&_list_sig_cache);
    _list_sig_cache_valid = true;
}

void LvglStationPage::_destroyFocusRowOverlays() {
    if (_focus_row_bg) {
        lv_obj_del(_focus_row_bg);
        _focus_row_bg = nullptr;
    }
    if (_focus_row_accent) {
        lv_obj_del(_focus_row_accent);
        _focus_row_accent = nullptr;
    }
}

void LvglStationPage::_destroyCurrentMarkerOverlay() {
    if (_current_marker) {
        lv_obj_del(_current_marker);
        _current_marker = nullptr;
    }
}

void LvglStationPage::_destroyStationOverlays() {
    _destroyFocusRowOverlays();
    _destroyCurrentMarkerOverlay();
}

void LvglStationPage::_clampFocusForTotal(uint16_t tot) {
    if (tot == 0u) {
        _focus_station_num = 0;
        return;
    }
    if (_focus_station_num >= 1u && _focus_station_num <= tot && station_list_adapter::is_valid_station_num(_focus_station_num)) {
        return;
    }
    const uint16_t cur = station_list_adapter::current_station_num();
    if (station_list_adapter::is_valid_station_num(cur) && cur <= tot) {
        _focus_station_num = cur;
    } else {
        _focus_station_num = 1u;
    }
}

void LvglStationPage::_layoutFocusChrome(uint16_t focus_station) {
    if (!_list_area || !_lbl_list) return;
    if (!station_list_adapter::is_valid_station_num(focus_station)) {
        _destroyFocusRowOverlays();
        return;
    }

    const uint16_t tot = station_list_adapter::station_count();
    if (tot == 0u || focus_station > tot) {
        _destroyFocusRowOverlays();
        return;
    }

    const YoRadioPalette& pal = yoradio_palette();
    const lv_coord_t y = row_y_for_station_row(focus_station);

    auto style_focus_bg = [&](lv_obj_t* o) {
        if (!o) return;
        lv_obj_set_width(o, static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width - (LV_ACTIVE_PROFILE.frame_padding * 2u)));
        lv_obj_set_height(o, kRowOverlayHeight);
        lv_obj_set_style_bg_color(o, pal.list_row_selected_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(o, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(o, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    };
    auto style_focus_accent = [&](lv_obj_t* o) {
        if (!o) return;
        lv_obj_set_width(o, kFocusAccentStripW);
        lv_obj_set_height(o, kRowAccentH);
        lv_obj_set_style_bg_color(o, pal.accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(o, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    };

    if (!_focus_row_bg) {
        _focus_row_bg = lv_obj_create(_list_area);
        style_focus_bg(_focus_row_bg);
    }
    if (!_focus_row_bg) return;
    lv_obj_set_pos(_focus_row_bg, -kListPadLeft, y - kRowOverlayTopPad);
    style_focus_bg(_focus_row_bg); // palette may change preset / пресет мог смениться
    lv_obj_move_background(_focus_row_bg);

    if (!_focus_row_accent) {
        _focus_row_accent = lv_obj_create(_list_area);
        style_focus_accent(_focus_row_accent);
    }
    if (!_focus_row_accent) return;
    lv_obj_set_pos(_focus_row_accent, -kListPadLeft + 1, y + kRowAccentY);
    style_focus_accent(_focus_row_accent);

    lv_obj_move_foreground(_lbl_list);
    if (_current_marker) {
        lv_obj_move_foreground(_current_marker);
    }
}

void LvglStationPage::_layoutMarkerForCurrentStation(uint16_t current_station) {
    if (!_list_area || !_lbl_list || !station_list_adapter::is_valid_station_num(current_station)) {
        _destroyCurrentMarkerOverlay();
        return;
    }

    const YoRadioPalette& pal = yoradio_palette();
    const lv_coord_t y = row_y_for_station_row(current_station);
    const lv_coord_t mx = marker_left_x_in_list();

    if (!_current_marker) {
        _current_marker = lv_label_create(_list_area);
        if (!_current_marker) return;
        lv_obj_set_width(_current_marker, kMarkerBoxW);
        lv_label_set_long_mode(_current_marker, LV_LABEL_LONG_CLIP);
        lv_label_set_text(_current_marker, station_glyph_utf8_volume_2());
        station_set_font(_current_marker, reinterpret_cast<const void*>(&lv_font_yora_station_icons_22));
        lv_obj_set_style_text_align(_current_marker, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_clear_flag(_current_marker, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(_current_marker, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_style_text_color(_current_marker, pal.accent, LV_PART_MAIN);
    lv_obj_set_pos(_current_marker, mx, y + kMarkerY);

    lv_obj_move_foreground(_lbl_list);
    lv_obj_move_foreground(_current_marker);
}

void LvglStationPage::_buildStationOverlaysAfterList(uint16_t current_station_num) {
    _clampFocusForTotal(station_list_adapter::station_count());
    const uint16_t tot = station_list_adapter::station_count();
    if (tot > 0u && station_list_adapter::is_valid_station_num(_focus_station_num)) {
        _layoutFocusChrome(_focus_station_num);
    } else {
        _destroyFocusRowOverlays();
    }
    _layoutMarkerForCurrentStation(current_station_num);
}

void LvglStationPage::_scrollListToCurrentOnEnter() {
    if (!_list_area || !_lbl_list) {
        return;
    }
    const uint16_t total = station_list_adapter::station_count();
    if (total == 0u) {
        return;
    }
    // enter() runs before lv_scr_load_anim — flex heights may be stale; sync layout before view_h.
    // enter() до загрузки экрана — без update_layout высота списка может быть 0.
    if (_screen) {
        lv_obj_update_layout(_screen);
    }
    const uint16_t current = station_list_adapter::current_station_num();
    if (!station_list_adapter::is_valid_station_num(current) || current > total) {
        lv_obj_scroll_to_y(_list_area, 0, LV_ANIM_OFF);
        return;
    }
    const lv_coord_t row_y = row_y_for_station_row(current);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(_list_area, LV_PART_MAIN);
    const lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(_list_area, LV_PART_MAIN);
    const lv_coord_t h = lv_obj_get_height(_list_area);
    const lv_coord_t view_h = h - pad_top - pad_bottom;
    if (view_h <= 0) {
        return;
    }
    // Center current row in viewport (upper-middle bias vs pure center) — matches spec formula.
    // Текущая строка ближе к центру видимой области; LVGL clampит к допустимому scroll_y.
    lv_coord_t target = row_y - (view_h - kStationLinePitch) / 2;
    if (target < 0) {
        target = 0;
    }
    lv_obj_scroll_to_y(_list_area, target, LV_ANIM_OFF);
}

void LvglStationPage::_setFocusStation(uint16_t num) {
    const uint16_t tot = station_list_adapter::station_count();
    if (tot == 0u || !station_list_adapter::is_valid_station_num(num) || num > tot) return;
    _focus_station_num = num;
    _layoutFocusChrome(num);
}

bool LvglStationPage::_candidateStationFromScreenPoint(lv_coord_t screen_px, lv_coord_t screen_py, uint16_t* out_station) {
    if (!_list_area || !_lbl_list || !out_station) {
        return false;
    }

    lv_area_t list_coords{};
    lv_obj_get_coords(_list_area, &list_coords);

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(_list_area);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(_list_area, LV_PART_MAIN);
    const lv_coord_t local_touch_y =
        static_cast<lv_coord_t>(static_cast<int32_t>(screen_py) - static_cast<int32_t>(list_coords.y1));

    const int32_t y_doc =
        static_cast<int32_t>(scroll_y) + static_cast<int32_t>(local_touch_y) - static_cast<int32_t>(pad_top);
    if (y_doc < 0) {
        return false;
    }

    const uint32_t pitch_u = static_cast<uint32_t>(kStationLinePitch);
    if (pitch_u == 0u) {
        return false;
    }

    const uint32_t y_doc_u = static_cast<uint32_t>(y_doc);
    const uint32_t row_idx = y_doc_u / pitch_u;
    if (row_idx > static_cast<uint32_t>(UINT16_MAX) - 10u) {
        return false;
    }

    const uint16_t station_one_based = static_cast<uint16_t>(row_idx + 1u);
    const uint16_t tot = station_list_adapter::station_count();
    if (!station_list_adapter::is_valid_station_num(station_one_based) || station_one_based > tot) {
        return false;
    }
    *out_station = station_one_based;
    return true;
}

void LvglStationPage::_populateStationList() {
    if (!_list_area) return;

    lv_obj_clean(_list_area);
    _lbl_list = nullptr;
    _destroyStationOverlays();

    const uint16_t current = station_list_adapter::current_station_num();
    const uint16_t tot = station_list_adapter::station_count();
    _updateCountLabel(current, tot);

    if (!_ensureListTextBuffer(tot)) {
        _list_sig_cache_valid = false;
        _lbl_list = lv_label_create(_list_area);
        if (!_lbl_list) return;
        lv_label_set_text(_lbl_list, "Station list buffer allocation failed");
        station_set_font(_lbl_list, reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr));
        lv_obj_set_style_text_color(_lbl_list, yoradio_palette().list_row_text, LV_PART_MAIN);
        lv_obj_set_style_pad_left(_lbl_list, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);
        _registerListPointerHandlersOnLabel();
        return;
    }

    const size_t name_limit = (LV_ACTIVE_PROFILE.width >= 480u) ? 64u : 42u;
    if (!station_list_adapter::station_list_text(_list_text, _list_text_cap, name_limit)) {
        strlcpy(_list_text, "Station list read failed\n", _list_text_cap);
    }

    _lbl_list = lv_label_create(_list_area);
    if (!_lbl_list) return;
    lv_label_set_long_mode(_lbl_list, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(_lbl_list, _list_text);
    station_set_font(_lbl_list, reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr));
    lv_obj_set_style_text_color(_lbl_list, yoradio_palette().list_row_text, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(_lbl_list, kStationListLineSpace, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_lbl_list, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);

    _buildStationOverlaysAfterList(current);
    _registerListPointerHandlersOnLabel();
    _cacheListSignature();
}

void LvglStationPage::_registerListPointerHandlersOnLabel() {
    if (!_lbl_list) {
        return;
    }
    lv_obj_clear_flag(_lbl_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_lbl_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_lbl_list, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

void LvglStationPage::_listAreaPressedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    self->_onListAreaPressed(e);
}

void LvglStationPage::_onListAreaPressed(lv_event_t* e) {
    if (!_list_area || !e) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    if (lv_event_get_target(e) != _list_area) {
        return;
    }
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) {
        lv_indev_get_point(indev, &_list_press_pt);
    } else {
        _list_press_pt.x = 0;
        _list_press_pt.y = 0;
    }
    _list_press_scroll_y = lv_obj_get_scroll_y(_list_area);
    _list_stroke_max_manhattan = 0;
    _list_stroke_max_scroll_y_abs = 0;
    // Finger down while list still coasting — treat next clean click as stop, not focus / касание во время инерции.
    if (lv_obj_is_scrolling(_list_area)) {
        _list_arm_suppress_next_focus = true;
    }
}

void LvglStationPage::_listAreaReleasedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    self->_onListAreaReleased(e);
}

void LvglStationPage::_onListAreaReleased(lv_event_t* e) {
    if (!_list_area || !e) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    if (lv_event_get_target(e) != _list_area) {
        return;
    }
    const bool scroll_like = (_list_stroke_max_manhattan >= static_cast<int32_t>(kListTapMaxFingerTravelPx)) ||
                             (_list_stroke_max_scroll_y_abs >= static_cast<int32_t>(kListTapMaxScrollYDeltaPx));
    if (scroll_like) {
        _list_arm_suppress_next_focus = true;
    }
}

void LvglStationPage::_listAreaPressingEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    self->_onListAreaPressing(e);
}

void LvglStationPage::_onListAreaPressing(lv_event_t* e) {
    if (!_list_area || !e) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_PRESSING) {
        return;
    }
    if (lv_event_get_target(e) != _list_area) {
        return;
    }
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t cur{};
    lv_indev_get_point(indev, &cur);
    const int32_t dx = static_cast<int32_t>(cur.x) - static_cast<int32_t>(_list_press_pt.x);
    const int32_t dy = static_cast<int32_t>(cur.y) - static_cast<int32_t>(_list_press_pt.y);
    const int32_t manh = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (manh > _list_stroke_max_manhattan) {
        _list_stroke_max_manhattan = manh;
    }
    const lv_coord_t sy = lv_obj_get_scroll_y(_list_area);
    int32_t ds = static_cast<int32_t>(sy) - static_cast<int32_t>(_list_press_scroll_y);
    if (ds < 0) {
        ds = -ds;
    }
    if (ds > _list_stroke_max_scroll_y_abs) {
        _list_stroke_max_scroll_y_abs = ds;
    }
}

void LvglStationPage::_listAreaShortClickedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    self->_onListAreaShortClicked(e);
}

void LvglStationPage::_onListAreaShortClicked(lv_event_t* e) {
    if (!_list_area || !_lbl_list || !e) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) {
        return;
    }
    if (lv_event_get_target(e) != _list_area) {
        return;
    }
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p{};
    lv_dir_t gd = LV_DIR_NONE;
    if (indev) {
        gd = lv_indev_get_gesture_dir(indev);
        lv_indev_get_point(indev, &p);
    }
    // Momentum / throw: LVGL may still synthesize rare edge cases — skip while scroll anim active.
    // Инерция: если список ещё «едет», не двигаем фокус (минимальная страховка без F2–F5).
    if (lv_obj_is_scrolling(_list_area)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip scrolling gd=%d p=(%d,%d)\n", static_cast<int>(gd),
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    if (!indev) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.println("[station] SHORT_CLICKED skip no_indev");
#endif
        return;
    }
    // Bitmask: LVGL uses lv_dir_t as flags (LV_DIR_HOR = L|R, LV_DIR_VER = T|B).
    if ((gd & LV_DIR_HOR) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip horiz_gesture gd=%u p=(%d,%d)\n", static_cast<unsigned>(gd),
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    if ((gd & LV_DIR_VER) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip vert_gesture gd=%u p=(%d,%d)\n", static_cast<unsigned>(gd),
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    const lv_dir_t scroll_lr = lv_indev_get_scroll_dir(indev);
    if ((scroll_lr & LV_DIR_VER) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip indev_scroll_ver sd=%u\n", static_cast<unsigned>(scroll_lr));
#endif
        return;
    }

    const lv_coord_t scroll_y_now = lv_obj_get_scroll_y(_list_area);
    const int32_t d_scroll_end =
        static_cast<int32_t>(scroll_y_now) - static_cast<int32_t>(_list_press_scroll_y);
    const int32_t ad_scroll_end = d_scroll_end < 0 ? -d_scroll_end : d_scroll_end;
    const int32_t ad_scroll =
        ad_scroll_end > _list_stroke_max_scroll_y_abs ? ad_scroll_end : _list_stroke_max_scroll_y_abs;
    if (ad_scroll >= static_cast<int32_t>(kListTapMaxScrollYDeltaPx)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip scroll_ydelta end=%d peak=%d\n", static_cast<int>(ad_scroll_end),
                      static_cast<int>(_list_stroke_max_scroll_y_abs));
#endif
        return;
    }
    const int32_t dx = static_cast<int32_t>(p.x) - static_cast<int32_t>(_list_press_pt.x);
    const int32_t dy = static_cast<int32_t>(p.y) - static_cast<int32_t>(_list_press_pt.y);
    const int32_t travel_end = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    const int32_t travel =
        travel_end > _list_stroke_max_manhattan ? travel_end : _list_stroke_max_manhattan;
    if (travel >= static_cast<int32_t>(kListTapMaxFingerTravelPx)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip finger_travel end=%d peak=%d p=(%d,%d)\n",
                      static_cast<int>(travel_end), static_cast<int>(_list_stroke_max_manhattan),
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }

    lv_area_t ar{};
    lv_obj_get_coords(_list_area, &ar);
    if (static_cast<int32_t>(p.x) > static_cast<int32_t>(ar.x2) - kScrollbarRightIgnorePx) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip scrollbar p.x=%d\n", static_cast<int>(p.x));
#endif
        return;
    }

    uint16_t station_num = 0;
    if (!_candidateStationFromScreenPoint(p.x, p.y, &station_num)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip no_row p=(%d,%d)\n", static_cast<int>(p.x),
                      static_cast<int>(p.y));
#endif
        return;
    }
    if (_list_arm_suppress_next_focus) {
        _list_arm_suppress_next_focus = false;
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED arm consumed (no focus) st=%u p=(%d,%d)\n",
                      static_cast<unsigned>(station_num), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
#if YORADIO_LVGL_TOUCH_DEBUG
    Serial.printf("[station] SHORT_CLICKED focus st=%u p=(%d,%d)\n", static_cast<unsigned>(station_num),
                  static_cast<int>(p.x), static_cast<int>(p.y));
#endif
    _setFocusStation(station_num);
    // Stage 6.3G: same path as focus — scroll/arm/horizontal guards already exited above / только чистый тап.
    station_list_adapter::play_station(station_num);
}

bool LvglStationPage::_ensureListTextBuffer(uint16_t total) {
    const size_t needed = static_cast<size_t>(total > 0u ? total : 1u) * kStationLineBytes + kStationListExtraBytes;
    if (_list_text && _list_text_cap >= needed) return true;

    _releaseListTextBuffer();
    _list_text = static_cast<char*>(ps_malloc(needed));
    if (!_list_text) {
        _list_text = static_cast<char*>(malloc(needed));
    }
    if (!_list_text) {
        _list_text_cap = 0;
        return false;
    }
    _list_text_cap = needed;
    _list_text[0] = '\0';
    return true;
}

void LvglStationPage::_releaseListTextBuffer() {
    if (_list_text) {
        free(_list_text);
        _list_text = nullptr;
    }
    _list_text_cap = 0;
}

void LvglStationPage::exit() {}

void LvglStationPage::liveReapplyTheme() {
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    wgt_status_line::reapplyTheme(_status_line);

    if (_lbl_title) lv_obj_set_style_text_color(_lbl_title, pal.text_primary, LV_PART_MAIN);
    if (_lbl_count) lv_obj_set_style_text_color(_lbl_count, pal.text_secondary, LV_PART_MAIN);
    if (_lbl_list) lv_obj_set_style_text_color(_lbl_list, pal.list_row_text, LV_PART_MAIN);

    if (_hint_area) {
        lv_obj_set_style_bg_color(_hint_area, pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_border_color(_hint_area, pal.divider, LV_PART_MAIN);
    }
    if (_lbl_hint_icon) lv_obj_set_style_text_color(_lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
    if (_lbl_hint_text) lv_obj_set_style_text_color(_lbl_hint_text, pal.text_secondary, LV_PART_MAIN);

    station_reapply_dividers(_screen, pal);

    // Focus/marker chrome only — positions unchanged; no list rebuild / scroll / playlist.
    // Только цвета оверлеев; позиции и scroll не трогаем.
    if (_focus_row_bg && station_list_adapter::is_valid_station_num(_focus_station_num)) {
        _layoutFocusChrome(_focus_station_num);
    }
    if (_current_marker) {
        lv_obj_set_style_text_color(_current_marker, pal.accent, LV_PART_MAIN);
    }

    lv_obj_invalidate(_screen);
}

void LvglStationPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _status_line = {};
    _lbl_title = nullptr;
    _lbl_count = nullptr;
    _lbl_hint_icon = nullptr;
    _lbl_hint_text = nullptr;
    _list_area = nullptr;
    _lbl_list = nullptr;
    _focus_row_bg = nullptr;
    _focus_row_accent = nullptr;
    _current_marker = nullptr;
    _hint_area = nullptr;
    _station_total = 0;
    _focus_station_num = 0;
    _list_sig_cache_valid = false;
    _list_arm_suppress_next_focus = false;
    _releaseListTextBuffer();
}

lv_obj_t* LvglStationPage::screen() {
    return _screen;
}

} // namespace lvgl_ui

