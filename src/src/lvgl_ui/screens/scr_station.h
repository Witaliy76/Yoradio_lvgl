#ifndef SCR_STATION_H
#define SCR_STATION_H

/*
 * scr_station.h — Station list page: tap-to-focus overlays with separate current-station marker.
 * scr_station.h — страница Station: tap-to-focus оверлеи и независимый маркер «текущая станция».
 *
 * Focus station is UI-local state; current station comes from the adapter (what is playing).
 * Фокус — UI-local state; текущая станция берётся из адаптера (что играет).
 */

#include <stdint.h>

#include "lvgl.h"

#include "../adapters/station_list_adapter.h"
#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// STATIONREF-A: forward declaration — builders take const YoRadioPalette& without pulling
// the theme header into this .h (full definition in scr_station.cpp via lv_theme_yoradio.h).
// STATIONREF-A: forward-декларация — билдеры принимают const YoRadioPalette& без include темы в .h.
struct YoRadioPalette;

// Station list page: scrollable single-label list, tap-to-focus, tap-to-play.
// Страница Station: прокручиваемый однострочный список, tap для фокуса и воспроизведения.
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
    // PageChain auto-delete: LVGL tree already freed — only null handles and free list buffer.
    // PageChain auto-delete: дерево уже освобождено LVGL — только обнуляем указатели и буфер.
    void releaseAfterAutoDelete() override;

    // DspTask-only hook from displayQueue on NEWSTATION event.
    // Updates current-station marker and count; does not rebuild the list.
    // Вызывается из displayQueue (NEWSTATION, только DspTask). Обновляет маркер и count, список не пересоздаёт.
    void refreshCurrentStationVisuals();

private:
    // Shared teardown (no lv_obj_del): releases the heap list-text buffer and nulls all handles.
    // Used by both destroy() (after lv_obj_del) and releaseAfterAutoDelete() (after PageChain deletion).
    // Общий сброс (без lv_obj_del): освобождает heap-буфер списка и обнуляет все указатели.
    void _nullHandles();

    // STATIONREF-A: private static layout builders — keep create() a short orchestration skeleton.
    // Called only from create(), in order. Access to private members via `self` reference.
    // STATIONREF-A: private static билдеры — create() остаётся коротким оркестратором.
    static void create_status_chrome(LvglStationPage& self, const YoRadioPalette& pal);
    static void create_header(LvglStationPage& self, const YoRadioPalette& pal);
    static void create_list_area(LvglStationPage& self, const YoRadioPalette& pal);
    static void create_hint_band(LvglStationPage& self, const YoRadioPalette& pal);

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

    // Stroke baselines captured on PRESSED; peak movement accumulated during PRESSING.
    // End point can lie near press after a flick — peaks guard against scroll misdetection.
    // База жеста фиксируется на PRESSED; пики накапливаются по PRESSING для защиты от ложных тапов.
    lv_point_t _list_press_pt{};
    lv_coord_t _list_press_scroll_y = 0;
    int32_t _list_stroke_max_manhattan = 0;
    int32_t _list_stroke_max_scroll_y_abs = 0;
    // After scroll or touch-while-coasting, next qualifying SHORT_CLICKED is consumed (stop UX); next clean tap focuses.
    // После прокрутки/касания при инерции следующий «чистый» SHORT_CLICKED поглощается; следующий тап — фокус.
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

    // Enter-only: scroll list so current station row is in view.
    // Must not be called from refreshCurrentStationVisuals() — no scroll jump on live NEWSTATION.
    // Только при входе: центрировать текущую станцию в видимой области. Не вызывать из refreshCurrentStationVisuals().
    void _scrollListToCurrentOnEnter();
};

} // namespace lvgl_ui

#endif // SCR_STATION_H
