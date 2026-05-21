#ifndef SCR_STATION_H
#define SCR_STATION_H

/*
 * scr_station.h — Station Page (6.3F): tap-to-focus overlays vs current-marker split.
 * scr_station.h — страница Station: разделение фокус UI и маркера «текущая станция».
 *
 * Scope: 6.3F9 input + 6.3 marker layout — speaker glyph left gutter; F8/arm unchanged in cpp.
 */

#include <stdint.h>

#include "lvgl.h"

#include "../adapters/station_list_adapter.h"
#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// Station Page: tap-to-focus on list scroll root; playback marker from adapter — tap-to-play later.
class LvglStationPage final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;
    void liveReapplyTheme() override;

    // 6.3D-b1: DspTask-only hooks from displayQueue (NEWSTATION / optional DRAWPLAYLIST).
    void refreshCurrentStationVisuals();
    void onPlaylistDataMaybeChanged();

private:
    lv_obj_t* _screen = nullptr;
    wgt_status_line::Instance _status_line{};

    lv_obj_t* _lbl_title = nullptr;
    lv_obj_t* _lbl_count = nullptr;
    lv_obj_t* _lbl_hint_icon = nullptr;
    lv_obj_t* _lbl_hint_text = nullptr;
    lv_obj_t* _list_area = nullptr;
    lv_obj_t* _lbl_list = nullptr;
    lv_obj_t* _focus_row_bg = nullptr;
    lv_obj_t* _focus_row_accent = nullptr;
    lv_obj_t* _current_marker = nullptr;
    lv_obj_t* _hint_area = nullptr;

    uint16_t _focus_station_num = 0;
    uint16_t _station_total = 0;
    char* _list_text = nullptr;
    size_t _list_text_cap = 0;

    station_list_adapter::StationListSignature _list_sig_cache{};
    bool _list_sig_cache_valid = false;

    // 6.3F8: stroke baselines + peak movement during PRESSING (end point can lie near press after a flick).
    // 6.3F8: база жеста + пик смещения по PRESSING (палец после флика часто «возвращается» к месту нажатия).
    lv_point_t _list_press_pt{};
    lv_coord_t _list_press_scroll_y = 0;
    int32_t _list_stroke_max_manhattan = 0;
    int32_t _list_stroke_max_scroll_y_abs = 0;
    // F9: after scroll or touch-while-coasting, next qualifying SHORT_CLICKED is eaten (stop UX); following tap focuses.
    // F9: после прокрутки/касания во время инерции следующий «чистый» клик поглощается; следующий тап — фокус.
    bool _list_arm_suppress_next_focus = false;

    void _populateStationList();
    void _cacheListSignature();
    void _refreshOnPageActivate();
    void _updateCountLabel(uint16_t current, uint16_t total);
    bool _ensureListTextBuffer(uint16_t total);
    void _releaseListTextBuffer();

    static void _listAreaPressedEvt(lv_event_t* e);
    void _onListAreaPressed(lv_event_t* e);

    static void _listAreaPressingEvt(lv_event_t* e);
    void _onListAreaPressing(lv_event_t* e);

    static void _listAreaReleasedEvt(lv_event_t* e);
    void _onListAreaReleased(lv_event_t* e);

    static void _listAreaShortClickedEvt(lv_event_t* e);
    void _onListAreaShortClicked(lv_event_t* e);
    bool _candidateStationFromScreenPoint(lv_coord_t screen_px, lv_coord_t screen_py, uint16_t* out_station);

    void _registerListPointerHandlersOnLabel();

    void _clampFocusForTotal(uint16_t total_stations);
    void _setFocusStation(uint16_t num);

    void _destroyFocusRowOverlays();
    void _destroyCurrentMarkerOverlay();
    void _destroyStationOverlays();

    void _layoutFocusChrome(uint16_t focus_station_num);
    void _layoutMarkerForCurrentStation(uint16_t current_station_num);
    void _buildStationOverlaysAfterList(uint16_t current_station_num);

    // Stage 6.3H: enter-only — scroll list so current station row is in view (no jump on live NEWSTATION).
    // Этап 6.3H: только при входе на страницу — не вызывать из refreshCurrentStationVisuals().
    void _scrollListToCurrentOnEnter();
};

} // namespace lvgl_ui

#endif // SCR_STATION_H
