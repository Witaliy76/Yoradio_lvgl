/*
 * LvglPresetScreen — Stage 6.4C visual polish (positional station number Preset).
 * LvglPresetScreen — Этап 6.4C: визуальный polish Preset Temporary.
 *
 * Structure: screen → title (centered) + 8 card rows + helper (bottom center).
 * Структура: экран → заголовок (центр) + 8 карточек + подсказка (низ центр).
 *
 * DspTask-only lv_*; tap valid → play_station + dismiss; long press → save (screen stays open).
 * Feedback shown in bottom helper label; title «PRESETS» never changes.
 * Только DspTask; тап = play+dismiss; long press = save без закрытия.
 * Feedback — в нижней подсказке; заголовок не меняется.
 */

#include "scr_preset.h"

#include <cstdio>
#include <cstring>

#include "lvgl.h"
#include "../../core/config.h"
#include "../adapters/preset_store.h"
#include "../adapters/station_list_adapter.h"
#include "../fonts/lv_fonts.h"
#include "../lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

namespace {

// ──────────────────────────────────────────────────────────────────────────────
// Строки UI (L10n readiness) / UI strings
// Все видимые пользователю строки — здесь. / All user-visible strings in one place.
// ──────────────────────────────────────────────────────────────────────────────

// Заголовок экрана — всегда статичный. / Screen title — always static.
static constexpr char kStrPresets[]      = "ПРЕСЕТЫ";
// Пустой слот. / Empty slot placeholder.
static constexpr char kStrEmpty[]        = "Пусто";
// Станция отсутствует в списке. / Station not found in the station list.
static constexpr char kStrUnavailable[]  = "Недоступно";
// Нет текущей станции для сохранения. / No current station playing — cannot save.
static constexpr char kStrNoStation[]    = "НЕТ СТАНЦИИ ДЛЯ СОХРАНЕНИЯ";
// Сохранение не удалось. / Preset save operation failed.
static constexpr char kStrSaveFailed[]   = "ОШИБКА СОХРАНЕНИЯ";
// Формат сообщения об успешном сохранении; %u = номер слота (1-based). / Save success format; %u = 1-based slot.
static constexpr char kStrSavedFmt[]     = "ПРЕСЕТ %u СОХРАНЕН";
// Подсказка по умолчанию в нижней строке; «15С» — статический placeholder, заменяется при enter() через kStrCountdownFmt.
// Default bottom helper; "15С" is a static placeholder, overwritten at enter() by kStrCountdownFmt.
static constexpr char kHelperDefault[]   = "УДЕРЖИВАТЬ ДЛЯ СОХРАНЕНИЯ \xE2\x80\xA2 ВОЗВРАТ 15";
// Формат динамического обратного отсчёта; %d = оставшихся секунд. / Countdown format; %d = seconds remaining.
static constexpr char kStrCountdownFmt[] = "УДЕРЖИВАТЬ ДЛЯ СОХРАНЕНИЯ \xE2\x80\xA2 ВОЗВРАТ %d";
// U+2022 BULLET — подтверждён в подмножестве Montserrat (Station + Main используют тот же символ).
// U+2022 BULLET — confirmed in Montserrat subset (Station + Main use the same glyph).
static constexpr char kBullet[]          = " \xE2\x80\xA2 ";

// ──────────────────────────────────────────────────────────────────────────────
// Визуальные константы / Visual constants
// ──────────────────────────────────────────────────────────────────────────────

// Задержки восстановления нижней подсказки (мс); экран остаётся открытым — меняется только текст.
// Helper restore delays (ms); screen stays open — only helper text reverts.
constexpr uint32_t kHelperRestoreMsSuccess = 800u;
constexpr uint32_t kHelperRestoreMsError   = 1200u;

// Период таймера обратного отсчёта (мс): ~3 тика в секунду; label обновляется только при смене секунды.
// Countdown timer period (ms): ~3 ticks/s; label updates only when the displayed second changes.
constexpr uint32_t kCountdownPeriodMs = 333u;

// ──────────────────────────────────────────────────────────────────────────────
// Геометрия раскладки / Layout geometry
// ──────────────────────────────────────────────────────────────────────────────

// Вертикальный зазор между flex-элементами: заголовок↔строка0, между строками, строка7↔подсказка.
// Vertical gap between flex items: title↔row0, between rows, row7↔helper. 9 gaps × 4 px = 36 px.
constexpr lv_coord_t kRowGap    = 4;

// Высота карточки: шрифт M22 даёт ≈ 25 px строки → flex cross-center оставляет ≈ 9 px сверху/снизу.
// Card row height: M22 line height ≈ 25 px; flex cross-center → ≈ 9 px top/bottom.
constexpr lv_coord_t kRowH      = 44;

// Радиус скругления углов карточки. / Card rounded corner radius.
constexpr lv_coord_t kRowRadius = 8;
// Ширина рамки карточки. / Card border width.
constexpr lv_coord_t kRowBorder = 1;

// Горизонтальный padding карточки (уменьшен 10→8 для расширения зоны названия станции).
// Card inner horizontal padding (reduced 10→8 to give more name space).
constexpr lv_coord_t kRowPadLR  = 8;

// Зазор между колонками внутри строки (уменьшен 6→4 для расширения зоны названия).
// Column gap inside a row (reduced 6→4 to give more name space).
constexpr lv_coord_t kRowColGap = 4;

// Ширины колонок / Column widths.
// name label: base=1px + flex_grow=1 — LVGL flex отдаёт всё оставшееся место этому label.
// name label: width=1 + flex_grow=1 — LVGL flex distributes ALL remaining space here.
// LV_SIZE_CONTENT как база = минимум = весь текст; мешает LV_LABEL_LONG_DOT обрезать по границе.
// LV_SIZE_CONTENT as base makes LVGL use full text width as minimum, conflicting with LONG_DOT.
// Доступная ширина имени на 480px: 480−2×8(scr)−2×8(pad)−24(slot)−1(div)−56(num)−3×4(gaps) = 355 px.
// Available name width on 480px: 480−16−16−24−1−56−12 = 355 px.
constexpr lv_coord_t kSlotW     = 24;  // ширина колонки номера слота «1»..«8» / slot number column
constexpr lv_coord_t kNumW      = 56;  // ширина колонки номера станции «#257» или «--» / station number (reduced 64→56)
constexpr lv_coord_t kDivH      = 24;  // высота вертикального разделителя (px) / vertical divider height (px)

// Бюджет байт на имя станции для UTF-8-безопасного усечения. / Station name byte budget for UTF-8 truncation.
constexpr size_t kRowNameBytes  = 96;

// ──────────────────────────────────────────────────────────────────────────────
// Вспомогательные функции / Helper functions
// ──────────────────────────────────────────────────────────────────────────────

// UTF-8-безопасное усечение строки на месте: обрезка не делается посередине многобайтового символа.
// In-place UTF-8-safe truncation: never cuts inside a multi-byte sequence.
static void truncate_utf8_in_place(char* s, size_t max_bytes) {
    if (!s) return;
    const size_t len = strlen(s);
    if (len <= max_bytes) return;
    if (max_bytes == 0u) { s[0] = '\0'; return; }
    size_t cut = max_bytes;
    while (cut > 0u && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) --cut;
    if (cut == 0u) cut = max_bytes;
    s[cut] = '\0';
}

// Делает объект полностью прозрачным: без фона, рамки и внутренних отступов.
// Makes an object fully transparent: no background, border, or padding.
static void style_transp(lv_obj_t* o) {
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
}

// Извлекает указатель на экран из user_data события. / Extracts screen pointer from event user_data.
static LvglPresetScreen* self_from_event(lv_event_t* e) {
    return e ? static_cast<LvglPresetScreen*>(lv_event_get_user_data(e)) : nullptr;
}

// Извлекает индекс слота из user_data LVGL-объекта-карточки. / Extracts slot index from card LVGL object user_data.
static uint8_t slot_from_event(lv_event_t* e) {
    if (!e) return 0xFFu;
    lv_obj_t* row = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!row) return 0xFFu;
    const intptr_t s = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
    return (s >= 0 && s < LvglPresetScreen::kSlotCount) ? static_cast<uint8_t>(s) : 0xFFu;
}

} // namespace

