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
#include "../lv_page_chain.h"
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
// Footer hint: scroll instruction + tap-to-Main action, joined by separator.
// Подсказка footer: инструкция прокрутки + возврат на Main через разделитель.
static constexpr char kStrHintSwipe[]               = "Swipe up/down to scroll";
static constexpr char kStrHintReturnMain[]          = "Tap to return to Main";
static constexpr char kStrListBufferAllocFailed[]   = "Station list buffer allocation failed";
static constexpr char kStrListReadFailed[]          = "Station list read failed\n";

// ─────────────────────────────────────────────────────────────────────────────
// UI format-string constants / Форматные строки UI
// ─────────────────────────────────────────────────────────────────────────────

// Station count: current / total / Номер текущей / всего
static constexpr char kFmtCountCurrentTotal[]  = "%u / %u";
// Station count: unknown current / Текущая неизвестна
static constexpr char kFmtCountUnknownTotal[]  = "-- / %u";
// Hint band: kStrHintSwipe + separator + kStrHintReturnMain / Строка подсказки footer
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
// Pure integer math helpers / Целочисленные math-помощники
// Used only by input pipeline / Используются только input pipeline.
// ─────────────────────────────────────────────────────────────────────────────

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

static int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }

static int32_t manhattan_distance(const lv_point_t& from, const lv_point_t& to) {
    const int32_t dx = static_cast<int32_t>(to.x) - static_cast<int32_t>(from.x);
    const int32_t dy = static_cast<int32_t>(to.y) - static_cast<int32_t>(from.y);
    return abs_i32(dx) + abs_i32(dy);
}

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

// Footer hint button styling — Weather-style pill with clickable emphasis.
// Applied to _hint_area on create and on liveReapplyTheme().
// Does not create objects, register callbacks or change layout values.
// Стиль кнопки footer — Weather-style pill с кликабельным акцентом.
// Применяется к _hint_area при create и при liveReapplyTheme(). Не создаёт объекты и не регистрирует callbacks.
static void style_hint_button(lv_obj_t* obj, const YoRadioPalette& pal) {
    if (!obj) return;
    // Normal state / Нормальное состояние
    lv_obj_set_style_bg_color(obj, pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, pal.divider, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, kHintBorderWidth + 1, LV_PART_MAIN); // 2 px
    lv_obj_set_style_radius(obj, kHintRadius, LV_PART_MAIN);
    // Pressed state / Нажатое состояние
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, pal.text_meta, LV_STATE_PRESSED);
}

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

