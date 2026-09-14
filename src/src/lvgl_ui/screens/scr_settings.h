// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef SCR_SETTINGS_H
#define SCR_SETTINGS_H

#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// SETTINGSREF-A: forward declaration — builders take const YoRadioPalette& without theme .h here.
// SETTINGSREF-A: forward-декларация — билдеры принимают palette без include темы в .h.
struct YoRadioPalette;

// In-page views inside Settings carousel slot — no extra PageChain page.
// Представления внутри слота Settings — без новой страницы карусели.
enum class SettingsView : uint8_t {
    Main,
    Display,
    // 6.7S6: Music Rail detail — lazy, destroy-on-Back (unlike Display MEM1).
    // 6.7S6: Music Rail detail — lazy, destroy-on-Back (в отличие от Display MEM1).
    MusicRail,
    // FU6 UX: Display -> Scrolling sub-view. Lazy + destroy-on-Back (MusicRail pattern),
    // so the registered preview label dies with the tree. / Ленивая, уничтожается на Back.
    Scrolling,
    // TIMERS detail: RADIO / DEEP SLEEP tabs, lazy + destroy-on-Back.
    // Detail TIMERS: вкладки RADIO / DEEP SLEEP, lazy + destroy-on-Back.
    SleepTimer,
};

// Settings carousel page: main category rows + optional detail views (Display first).
// Страница Settings: главный вид + detail views (сначала Display).
class LvglSettingsPage final : public ILvglScreen {
public:
    // Row widget handles for theme reapply / хэндлы строк для перекраски темы
    struct RowChrome {
        lv_obj_t* hit     = nullptr; // Full-row tap target / зона тапа по всей строке
        lv_obj_t* icon    = nullptr;
        lv_obj_t* label   = nullptr;
        lv_obj_t* value   = nullptr;
        lv_obj_t* chevron = nullptr;
    };

    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;
    void liveReapplyTheme() override;
    void releaseAfterAutoDelete() override;

