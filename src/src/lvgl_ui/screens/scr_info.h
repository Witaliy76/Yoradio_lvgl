#ifndef SCR_INFO_H
#define SCR_INFO_H

#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// Device info page (network / firmware / system / memory); not stream metadata.
// Страница сведений об устройстве; не метаданные потока.
class LvglInfoPage final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;
    void liveReapplyTheme() override;

private:
    lv_obj_t* _screen = nullptr;
    wgt_status_line::Instance _status_line{};
    lv_obj_t* _lbl_info_title = nullptr;

    lv_obj_t* _val_ssid = nullptr;
    lv_obj_t* _val_ip = nullptr;
    lv_obj_t* _val_wifi = nullptr;
    lv_obj_t* _val_mac = nullptr;

    lv_obj_t* _val_firmware = nullptr;
    lv_obj_t* _val_build = nullptr;
    lv_obj_t* _val_chip = nullptr;
    lv_obj_t* _val_cpu = nullptr;
    lv_obj_t* _val_uptime = nullptr;

    lv_obj_t* _val_display = nullptr;
    lv_obj_t* _val_lvgl = nullptr;

    lv_obj_t* _val_heap = nullptr;
    lv_obj_t* _val_psram = nullptr;
    lv_obj_t* _val_sd = nullptr;
};

} // namespace lvgl_ui

#endif