// Hint band: clickable action strip — tap returns to Main.
// Uses Weather-style pill button (LV_OPA_40 fill, 2 px border, pressed token).
// Band: кликабельная полоса — тап возвращает на Main.
// Weather-style pill: LV_OPA_40 заливка, 2 px рамка, pressed токен.
void LvglStationPage::create_hint_band(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    self._hint_area = lv_obj_create(self._screen);
    if (!self._hint_area) return;
    lv_obj_set_width(self._hint_area, LV_PCT(100));
    lv_obj_set_height(self._hint_area, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(self._hint_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(self._hint_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(self._hint_area, kHintPadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_right(self._hint_area, kHintPadHorizontal, LV_PART_MAIN);
    lv_obj_set_style_pad_top(self._hint_area, kHintPadVertical, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(self._hint_area, kHintPadVertical, LV_PART_MAIN);
    style_hint_button(self._hint_area, pal);
    // Clickable action surface: _hint_area is the tap target.
    // GESTURE_BUBBLE propagates horizontal swipes to the PageChain carousel handler on _screen.
    // Кликабельная поверхность: _hint_area — таргет тапа.
    // GESTURE_BUBBLE пробрасывает горизонтальные swipes на обработчик карусели на _screen.
    lv_obj_add_flag(self._hint_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(self._hint_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(self._hint_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(self._hint_area, _hintAreaClickedEvt, LV_EVENT_CLICKED, &self);

    // Inner row: icon + text centered; row and labels are non-clickable so _hint_area is the tap surface.
    // Внутренняя строка: иконка + текст по центру; row и labels некликабельны — _hint_area — таргет.
    lv_obj_t* hint_row = lv_obj_create(self._hint_area);
    if (!hint_row) return;
    lv_obj_set_width(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_height(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hint_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hint_row, kHintRowGap, LV_PART_MAIN);
    style_transparent(hint_row);
    lv_obj_clear_flag(hint_row, LV_OBJ_FLAG_CLICKABLE);

    self._lbl_hint_icon = lv_label_create(hint_row);
    if (self._lbl_hint_icon) {
        lv_label_set_text(self._lbl_hint_icon, kIconHintClick);
        station_set_font(self._lbl_hint_icon, kFontHintIcon);
        lv_obj_set_style_text_color(self._lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_clear_flag(self._lbl_hint_icon, LV_OBJ_FLAG_CLICKABLE);
    }

    self._lbl_hint_text = lv_label_create(hint_row);
    if (self._lbl_hint_text) {
        {
            // kMetaFieldSepUtf8 matches scr_main k_meta_field_sep byte-for-byte (U+2022).
            // kMetaFieldSepUtf8 совпадает с k_meta_field_sep из scr_main побайтово (U+2022).
            char hint_buf[kHintBufferSize];
            snprintf(hint_buf, sizeof(hint_buf), kFmtHintBand,
                     kStrHintSwipe, kMetaFieldSepUtf8, kStrHintReturnMain);
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
            - 32u - 20u - 30u);
        if (max_w > kHintMinTextWidth) {
            lv_obj_set_width(self._lbl_hint_text, max_w);
        }
        lv_obj_clear_flag(self._lbl_hint_text, LV_OBJ_FLAG_CLICKABLE);
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

// ─────────────────────────────────────────────────────────────────────────────
// Runtime refresh and signature pipeline / Конвейер обновления и сигнатуры
// ─────────────────────────────────────────────────────────────────────────────

void LvglStationPage::_refreshOnPageActivate() {
    if (!_list_area) return;
    station_list_adapter::StationListSignature now{};
    if (!station_list_adapter::list_signature(&now)) {
        // Signature read failure: skip rebuild, only refresh marker/count.
        // Ошибка чтения сигнатуры: пропустить rebuild, только обновить маркер/count.
        refreshCurrentStationVisuals();
        return;
    }
    if (_list_sig_cache_valid && station_list_adapter::list_signature_equal(now, _list_sig_cache)) {
        // Playlist unchanged: refresh marker/count only.
        // Плейлист не изменился: только маркер/count.
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
    // Reposition existing marker, or create if absent.
    // Переместить существующий маркер или создать если отсутствует.
    if (_current_marker) {
        _positionCurrentMarker(current);
        return;
    }
    _layoutMarkerForCurrentStation(current);
}

// ─────────────────────────────────────────────────────────────────────────────
// Count pipeline / Конвейер счётчика
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// List population pipeline / Конвейер заполнения списка
// ─────────────────────────────────────────────────────────────────────────────

void LvglStationPage::_clearStationListVisuals() {
    // Overlays first: delete while handles are still valid (STATIONFIX-1 order).
    // Сначала overlays: удаляем пока handles ещё валидны (порядок STATIONFIX-1).
    _destroyStationOverlays();
    if (_list_area) {
        lv_obj_clean(_list_area);
    }
    _lbl_list = nullptr;
}

bool LvglStationPage::_showStationListAllocationError() {
    // Allocation error: show message label. LVGL makes its own copy (lv_label_set_text).
    // _list_text is not used here (not allocated). No overlays, no signature update.
    // Ошибка выделения: LVGL хранит собственную копию. _list_text не используется.
    _lbl_list = lv_label_create(_list_area);
    if (!_lbl_list) return false;
    lv_label_set_text(_lbl_list, kStrListBufferAllocFailed);
    station_set_font(_lbl_list, kFontStationList);
    lv_obj_set_style_text_color(_lbl_list, yoradio_palette().list_row_text, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_lbl_list, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);
    _registerListPointerHandlersOnLabel();
    return true;
}

bool LvglStationPage::_createStationListLabelFromBuffer() {
    // Normal list: lv_label_set_text_static — LVGL stores only the pointer, no copy.
    // _list_text must stay allocated while _lbl_list exists.
    // Обычный список: LVGL хранит только указатель — _list_text должен жить пока _lbl_list существует.
    _lbl_list = lv_label_create(_list_area);
    if (!_lbl_list) return false;
    lv_label_set_long_mode(_lbl_list, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(_lbl_list, _list_text);
    station_set_font(_lbl_list, kFontStationList);
    lv_obj_set_style_text_color(_lbl_list, yoradio_palette().list_row_text, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(_lbl_list, kStationListLineSpace, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_lbl_list, list_label_pad_left_for_marker_gutter(), LV_PART_MAIN);
    return true;
}

void LvglStationPage::_populateStationList() {
    if (!_list_area) return;

    _clearStationListVisuals();

    const uint16_t current = station_list_adapter::current_station_num();
    const uint16_t tot     = station_list_adapter::station_count();

    _updateCountLabel(current, tot);

    if (!_ensureListTextBuffer(tot)) {
        _list_sig_cache_valid = false;
        _showStationListAllocationError();
        return;
    }

    const size_t name_limit = (LV_ACTIVE_PROFILE.width >= kWideProfileMinWidth)
                              ? kStationNameLimitWide : kStationNameLimitCompact;
    if (!station_list_adapter::station_list_text(_list_text, _list_text_cap, name_limit)) {
        strlcpy(_list_text, kStrListReadFailed, _list_text_cap);
    }

    if (!_createStationListLabelFromBuffer()) {
        return;
    }

    _buildStationOverlaysAfterList(current);
    _registerListPointerHandlersOnLabel();
    _cacheListSignature();
}

void LvglStationPage::_registerListPointerHandlersOnLabel() {
    if (!_lbl_list) return;
    lv_obj_clear_flag(_lbl_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_lbl_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_lbl_list, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

void LvglStationPage::_cacheListSignature() {
    (void)station_list_adapter::list_signature(&_list_sig_cache);
    _list_sig_cache_valid = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlay destruction / Удаление overlay объектов
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Focus overlay pipeline / Конвейер оверлея фокуса
// ─────────────────────────────────────────────────────────────────────────────

void LvglStationPage::_clampFocusForTotal(uint16_t tot) {
    if (tot == 0u) {
        _focus_station_num = 0;
        return;
    }
    if (_focus_station_num >= 1u && _focus_station_num <= tot
        && station_list_adapter::is_valid_station_num(_focus_station_num)) {
        return;
    }
    const uint16_t cur = station_list_adapter::current_station_num();
    if (station_list_adapter::is_valid_station_num(cur) && cur <= tot) {
        _focus_station_num = cur;
    } else {
        _focus_station_num = 1u;
    }
}

void LvglStationPage::_styleFocusRowOverlays(const YoRadioPalette& pal) {
    if (_focus_row_bg) {
        lv_obj_set_width(_focus_row_bg,
            static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.width - (LV_ACTIVE_PROFILE.frame_padding * 2u)));
        lv_obj_set_height(_focus_row_bg, kRowOverlayHeight);
        lv_obj_set_style_bg_color(_focus_row_bg, pal.list_row_selected_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_focus_row_bg, kFocusBgOpa, LV_PART_MAIN);
        lv_obj_set_style_border_width(_focus_row_bg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(_focus_row_bg, kFocusBgRadius, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_focus_row_bg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(_focus_row_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(_focus_row_bg, LV_OBJ_FLAG_CLICKABLE);
    }
    if (_focus_row_accent) {
        lv_obj_set_width(_focus_row_accent, kFocusAccentStripW);
        lv_obj_set_height(_focus_row_accent, kRowAccentH);
        lv_obj_set_style_bg_color(_focus_row_accent, pal.accent, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_focus_row_accent, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(_focus_row_accent, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(_focus_row_accent, kFocusAccentRadius, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_focus_row_accent, 0, LV_PART_MAIN);
        lv_obj_clear_flag(_focus_row_accent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(_focus_row_accent, LV_OBJ_FLAG_CLICKABLE);
    }
}

void LvglStationPage::_positionFocusRowOverlays(uint16_t focus_station_num) {
    const lv_coord_t y = row_y_for_station_row(focus_station_num);
    if (_focus_row_bg) {
        lv_obj_set_pos(_focus_row_bg, -kListPadLeft, y - kRowOverlayTopPad);
    }
    if (_focus_row_accent) {
        lv_obj_set_pos(_focus_row_accent, -kListPadLeft + 1, y + kRowAccentY);
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

    // Ensure objects exist.
    if (!_focus_row_bg) {
        _focus_row_bg = lv_obj_create(_list_area);
    }
    if (!_focus_row_bg) return;

    if (!_focus_row_accent) {
        _focus_row_accent = lv_obj_create(_list_area);
    }
    if (!_focus_row_accent) return;

    // Style, position, then z-order.
    // Стиль, позиция, затем z-order.
    _styleFocusRowOverlays(pal);
    _positionFocusRowOverlays(focus_station);

    // Z-order: bg behind list label; marker stays foreground.
    // Z-order: bg под label списка; маркер остаётся сверху.
    lv_obj_move_background(_focus_row_bg);
    lv_obj_move_foreground(_lbl_list);
    if (_current_marker) {
        lv_obj_move_foreground(_current_marker);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Current marker pipeline / Конвейер маркера текущей станции
// ─────────────────────────────────────────────────────────────────────────────

bool LvglStationPage::_ensureCurrentMarker() {
    if (_current_marker) return true;
    _current_marker = lv_label_create(_list_area);
    if (!_current_marker) return false;
    lv_obj_set_width(_current_marker, kMarkerBoxW);
    lv_label_set_long_mode(_current_marker, LV_LABEL_LONG_CLIP);
    lv_label_set_text(_current_marker, kIconCurrentStation);
    station_set_font(_current_marker, kFontCurrentMarker);
    lv_obj_set_style_text_align(_current_marker, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_flag(_current_marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_current_marker, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

void LvglStationPage::_styleCurrentMarker(const YoRadioPalette& pal) {
    if (!_current_marker) return;
    lv_obj_set_style_text_color(_current_marker, pal.accent, LV_PART_MAIN);
}

void LvglStationPage::_positionCurrentMarker(uint16_t current_station_num) {
    if (!_current_marker) return;
    const lv_coord_t y = row_y_for_station_row(current_station_num);
    const lv_coord_t mx = marker_left_x_in_list();
    lv_obj_set_pos(_current_marker, mx, y + kMarkerY);
}

void LvglStationPage::_layoutMarkerForCurrentStation(uint16_t current_station) {
    if (!_list_area || !_lbl_list || !station_list_adapter::is_valid_station_num(current_station)) {
        _destroyCurrentMarkerOverlay();
        return;
    }

    if (!_ensureCurrentMarker()) return;

    const YoRadioPalette& pal = yoradio_palette();
    _styleCurrentMarker(pal);
    _positionCurrentMarker(current_station);

    // Marker always in foreground, above list label.
    // Маркер всегда поверх label списка.
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
    (void)screen_px;  // X not used for row math — scrollbar strip guard runs before this call.
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

// ─────────────────────────────────────────────────────────────────────────────
// Pointer/input pipeline / Конвейер ввода
// ─────────────────────────────────────────────────────────────────────────────

// ── Event wrappers — thin static forwarders only ───────────────────────────

void LvglStationPage::_listAreaPressedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (self) self->_onListAreaPressed(e);
}

void LvglStationPage::_listAreaPressingEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (self) self->_onListAreaPressing(e);
}

void LvglStationPage::_listAreaReleasedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (self) self->_onListAreaReleased(e);
}

void LvglStationPage::_listAreaShortClickedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (self) self->_onListAreaShortClicked(e);
}

// ── Stroke tracking helpers ────────────────────────────────────────────────

void LvglStationPage::_resetListStrokeTracking() {
    _list_press_pt            = {};
    _list_press_scroll_y      = 0;
    _list_stroke_max_manhattan    = 0;
    _list_stroke_max_scroll_y_abs = 0;
}

void LvglStationPage::_captureListPressBaseline(lv_indev_t* indev) {
    _resetListStrokeTracking();
    if (indev) {
        lv_indev_get_point(indev, &_list_press_pt);
    }
    _list_press_scroll_y = lv_obj_get_scroll_y(_list_area);
}

void LvglStationPage::_updateListStrokePeaks(const lv_point_t& cur, lv_coord_t scroll_y) {
    const int32_t manh = manhattan_distance(_list_press_pt, cur);
    _list_stroke_max_manhattan = max_i32(_list_stroke_max_manhattan, manh);

    const int32_t ds = abs_i32(
        static_cast<int32_t>(scroll_y) - static_cast<int32_t>(_list_press_scroll_y));
    _list_stroke_max_scroll_y_abs = max_i32(_list_stroke_max_scroll_y_abs, ds);
}

bool LvglStationPage::_isTrackedStrokeScrollLike() const {
    return (_list_stroke_max_manhattan    >= static_cast<int32_t>(kListTapMaxFingerTravelPx))
        || (_list_stroke_max_scroll_y_abs >= static_cast<int32_t>(kListTapMaxScrollYDeltaPx));
}

bool LvglStationPage::_consumeListFocusSuppression() {
    if (!_list_arm_suppress_next_focus) return false;
    _list_arm_suppress_next_focus = false;
    return true;
}

// Full input state reset on screen teardown (destroy / auto-delete only).
// Полный reset state ввода при разрушении экрана (только destroy/auto-delete).
void LvglStationPage::_resetListInputState() {
    _resetListStrokeTracking();
    _list_arm_suppress_next_focus = false;
}

// ── Pressed pipeline ───────────────────────────────────────────────────────

void LvglStationPage::_onListAreaPressed(lv_event_t* e) {
    if (!_list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (lv_event_get_target(e) != _list_area) return;

    lv_indev_t* indev = lv_indev_get_act();
    _captureListPressBaseline(indev);

    // Touch while list is still coasting: arm suppression so next clean click
    // stops the scroll instead of focusing a station.
    // Касание при инерции: arm suppress — следующий clean click остановит прокрутку, не сфокусирует.
    if (lv_obj_is_scrolling(_list_area)) {
        _list_arm_suppress_next_focus = true;
    }
}

// ── Pressing pipeline ──────────────────────────────────────────────────────

void LvglStationPage::_onListAreaPressing(lv_event_t* e) {
    if (!_list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_PRESSING) return;
    if (lv_event_get_target(e) != _list_area) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t cur{};
    lv_indev_get_point(indev, &cur);
    _updateListStrokePeaks(cur, lv_obj_get_scroll_y(_list_area));
}

// ── Released pipeline ──────────────────────────────────────────────────────

void LvglStationPage::_onListAreaReleased(lv_event_t* e) {
    if (!_list_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (lv_event_get_target(e) != _list_area) return;

    // Classify the completed stroke: if it looks scroll-like, arm suppression.
    // Классификация завершённого жеста: если похож на прокрутку — arm suppress.
    if (_isTrackedStrokeScrollLike()) {
        _list_arm_suppress_next_focus = true;
    }
}

// ── Short-click pipeline ───────────────────────────────────────────────────

void LvglStationPage::_onListAreaShortClicked(lv_event_t* e) {
    if (!_list_area || !_lbl_list || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    if (lv_event_get_target(e) != _list_area) return;

    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p{};
    lv_dir_t gd = LV_DIR_NONE;
    if (indev) {
        gd = lv_indev_get_gesture_dir(indev);
        lv_indev_get_point(indev, &p);
    }

    // Guard 1: reject while list is still coasting (LVGL may synthesize clicks).
    // Guard 1: отклонять пока список ещё движется по инерции.
    if (lv_obj_is_scrolling(_list_area)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip scrolling gd=%d p=(%d,%d)\n",
                      static_cast<int>(gd), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    // Guard 2: require active input device.
    if (!indev) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.println("[station] SHORT_CLICKED skip no_indev");
#endif
        return;
    }
    // Guard 3/4: reject horizontal and vertical gestures (lv_dir_t is bit flags).
    // Guard 3/4: отклонять горизонтальные и вертикальные жесты (lv_dir_t — битовые флаги).
    if ((gd & LV_DIR_HOR) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip horiz_gesture gd=%u p=(%d,%d)\n",
                      static_cast<unsigned>(gd), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    if ((gd & LV_DIR_VER) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip vert_gesture gd=%u p=(%d,%d)\n",
                      static_cast<unsigned>(gd), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    // Guard 5: reject if indev has a vertical scroll direction latched.
    // Guard 5: отклонять если у indev зафиксировано вертикальное направление скролла.
    const lv_dir_t scroll_dir = lv_indev_get_scroll_dir(indev);
    if ((scroll_dir & LV_DIR_VER) != 0) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip indev_scroll_ver sd=%u\n",
                      static_cast<unsigned>(scroll_dir));
#endif
        return;
    }

    // Guard 6: reject if effective scroll-y delta (end or peak) exceeds threshold.
    // Endpoint alone is not enough: a fast flick may return near start position.
    // Guard 6: отклонять если эффективная scroll-y дельта (конец или пик) выше порога.
    const lv_coord_t scroll_y_now = lv_obj_get_scroll_y(_list_area);
    const int32_t d_scroll_end = abs_i32(
        static_cast<int32_t>(scroll_y_now) - static_cast<int32_t>(_list_press_scroll_y));
    const int32_t ad_scroll = max_i32(d_scroll_end, _list_stroke_max_scroll_y_abs);
    if (ad_scroll >= static_cast<int32_t>(kListTapMaxScrollYDeltaPx)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip scroll_ydelta end=%d peak=%d\n",
                      static_cast<int>(d_scroll_end),
                      static_cast<int>(_list_stroke_max_scroll_y_abs));
#endif
        return;
    }
    // Guard 7: reject if effective finger travel (end or peak) exceeds threshold.
    // Guard 7: отклонять если эффективное расстояние (конец или пик) выше порога.
    const int32_t travel_end = manhattan_distance(_list_press_pt, p);
    const int32_t travel = max_i32(travel_end, _list_stroke_max_manhattan);
    if (travel >= static_cast<int32_t>(kListTapMaxFingerTravelPx)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip finger_travel end=%d peak=%d p=(%d,%d)\n",
                      static_cast<int>(travel_end),
                      static_cast<int>(_list_stroke_max_manhattan),
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }
    // Guard 8: reject tap in right scrollbar strip.
    // Guard 8: отклонять tap в правой полосе scrollbar.
    {
        lv_area_t ar{};
        lv_obj_get_coords(_list_area, &ar);
        if (static_cast<int32_t>(p.x) > static_cast<int32_t>(ar.x2) - kScrollbarRightIgnorePx) {
#if YORADIO_LVGL_TOUCH_DEBUG
            Serial.printf("[station] SHORT_CLICKED skip scrollbar p.x=%d\n", static_cast<int>(p.x));
#endif
            return;
        }
    }
    // Guard 9: map tap to a valid station row.
    // Guard 9: отобразить tap на строку станции.
    uint16_t station_num = 0;
    if (!_candidateStationFromScreenPoint(p.x, p.y, &station_num)) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED skip no_row p=(%d,%d)\n",
                      static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }

    // Suppression: consume arm if set (stop UX — first qualifying tap after scroll/coasting).
    // Suppression: потребить arm если установлен (stop UX — первый qualifying tap после прокрутки).
    if (_consumeListFocusSuppression()) {
#if YORADIO_LVGL_TOUCH_DEBUG
        Serial.printf("[station] SHORT_CLICKED arm consumed (no focus) st=%u p=(%d,%d)\n",
                      static_cast<unsigned>(station_num), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
        return;
    }

    // Accepted tap: set focus then play.
    // Принятый тап: сначала фокус, потом воспроизведение.
#if YORADIO_LVGL_TOUCH_DEBUG
    Serial.printf("[station] SHORT_CLICKED focus st=%u p=(%d,%d)\n",
                  static_cast<unsigned>(station_num), static_cast<int>(p.x), static_cast<int>(p.y));
#endif
    _setFocusStation(station_num);
    // All guards passed: this is a clean tap — set focus and start playback.
    // Все guards пройдены: чистый тап — установить фокус и запустить воспроизведение.
    station_list_adapter::play_station(station_num);
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer action / Действие футера
// ─────────────────────────────────────────────────────────────────────────────

void LvglStationPage::_hintAreaClickedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (self) self->_onHintAreaClicked(e);
}

// Clean tap on footer: request Main via PageChain. Audio continues.
// Чистый тап на footer: запрос Main через PageChain. Аудио продолжает играть.
void LvglStationPage::_onHintAreaClicked(lv_event_t* e) {
    if (!_hint_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_event_get_target(e) != _hint_area) return;
    lvgl_ui::goToCarouselPage(PageChain::MAIN_INDEX);
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

    // Reapply footer button style (normal + pressed states) and text colors.
    // Обновить стиль кнопки footer (нормальное + нажатое) и цвета текста.
    if (_hint_area) {
        style_hint_button(_hint_area, pal);
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
    _resetListInputState();
    _releaseListTextBuffer();
}

lv_obj_t* LvglStationPage::screen() {
    return _screen;
}

} // namespace lvgl_ui

