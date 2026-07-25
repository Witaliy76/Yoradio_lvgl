// Author: Witaliy76 - https://github.com/Witaliy76
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

    // INFOREF-B: runtime refresh methods — update() delegates to these in pipeline order.
    // Called only from update(), sequentially. now_ms is a single millis() snapshot per pass.
    // INFOREF-B: методы обновления runtime — update() делегирует им в порядке pipeline.
    // Вызываются только из update(), последовательно. now_ms — один snapshot millis() за pass.
    void _refreshNetwork(uint32_t now_ms);
    void _refreshSystem(uint32_t now_ms);
    void _refreshDisplay();
    void _refreshMemory();

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

    // INFOREF-B: Wi-Fi sample cache — instance-owned.
    // Reset by _nullHandles() (destroy + auto-delete paths). NOT reset by enter() or exit().
    // After recreate, _wifi_sample_ms == 0 forces a fresh RSSI/channel sample on first update().
    // INFOREF-B: кэш Wi-Fi sample — принадлежит экземпляру.
    // Сбрасывается _nullHandles() (destroy + auto-delete). Не сбрасывается enter() и exit().
    // После пересоздания _wifi_sample_ms == 0 форсирует свежий sample при первом update().
    uint32_t _wifi_sample_ms    = 0;
    int      _wifi_rssi_cached  = -100;
    int      _wifi_ch_cached    = -1;
    bool     _wifi_was_connected = false;
};

} // namespace lvgl_ui

#endif
