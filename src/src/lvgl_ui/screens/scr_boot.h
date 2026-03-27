#ifndef SCR_BOOT_H
#define SCR_BOOT_H

#include <stdint.h>
#include "../lv_screen.h"

namespace lvgl_ui {

// LVGL Boot screen (Stage 5.4): calm startup — black, centered bitmap logo, one status line, indeterminate bar.
// Экран Boot LVGL (5.4): спокойный старт — чёрный фон, лого по центру, одна строка статуса, индикатор загрузки.
// All lv_* only from DspTask via PageChain::showBoot / Display::loop.
class LvglBootScreen final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;

    // Status from existing boot queue signals (BOOTSTRING / WAITFORSD) — UTF-8, single line.
    // Статус из существующих сигналов очереди — одна строка UTF-8.
    void setStatusUtf8(const char* text);
    // TEMPORARY stub: Display still notifies on boot signals; shuttle is indeterminate-only (see scr_boot.cpp).
    // ВРЕМЕННАЯ заглушка: очередь шлёт сигналы; бегунок не отражает прогресс.
    void onBootSignal();

private:
    static void shuttleAnimExec(void* var, int32_t x);
    void startIndeterminateAnim();

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _root = nullptr; // Root flex container / корневой flex-контейнер
    lv_obj_t* _img_logo = nullptr;
    lv_obj_t* _lbl_status = nullptr;
    char _status_text[128] = {0}; // Stable label storage / стабильный буфер текста статуса
    lv_obj_t* _prog_track = nullptr;
    lv_obj_t* _prog_glow = nullptr;
    lv_obj_t* _prog_shuttle = nullptr;
};

} // namespace lvgl_ui

#endif