    // 6.7S2a: Display detail blocks PageChain horizontal swipe on Settings slot.
    // 6.7S2a: Display detail блокирует горизонтальный swipe карусели.
    bool isDisplayDetailActive() const { return _view == SettingsView::Display; }
    // 6.7S6: any Settings detail view blocks carousel swipe until Back.
    // 6.7S6: любой detail Settings блокирует swipe карусели до Back.
    bool isSettingsDetailBlockingCarousel() const {
        return _view == SettingsView::Display || _view == SettingsView::MusicRail ||
               _view == SettingsView::Scrolling || _view == SettingsView::SleepTimer;
    }

private:
    // SETTINGSREF-A: private static layout builders — create() stays orchestration only.
    // SETTINGSREF-A: private static билдеры — create() только оркестрация.
    static bool create_main_structure(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void populate_main_rows(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void build_display_detail(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void build_music_rail_detail(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void build_scrolling_detail(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void build_sleep_timer_detail(LvglSettingsPage& self, const YoRadioPalette& pal);

    // 6.7S-MEM1: Display detail tree — lazy once per Settings lifecycle.
    // 6.7S-MEM1: дерево Display detail — лениво один раз за lifecycle Settings.
    bool _ensureDisplayView();
    // 6.7S6: Music Rail detail — lazy per visit, destroyed on Back.
    // 6.7S6: Music Rail detail — lazy при входе, уничтожается на Back.
    bool _ensureMusicRailView();
    void _destroyMusicRailView();
    // FU6 UX: Scrolling sub-view — same lazy/destroy-on-Back contract as Music Rail.
    bool _ensureScrollingView();
    void _destroyScrollingView();
    // Sleep Timer detail — same lazy/destroy-on-Back contract as Music Rail.
    bool _ensureSleepTimerView();
    void _destroySleepTimerView();
    void _nullHandles();
    void _applyThemeColors();
    void _showView(SettingsView view);
    void _syncMainRowValues();
    void _syncDisplayValues();
    void _syncMusicRailValues();
    void _syncScrollingValues();
    void _syncSleepTimerValues();
    void _updateBrightnessLabels(uint8_t pct);
    void _updateDimLevelLabels(uint8_t pct);
    void _updateScrollSpeedLabel(uint8_t px_per_sec);
    void _updateScrollDelayLabel(uint8_t sec);
    void _handleTimerAdjustEvent(lv_event_t* e);
    void _handleTimerSwitchEvent(lv_event_t* e, uint8_t event_index);
    void _adjustTimerField(uint8_t field_index, int8_t direction, uint8_t step);
    void _persistTimerDraft(uint8_t event_index);
    void _finishTimerEditing(bool persist);
    void _loadTimerTabValues(bool force);
    void _updateTimerLabels();
    void _syncDimLevelSliderRange(bool persist_clamp);
    uint8_t _normalBrightnessForDimUi() const;
    void _applySliderTheme(const YoRadioPalette& pal);
    void _applyAutodimRowTreatment(const YoRadioPalette& pal);
    void _applyMusicRailProfileRowTreatment(const YoRadioPalette& pal);
    void _applyTimersTheme(const YoRadioPalette& pal);
    void _applyTimerInteractionState();

    static void footerClickedEvt(lv_event_t* e);
    static void displayRowClickedEvt(lv_event_t* e);
    static void displayBackClickedEvt(lv_event_t* e);
    static void sleepTimerRowClickedEvt(lv_event_t* e);
    static void sleepTimerBackClickedEvt(lv_event_t* e);
    static void timersRadioTabClickedEvt(lv_event_t* e);
    static void timersDeepSleepTabClickedEvt(lv_event_t* e);
    static void timerAdjustButtonEvt(lv_event_t* e);
    static void timerEvent1SwitchEvt(lv_event_t* e);
    static void timerEvent2SwitchEvt(lv_event_t* e);
    static void timerPrimaryClickedEvt(lv_event_t* e);
    static void enterDeepSleepNowClickedEvt(lv_event_t* e);
    static void themeRowClickedEvt(lv_event_t* e);
    static void brightnessSliderEvt(lv_event_t* e);
    static void autodimRowClickedEvt(lv_event_t* e);
    static void performanceMonitorRowClickedEvt(lv_event_t* e);
    static void dimAfterRowClickedEvt(lv_event_t* e);
    static void dimLevelSliderEvt(lv_event_t* e);
    static void scrollingRowClickedEvt(lv_event_t* e);
    static void scrollingBackClickedEvt(lv_event_t* e);
    static void scrollSpeedSliderEvt(lv_event_t* e);
    static void scrollTypeRowClickedEvt(lv_event_t* e);
    static void scrollDelaySliderEvt(lv_event_t* e);
    static void wifiRowClickedEvt(lv_event_t* e);
    static void musicRowClickedEvt(lv_event_t* e);
    static void musicBackClickedEvt(lv_event_t* e);
    static void musicPresenceRailClickedEvt(lv_event_t* e);
    static void musicProfileRowClickedEvt(lv_event_t* e);
    static void resumeOnStartupRowClickedEvt(lv_event_t* e);

    SettingsView _view = SettingsView::Main;
    bool         _brightness_drag_active = false;
    bool         _dim_level_drag_active  = false;
    bool         _scroll_speed_drag_active = false;
    bool         _scroll_delay_drag_active = false;
    bool         _timers_deep_sleep_tab = false;
    bool         _timer_syncing_controls = false;
    bool         _timer_event_enabled[2] = {false, false};
    bool         _timer_edit_dirty[2] = {false, false};
    uint8_t      _timer_repeat_count[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint16_t     _timer_event1_draft = 0;
    uint16_t     _timer_event2_draft = 0;

    lv_obj_t* _screen           = nullptr;
    wgt_status_line::Instance   _status_line{};
    lv_obj_t* _view_main        = nullptr;
    lv_obj_t* _cont_content     = nullptr;
    lv_obj_t* _footer_area      = nullptr;
    lv_obj_t* _lbl_footer       = nullptr;
    lv_obj_t* _view_music       = nullptr;
    lv_obj_t* _music_back_hit   = nullptr;
    lv_obj_t* _music_header_icon = nullptr;
    lv_obj_t* _lbl_music_header  = nullptr;
    lv_obj_t* _cont_music_content = nullptr;
    RowChrome   _row_rail_enabled{};
    RowChrome   _row_rail_profile{};
    lv_obj_t* _view_display     = nullptr;
    lv_obj_t* _display_back_hit = nullptr;
    lv_obj_t* _display_header_icon = nullptr;
    lv_obj_t* _lbl_display_header  = nullptr;
    lv_obj_t* _cont_display_content = nullptr;
    lv_obj_t* _lbl_brightness_title = nullptr;
    lv_obj_t* _brightness_slider    = nullptr;
    lv_obj_t* _lbl_brightness_value = nullptr;
    lv_obj_t* _lbl_dim_level_title  = nullptr;
    lv_obj_t* _dim_level_slider     = nullptr;
    lv_obj_t* _lbl_dim_level_value  = nullptr;
    // FU6 UX: Scrolling sub-view tree / Поддерево подстраницы «Scrolling»
    lv_obj_t* _view_scrolling         = nullptr;
    lv_obj_t* _scrolling_back_hit     = nullptr;
    lv_obj_t* _scrolling_header_icon  = nullptr;
    lv_obj_t* _lbl_scrolling_header   = nullptr;
    lv_obj_t* _cont_scrolling_content = nullptr;
    lv_obj_t* _lbl_scroll_speed_title = nullptr;
    lv_obj_t* _scroll_speed_slider    = nullptr;
    lv_obj_t* _lbl_scroll_speed_value = nullptr;
    lv_obj_t* _lbl_scroll_delay_title = nullptr;
    lv_obj_t* _scroll_delay_slider    = nullptr;
    lv_obj_t* _lbl_scroll_delay_value = nullptr;
    lv_obj_t* _lbl_preview_caption    = nullptr;
    lv_obj_t* _lbl_scroll_preview     = nullptr;
    // TIMERS detail sub-view tree / Поддерево подстраницы TIMERS
    lv_obj_t* _view_sleep_timer          = nullptr;
    lv_obj_t* _sleep_timer_back_hit      = nullptr;
    lv_obj_t* _sleep_timer_header_icon   = nullptr;
    lv_obj_t* _lbl_sleep_timer_header    = nullptr;
    lv_obj_t* _cont_sleep_timer_content  = nullptr;
    lv_obj_t* _timer_tab_buttons[2]      = {nullptr, nullptr};
    lv_obj_t* _timer_tab_labels[2]       = {nullptr, nullptr};
    lv_obj_t* _timer_event_cards[2]      = {nullptr, nullptr};
    lv_obj_t* _timer_event_titles[2]     = {nullptr, nullptr};
    lv_obj_t* _timer_at_labels[2]        = {nullptr, nullptr};
    lv_obj_t* _timer_switches[2]         = {nullptr, nullptr};
    lv_obj_t* _timer_switch_labels[2]    = {nullptr, nullptr};
    // Fields: [event1 hours, event1 minutes, event2 hours, event2 minutes].
    // Step buttons: [field0 -, field0 +, field1 -, field1 +, ...].
    lv_obj_t* _timer_step_buttons[8]     = {nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* _timer_step_labels[8]      = {nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* _timer_captions[4]         = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* _timer_value_labels[4]     = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* _lbl_timer_state           = nullptr;
    lv_obj_t* _lbl_timer_hint            = nullptr;
    lv_obj_t* _btn_timer_primary         = nullptr;
    lv_obj_t* _lbl_timer_primary         = nullptr;
    lv_obj_t* _btn_deep_sleep_now        = nullptr;
    lv_obj_t* _lbl_deep_sleep_now        = nullptr;
    RowChrome   _row_display{};
    RowChrome   _row_music{};
    RowChrome   _row_resume_startup{};
    RowChrome   _row_sleep_timer{};  // TIMERS category row on Main (opens the detail view)
    RowChrome   _row_wifi{};
    RowChrome   _row_theme{};
    RowChrome   _row_perf_monitor{};
    RowChrome   _row_autodim{};
    RowChrome   _row_dim_after{};
    RowChrome   _row_scrolling{};    // SCROLLING > row on the Display page
    RowChrome   _row_scroll_type{};  // SCROLL TYPE row inside the sub-view
};

} // namespace lvgl_ui

#endif
