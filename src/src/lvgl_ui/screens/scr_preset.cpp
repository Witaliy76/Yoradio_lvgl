/*
 * LvglPresetScreen — Temporary preset selector and saver.
 * LvglPresetScreen — временный экран Preset: выбор и сохранение слотов.
 *
 * Short tap: play occupied preset and dismiss.
 * Long press: save current station into slot and keep screen open.
 * Footer: countdown/feedback surface; click dismisses to origin.
 *
 * Тап: play + dismiss. Long press: save без закрытия. Footer: отсчёт/feedback, tap → origin.
 * All LVGL access is DspTask-only. / Все lv_* только из DspTask.
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_preset.h"

#include <cstdio>
#include <cstring>

#include "lvgl.h"
#include "../../core/config.h"
#include "../../i18n/i18n.h"
#include "../adapters/preset_store.h"
#include "../adapters/station_list_adapter.h"
#include "../fonts/lv_fonts.h"
#include "../lvgl_ui.h"
#include "../profiles/lv_profile_select.h"
#include "../theme/lv_theme_yoradio.h"
#include "../widgets/wgt_footer_pill.h"

namespace lvgl_ui {

namespace {

static constexpr char kStrEmptyText[] = "";
static constexpr char kStrEmptyStationNumber[] = "--";

// ── UI format strings / Форматные строки UI ──────────────────────────────────

static constexpr char kFmtSlotNumber[] =
    "%d";

static constexpr char kFmtStationNumber[] =
    "#%u";

static constexpr char kFmtDecoratedStationName[] =
    "%s%s";

// ── Glyph and separator resources / Ресурсы символов ─────────────────────────

// U+2022 BULLET — confirmed in Montserrat subset (Station + Main use the same glyph).
// U+2022 BULLET — подтверждён в подмножестве Montserrat (Station + Main используют тот же символ).
static constexpr char kBulletPrefixUtf8[] =
    " \xE2\x80\xA2 ";

// ── Font resources / Ресурсы шрифтов ─────────────────────────────────────────

static const lv_font_t* const kFontTitle =
    &lv_font_yora_montserrat_20_cyr;

static const lv_font_t* const kFontSlotNumber =
    &lv_font_yora_montserrat_16_cyr;  // M16 — accepted slot index size (post-6.4 polish)

static const lv_font_t* const kFontStationName =
    &lv_font_yora_montserrat_22_cyr;

static const lv_font_t* const kFontFooter =
    &lv_font_yora_montserrat_14_cyr;

static const lv_font_t* preset_station_number_font() {
    return static_cast<const lv_font_t*>(LV_ACTIVE_PROFILE.font_normal);
}

// ── Timing constants / Тайминги ───────────────────────────────────────────────

constexpr uint32_t kHelperRestoreMsSuccess = 800u;
constexpr uint32_t kHelperRestoreMsError   = 1200u;
constexpr uint32_t kCountdownPeriodMs      = 333u;

// ── Buffer and text constants / Буферы и текстовые размеры ───────────────────

static constexpr size_t kSlotNumberBufferSize        = 4u;
static constexpr size_t kStationNumberBufferSize     = 12u;
static constexpr size_t kSavedMessageBufferSize      = 48u;
static constexpr size_t kCountdownBufferSize         = 96u;
static constexpr size_t kStationNameDisplayMaxBytes  = 76u;
static constexpr size_t kDecoratedNameExtraBytes     = 8u;
static constexpr size_t kRowNameBytes                = 96u;

// ── Layout and visual constants / Геометрия и визуальные константы ───────────

constexpr lv_coord_t kRowGap    = 4;
constexpr lv_coord_t kRowH      = 44;
constexpr lv_coord_t kRowRadius = 8;
constexpr lv_coord_t kRowBorder = 1;
constexpr lv_coord_t kRowPadLR  = 8;
constexpr lv_coord_t kRowColGap = 4;
constexpr lv_coord_t kSlotW     = 24;
constexpr lv_coord_t kNumW      = 56;
constexpr lv_coord_t kDivH      = 24;

constexpr lv_coord_t kTitlePadTop    = 2;
constexpr lv_coord_t kTitlePadBottom = 8;

constexpr lv_coord_t kDividerWidth   = 1;
constexpr lv_opa_t   kDividerOpacity = LV_OPA_60;

constexpr lv_coord_t kFooterBoxH      = 32;

constexpr lv_coord_t kFooterBoxPadH   = 12;
constexpr lv_coord_t kFooterBoxPadV   = 6;
// kFooterBorderWidth, kFooterNormalOpacity, kFooterPressedOpacity — now in wgt_footer_pill.cpp

constexpr lv_coord_t kNameFlexBaseWidth = 1;

// ── Generic helpers / Общие вспомогательные функции ──────────────────────────

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

// Formatted UI text is accepted only when complete; fallback is copied without UTF-8 truncation.
// Форматированный UI-текст принимается только целиком; fallback копируется без усечения UTF-8.
static bool preset_copy_complete(char* out, size_t cap, const char* text) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (!text) return false;
    const size_t bytes = strlen(text) + 1u;
    if (bytes > cap) return false;
    memcpy(out, text, bytes);
    return true;
}

template <typename... Args>
static bool preset_format_checked(char* out, size_t cap, const char* fallback,
                                  const char* format, Args... args) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (format) {
        const int written = snprintf(out, cap, format, args...);
        if (written >= 0 && static_cast<size_t>(written) < cap) return true;
    }
    preset_copy_complete(out, cap, fallback ? fallback : "");
    return false;
}

static LvglPresetScreen* self_from_event(lv_event_t* e) {
    return e ? static_cast<LvglPresetScreen*>(lv_event_get_user_data(e)) : nullptr;
}

static uint8_t slot_from_event(lv_event_t* e) {
    if (!e) return 0xFFu;
    lv_obj_t* row = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!row) return 0xFFu;
    const intptr_t s = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
    return (s >= 0 && s < LvglPresetScreen::kSlotCount) ? static_cast<uint8_t>(s) : 0xFFu;
}

// Footer visual contract is now provided by wgt_footer_pill.
// Визуальный contract footer предоставляется wgt_footer_pill.

} // namespace

// ── Layout builders / Билдеры раскладки ───────────────────────────────────────

void LvglPresetScreen::create_title(LvglPresetScreen& self, const YoRadioPalette& pal) {
    self._title = lv_label_create(self._screen);
    if (!self._title) return;

    lv_obj_set_width(self._title, LV_PCT(100));
    lv_obj_set_height(self._title, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(self._title, kTitlePadTop, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(self._title, kTitlePadBottom, LV_PART_MAIN);
    lv_label_set_text(self._title, i18n::text(i18n::TextId::PresetTitle));
    lv_obj_set_style_text_color(self._title, pal.text_primary, LV_PART_MAIN);
    lv_obj_set_style_text_align(self._title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(self._title, kFontTitle, LV_PART_MAIN);
    lv_obj_clear_flag(self._title, LV_OBJ_FLAG_CLICKABLE);
}

void LvglPresetScreen::create_preset_row(LvglPresetScreen& self, uint8_t slot, const YoRadioPalette& pal) {
    self._rows[slot] = lv_obj_create(self._screen);
    if (!self._rows[slot]) return;

    lv_obj_set_width(self._rows[slot], LV_PCT(100));
    lv_obj_set_height(self._rows[slot], kRowH);
    lv_obj_set_style_bg_color(self._rows[slot], pal.panel_background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(self._rows[slot], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(self._rows[slot], pal.panel_border, LV_PART_MAIN);
    lv_obj_set_style_border_width(self._rows[slot], kRowBorder, LV_PART_MAIN);
    lv_obj_set_style_border_opa(self._rows[slot], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(self._rows[slot], kRowRadius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(self._rows[slot], pal.list_row_selected_bg, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(self._rows[slot], pal.accent_soft, LV_STATE_PRESSED);
    lv_obj_set_style_pad_top(self._rows[slot], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(self._rows[slot], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(self._rows[slot], kRowPadLR, LV_PART_MAIN);
    lv_obj_set_style_pad_right(self._rows[slot], kRowPadLR, LV_PART_MAIN);
    lv_obj_set_style_pad_column(self._rows[slot], kRowColGap, LV_PART_MAIN);
    lv_obj_clear_flag(self._rows[slot], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(self._rows[slot], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(self._rows[slot], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(self._rows[slot], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_user_data(self._rows[slot], reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
    lv_obj_add_event_cb(self._rows[slot], _rowEventCb, LV_EVENT_ALL, &self);

    self._slot_labels[slot] = lv_label_create(self._rows[slot]);
    if (self._slot_labels[slot]) {
        lv_obj_set_width(self._slot_labels[slot], kSlotW);
        lv_obj_set_height(self._slot_labels[slot], LV_SIZE_CONTENT);
        char buf[kSlotNumberBufferSize];
        preset_format_checked(buf, sizeof(buf), "", kFmtSlotNumber,
                              static_cast<int>(slot) + 1);
        lv_label_set_text(self._slot_labels[slot], buf);
        lv_obj_set_style_text_color(self._slot_labels[slot], pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._slot_labels[slot], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(self._slot_labels[slot], kFontSlotNumber, LV_PART_MAIN);
        lv_obj_clear_flag(self._slot_labels[slot], LV_OBJ_FLAG_CLICKABLE);
    }

    self._vdiv_lines[slot] = lv_obj_create(self._rows[slot]);
    if (self._vdiv_lines[slot]) {
        lv_obj_set_width(self._vdiv_lines[slot], kDividerWidth);
        lv_obj_set_height(self._vdiv_lines[slot], kDivH);
        lv_obj_set_style_bg_color(self._vdiv_lines[slot], pal.divider, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(self._vdiv_lines[slot], kDividerOpacity, LV_PART_MAIN);
        lv_obj_set_style_border_width(self._vdiv_lines[slot], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(self._vdiv_lines[slot], 0, LV_PART_MAIN);
        lv_obj_clear_flag(self._vdiv_lines[slot], LV_OBJ_FLAG_CLICKABLE);
    }

    self._num_labels[slot] = lv_label_create(self._rows[slot]);
    if (self._num_labels[slot]) {
        lv_obj_set_width(self._num_labels[slot], kNumW);
        lv_obj_set_height(self._num_labels[slot], LV_SIZE_CONTENT);
        lv_label_set_text(self._num_labels[slot], kStrEmptyText);
        const lv_font_t* num_font = preset_station_number_font();
        if (num_font) {
            lv_obj_set_style_text_font(self._num_labels[slot], num_font, LV_PART_MAIN);
        }
        lv_obj_clear_flag(self._num_labels[slot], LV_OBJ_FLAG_CLICKABLE);
    }

    self._name_labels[slot] = lv_label_create(self._rows[slot]);
    if (self._name_labels[slot]) {
        const lv_coord_t name_line_h = lv_font_get_line_height(kFontStationName);
        lv_obj_set_width(self._name_labels[slot], kNameFlexBaseWidth);
        lv_obj_set_flex_grow(self._name_labels[slot], 1);
        lv_obj_set_height(self._name_labels[slot], name_line_h);
        lv_label_set_long_mode(self._name_labels[slot], LV_LABEL_LONG_DOT);
        lv_label_set_text(self._name_labels[slot], kStrEmptyText);
        lv_obj_set_style_text_font(self._name_labels[slot], kFontStationName, LV_PART_MAIN);
        lv_obj_clear_flag(self._name_labels[slot], LV_OBJ_FLAG_CLICKABLE);
    }
}

void LvglPresetScreen::create_preset_rows(LvglPresetScreen& self, const YoRadioPalette& pal) {
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        create_preset_row(self, slot, pal);
    }
}

void LvglPresetScreen::create_footer(LvglPresetScreen& self, const YoRadioPalette& pal) {
    self._helper_box = lv_obj_create(self._screen);
    if (!self._helper_box) return;

    lv_obj_remove_style_all(self._helper_box); // Preset-specific: reset inherited LVGL theme styles
    wgt_footer_pill::prepare_surface(self._helper_box);
    wgt_footer_pill::apply_palette(self._helper_box, pal);
    lv_obj_set_width(self._helper_box, LV_PCT(100));
    lv_obj_set_height(self._helper_box, kFooterBoxH);
    lv_obj_set_flex_flow(self._helper_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(self._helper_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(self._helper_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(self._helper_box, _helperEventCb, LV_EVENT_CLICKED, &self);

    self._helper = lv_label_create(self._helper_box);
    if (!self._helper) return;

    lv_obj_set_width(self._helper, LV_PCT(100));
    lv_obj_set_flex_grow(self._helper, 1);
    lv_obj_set_style_min_width(self._helper, 0, LV_PART_MAIN);
    lv_label_set_long_mode(self._helper, LV_LABEL_LONG_DOT);
    char helper[kCountdownBufferSize];
    preset_format_checked(helper, sizeof(helper), "",
                          i18n::text(i18n::TextId::PresetCountdownFormat), 15);
    lv_label_set_text(self._helper, helper);
    lv_obj_set_style_text_color(self._helper, pal.text_secondary, LV_PART_MAIN);
    lv_obj_set_style_text_align(self._helper, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(self._helper, kFontFooter, LV_PART_MAIN);
    wgt_footer_pill::make_child_passive(self._helper);
    lv_obj_clear_flag(self._helper, LV_OBJ_FLAG_SCROLLABLE);
}

// ── Lifecycle / Жизненный цикл ────────────────────────────────────────────────

ScreenType LvglPresetScreen::screenType() const { return ScreenType::Temporary; }

void LvglPresetScreen::create() {
    if (_screen) return;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    const lv_coord_t scr_pad  = static_cast<lv_coord_t>(LV_ACTIVE_PROFILE.frame_padding);

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, scr_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, kRowGap, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_title(*this, pal);
    create_preset_rows(*this, pal);
    create_footer(*this, pal);
}

void LvglPresetScreen::enter() {
    _long_press_handled  = false;
    _feedbackActive      = false;
    _lastCountdownSecond = -1;
    _cancelFeedbackTimer();
    (void)preset_store::begin();
    _setHelperDefault();
    _startCountdownTimer();
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

// ── Theme / content pipeline / Тема и контент ─────────────────────────────────

void LvglPresetScreen::_nullHandles() {
    _screen     = nullptr;
    _title      = nullptr;
    _helper_box = nullptr;
    _helper     = nullptr;
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

    if (_helper_box) {
        wgt_footer_pill::apply_palette(_helper_box, pal);
    }

    if (_helper) {
        if (!_feedbackActive) {
            lv_obj_set_style_text_color(_helper, pal.text_secondary, LV_PART_MAIN);
        } else {
            const char* t = lv_label_get_text(_helper);
            const bool err = t &&
                (strcmp(t, i18n::text(i18n::TextId::PresetNoCurrentStation)) == 0 ||
                 strcmp(t, i18n::text(i18n::TextId::PresetSaveFailed)) == 0);
            lv_obj_set_style_text_color(_helper, err ? pal.text_secondary : pal.accent, LV_PART_MAIN);
        }
    }

    for (uint8_t s = 0; s < kSlotCount; ++s) _applyRowColors(s, pal);
}

void LvglPresetScreen::_updateRowContent(uint8_t slot) {
    if (slot >= kSlotCount) return;

    const YoRadioPalette& pal   = yoradio_palette();
    const bool occupied         = preset_store::isOccupied(slot);
    const uint16_t station_num  = occupied ? preset_store::getStationNum(slot) : 0u;
    const bool valid            = occupied && station_list_adapter::is_valid_station_num(station_num);

    if (_num_labels[slot]) {
        if (!occupied) {
            lv_label_set_text(_num_labels[slot], kStrEmptyStationNumber);
        } else {
            char nb[kStationNumberBufferSize];
            preset_format_checked(nb, sizeof(nb), kStrEmptyStationNumber,
                                  kFmtStationNumber, static_cast<unsigned>(station_num));
            lv_label_set_text(_num_labels[slot], nb);
        }
    }

    if (_name_labels[slot]) {
        if (!occupied) {
            lv_label_set_text(_name_labels[slot],
                              i18n::text(i18n::TextId::PresetEmptySlot));
        } else if (!valid) {
            lv_label_set_text(_name_labels[slot],
                              i18n::text(i18n::TextId::PresetUnavailable));
        } else {
            char name_buf[kRowNameBytes];
            if (station_list_adapter::station_name(station_num, name_buf, sizeof(name_buf))) {
                truncate_utf8_in_place(name_buf, kStationNameDisplayMaxBytes);
                char full[kRowNameBytes + kDecoratedNameExtraBytes];
                preset_format_checked(full, sizeof(full), name_buf,
                                      kFmtDecoratedStationName, kBulletPrefixUtf8, name_buf);
                lv_label_set_text(_name_labels[slot], full);
            } else {
                lv_label_set_text(_name_labels[slot],
                                  i18n::text(i18n::TextId::PresetUnavailable));
            }
        }
    }

    _applyRowColors(slot, pal);
}

// ── Timer pipeline / Таймеры ──────────────────────────────────────────────────

void LvglPresetScreen::_updateCountdownHelper() {
    if (!_helper) return;
    const uint32_t rem = temporaryRemainingMs();
    const int16_t secs = (rem > 0u)
        ? static_cast<int16_t>((rem + 999u) / 1000u)
        : static_cast<int16_t>(0);
    if (secs == _lastCountdownSecond) return;
    _lastCountdownSecond = secs;
    char buf[kCountdownBufferSize];
    preset_format_checked(buf, sizeof(buf), "",
                          i18n::text(i18n::TextId::PresetCountdownFormat),
                          static_cast<int>(secs > 0 ? secs : 1));
    lv_label_set_text(_helper, buf);
    lv_obj_set_style_text_color(_helper, yoradio_palette().text_secondary, LV_PART_MAIN);
}

void LvglPresetScreen::_setHelperDefault() {
    _feedbackActive      = false;
    _lastCountdownSecond = -1;
    _updateCountdownHelper();
}

void LvglPresetScreen::_setHelperMessage(const char* text, bool success) {
    if (!_helper || !text) return;
    _feedbackActive = true;
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
    _countdown_timer = lv_timer_create(_countdownTimerCb, kCountdownPeriodMs, this);
}

void LvglPresetScreen::_countdownTimerCb(lv_timer_t* timer) {
    if (!timer) return;
    auto* self = static_cast<LvglPresetScreen*>(timer->user_data);
    if (!self || self->_feedbackActive) return;
    self->_updateCountdownHelper();
}

// ── Row/footer event pipeline / События строк и footer ───────────────────────

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
        dismissActiveTemporary();
    }
}

void LvglPresetScreen::_onRowLongPressed(uint8_t slot) {
    if (slot >= kSlotCount || _long_press_handled) return;
    _long_press_handled = true;
    refreshActiveTemporaryTimeout();

    if (preset_store::saveCurrentStation(slot)) {
        _updateRowContent(slot);
        char msg[kSavedMessageBufferSize];
        preset_format_checked(msg, sizeof(msg), "",
                              i18n::text(i18n::TextId::PresetSavedFormat),
                              static_cast<unsigned>(slot) + 1u);
        _setHelperMessage(msg, /*success=*/true);
        refreshActiveTemporaryTimeout();
        _scheduleHelperRestore(kHelperRestoreMsSuccess);
        return;
    }

    const uint16_t cur = config.lastStation();
    if (!station_list_adapter::is_valid_station_num(cur)) {
        _setHelperMessage(i18n::text(i18n::TextId::PresetNoCurrentStation),
                          /*success=*/false);
    } else {
        _setHelperMessage(i18n::text(i18n::TextId::PresetSaveFailed),
                          /*success=*/false);
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

void LvglPresetScreen::_helperEventCb(lv_event_t* e) {
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LvglPresetScreen* const self = self_from_event(e);
    if (!self) return;
    // PageChain ownership: ignore stale callbacks after destroy/dismiss (dismissTemporary is idempotent).
    // Владение PageChain: игнор stale callback после destroy/dismiss (dismissTemporary идемпотентен).
    if (!isTemporaryActiveFor(self)) return;
    dismissActiveTemporary();
}

} // namespace lvgl_ui
