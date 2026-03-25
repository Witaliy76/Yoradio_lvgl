#ifndef SCR_MAIN_H
#define SCR_MAIN_H

#include "../lv_screen.h"

namespace lvgl_ui {

// Main player screen skeleton (Stage 5.5). Three semantic zones:
//   tech_bar   — station number, bitrate, RSSI (top strip)
//   center     — station name, title/artist (central block)
//   aux_bar    — volume only (bottom strip; no debug metrics)
// Скелет главного экрана плейера (Stage 5.5). Три смысловых блока:
//   tech_bar   — номер станции, битрейт, RSSI (верх)
//   center     — название станции, title/artist (центр)
//   aux_bar    — только громкость (низ, без отладочных метрик)
class LvglMainScreen final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;

private:
    lv_obj_t* _screen = nullptr;

    // Tech bar labels / Верхний тех-блок
    lv_obj_t* _lbl_station_num = nullptr;
    lv_obj_t* _lbl_bitrate     = nullptr;
    lv_obj_t* _lbl_rssi        = nullptr;

    // Center semantic block / Центральный смысловой блок
    lv_obj_t* _lbl_station_name = nullptr;
    lv_obj_t* _lbl_title        = nullptr;

    // Auxiliary / status bar / Нижний вспомогательный блок
    lv_obj_t* _lbl_volume = nullptr;
};

} // namespace lvgl_ui

#endif
