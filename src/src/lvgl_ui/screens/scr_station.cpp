/*
 * LvglStationPage — Station page chrome + compile-time selected list renderer (STATIONPAGED-2).
 * Страница Station: chrome + renderer списка, выбранный на этапе компиляции (STATIONPAGED-2).
 *
 * ILvglScreen lifecycle: create → enter → update → exit → destroy (DspTask only).
 */

// Author: Witaliy76 - https://github.com/Witaliy76
#include "scr_station.h"

#include <cstdio>
#include <cstring>

#include "Arduino.h"
#include <cstdint>

#include "lvgl.h"
#include "../../core/options.h"
#include "../../core/source_catalog.h"
#include "../adapters/station_list_adapter.h"
#include "../control_glyph_utf8.h"
#include "../fonts/lv_fonts.h"
#include "../font_provider.h"
#include "../../i18n/i18n.h"
#include "../lv_page_chain.h"
#include "../lvgl_ui.h"
#include "../widgets/wgt_footer_pill.h"
#include "../profiles/resolved_ui_metrics.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

namespace {

static constexpr char kStrCountPlaceholder[]      = "-- / --";
// U+203A is in the factory text face used by the page title.
// U+203A есть в заводском текстовом шрифте заголовка.
static constexpr char kHeaderChevron[] = " \u203A";

static const char* const kIconHintClick = station_glyph_utf8_hand_click();

static const void* font_title() { return FontProvider::text(20); }
static const void* font_count() { return FontProvider::text(18); }
static const void* font_hint_text() { return FontProvider::text(16); }
static const void* font_hint_icon() { return FontProvider::icon(20); }

constexpr size_t kCountBufferSize = 24;

static constexpr lv_coord_t kRootRowGap = 8;

static constexpr lv_coord_t kHintPadHorizontal = 16;
static constexpr lv_coord_t kHintPadVertical   = 10;
static constexpr lv_coord_t kHintRowGap        = 10;
static constexpr lv_coord_t kHintMinTextWidth  = 80;
static constexpr lv_coord_t kHeaderPressRadius = 8;

static void station_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

// Localized count formats must either fit completely or use one complete fallback.
// Локализованный счётчик выводится только целиком, иначе используется полный fallback.
template <typename... Args>
static bool station_format_checked(char* out, size_t cap, const char* fallback,
                                   const char* format, Args... args) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (format) {
        const int written = snprintf(out, cap, format, args...);
        if (written >= 0 && static_cast<size_t>(written) < cap) return true;
    }
    const char* safe = fallback ? fallback : "";
    const size_t bytes = strlen(safe) + 1u;
    if (bytes <= cap) memcpy(out, safe, bytes);
    return false;
}

// Title plus chevron in the existing label. The count stays a separate right-side label.
// Название и шеврон в существующей надписи. Счётчик остаётся отдельной правой меткой.
static void format_header_title(char* out, size_t cap, const char* name) {
    if (!out || cap == 0u) return;
    out[0] = '\0';
    const char* safe = name ? name : "";
    const int written = snprintf(out, cap, "%s%s", safe, kHeaderChevron);
    if (written < 0 || static_cast<size_t>(written) >= cap) {
        out[0] = '\0';
    }
}

// Interactive header only. Sources keeps the same hit target and does not flash.
// Только интерактивный заголовок. На Sources область та же, вспышки нет.
static void apply_header_press(lv_obj_t* header, bool interactive) {
    if (!header) return;
    const YoRadioPalette& pal = yoradio_palette();
    const lv_style_selector_t pressed =
        static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED);
    // Background only: a pressed border would change the header height and the hit area.
    // Dark uses a lighter wash so white title text stays readable on main_chrome_pressed_bg.
    // Только фон: рамка в pressed изменила бы высоту заголовка и область нажатия.
    // На Dark заливка слабее, чтобы белый заголовок читался на main_chrome_pressed_bg.
    lv_opa_t opa = LV_OPA_COVER;
    if (!interactive) {
        opa = LV_OPA_TRANSP;
    } else if (yoradio_theme_active_preset() == ThemePreset::Dark) {
        opa = LV_OPA_40;
    }
    lv_obj_set_style_radius(header, kHeaderPressRadius, pressed);
    lv_obj_set_style_bg_color(header, pal.main_chrome_pressed_bg, pressed);
    lv_obj_set_style_bg_opa(header, opa, pressed);
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

