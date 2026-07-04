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

    // ── Runtime refresh and signature ──────────────────────────────────────
    void _refreshOnPageActivate();
    void _updateCountLabel(uint16_t current, uint16_t total);
    void _cacheListSignature();

    // ── List population and label creation ─────────────────────────────────
    // _clearStationListVisuals(): overlays first, then lv_obj_clean, then null label handle.
    // Order is load-bearing (STATIONFIX-1): overlays deleted while handles are valid.
    // _clearStationListVisuals(): сначала overlays, потом lv_obj_clean, потом обнуление label.
    void _clearStationListVisuals();
    void _populateStationList();
    void _registerListPointerHandlersOnLabel();

    // Allocation-failure path: create error label via lv_label_set_text (LVGL owns copy).
    // Returns false if LVGL could not allocate the label.
    // Ветка ошибки выделения: lv_label_set_text (LVGL хранит копию). Возвращает false при ошибке.
    bool _showStationListAllocationError();

    // Normal list label: lv_label_set_text_static — LVGL only stores the pointer.
    // _list_text must remain allocated while _lbl_list exists.
    // Обычный label: lv_label_set_text_static — LVGL хранит только указатель.
    // _list_text должен оставаться живым пока _lbl_list существует.
    bool _createStationListLabelFromBuffer();

    // ── Overlay lifecycle and layout ────────────────────────────────────────
    void _destroyFocusRowOverlays();
    void _destroyCurrentMarkerOverlay();
    void _destroyStationOverlays();

    void _clampFocusForTotal(uint16_t total_stations);
    void _buildStationOverlaysAfterList(uint16_t current_station_num);

    void _layoutFocusChrome(uint16_t focus_station_num);
    void _styleFocusRowOverlays(const YoRadioPalette& pal);
    void _positionFocusRowOverlays(uint16_t focus_station_num);

    void _layoutMarkerForCurrentStation(uint16_t current_station_num);
    bool _ensureCurrentMarker();
    void _styleCurrentMarker(const YoRadioPalette& pal);
    void _positionCurrentMarker(uint16_t current_station_num);

    // ── Enter-scroll ───────────────────────────────────────────────────────
    // Called only from enter() — no scroll jump on live NEWSTATION.
    // Вызывается только из enter() — нет прыжка скролла при NEWSTATION.
    void _scrollListToCurrentOnEnter();

    // ── Pointer event entry points (thin static wrappers → instance handlers) ─
    static void _listAreaPressedEvt(lv_event_t* e);
    static void _listAreaPressingEvt(lv_event_t* e);
    static void _listAreaReleasedEvt(lv_event_t* e);
    static void _listAreaShortClickedEvt(lv_event_t* e);

    // ── Stroke tracking and suppression ────────────────────────────────────
    // _resetListStrokeTracking(): zeros stroke peaks and press baseline.
    // Does NOT reset _list_arm_suppress_next_focus — suppression survives new PRESSED.
    // _resetListStrokeTracking(): обнуляет пики и базу жеста.
    // Не сбрасывает _list_arm_suppress_next_focus — arm переживает новый PRESSED.
    void _resetListStrokeTracking();
    void _captureListPressBaseline(lv_indev_t* indev);
    void _updateListStrokePeaks(const lv_point_t& current_point, lv_coord_t current_scroll_y);
    bool _isTrackedStrokeScrollLike() const;
    // Consume suppression arm. Returns true if arm was set (caller should skip focus+play).
    // Потребляет suppression arm. Возвращает true если arm был установлен.
    bool _consumeListFocusSuppression();
    // Full input state reset — called only from _nullHandles() on destroy/auto-delete.
    // Полный reset state ввода — вызывается только из _nullHandles() при destroy/auto-delete.
    void _resetListInputState();

    // ── Pointer instance handlers ───────────────────────────────────────────
    void _onListAreaPressed(lv_event_t* e);
    void _onListAreaPressing(lv_event_t* e);
    void _onListAreaReleased(lv_event_t* e);
    void _onListAreaShortClicked(lv_event_t* e);

    // ── Candidate-row mapping ───────────────────────────────────────────────
    bool _candidateStationFromScreenPoint(lv_coord_t screen_px, lv_coord_t screen_py, uint16_t* out_station);

    // ── Focus/play action ───────────────────────────────────────────────────
    void _setFocusStation(uint16_t num);

    // ── External buffer ownership ──────────────────────────────────────────
    bool _ensureListTextBuffer(uint16_t total);
    void _releaseListTextBuffer();
};

} // namespace lvgl_ui

#endif // SCR_STATION_H