// ──────────────────────────────────────────
// ILvglScreen lifecycle
// ──────────────────────────────────────────

ScreenType LvglPresetScreen::screenType() const { return ScreenType::Temporary; }

void LvglPresetScreen::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    const lv_coord_t scr_pad  = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);

    // ── Корень экрана / Screen root ──────────────────────────────────
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, scr_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, kRowGap, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ── Заголовок «PRESETS» — по центру, не конфликтует с FPS/CPU виджетом слева ─
    // Title centered horizontally; FPS widget is at far left — no visual collision.
    // Заголовок по центру; FPS widget слева — конфликта нет.
    _title = lv_label_create(_screen);
    if (_title) {
        lv_obj_set_width(_title, LV_PCT(100));
        lv_obj_set_height(_title, LV_SIZE_CONTENT);
        // pad_top уменьшен (2px ближе к краю), pad_bottom создаёт зазор до первой карточки.
        // Reduced pad_top (2px closer to edge) + pad_bottom creates visual separation to first card.
        lv_obj_set_style_pad_top(_title, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(_title, 8, LV_PART_MAIN);
        lv_label_set_text(_title, kStrPresets);
        lv_obj_set_style_text_color(_title, pal.text_primary, LV_PART_MAIN);
        lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        // M20: заметно крупнее M14/M16, но не такой тяжёлый как M22. / M20: noticeable larger than M14/M16, not as heavy as M22.
        lv_obj_set_style_text_font(_title, &lv_font_yora_montserrat_20_cyr, LV_PART_MAIN);
        lv_obj_clear_flag(_title, LV_OBJ_FLAG_CLICKABLE);
    }

    // ── 8 карточек-строк / 8 card rows ───────────────────────────────
    for (int i = 0; i < kSlotCount; ++i) {

        // Контейнер карточки — кликабельный, скруглённый, с рамкой и pressed state.
        // Card container — clickable, rounded, border, pressed state.
        _rows[i] = lv_obj_create(_screen);
        if (!_rows[i]) continue;

        lv_obj_set_width(_rows[i], LV_PCT(100));
        lv_obj_set_height(_rows[i], kRowH);
        lv_obj_set_style_bg_color(_rows[i], pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_rows[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(_rows[i], pal.panel_border, LV_PART_MAIN);
        lv_obj_set_style_border_width(_rows[i], kRowBorder, LV_PART_MAIN);
        lv_obj_set_style_border_opa(_rows[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(_rows[i], kRowRadius, LV_PART_MAIN);

        // Pressed state: немного темнее фон + рамка accent-soft.
        // Pressed state: slightly darker bg + accent-soft border.
        lv_obj_set_style_bg_color(_rows[i], pal.list_row_selected_bg, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(_rows[i], pal.accent_soft, LV_STATE_PRESSED);

        lv_obj_set_style_pad_top(_rows[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(_rows[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(_rows[i], kRowPadLR, LV_PART_MAIN);
        lv_obj_set_style_pad_right(_rows[i], kRowPadLR, LV_PART_MAIN);
        lv_obj_set_style_pad_column(_rows[i], kRowColGap, LV_PART_MAIN);
        lv_obj_clear_flag(_rows[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(_rows[i], LV_OBJ_FLAG_CLICKABLE);

        // Строка flex: слева направо, cross-axis CENTER (вертикальное центрирование детей).
        // Row flex: left-to-right, cross-axis CENTER (vertically centers all children).
        lv_obj_set_flex_flow(_rows[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(_rows[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Callback на карточке; экран через user_data параметра; слот через user_data объекта.
        // Event callback on card; screen via event user_data; slot via obj user_data.
        lv_obj_set_user_data(_rows[i], reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(_rows[i], _rowEventCb, LV_EVENT_ALL, this);

        // ── Номер слота (1..8) / Slot number (1..8) ──────────────────
        _slot_labels[i] = lv_label_create(_rows[i]);
        if (_slot_labels[i]) {
            lv_obj_set_width(_slot_labels[i], kSlotW);
            lv_obj_set_height(_slot_labels[i], LV_SIZE_CONTENT);
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", i + 1);
            lv_label_set_text(_slot_labels[i], buf);
            lv_obj_set_style_text_color(_slot_labels[i], pal.text_secondary, LV_PART_MAIN);
            lv_obj_set_style_text_align(_slot_labels[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            if (LV_ACTIVE_PROFILE.font_small) {
                lv_obj_set_style_text_font(_slot_labels[i],
                    static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_small), LV_PART_MAIN);
            }
            lv_obj_clear_flag(_slot_labels[i], LV_OBJ_FLAG_CLICKABLE);
        }

        // ── Вертикальный разделитель (1 px × kDivH, полупрозрачный) / Vertical divider line ──
        _vdiv_lines[i] = lv_obj_create(_rows[i]);
        if (_vdiv_lines[i]) {
            lv_obj_set_width(_vdiv_lines[i], 1);
            lv_obj_set_height(_vdiv_lines[i], kDivH);
            lv_obj_set_style_bg_color(_vdiv_lines[i], pal.divider, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_vdiv_lines[i], LV_OPA_60, LV_PART_MAIN);
            lv_obj_set_style_border_width(_vdiv_lines[i], 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(_vdiv_lines[i], 0, LV_PART_MAIN);
            lv_obj_clear_flag(_vdiv_lines[i], LV_OBJ_FLAG_CLICKABLE);
        }

        // ── Номер станции «#257» (accent) или «--» (пустой слот) / Station number ───
        _num_labels[i] = lv_label_create(_rows[i]);
        if (_num_labels[i]) {
            lv_obj_set_width(_num_labels[i], kNumW);
            lv_obj_set_height(_num_labels[i], LV_SIZE_CONTENT);
            lv_label_set_text(_num_labels[i], "");
            if (LV_ACTIVE_PROFILE.font_normal) {
                lv_obj_set_style_text_font(_num_labels[i],
                    static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_normal), LV_PART_MAIN);
            }
            lv_obj_clear_flag(_num_labels[i], LV_OBJ_FLAG_CLICKABLE);
        }

        // ── Название станции (M22, flex-grow, LV_LABEL_LONG_DOT) ─────
        // Ширина 1px: LVGL flex отдаёт всё оставшееся место этому label.
        // LV_SIZE_CONTENT как база = минимум = весь текст; мешает LONG_DOT обрезать по границе.
        // Width 1px: LVGL flex distributes ALL remaining space here (not "content+grow").
        // LV_SIZE_CONTENT as base makes LVGL use full text width as minimum, fighting LONG_DOT.
        _name_labels[i] = lv_label_create(_rows[i]);
        if (_name_labels[i]) {
            // Высота = точная высота строки M22: запрещает перенос; LONG_DOT обрезает по ширине.
            // Fixed height = exact M22 line-height → no wrap; LONG_DOT truncates at flex boundary.
            const lv_coord_t m22_line_h =
                lv_font_get_line_height(&lv_font_yora_montserrat_22_cyr);
            lv_obj_set_width(_name_labels[i], 1);           // минимальная база; flex_grow заполняет остаток
            lv_obj_set_flex_grow(_name_labels[i], 1);
            lv_obj_set_height(_name_labels[i], m22_line_h); // строго одна строка / strictly one line
            lv_label_set_long_mode(_name_labels[i], LV_LABEL_LONG_DOT);
            lv_label_set_text(_name_labels[i], "");
            lv_obj_set_style_text_font(_name_labels[i], &lv_font_yora_montserrat_22_cyr, LV_PART_MAIN);
            lv_obj_clear_flag(_name_labels[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }

    // ── Нижняя подсказка — hint + цель для feedback / Bottom helper — hint + feedback target ──
    // Заголовок неизменен; весь feedback отображается здесь, а не в заголовке.
    // Title never changes; all feedback goes here, not to the title.
    _helper = lv_label_create(_screen);
    if (_helper) {
        lv_obj_set_width(_helper, LV_PCT(100));
        lv_obj_set_height(_helper, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_top(_helper, 4, LV_PART_MAIN);
        lv_label_set_text(_helper, kHelperDefault);
        lv_obj_set_style_text_color(_helper, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(_helper, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        // M14 — крупнее M12; с запасом помещается под 8 строками на 480px. / M14 — larger than M12; fits below 8 rows.
        lv_obj_set_style_text_font(_helper, &lv_font_yora_montserrat_14_cyr, LV_PART_MAIN);
        lv_obj_clear_flag(_helper, LV_OBJ_FLAG_CLICKABLE);
    }
}

void LvglPresetScreen::enter() {
    _long_press_handled  = false;
    _feedbackActive      = false;
    _lastCountdownSecond = -1;
    _cancelFeedbackTimer();
    (void)preset_store::begin();
    _setHelperDefault();           // сбрасывает _feedbackActive и рендерит отсчёт / clears _feedbackActive + renders countdown
    _startCountdownTimer();        // периодический таймер обновления подсказки / recurring helper update timer
    for (uint8_t s = 0; s < kSlotCount; ++s) _updateRowContent(s);
}

void LvglPresetScreen::update() {}

void LvglPresetScreen::exit() {
    _cancelFeedbackTimer();
    _cancelCountdownTimer();
}

void LvglPresetScreen::destroy() {
    _cancelFeedbackTimer();
    _cancelCountdownTimer();
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglPresetScreen::liveReapplyTheme() {
    if (!_screen) return;
    _applyAllColors();
}

lv_obj_t* LvglPresetScreen::screen() { return _screen; }

// ──────────────────────────────────────────
// Internal implementation
// ──────────────────────────────────────────

void LvglPresetScreen::_nullHandles() {
    _screen = nullptr;
    _title  = nullptr;
    _helper = nullptr;
    for (int i = 0; i < kSlotCount; ++i) {
        _rows[i]        = nullptr;
        _vdiv_lines[i]  = nullptr;
        _slot_labels[i] = nullptr;
        _num_labels[i]  = nullptr;
        _name_labels[i] = nullptr;
    }
    _feedback_timer      = nullptr;
    _countdown_timer     = nullptr;
    _lastCountdownSecond = -1;
    _feedbackActive      = false;
    _long_press_handled  = false;
}

void LvglPresetScreen::_applyRowColors(uint8_t slot, const YoRadioPalette& pal) {
    if (slot >= kSlotCount) return;

    if (_rows[slot]) {
        lv_obj_set_style_bg_color(_rows[slot], pal.panel_background, LV_PART_MAIN);
        lv_obj_set_style_border_color(_rows[slot], pal.panel_border, LV_PART_MAIN);
        lv_obj_set_style_bg_color(_rows[slot], pal.list_row_selected_bg, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(_rows[slot], pal.accent_soft, LV_STATE_PRESSED);
    }
    if (_vdiv_lines[slot]) {
        lv_obj_set_style_bg_color(_vdiv_lines[slot], pal.divider, LV_PART_MAIN);
    }
    if (_slot_labels[slot]) {
        lv_obj_set_style_text_color(_slot_labels[slot], pal.text_secondary, LV_PART_MAIN);
    }

    const bool occupied = preset_store::isAvailable() && preset_store::isOccupied(slot);
    const uint16_t num  = occupied ? preset_store::getStationNum(slot) : 0u;
    const bool valid    = occupied && station_list_adapter::is_valid_station_num(num);

    if (_num_labels[slot]) {
        lv_color_t num_col = valid ? pal.accent : (occupied ? pal.accent_soft : pal.text_meta);
        lv_obj_set_style_text_color(_num_labels[slot], num_col, LV_PART_MAIN);
    }
    if (_name_labels[slot]) {
        lv_color_t name_col = valid ? pal.text_primary : pal.text_secondary;
        lv_obj_set_style_text_color(_name_labels[slot], name_col, LV_PART_MAIN);
    }
}

void LvglPresetScreen::_applyAllColors() {
    const YoRadioPalette& pal = yoradio_palette();

    if (_screen) lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    if (_title)  lv_obj_set_style_text_color(_title,  pal.text_primary,   LV_PART_MAIN);
    if (_helper) lv_obj_set_style_text_color(_helper, pal.text_secondary, LV_PART_MAIN);

    for (uint8_t s = 0; s < kSlotCount; ++s) _applyRowColors(s, pal);
}

void LvglPresetScreen::_updateRowContent(uint8_t slot) {
    if (slot >= kSlotCount) return;

    const YoRadioPalette& pal   = yoradio_palette();
    const bool occupied         = preset_store::isOccupied(slot);
    const uint16_t station_num  = occupied ? preset_store::getStationNum(slot) : 0u;
    const bool valid            = occupied && station_list_adapter::is_valid_station_num(station_num);

    // ── Номер станции / Station number ────────────────────────────────
    if (_num_labels[slot]) {
        if (!occupied) {
            lv_label_set_text(_num_labels[slot], "--");
        } else {
            char nb[12];
            snprintf(nb, sizeof(nb), "#%u", static_cast<unsigned>(station_num));
            lv_label_set_text(_num_labels[slot], nb);
        }
    }

    // ── Название станции / Station name ───────────────────────────────
    if (_name_labels[slot]) {
        if (!occupied) {
            // kStrEmpty — кратко; нижняя подсказка объясняет действие. / kStrEmpty — concise; helper explains the action.
            lv_label_set_text(_name_labels[slot], kStrEmpty);
        } else if (!valid) {
            lv_label_set_text(_name_labels[slot], kStrUnavailable);
        } else {
            // Bullet-prefix подтверждён в шрифте (Station/Main используют тот же символ).
            // Bullet prefix confirmed in font (Station/Main use the same glyph).
            char name_buf[kRowNameBytes];
            if (station_list_adapter::station_name(station_num, name_buf, sizeof(name_buf))) {
                truncate_utf8_in_place(name_buf, 76u);
                char full[kRowNameBytes + 8];
                snprintf(full, sizeof(full), "%s%s", kBullet, name_buf);
                lv_label_set_text(_name_labels[slot], full);
            } else {
                lv_label_set_text(_name_labels[slot], kStrUnavailable);
            }
        }
    }

    _applyRowColors(slot, pal);
}

void LvglPresetScreen::_updateCountdownHelper() {
    if (!_helper) return;
    const uint32_t rem = temporaryRemainingMs();
    // Округляем вверх: сразу после открытия показывается 15S; последнее значение — 1S.
    // Round up: "15S" shows right after open/refresh, "1S" is the last value before dismiss.
    const int16_t secs = (rem > 0u)
        ? static_cast<int16_t>((rem + 999u) / 1000u)
        : static_cast<int16_t>(0);
    if (secs == _lastCountdownSecond) return;
    _lastCountdownSecond = secs;
    // kStrCountdownFmt in Russian needs ~71 bytes at peak (Cyrillic = 2 bytes/char + 2-digit secs).
    // kStrCountdownFmt на русском требует ~71 байт (кириллица = 2 байта/символ + 2-цифровые секунды).
    char buf[96];
    snprintf(buf, sizeof(buf), kStrCountdownFmt, static_cast<int>(secs > 0 ? secs : 1));
    lv_label_set_text(_helper, buf);
    lv_obj_set_style_text_color(_helper, yoradio_palette().text_secondary, LV_PART_MAIN);
}

void LvglPresetScreen::_setHelperDefault() {
    _feedbackActive      = false;
    _lastCountdownSecond = -1;   // принудительная перерисовка при следующем вызове _updateCountdownHelper
    _updateCountdownHelper();
}

void LvglPresetScreen::_setHelperMessage(const char* text, bool success) {
    if (!_helper || !text) return;
    _feedbackActive = true;      // приостанавливает отсчёт; feedback_timer вернёт его обратно
    lv_label_set_text(_helper, text);
    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_text_color(_helper, success ? pal.accent : pal.text_secondary, LV_PART_MAIN);
}

void LvglPresetScreen::_cancelFeedbackTimer() {
    if (_feedback_timer) {
        lv_timer_del(_feedback_timer);
        _feedback_timer = nullptr;
    }
}

void LvglPresetScreen::_feedbackTimerCb(lv_timer_t* timer) {
    if (!timer) return;
    auto* self = static_cast<LvglPresetScreen*>(timer->user_data);
    if (!self) return;
    self->_feedback_timer = nullptr;
    // Возврат к отсчёту; _setHelperDefault сбрасывает _feedbackActive и показывает реальный остаток.
    // Restore to countdown; _setHelperDefault clears _feedbackActive and renders actual remaining.
    self->_setHelperDefault();
}

void LvglPresetScreen::_scheduleHelperRestore(uint32_t delay_ms) {
    _cancelFeedbackTimer();
    _feedback_timer = lv_timer_create(_feedbackTimerCb, delay_ms, this);
    if (_feedback_timer) lv_timer_set_repeat_count(_feedback_timer, 1);
}

void LvglPresetScreen::_cancelCountdownTimer() {
    if (_countdown_timer) {
        lv_timer_del(_countdown_timer);
        _countdown_timer = nullptr;
    }
}

void LvglPresetScreen::_startCountdownTimer() {
    _cancelCountdownTimer();
    // kCountdownPeriodMs: ~3 тика в секунду; label обновляется только при смене отображаемой секунды.
    // ~3 ticks/s; label updates only when the displayed second changes.
    _countdown_timer = lv_timer_create(_countdownTimerCb, kCountdownPeriodMs, this);
    // repeat_count по умолчанию = -1 (бесконечно); lv_timer_set_repeat_count не нужен.
    // Default repeat_count = -1 (infinite); no lv_timer_set_repeat_count needed.
}

void LvglPresetScreen::_countdownTimerCb(lv_timer_t* timer) {
    if (!timer) return;
    auto* self = static_cast<LvglPresetScreen*>(timer->user_data);
    if (!self || self->_feedbackActive) return;
    self->_updateCountdownHelper();
}

// ──────────────────────────────────────────
// Row event handlers
// ──────────────────────────────────────────

void LvglPresetScreen::_onRowPressed(uint8_t slot) {
    (void)slot;
    _long_press_handled = false;
    refreshActiveTemporaryTimeout();
}

void LvglPresetScreen::_onRowShortClicked(uint8_t slot) {
    if (slot >= kSlotCount || !preset_store::isOccupied(slot)) return;
    const uint16_t num = preset_store::getStationNum(slot);
    if (!station_list_adapter::is_valid_station_num(num)) return;
    if (station_list_adapter::play_station(num)) {
        dismissActiveTemporary();  // немедленный dismiss при валидном тапе / immediate dismiss on valid tap-to-play
    }
}

void LvglPresetScreen::_onRowLongPressed(uint8_t slot) {
    if (slot >= kSlotCount || _long_press_handled) return;
    _long_press_handled = true;
    refreshActiveTemporaryTimeout();

    if (preset_store::saveCurrentStation(slot)) {
        _updateRowContent(slot);                // обновляем текст и цвета строки / refresh row text + colors
        // kStrSavedFmt in Russian needs up to 32 bytes per slot (Cyrillic = 2 bytes/char).
        // kStrSavedFmt на русском требует до 32 байт (кириллица = 2 байта/символ).
        char msg[48];
        snprintf(msg, sizeof(msg), kStrSavedFmt, static_cast<unsigned>(slot) + 1u);
        _setHelperMessage(msg, /*success=*/true);
        refreshActiveTemporaryTimeout();
        _scheduleHelperRestore(kHelperRestoreMsSuccess);
        return;
    }

    // Сохранение не удалось — определяем причину для сообщения пользователю.
    // Save failed — identify reason for user message.
    const uint16_t cur = config.lastStation();
    if (!station_list_adapter::is_valid_station_num(cur)) {
        _setHelperMessage(kStrNoStation, /*success=*/false);
    } else {
        _setHelperMessage(kStrSaveFailed, /*success=*/false);
    }
    refreshActiveTemporaryTimeout();
    _scheduleHelperRestore(kHelperRestoreMsError);
}

void LvglPresetScreen::_onRowReleased() {
    _long_press_handled = false;
    refreshActiveTemporaryTimeout();
}

void LvglPresetScreen::_rowEventCb(lv_event_t* e) {
    LvglPresetScreen* self = self_from_event(e);
    if (!self) return;
    const lv_event_code_t code = lv_event_get_code(e);
    const uint8_t slot = slot_from_event(e);
    if (slot >= kSlotCount) return;

    switch (code) {
        case LV_EVENT_PRESSED:             self->_onRowPressed(slot);      break;
        case LV_EVENT_SHORT_CLICKED:       self->_onRowShortClicked(slot); break;
        case LV_EVENT_LONG_PRESSED:        self->_onRowLongPressed(slot);  break;
        case LV_EVENT_LONG_PRESSED_REPEAT: refreshActiveTemporaryTimeout(); break;
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:          self->_onRowReleased();         break;
        default: break;
    }
}

} // namespace lvgl_ui
