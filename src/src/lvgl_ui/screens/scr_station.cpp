/*
 * LvglStationPage — Station list: scrollable single-label list with tap-to-focus and tap-to-play.
 * Страница Station: прокручиваемый список, tap для фокуса, tap для воспроизведения.
 *
 * DspTask-only lv_*; one label scrolls natively; text buffer lives outside LVGL heap (PSRAM/heap).
 * Только DspTask для lv_*; один label скроллится native; буфер текста вне LVGL heap (PSRAM/heap).
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
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

// ─────────────────────────────────────────────────────────────────────────────
// UI string constants (l10n readiness) / Строки UI
// All user-visible static strings are gathered here for future localization.
// Все видимые строки собраны здесь для будущей локализации.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr char kStrStationsTitle[]          = "STATIONS";
static constexpr char kStrCountPlaceholder[]        = "-- / --";
static constexpr char kStrHintSwipe[]               = "Swipe up/down to scroll";
static constexpr char kStrHintTap[]                 = "Tap a station to play";
static constexpr char kStrListBufferAllocFailed[]   = "Station list buffer allocation failed";
static constexpr char kStrListReadFailed[]          = "Station list read failed\n";

// ─────────────────────────────────────────────────────────────────────────────
// UI format-string constants / Форматные строки UI
// ─────────────────────────────────────────────────────────────────────────────

// Station count: current / total / Номер текущей / всего
static constexpr char kFmtCountCurrentTotal[]  = "%u / %u";
// Station count: unknown current / Текущая неизвестна
static constexpr char kFmtCountUnknownTotal[]  = "-- / %u";
// Hint band: composed from kStrHintSwipe + kMetaFieldSepUtf8 + kStrHintTap / Строка подсказки
static constexpr char kFmtHintBand[]           = "%s%s%s";

// ─────────────────────────────────────────────────────────────────────────────
// Glyph resources / Ресурсы глифов
// UTF-8 codepoints delivered through control_glyph_utf8.h inline functions.
// UTF-8 codepoints из control_glyph_utf8.h inline-функций.
// ─────────────────────────────────────────────────────────────────────────────

// Same U+2022 • as scr_main k_meta_field_sep — rendered via lv_font_yora_montserrat_16_cyr.
// Тот же U+2022 •, что и в scr_main — рендерится через lv_font_yora_montserrat_16_cyr.
static constexpr char kMetaFieldSepUtf8[] = " \xE2\x80\xA2 ";

// Hint hand-click icon / Иконка hand-click в подсказке
static const char* const kIconHintClick      = station_glyph_utf8_hand_click();
// Current-station speaker glyph / Глиф громкости для текущей станции
static const char* const kIconCurrentStation = station_glyph_utf8_volume_2();

// ─────────────────────────────────────────────────────────────────────────────
// Font resources / Шрифты экрана
// ─────────────────────────────────────────────────────────────────────────────

// Page title "STATIONS" / Шрифт заголовка
static const void* const kFontTitle        = reinterpret_cast<const void*>(&lv_font_yora_montserrat_20_cyr);
// Station count label / Шрифт счётчика
static const void* const kFontCount        = reinterpret_cast<const void*>(&lv_font_yora_montserrat_18_cyr);
// Station list body / Шрифт списка станций
static const void* const kFontStationList  = reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr);
// Hint band text / Шрифт текста подсказки
static const void* const kFontHintText     = reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr);
// Hint band icon / Шрифт иконки подсказки
static const void* const kFontHintIcon     = reinterpret_cast<const void*>(&lv_font_yora_station_icons_20);
// Current-station speaker marker / Шрифт маркера текущей станции
static const void* const kFontCurrentMarker = reinterpret_cast<const void*>(&lv_font_yora_station_icons_22);

// ─────────────────────────────────────────────────────────────────────────────
// Buffer and text-layout constants / Константы буфера и текстовой раскладки
// ─────────────────────────────────────────────────────────────────────────────

constexpr size_t kStationLineBytes      = 112;  // Max UTF-8 bytes per station line in list buffer / лимит байт на строку
constexpr size_t kStationListExtraBytes = 96;   // Extra allocation headroom / запас выделения
// Formatting scratch buffers / Вспомогательные буферы форматирования
constexpr size_t kCountBufferSize  = 24;
constexpr size_t kHintBufferSize   = 80;
// Station-name character limits by profile width / Лимиты имени по ширине профиля
constexpr uint32_t kWideProfileMinWidth      = 480u;
constexpr size_t   kStationNameLimitWide     = 64u;
constexpr size_t   kStationNameLimitCompact  = 42u;

// ─────────────────────────────────────────────────────────────────────────────
// Visual and layout constants / Визуальные и геометрические константы
// ─────────────────────────────────────────────────────────────────────────────

// Root flex row gap / Зазор flex-строк корня
static constexpr lv_coord_t kRootRowGap = 8;

// List font: M22 (line_height 25) + line_space — kStationLinePitch must stay in sync with _lbl_list.
// Шрифт списка M22; шаг строки = высота (25) + kStationListLineSpace.
constexpr lv_coord_t kStationListFontLineHeight = 25; // M22 cap-height used for overlay math / высота строки шрифта
constexpr lv_coord_t kStationListLineSpace      = 16; // LVGL line_space between rows / межстрочный зазор
constexpr lv_coord_t kStationLinePitch = kStationListFontLineHeight + kStationListLineSpace; // Row step for overlay Y / шаг строки для Y оверлеев

// Overlays align to the *text* band (first kStationListFontLineHeight px), not the line_space gap below.
// Оверлеи и маркер — по визуальной строке текста, не по полному шагу (line_space после текста — пустой).
constexpr lv_coord_t kRowOverlayTopPad  = 8;  // Highlight extends above/below text / паддинг подсветки
constexpr lv_coord_t kRowOverlayHeight  = kStationListFontLineHeight + 2 * kRowOverlayTopPad; // Total band height / высота полосы
constexpr lv_coord_t kRowAccentY        = -2; // Vertical offset of left accent vs overlay / смещение левой полоски
constexpr lv_coord_t kRowAccentH        = 29; // Height of left accent strip / высота вертикальной полоски
constexpr lv_coord_t kMarkerIconLineHeight = 17; // Speaker glyph cap-height for centering / высота глифа для центрирования
constexpr lv_coord_t kMarkerY = (kStationListFontLineHeight - kMarkerIconLineHeight) / 2; // Center in text band / центр в строке текста
// Focus overlay styling / Стили оверлея фокуса
static constexpr lv_coord_t kFocusBgRadius      = 8;
static constexpr lv_coord_t kFocusAccentRadius   = 2;
static constexpr lv_opa_t   kFocusBgOpa          = LV_OPA_70;
// Hint band styling / Стили band-подсказки
static constexpr lv_coord_t kHintBorderWidth     = 1;
static constexpr lv_coord_t kHintRadius          = 14;
static constexpr lv_coord_t kHintPadHorizontal   = 16;
static constexpr lv_coord_t kHintPadVertical     = 10;
static constexpr lv_coord_t kHintRowGap          = 10;
static constexpr lv_coord_t kHintMinTextWidth    = 80;
static constexpr lv_opa_t   kHintBgOpa           = LV_OPA_30;

// Left gutter layout: accent | gap | marker slot | gap | list text (pad on _lbl_list).
// Левый gutter: accent | зазор | слот маркера | зазор | текст списка.
constexpr lv_coord_t kListPadLeft            = 16; // List area inner left padding / левый padding контейнера списка
constexpr lv_coord_t kListPadTop             = 8;  // List area inner top/bottom padding / верхний padding (совпадает с нижним)
constexpr lv_coord_t kListPadBottom          = 8;  // List area inner bottom padding / нижний padding
constexpr lv_coord_t kFocusAccentStripW      = 3;  // Focus accent strip width at row left / ширина полоски фокуса
constexpr lv_coord_t kMarkerGutterAfterAccent = 5; // Gap accent → speaker glyph / зазор акцент → иконка
constexpr lv_coord_t kMarkerBoxW             = 32; // Reserved slot width for glyph / ширина слота под маркер
constexpr lv_coord_t kMarkerToNameGap        = 5;  // Gap after marker before list text / зазор после слота → текст

// ─────────────────────────────────────────────────────────────────────────────
// Input constants / Константы ввода
// ─────────────────────────────────────────────────────────────────────────────

// Ignore taps in right strip where the vertical scrollbar sits.
// Игнорировать тапы в правой полосе, где находится scrollbar.
constexpr int32_t kScrollbarRightIgnorePx = 26;
// Tap vs scroll discrimination: if peak scroll delta or finger travel exceeds these limits,
// the gesture is treated as scroll, not tap. Peaks are accumulated during PRESSING.
// Различие тапа и прокрутки: пики scroll_y и пальца накапливаются по PRESSING.
constexpr lv_coord_t kListTapMaxScrollYDeltaPx  = 14;
constexpr lv_coord_t kListTapMaxFingerTravelPx  = 24;
// Overflow guard for row index calculation / Защита от overflow в расчёте строки
static constexpr uint32_t kStationRowSafetyLimit = UINT16_MAX - 10u;

// ─────────────────────────────────────────────────────────────────────────────
// Geometry helpers / Вспомогательные функции геометрии
// ─────────────────────────────────────────────────────────────────────────────

static lv_coord_t marker_left_x_in_list() {
    return -kListPadLeft + 1 + kFocusAccentStripW + kMarkerGutterAfterAccent;
}

// Indent list label so long names never run under the left gutter marker.
// Отступ текста лейбла, чтобы длинные имена не уходили под маркер.
static lv_coord_t list_label_pad_left_for_marker_gutter() {
    const int32_t mleft = static_cast<int32_t>(marker_left_x_in_list());
    const int32_t mright = mleft + static_cast<int32_t>(kMarkerBoxW);
    const int32_t pad = mright + static_cast<int32_t>(kMarkerToNameGap);
    return static_cast<lv_coord_t>(pad > 0 ? pad : 0);
}

static lv_coord_t row_y_for_station_row(uint16_t station_one_based) {
    return static_cast<lv_coord_t>(
        (static_cast<uint32_t>(station_one_based) - 1u) * static_cast<uint32_t>(kStationLinePitch));
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic LVGL helpers / Вспомогательные функции LVGL
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Layout factories / Фабрики разметки
// ─────────────────────────────────────────────────────────────────────────────

// 1 px divider — matches status divider geometry and theme token.
// 1 px разделитель — геометрия как у status divider.
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

// ─────────────────────────────────────────────────────────────────────────────
// Theme helpers / Вспомогательные функции темы
// ─────────────────────────────────────────────────────────────────────────────

// Recursive walker: recolors only 1 px dividers (h==1 + OPA_COVER) without touching other objects.
// Рекурсивный обход: перекрашивает только 1 px разделители без затрагивания других объектов.
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

// ─────────────────────────────────────────────────────────────────────────────
// Layout builders / Билдеры разметки
// ─────────────────────────────────────────────────────────────────────────────

// Status chrome: wgt_status_line + 1 px divider below it.
// Status chrome: wgt_status_line + 1 px разделитель под ним.
void LvglStationPage::create_status_chrome(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    wgt_status_line::create(self._screen, self._status_line);
    if (!self._status_line.root) return;
    add_thin_divider(self._screen, pal);
}

// Header row: STATIONS title (left) + station count (right, initially --/--).
// Строка заголовка: заголовок STATIONS (слева) + счётчик станций (справа, изначально --/--).
void LvglStationPage::create_header(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    lv_obj_t* header = lv_obj_create(self._screen);
    if (!header) return;
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_transparent(header);

    self._lbl_title = lv_label_create(header);
    if (self._lbl_title) {
        lv_label_set_text(self._lbl_title, kStrStationsTitle);
        station_set_font(self._lbl_title, kFontTitle);
        lv_obj_set_style_text_color(self._lbl_title, pal.text_primary, LV_PART_MAIN);
    }

    self._lbl_count = lv_label_create(header);
    if (self._lbl_count) {
        lv_label_set_text(self._lbl_count, kStrCountPlaceholder);
        station_set_font(self._lbl_count, kFontCount);
        lv_obj_set_style_text_color(self._lbl_count, pal.text_secondary, LV_PART_MAIN);
    }
}

// Scrollable list area with CLICKABLE + GESTURE_BUBBLE + VER scroll + 4 pointer event callbacks.
// Indev must hit the _list_area (not _lbl_list) so tap routing works when the label is non-clickable.
// Прокручиваемая область списка: CLICKABLE + GESTURE_BUBBLE + VER scroll + 4 pointer callbacks.
// Индеф должен попадать на _list_area (не на _lbl_list), чтобы tap routing работал.
void LvglStationPage::create_list_area(LvglStationPage& self, const YoRadioPalette& pal) {
    (void)pal;
    if (!self._screen) return;
    self._list_area = lv_obj_create(self._screen);
    if (!self._list_area) return;
    lv_obj_set_width(self._list_area, LV_PCT(100));
    lv_obj_set_flex_grow(self._list_area, 1);
    lv_obj_set_style_bg_opa(self._list_area, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(self._list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(self._list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(self._list_area, kListPadLeft, LV_PART_MAIN);
    lv_obj_set_style_pad_right(self._list_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(self._list_area, kListPadTop, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(self._list_area, kListPadBottom, LV_PART_MAIN);
    lv_obj_add_flag(self._list_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(self._list_area, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    // CLICKABLE required so indev hits the scroll widget — tap routes here, not the non-clickable label.
    // CLICKABLE нужен: tap идёт на _list_area, а не на некликабельный _lbl_list.
    lv_obj_add_flag(self._list_area, LV_OBJ_FLAG_CLICKABLE);
    // Bubble horizontal gestures to page root carousel (GESTURE handler on _screen).
    // Пузырить горизонтальные жесты на корень — обработчик карусели на _screen.
    lv_obj_add_flag(self._list_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(self._list_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(self._list_area, LV_SCROLLBAR_MODE_AUTO);
    // PRESSED captures scroll_y + point; SHORT_CLICKED filtered if stroke looked like scroll.
    // PRESSED фиксирует scroll_y и точку; SHORT_CLICKED отбрасываем, если жест выглядел как прокрутка.
    lv_obj_add_event_cb(self._list_area, _listAreaPressedEvt,      LV_EVENT_PRESSED,       &self);
    lv_obj_add_event_cb(self._list_area, _listAreaPressingEvt,     LV_EVENT_PRESSING,      &self);
    lv_obj_add_event_cb(self._list_area, _listAreaReleasedEvt,     LV_EVENT_RELEASED,      &self);
    lv_obj_add_event_cb(self._list_area, _listAreaShortClickedEvt, LV_EVENT_SHORT_CLICKED, &self);
}

// Hint band: quiet info strip at the bottom; not a button.
// Border uses the divider token (panel_border at low opacity was nearly invisible on dark themes).
// Band подсказки внизу; не кнопка. Рамка — divider token (panel_border на малой непрозрачности пропадал).
void LvglStationPage::create_hint_band(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    self._hint_area = lv_obj_create(self._screen);
    if (!self._hint_area) return;
    lv_obj_set_width(self._hint_area, LV_PCT(100));
    lv_obj_set_height(self._hint_area, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(self._hint_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(self._hint_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(self._hint_area, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(self._hint_area, kHintBgOpa, LV_PART_MAIN);
    lv_obj_set_style_border_color(self._hint_area, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_border_opa(self._hint_area, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(self._hint_area, kHintBorderWidth, LV_PART_MAIN);
    lv_obj_set_style_radius(self._hint_area, kHintRadius, LV_PART_MAIN);
    lv_obj_set_style_pad_left(self._hint_area, kHintPadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_right(self._hint_area, kHintPadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_top(self._hint_area, kHintPadVertical, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(self._hint_area, kHintPadVertical, LV_PART_MAIN);
    lv_obj_clear_flag(self._hint_area, LV_OBJ_FLAG_SCROLLABLE);

    // Inner row: icon + text centered as one unit in the band, not stretched to full width.
    // Внутренняя строка: иконка + текст как одна единица, выровнены по центру, не растянуты.
    lv_obj_t* hint_row = lv_obj_create(self._hint_area);
    if (!hint_row) return;
    lv_obj_set_width(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_height(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hint_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hint_row, kHintRowGap, LV_PART_MAIN);
    style_transparent(hint_row);

    self._lbl_hint_icon = lv_label_create(hint_row);
    if (self._lbl_hint_icon) {
        lv_label_set_text(self._lbl_hint_icon, kIconHintClick);
        station_set_font(self._lbl_hint_icon, kFontHintIcon);
        lv_obj_set_style_text_color(self._lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    self._lbl_hint_text = lv_label_create(hint_row);
    if (self._lbl_hint_text) {
        {
            // kMetaFieldSepUtf8 matches scr_main k_meta_field_sep byte-for-byte (U+2022).
            // kMetaFieldSepUtf8 совпадает с k_meta_field_sep из scr_main побайтово (U+2022).
            char hint_buf[kHintBufferSize];
            snprintf(hint_buf, sizeof(hint_buf), kFmtHintBand,
                     kStrHintSwipe, kMetaFieldSepUtf8, kStrHintTap);
            lv_label_set_text(self._lbl_hint_text, hint_buf);
        }
        station_set_font(self._lbl_hint_text, kFontHintText);
        lv_obj_set_style_text_color(self._lbl_hint_text, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_text, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        // Single line: clip if band too narrow / одна строка без переноса.
        lv_label_set_long_mode(self._lbl_hint_text, LV_LABEL_LONG_CLIP);
        // Reserve room for icon slot + inner gaps + outer pads.
        // Вычесть иконку + зазоры + внешние отступы.
        const lv_coord_t max_w = static_cast<lv_coord_t>(
            LV_ACTIVE_PROFILE.width
            - 2u * static_cast<uint32_t>(LV_ACTIVE_PROFILE.frame_padding)
            - 32u - 20u - 40u);
        if (max_w > kHintMinTextWidth) {
            lv_obj_set_width(self._lbl_hint_text, max_w);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Class lifecycle / Жизненный цикл класса
// ─────────────────────────────────────────────────────────────────────────────

void LvglStationPage::create() {
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

    create_header(*this, pal);
    create_list_area(*this, pal);

    if (_list_area) {
        _populateStationList();
    }

    create_hint_band(*this, pal);

    installCarouselGesturesOnPageRoot(_screen);
}

void LvglStationPage::enter() {
    // Signature check + optional list rebuild; then center scroll on current; then refresh status line.
    // Проверка сигнатуры + возможный rebuild списка; затем центрирование; затем обновление status.
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

    // Update only the current-station marker and count — focus overlays remain UI-local.
    // Focus is not forced to current on NEWSTATION; it only changes on tap or rebuild clamp.
    // Обновляем только маркер «текущая» и счётчик — фокусные оверлеи остаются UI-local.
    // NEWSTATION не принуждает фокус к current; он меняется только на tap или при clamp rebuild.
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
    char count_buf[kCountBufferSize];
    if (total == 0u) {
        lv_label_set_text(_lbl_count, kStrCountPlaceholder);
        return;
    }
    if (station_list_adapter::is_valid_station_num(current)) {
        snprintf(count_buf, sizeof(count_buf), kFmtCountCurrentTotal,
                 static_cast<unsigned>(current), static_cast<unsigned>(total));
    } else {
        snprintf(count_buf, sizeof(count_buf), kFmtCountUnknownTotal,
                 static_cast<unsigned>(total));
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
        lv_obj_set_style_bg_opa(o, kFocusBgOpa, LV_PART_MAIN);
        lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(o, kFocusBgRadius, LV_PART_MAIN);
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
        lv_obj_set_style_radius(o, kFocusAccentRadius, LV_PART_MAIN);
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
        lv_label_set_text(_current_marker, kIconCurrentStation);
        station_set_font(_current_marker, kFontCurrentMarker);
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
    // enter() runs before lv_scr_load_anim — flex heights may be stale; update layout before reading view_h.
    // enter() вызывается до загрузки экрана — без update_layout высота списка может быть 0.
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
    // Center current row in viewport. LVGL clamps to valid scroll_y range automatically.
    // Центрировать текущую строку в видимой области. LVGL clamp применяется автоматически.
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
    if (row_idx > kStationRowSafetyLimit) {
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

    // Explicitly tracked overlays are deleted before lv_obj_clean so their handles cannot become
    // dangling before the explicit lv_obj_del call inside _destroyStationOverlays().
    // lv_obj_clean then removes the remaining list children (primarily _lbl_list).
    // Оверлеи удаляем до lv_obj_clean: их handles не станут dangling до явного lv_obj_del.
    // lv_obj_clean после этого убирает оставшихся children (прежде всего _lbl_list).
    _destroyStationOverlays();
    lv_obj_clean(_list_area);
    _lbl_list = nullptr;

    const uint16_t current = station_list_adapter::current_station_num();
    const uint16_t tot = station_list_adapter::station_count();
    _updateCountLabel(current, tot);

    if (!_ensureListTextBuffer(tot)) {
        _list_sig_cache_valid = false;
        _lbl_list = lv_label_create(_list_area);
        if (!_lbl_list) return;
        lv_label_set_text(_lbl_list, kStrListBufferAllocFailed);
        station_set_font(_lbl_list, kFontStationList);
        lv_obj_set_style_text_color(_lbl_list, yoradio_palette().list_row_text, LV_PART_MAIN);
        lv_obj_set_style_pad_left(_lbl_list, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);
        _registerListPointerHandlersOnLabel();
        return;
    }

    const size_t name_limit = (LV_ACTIVE_PROFILE.width >= kWideProfileMinWidth)
                              ? kStationNameLimitWide : kStationNameLimitCompact;
    if (!station_list_adapter::station_list_text(_list_text, _list_text_cap, name_limit)) {
        strlcpy(_list_text, kStrListReadFailed, _list_text_cap);
    }

    _lbl_list = lv_label_create(_list_area);
    if (!_lbl_list) return;
    lv_label_set_long_mode(_lbl_list, LV_LABEL_LONG_CLIP);
    // _list_text is owned by LvglStationPage; LVGL stores only the pointer (no copy).
    // Buffer must stay alive while _lbl_list exists; label must be deleted before buffer release.
    // _list_text принадлежит LvglStationPage; LVGL хранит только указатель без копирования.
    // Буфер должен жить пока _lbl_list существует; label удаляется до освобождения буфера.
    lv_label_set_text_static(_lbl_list, _list_text);
    station_set_font(_lbl_list, kFontStationList);
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
    // Touch while list is still coasting: arm suppression so next clean click stops the scroll, not focuses.
    // Касание при инерции: arm suppress — следующий clean click остановит прокрутку, а не сфокусирует.
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
    // Momentum / coasting: LVGL may synthesize SHORT_CLICKED during deceleration — reject.
    // Инерция: LVGL может синтезировать SHORT_CLICKED при торможении — отбрасываем.
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
    // All guards passed: this is a clean tap — set focus and start playback.
    // Все guards пройдены: чистый тап — установить фокус и запустить воспроизведение.
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

    // Recolor focus/marker overlays only — positions, scroll and list are not touched.
    // Перекраска оверлеев фокуса/маркера; позиции, scroll и список не трогаем.
    if (_focus_row_bg && station_list_adapter::is_valid_station_num(_focus_station_num)) {
        _layoutFocusChrome(_focus_station_num);
    }
    if (_current_marker) {
        lv_obj_set_style_text_color(_current_marker, pal.accent, LV_PART_MAIN);
    }

    lv_obj_invalidate(_screen);
}

void LvglStationPage::destroy() {
    // Manual delete: drop the LVGL root tree, then null handles and free the list-text buffer.
    // Ручное удаление: удаляем дерево LVGL, затем обнуляем указатели и освобождаем буфер списка.
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglStationPage::releaseAfterAutoDelete() {
    // PageChain auto-delete: LVGL already freed the screen tree. Focus and current-station state
    // live in the adapter; scroll position is not preserved (enter() scrolls to current on recreate).
    // Must free the heap list-text buffer (not an LVGL object). Never call lv_obj_del here.
    //
    // PageChain auto-delete: LVGL уже освободил дерево. Фокус и текущая станция хранятся в адаптере;
    // позиция скролла не сохраняется (enter() центрирует при пересоздании). Освобождаем heap-буфер.
    _nullHandles();
}

void LvglStationPage::_nullHandles() {
    _screen = nullptr;  // dangling after auto_del; already nullptr on the manual destroy() path
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

