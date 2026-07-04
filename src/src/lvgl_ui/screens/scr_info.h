#ifndef SCR_INFO_H
#define SCR_INFO_H

#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// INFOREF-A: forward declaration — builders take const YoRadioPalette& without pulling the
// theme header into this .h (full definition stays in scr_info.cpp via lv_theme_yoradio.h).
// INFOREF-A: forward-декларация — билдеры принимают const YoRadioPalette& без include темы в .h.
struct YoRadioPalette;

// Device info page: network / firmware / system / display / memory.
// Not stream metadata. Страница сведений об устройстве. Не метаданные потока.
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
    // PageChain auto-delete: LVGL tree already freed — only null handles, never lv_obj_del.
    // PageChain auto-delete: дерево LVGL уже освобождено — только обнуляем указатели.
    void releaseAfterAutoDelete() override;

private:
    // Shared handle nulling used by destroy() and releaseAfterAutoDelete().
    // Общий сброс указателей для destroy() и releaseAfterAutoDelete().
    void _nullHandles();

    // INFOREF-A: private static layout builders — keep create() a short orchestration skeleton.
    // Access to private members via `self` reference; called only from create(), in order.
    // INFOREF-A: private static билдеры — create() остаётся коротким оркестратором.
    static void create_status_chrome(LvglInfoPage& self, const YoRadioPalette& pal);
    static void create_title(LvglInfoPage& self, const YoRadioPalette& pal);
    static void create_content(LvglInfoPage& self, const YoRadioPalette& pal);

    lv_obj_t* _screen          = nullptr;
    wgt_status_line::Instance  _status_line{};
    lv_obj_t* _lbl_info_title  = nullptr;

    lv_obj_t* _val_ssid        = nullptr;
    lv_obj_t* _val_ip          = nullptr;
    lv_obj_t* _val_wifi        = nullptr;
    lv_obj_t* _val_mac         = nullptr;

    lv_obj_t* _val_firmware    = nullptr;
    lv_obj_t* _val_build       = nullptr;
    lv_obj_t* _val_chip        = nullptr;
    lv_obj_t* _val_cpu         = nullptr;
    lv_obj_t* _val_uptime      = nullptr;

    lv_obj_t* _val_display     = nullptr;
    lv_obj_t* _val_lvgl        = nullptr;

    lv_obj_t* _val_heap        = nullptr;
    lv_obj_t* _val_psram       = nullptr;
    lv_obj_t* _val_sd          = nullptr;
};

} // namespace lvgl_ui

#endif
