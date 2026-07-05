#ifndef SCR_STATION_H
#define SCR_STATION_H

/*
 * scr_station.h — Station list page: tap-to-focus overlays with separate current-station marker.
 * scr_station.h — страница Station: tap-to-focus оверлеи и независимый маркер «текущая станция».
 *
 * Focus station is UI-local state; current station comes from the adapter (what is playing).
 * List rendering delegated to station_list_legacy_scroll (continuous-scroll renderer).
 * Фокус — UI-local state; текущая станция из адаптера. Список — station_list_legacy_scroll.
 */

#include <stdint.h>

#include "lvgl.h"

#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"
#include "station_list_legacy_scroll.h"

namespace lvgl_ui {

struct YoRadioPalette;

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
    void releaseAfterAutoDelete() override;

    void refreshCurrentStationVisuals();

private:
    void _nullHandles();

    static void create_status_chrome(LvglStationPage& self, const YoRadioPalette& pal);
    static void create_header(LvglStationPage& self, const YoRadioPalette& pal);
    static void create_hint_band(LvglStationPage& self, const YoRadioPalette& pal);

    static void _hintAreaClickedEvt(lv_event_t* e);
    void _onHintAreaClicked(lv_event_t* e);

    void _refreshOnPageActivate();
    void _updateCountLabel(uint16_t current, uint16_t total);

    lv_obj_t* _screen = nullptr;
    wgt_status_line::Instance _status_line{};

    lv_obj_t* _lbl_title = nullptr;
    lv_obj_t* _lbl_count = nullptr;
    lv_obj_t* _lbl_hint_icon = nullptr;
    lv_obj_t* _lbl_hint_text = nullptr;
    lv_obj_t* _hint_area = nullptr;

    uint16_t _station_total = 0;

    station_list_legacy_scroll::Instance _list{};
};

} // namespace lvgl_ui

#endif // SCR_STATION_H