void LvglStationPage::create_status_chrome(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    wgt_status_line::create(self._screen, self._status_line);
    if (!self._status_line.root) return;
    add_thin_divider(self._screen, pal);
}

void LvglStationPage::create_header(LvglStationPage& self, const YoRadioPalette& pal) {
    if (!self._screen) return;
    lv_obj_t* header = lv_obj_create(self._screen);
    if (!header) return;
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_transparent(header);

    self._header = header;
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(header, _headerClickedEvt, LV_EVENT_CLICKED, &self);

    self._lbl_title = lv_label_create(header);
    if (self._lbl_title) {
        lv_label_set_text(self._lbl_title, i18n::text(i18n::TextId::StationTitle));
        station_set_font(self._lbl_title, font_title());
        lv_obj_set_style_text_color(self._lbl_title, pal.text_primary, LV_PART_MAIN);
        // The header owns the press. Children must not take LV_STATE_PRESSED away from it.
        // Нажатие принадлежит заголовку. Дочерние объекты не забирают у него LV_STATE_PRESSED.
        lv_obj_clear_flag(self._lbl_title, LV_OBJ_FLAG_CLICKABLE);
    }

    self._lbl_count = lv_label_create(header);
    if (self._lbl_count) {
        lv_label_set_text(self._lbl_count, kStrCountPlaceholder);
        station_set_font(self._lbl_count, font_count());
        lv_obj_set_style_text_color(self._lbl_count, pal.text_secondary, LV_PART_MAIN);
        lv_obj_clear_flag(self._lbl_count, LV_OBJ_FLAG_CLICKABLE);
    }
    apply_header_press(header, false);
}

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
    wgt_footer_pill::prepare_surface(self._hint_area);
    wgt_footer_pill::apply_palette(self._hint_area, pal);
    lv_obj_add_flag(self._hint_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(self._hint_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(self._hint_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(self._hint_area, _hintAreaClickedEvt, LV_EVENT_CLICKED, &self);

    lv_obj_t* hint_row = lv_obj_create(self._hint_area);
    if (!hint_row) return;
    lv_obj_set_width(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_height(hint_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hint_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hint_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hint_row, kHintRowGap, LV_PART_MAIN);
    style_transparent(hint_row);
    wgt_footer_pill::make_child_passive(hint_row);

    self._lbl_hint_icon = lv_label_create(hint_row);
    if (self._lbl_hint_icon) {
        lv_label_set_text(self._lbl_hint_icon, kIconHintClick);
        station_set_font(self._lbl_hint_icon, font_hint_icon());
        lv_obj_set_style_text_color(self._lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        wgt_footer_pill::make_child_passive(self._lbl_hint_icon);
    }

    self._lbl_hint_text = lv_label_create(hint_row);
    if (self._lbl_hint_text) {
        lv_label_set_text(self._lbl_hint_text, i18n::text(i18n::TextId::StationFooter));
        station_set_font(self._lbl_hint_text, font_hint_text());
        lv_obj_set_style_text_color(self._lbl_hint_text, pal.text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_align(self._lbl_hint_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(self._lbl_hint_text, LV_LABEL_LONG_CLIP);
        const lv_coord_t max_w = static_cast<lv_coord_t>(
            kResolvedUiMetrics.viewport_width
            - 2u * static_cast<uint32_t>(kResolvedUiMetrics.factory.frame_padding)
            - 32u - 20u - 30u);
        if (max_w > kHintMinTextWidth) {
            lv_obj_set_width(self._lbl_hint_text, max_w);
        }
        wgt_footer_pill::make_child_passive(self._lbl_hint_text);
    }
}

void LvglStationPage::create() {
    if (_screen) return;

    const YoRadioPalette& pal = yoradio_palette();

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    lv_obj_set_flex_flow(_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(
        _screen, kResolvedUiMetrics.factory.frame_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_screen, kRootRowGap, LV_PART_MAIN);
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_status_chrome(*this, pal);

    if (!_status_line.root) {
        lv_obj_del(_screen);
        _screen = nullptr;
        return;
    }

    create_header(*this, pal);

    if (station_list_active::create(_list, _screen, pal)) {
        create_hint_band(*this, pal);
        installCarouselGesturesOnPageRoot(_screen);
        // Footer affects flex height — finalize layout before rows_per_page / first page fill.
        // Footer влияет на flex-высоту — layout до расчёта rows_per_page и первой страницы.
        lv_obj_update_layout(_screen);
    } else {
        create_hint_band(*this, pal);
        installCarouselGesturesOnPageRoot(_screen);
    }
}

void LvglStationPage::armBrowser(yoradio::source::SourceId id) {
    if (id == yoradio::source::SourceId::Sd) _armed = ArmedBrowser::Sd;
    else if (id == yoradio::source::SourceId::Radio) _armed = ArmedBrowser::Radio;
    else _armed = ArmedBrowser::None;
}

void LvglStationPage::cancelArmedBrowser() {
    _armed = ArmedBrowser::None;
}

void LvglStationPage::enter() {
    if (_screen) lv_obj_update_layout(_screen);
    const ArmedBrowser armed = _armed;
    _armed = ArmedBrowser::None;
    if (armed == ArmedBrowser::Sd) _showSd(false, false);
    else if (armed == ArmedBrowser::Radio) _showRadio(false);
    else _applyEntry();
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
}

void LvglStationPage::update() {
    if (!_screen || !_status_line.root) return;
    wgt_status_line::update(_status_line);
    if (_level == BrowserLevel::Sd && _list.list_area) {
        _pollOpenFolder(true);
        return;
    }
    if (_level != BrowserLevel::Radio || !_list.list_area) {
        station_list_active::refreshCurrentStationVisuals(_list);
        return;
    }
    const auto result = station_list_active::refreshOnActivate(_list);
    if (result == station_list_active::RefreshOnActivateResult::Rebuilt) {
        _syncChrome();
    }
    station_list_active::refreshCurrentStationVisuals(_list);
}

void LvglStationPage::refreshCurrentStationVisuals() {
    if (!_screen) return;
    _syncChrome();
    station_list_active::refreshCurrentStationVisuals(_list);
}

namespace {

bool append_text(char* out, size_t cap, size_t& used, const char* text) {
    if (!out || cap == 0u || !text) return false;
    const size_t len = strlen(text);
    if (used + len + 1u > cap) return false;
    memcpy(out + used, text, len);
    used += len;
    out[used] = '\0';
    return true;
}

void truncate_utf8(char* s, size_t max_bytes) {
    if (!s) return;
    const size_t len = strlen(s);
    if (len <= max_bytes) return;
    if (max_bytes == 0u) {
        s[0] = '\0';
        return;
    }
    size_t cut = max_bytes;
    while (cut > 0u && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) --cut;
    if (cut == 0u) cut = max_bytes;
    s[cut] = '\0';
}

} // namespace

void LvglStationPage::serviceSdPending() {
    if (!_screen || _level != BrowserLevel::Sd || !_list.list_area) return;
    if (browserSdStatus() != BrowserStatus::Loading) return;
    // Idle list refresh stays on the 1 Hz path. This turn only completes a pending open.
    // Спокойный список остаётся на пути 1 Гц. Здесь завершается только ждущее открытие.
    _pollOpenFolder(false);
}

void LvglStationPage::_pollOpenFolder(bool allow_idle_refresh) {
    browserSdPoll(_request_id);
    if (browserSdShouldOpenRoot()) {
        _sd_jump = false;
        _sd_loading_row = false;
        browserSdOpen(_nextRequest(), "/");
        _redisplaySd(false);
        return;
    }
    const BrowserStatus status = browserSdStatus();
    if (_sd_jump && status != BrowserStatus::Loading) {
        _sd_loading_row = false;
        if (status == BrowserStatus::Ready) _list.jump_to_sounding = true;
        _sd_jump = false;
        station_list_active::populate(_list);
        _syncChrome();
        station_list_active::refreshCurrentStationVisuals(_list);
        return;
    }
    if (status == BrowserStatus::Loading) {
        if (!allow_idle_refresh) return;
    } else if (_sd_loading_row) {
        // Loading used row 1. A finished folder lands on ".." again unless a sounding jump wins.
        // Загрузка занимала строку 1. Готовая папка снова садится на «..», если нет прыжка к звучащему.
        _list.focus_row = 0u;
        _list.page_index = 0u;
        _sd_loading_row = false;
    }
    const auto result = station_list_active::refreshOnActivate(_list);
    if (result == station_list_active::RefreshOnActivateResult::Rebuilt) _syncChrome();
    station_list_active::refreshCurrentStationVisuals(_list);
}

void LvglStationPage::_releaseProvider() {
    if (_provider && _provider->release) _provider->release(_request_id);
    _provider = nullptr;
    _sd_jump = false;
    _sd_loading_row = false;
}

uint32_t LvglStationPage::_nextRequest() {
    _request_id = (_request_id == UINT32_MAX) ? 1u : _request_id + 1u;
    if (_request_id == 0u) _request_id = 1u;
    return _request_id;
}

void LvglStationPage::_bindFeed(const station_list_active::BrowserRowFeed& feed,
                                bool jump_to_sounding) {
    _list.jump_to_sounding = jump_to_sounding;
    station_list_active::setFeed(_list, feed);
    station_list_active::populate(_list);
    _syncChrome();
}

void LvglStationPage::_showSources() {
    _releaseProvider();
    _level = BrowserLevel::Sources;
    station_list_active::BrowserRowFeed feed{};
    feed.count = _sourcesCount;
    feed.page_text = _sourcesPageText;
    feed.sounding_row = _sourcesSounding;
    feed.on_tap = _sourcesTap;
    feed.revision = _sourcesRevision;
    feed.ctx = this;
    _bindFeed(feed, false);
}

void LvglStationPage::_showRadio(bool jump_to_sounding) {
    _releaseProvider();
    const BrowserProvider* provider = browserProvider(yoradio::source::SourceId::Radio);
    if (!provider || !provider->implemented) {
        _showSources();
        return;
    }
    _level = BrowserLevel::Radio;
    _provider = provider;
    _request_id = (_request_id == UINT32_MAX) ? 1u : _request_id + 1u;
    if (_provider->prepare) {
        (void)_provider->prepare(_request_id, _generation);
    }
    station_list_active::BrowserRowFeed feed{};
    feed.count = _radioCount;
    feed.page_text = _radioPageText;
    feed.sounding_row = _radioSounding;
    feed.on_tap = _radioTap;
    feed.press_focus = _radioPressFocus;
    feed.revision = _radioRevision;
    feed.ctx = this;
    _bindFeed(feed, jump_to_sounding);
}

void LvglStationPage::_showSd(bool playing_folder, bool jump_to_sounding) {
    // Disabled SD stays on the source list. The directory backend is not opened.
    // Выключенная SD остаётся в списке источников. Каталог карты не открывается.
    if (!yoradio::kResolvedStorageConfig.enabled) {
        _showSources();
        return;
    }
    const BrowserProvider* provider = browserProvider(yoradio::source::SourceId::Sd);
    if (!provider || !provider->implemented) {
        _showSources();
        return;
    }
    _releaseProvider();
    _level = BrowserLevel::Sd;
    _provider = provider;
    _sd_jump = jump_to_sounding;
    const uint32_t request_id = _nextRequest();
    if (playing_folder) browserSdOpenPlaying(request_id);
    else browserSdOpen(request_id, "/");
    _redisplaySd(false);
}

void LvglStationPage::_redisplaySd(bool jump_to_sounding) {
    _list.page_index = 0u;
    const bool loading = browserSdStatus() == BrowserStatus::Loading;
    // Row 1 is the Loading line, drawn with the same focus chrome as a selected folder.
    // Строка 1 — Loading, тем же фокусом, что и выбранная папка.
    _sd_loading_row = loading;
    _list.focus_row = loading ? 1u : 0u;
    station_list_active::BrowserRowFeed feed{};
    feed.count = _sdCount;
    feed.page_text = _sdPageText;
    feed.sounding_row = _sdSounding;
    feed.on_tap = _sdTap;
    feed.press_focus = _sdPressFocus;
    feed.revision = _sdRevision;
    feed.ctx = this;
    _bindFeed(feed, jump_to_sounding);
}

void LvglStationPage::_applyEntry() {
    const BrowserEntry entry = browserEntryFromPlayer();
    if (entry == BrowserEntry::Radio) _showRadio(true);
    else if (entry == BrowserEntry::Sd) _showSd(true, true);
    else _showSources();
}

const char* sdStatusLine(BrowserStatus status) {
    switch (status) {
    case BrowserStatus::Loading:
        return i18n::text(i18n::TextId::BrowserSdLoading);
    case BrowserStatus::Empty:
        return i18n::text(i18n::TextId::BrowserSdEmpty);
    case BrowserStatus::Unavailable:
        return i18n::text(i18n::TextId::BrowserSdUnavailable);
    case BrowserStatus::ResourceLimit:
        return i18n::text(i18n::TextId::BrowserSdResourceLimit);
    case BrowserStatus::Error:
        return i18n::text(i18n::TextId::BrowserSdError);
    default:
        return "";
    }
}

const char* sdPlayHint() {
    using yoradio::playback::PathPlayResult;
    switch (browserSdPlayResult()) {
    case PathPlayResult::None:
    case PathPlayResult::Accepted: return i18n::text(i18n::TextId::StationFooter);
    case PathPlayResult::Busy:
    case PathPlayResult::NotReady: return i18n::text(i18n::TextId::BrowserPlayBusy);
    case PathPlayResult::StopFailed: return i18n::text(i18n::TextId::BrowserPlayStopFailed);
    case PathPlayResult::NoMedia:
    case PathPlayResult::StaleMedia:
    case PathPlayResult::StaleEpoch:
    case PathPlayResult::SourceBusy: return i18n::text(i18n::TextId::BrowserPlayRetry);
    default: return i18n::text(i18n::TextId::BrowserPlayFailed);
    }
}

void LvglStationPage::_syncChrome() {
    if (_lbl_hint_text) lv_label_set_text(_lbl_hint_text, _level == BrowserLevel::Sd
        ? sdPlayHint() : i18n::text(i18n::TextId::StationFooter));
    apply_header_press(_header, _level != BrowserLevel::Sources);
    if (_lbl_title) {
        if (_level == BrowserLevel::Sources) {
            lv_label_set_text(_lbl_title, i18n::text(i18n::TextId::BrowserSourcesTitle));
        } else {
            char title[48];
            // The station header is not the source-row name.
            // Заголовок списка станций — не название строки источника.
            const char* name = (_level == BrowserLevel::Sd)
                ? i18n::text(i18n::TextId::BrowserSourceSd)
                : i18n::text(i18n::TextId::StationTitle);
            format_header_title(title, sizeof(title), name);
            lv_label_set_text(_lbl_title, title);
        }
    }
    if (!_lbl_count) return;
    char count_buf[kCountBufferSize];
    if (_level == BrowserLevel::Sd) {
        const BrowserStatus status = browserSdStatus();
        if (status != BrowserStatus::Ready && status != BrowserStatus::Empty) {
            lv_label_set_text(_lbl_count, kStrCountPlaceholder);
            return;
        }
        const uint16_t total = browserSdCount();
        const int32_t sounding = browserSdSoundingIndex();
        if (total == 0u || sounding < 0) {
            station_format_checked(
                count_buf, sizeof(count_buf), kStrCountPlaceholder,
                i18n::text(i18n::TextId::StationCountUnknownTotalFormat),
                static_cast<unsigned>(total));
        } else {
            station_format_checked(
                count_buf, sizeof(count_buf), kStrCountPlaceholder,
                i18n::text(i18n::TextId::StationCountCurrentTotalFormat),
                static_cast<unsigned>(sounding) + 1u, static_cast<unsigned>(total));
        }
        lv_label_set_text(_lbl_count, count_buf);
        return;
    }
    if (_level == BrowserLevel::Sources) {
        station_format_checked(
            count_buf, sizeof(count_buf), kStrCountPlaceholder,
            i18n::text(i18n::TextId::StationCountUnknownTotalFormat),
            static_cast<unsigned>(yoradio::source::implementedCount()));
        lv_label_set_text(_lbl_count, count_buf);
        return;
    }
    const uint16_t total = (_provider && _provider->count) ? _provider->count() : 0u;
    const int32_t content = browserRadioContentIndex();
    if (total == 0u || content < 0) {
        station_format_checked(
            count_buf, sizeof(count_buf), kStrCountPlaceholder,
            i18n::text(i18n::TextId::StationCountUnknownTotalFormat),
            static_cast<unsigned>(total));
    } else {
        station_format_checked(
            count_buf, sizeof(count_buf), kStrCountPlaceholder,
            i18n::text(i18n::TextId::StationCountCurrentTotalFormat),
            static_cast<unsigned>(content) + 1u, static_cast<unsigned>(total));
    }
    lv_label_set_text(_lbl_count, count_buf);
}

uint16_t LvglStationPage::_sourcesCount(void*) {
    return static_cast<uint16_t>(yoradio::source::implementedCount());
}

bool LvglStationPage::_sourcesPageText(void*, uint16_t first, uint16_t last,
                                       char* out, size_t cap, size_t name_limit) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    size_t used = 0;
    if (last >= yoradio::source::implementedCount()) return false;
    // U+203A is in the factory text face. It sits in the same label, right after the name.
    // U+203A есть в заводском текстовом шрифте. Он в той же строке, сразу после названия.
    static constexpr char kChevron[] = " \u203A";
    const size_t chevron_bytes = strlen(kChevron);
    for (uint16_t row = first; row <= last; ++row) {
        const yoradio::source::SourceEntry* entry = yoradio::source::implementedAt(row);
        if (!entry) return false;
        const yoradio::source::SourceId id = entry->id;
        const char* localized = entry->title;
        if (id == yoradio::source::SourceId::Radio) {
            localized = i18n::text(i18n::TextId::BrowserSourceRadio);
        } else if (id == yoradio::source::SourceId::Sd) {
            localized = i18n::text(i18n::TextId::BrowserSourceSd);
        }
        char title[64];
        snprintf(title, sizeof(title), "%s", localized ? localized : "");
        const size_t name_budget = name_limit > chevron_bytes ? (name_limit - chevron_bytes) : 0u;
        truncate_utf8(title, name_budget);
        if (!append_text(out, cap, used, title)
            || !append_text(out, cap, used, kChevron)
            || !append_text(out, cap, used, "\n")) {
            return false;
        }
    }
    return true;
}

int32_t LvglStationPage::_sourcesSounding(void*) {
    return browserSoundingCatalogRow();
}

void LvglStationPage::_sourcesTap(void* ctx, uint16_t row) {
    auto* self = static_cast<LvglStationPage*>(ctx);
    const yoradio::source::SourceEntry* entry = yoradio::source::implementedAt(row);
    if (!self || !entry) return;
    const yoradio::source::SourceId id = entry->id;
    if (id == yoradio::source::SourceId::Radio) self->_showRadio(true);
    else if (id == yoradio::source::SourceId::Sd) self->_showSd(false, false);
}

uint32_t LvglStationPage::_sourcesRevision(void*) {
    return 0u;
}

uint16_t LvglStationPage::_radioCount(void* ctx) {
    auto* self = static_cast<LvglStationPage*>(ctx);
    const uint16_t stations = (self && self->_provider && self->_provider->count)
                                  ? self->_provider->count()
                                  : 0u;
    return static_cast<uint16_t>(stations + (stations < 65535u ? 1u : 0u));
}

bool LvglStationPage::_radioPageText(void* ctx, uint16_t first, uint16_t last,
                                     char* out, size_t cap, size_t name_limit) {
    if (!out || cap == 0u || last < first) return false;
    out[0] = '\0';
    size_t used = 0;
    uint16_t station_first = first;
    if (first == 0u) {
        if (!append_text(out, cap, used, "..\n")) return false;
        if (last == 0u) {
            auto* self = static_cast<LvglStationPage*>(ctx);
            const uint16_t stations = (self && self->_provider && self->_provider->count)
                                          ? self->_provider->count()
                                          : 0u;
            if (stations == 0u) {
                return append_text(out, cap, used, i18n::text(i18n::TextId::StationEmptyList));
            }
            return true;
        }
        station_first = 1u;
    }
    return station_list_adapter::station_page_text(
        station_first, last, out + used, cap - used, name_limit);
}

int32_t LvglStationPage::_radioSounding(void*) {
    const int32_t content = browserRadioContentIndex();
    if (content < 0) return -1;
    return content + 1;
}

void LvglStationPage::_radioTap(void* ctx, uint16_t row) {
    auto* self = static_cast<LvglStationPage*>(ctx);
    if (!self) return;
    if (row == 0u) {
        self->_showSources();
        return;
    }
    if (!self->_provider || !self->_provider->play) return;
    char id[8];
    const int wrote = snprintf(id, sizeof(id), "%u", static_cast<unsigned>(row));
    if (wrote <= 0 || static_cast<size_t>(wrote) >= sizeof(id)) return;
    self->_provider->play(id);
}

bool LvglStationPage::_radioPressFocus(void*, uint16_t row) {
    // Radio ".." uses the same press-time focus as SD "..". Station rows stay unchanged.
    // Radio «..» получает тот же фокус при касании, что и SD «..». Строки станций не меняются.
    return row == 0u;
}

uint16_t LvglStationPage::_sdCount(void*) {
    if (browserSdStatus() == BrowserStatus::Loading) return 2u;
    const uint16_t items = browserSdCount();
    return static_cast<uint16_t>(items + 1u);
}

bool LvglStationPage::_sdPageText(void*, uint16_t first, uint16_t last,
                                  char* out, size_t cap, size_t name_limit) {
    if (!out || cap == 0u || last < first) return false;
    out[0] = '\0';
    size_t used = 0;
    const BrowserStatus status = browserSdStatus();
    if (status == BrowserStatus::Loading) {
        if (first == 0u && !append_text(out, cap, used, "..\n")) return false;
        if (first <= 1u && (last >= 1u || last == 0u)) {
            if (!append_text(out, cap, used, sdStatusLine(status))) return false;
        }
        return used > 0u;
    }
    if (first == 0u) {
        if (!append_text(out, cap, used, "..\n")) return false;
        if (last == 0u) {
            if (status == BrowserStatus::Ready) return true;
            return append_text(out, cap, used, sdStatusLine(status));
        }
    }
    if (status != BrowserStatus::Ready) return used > 0u;
    for (uint16_t row = first; row <= last; ++row) {
        if (row == 0u) continue;
        const uint16_t index = static_cast<uint16_t>(row - 1u);
        char name[96];
        bool directory = false;
        if (!browserSdName(index, name, sizeof(name), &directory)) return false;
        const size_t limit = (directory && name_limit > 0u) ? (name_limit - 1u) : name_limit;
        truncate_utf8(name, limit);
        if (directory && !append_text(out, cap, used, name)) return false;
        if (directory && !append_text(out, cap, used, "/")) return false;
        if (!directory && !append_text(out, cap, used, name)) return false;
        if (!append_text(out, cap, used, "\n")) return false;
    }
    return true;
}

int32_t LvglStationPage::_sdSounding(void*) {
    const int32_t index = browserSdSoundingIndex();
    if (index < 0) return -1;
    return index + 1;
}

void LvglStationPage::_sdTap(void* ctx, uint16_t row) {
    auto* self = static_cast<LvglStationPage*>(ctx);
    if (!self || self->_level != BrowserLevel::Sd) return;
    if (row == 0u) {
        if (browserSdAtRoot()) {
            self->_showSources();
            return;
        }
        const uint32_t previous = self->_request_id;
        if (!browserSdOpenParent(self->_nextRequest())) {
            self->_request_id = previous;
            return;
        }
        self->_sd_jump = false;
        self->_redisplaySd(false);
        return;
    }
    if (browserSdStatus() != BrowserStatus::Ready) return;
    const uint16_t index = static_cast<uint16_t>(row - 1u);
    if (browserSdIsDirectory(index)) {
        const uint32_t previous = self->_request_id;
        if (!browserSdOpenIndex(self->_nextRequest(), index)) {
            self->_request_id = previous;
            return;
        }
        self->_sd_jump = false;
        self->_redisplaySd(false);
        return;
    }
    if (!browserSdPlay(index)) self->_syncChrome();
}

bool LvglStationPage::_sdPressFocus(void*, uint16_t row) {
    if (browserSdStatus() != BrowserStatus::Ready) return false;
    if (row == 0u) return !browserSdAtRoot();
    return browserSdIsDirectory(static_cast<uint16_t>(row - 1u));
}

uint32_t LvglStationPage::_sdRevision(void*) {
    return browserSdRevision();
}

uint32_t LvglStationPage::_radioRevision(void* ctx) {
    auto* self = static_cast<LvglStationPage*>(ctx);
    if (!self || !self->_provider || !self->_provider->revision) return 0u;
    return self->_provider->revision();
}

void LvglStationPage::_headerClickedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (!self || !e || lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (target != self->_header && lv_obj_get_parent(target) != self->_header) return;
    if (self->_level == BrowserLevel::Sources) return;
    self->_showSources();
}

void LvglStationPage::_hintAreaClickedEvt(lv_event_t* e) {
    auto* self = static_cast<LvglStationPage*>(lv_event_get_user_data(e));
    if (self) self->_onHintAreaClicked(e);
}

void LvglStationPage::_onHintAreaClicked(lv_event_t* e) {
    if (!_hint_area || !e) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_event_get_target(e) != _hint_area) return;
    lvgl_ui::goToCarouselPage(PageChain::MAIN_INDEX);
}

void LvglStationPage::exit() {
    _releaseProvider();
}

void LvglStationPage::liveReapplyTheme() {
    if (!_screen) return;

    const YoRadioPalette& pal = yoradio_palette();
    lv_obj_set_style_bg_color(_screen, pal.device_background, LV_PART_MAIN);
    wgt_status_line::reapplyTheme(_status_line);

    if (_lbl_title) lv_obj_set_style_text_color(_lbl_title, pal.text_primary, LV_PART_MAIN);
    if (_lbl_count) lv_obj_set_style_text_color(_lbl_count, pal.text_secondary, LV_PART_MAIN);
    apply_header_press(_header, _level != BrowserLevel::Sources);

    if (_hint_area) {
        wgt_footer_pill::apply_palette(_hint_area, pal);
    }
    if (_lbl_hint_icon) lv_obj_set_style_text_color(_lbl_hint_icon, pal.text_secondary, LV_PART_MAIN);
    if (_lbl_hint_text) lv_obj_set_style_text_color(_lbl_hint_text, pal.text_secondary, LV_PART_MAIN);

    station_reapply_dividers(_screen, pal);
    station_list_active::liveReapplyTheme(_list, pal);

    lv_obj_invalidate(_screen);
}

void LvglStationPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _nullHandles();
}

void LvglStationPage::releaseAfterAutoDelete() {
    _nullHandles();
}

void LvglStationPage::_nullHandles() {
    _releaseProvider();
    station_list_active::releaseAfterTreeDelete(_list);
    _screen = nullptr;
    _status_line = {};
    _header = nullptr;
    _lbl_title = nullptr;
    _lbl_count = nullptr;
    _lbl_hint_icon = nullptr;
    _lbl_hint_text = nullptr;
    _hint_area = nullptr;
    _level = BrowserLevel::Sources;
    _request_id = 0;
    _generation = 0;
    _sd_jump = false;
    _sd_loading_row = false;
}

lv_obj_t* LvglStationPage::screen() {
    return _screen;
}

} // namespace lvgl_ui
