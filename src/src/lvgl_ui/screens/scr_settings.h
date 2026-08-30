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
        return _view == SettingsView::Display || _view == SettingsView::MusicRail;
    }

private:
    // SETTINGSREF-A: private static layout builders — create() stays orchestration only.
    // SETTINGSREF-A: private static билдеры — create() только оркестрация.
    static bool create_main_structure(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void populate_main_rows(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void build_display_detail(LvglSettingsPage& self, const YoRadioPalette& pal);
    static void build_music_rail_detail(LvglSettingsPage& self, const YoRadioPalette& pal);

    // 6.7S-MEM1: Display detail tree — lazy once per Settings lifecycle.
    // 6.7S-MEM1: дерево Display detail — лениво один раз за lifecycle Settings.
    bool _ensureDisplayView();
    // 6.7S6: Music Rail detail — lazy per visit, destroyed on Back.
    // 6.7S6: Music Rail detail — lazy при входе, уничтожается на Back.
    bool _ensureMusicRailView();
    void _destroyMusicRailView();
    void _nullHandles();
    void _applyThemeColors();
    void _showView(SettingsView view);
    void _syncMainRowValues();
    void _syncDisplayValues();
    void _syncMusicRailValues();
    void _updateBrightnessLabels(uint8_t pct);
    void _updateDimLevelLabels(uint8_t pct);
    void _syncDimLevelSliderRange(bool persist_clamp);
    uint8_t _normalBrightnessForDimUi() const;
    void _applySliderTheme(const YoRadioPalette& pal);
    void _applyAutodimRowTreatment(const YoRadioPalette& pal);
    void _applyMusicRailProfileRowTreatment(const YoRadioPalette& pal);
    void _showSleepDeviceWarning();
    void _hideSleepDeviceWarning();
    void _applySleepDeviceOverlayTheme(const YoRadioPalette& pal);

    static void footerClickedEvt(lv_event_t* e);
    static void displayRowClickedEvt(lv_event_t* e);
    static void displayBackClickedEvt(lv_event_t* e);
    static void sleepRowClickedEvt(lv_event_t* e);
    static void sleepActionRowClickedEvt(lv_event_t* e);
    static void sleepDeviceOverlayCancelEvt(lv_event_t* e);
    static void sleepDeviceOverlayConfirmEvt(lv_event_t* e);
    static void sleepDeviceOverlayBlockGestureEvt(lv_event_t* e);
    static void themeRowClickedEvt(lv_event_t* e);
    static void brightnessSliderEvt(lv_event_t* e);
    static void autodimRowClickedEvt(lv_event_t* e);
    static void performanceMonitorRowClickedEvt(lv_event_t* e);
    static void dimAfterRowClickedEvt(lv_event_t* e);
    static void dimLevelSliderEvt(lv_event_t* e);
    static void wifiRowClickedEvt(lv_event_t* e);
    static void musicRowClickedEvt(lv_event_t* e);
    static void musicBackClickedEvt(lv_event_t* e);
    static void musicPresenceRailClickedEvt(lv_event_t* e);
    static void musicProfileRowClickedEvt(lv_event_t* e);
    static void resumeOnStartupRowClickedEvt(lv_event_t* e);

    SettingsView _view = SettingsView::Main;
    bool         _brightness_drag_active = false;
    bool         _dim_level_drag_active  = false;

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
    RowChrome   _row_display{};
    RowChrome   _row_music{};
    RowChrome   _row_resume_startup{};
    RowChrome   _row_sleep{};
    RowChrome   _row_sleep_sub{};
    RowChrome   _row_wifi{};
    RowChrome   _row_theme{};
    RowChrome   _row_perf_monitor{};
    RowChrome   _row_autodim{};
    RowChrome   _row_dim_after{};
    lv_obj_t*   _sleep_device_overlay = nullptr;
};

} // namespace lvgl_ui

#endif
