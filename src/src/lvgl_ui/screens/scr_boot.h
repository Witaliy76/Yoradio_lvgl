// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef SCR_BOOT_H
#define SCR_BOOT_H

#include <stdint.h>
#include "../lv_screen.h"

namespace lvgl_ui {

// LvglBootScreen — fixed-dark transitional Boot UI.
// LvglBootScreen — переходный Boot UI с фиксированной тёмной темой.
//
// The screen owns an indeterminate shuttle indicator.
// It is not a carousel page and may be auto-deleted by LVGL
// during the handoff to the main PageChain.
// Экран содержит индикатор-бегунок (indeterminate).
// Не является страницей карусели; может быть auto-deleted LVGL при handoff в PageChain.
//
// All LVGL access is DspTask-only.
// Все вызовы lv_* только из DspTask.
class LvglBootScreen final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;

    // Status from existing boot queue signals (UTF-8, single line).
    // Статус из существующих сигналов очереди — одна строка UTF-8.
    void setStatusUtf8(const char* text);
    // Boot queue still invokes this. Shuttle is intentionally indeterminate — signals do not alter it.
    // Очередь Boot вызывает этот callback. Бегунок indeterminate — сигналы его не меняют.
    void onBootSignal();

private:
    // Null all LVGL handles without deleting objects.
    // Boot screen may be auto-deleted by LVGL (auto_del=true); only detach, never lv_obj_del.
    // Обнулить все LVGL handles без удаления объектов.
    // Экран Boot может быть auto-deleted LVGL; только отсоединяем, без lv_obj_del.
    void _nullHandles();

    // BOOTREF-A: private static layout builders — keep create() a short orchestration skeleton.
    // Access to private members via `self` reference; called only from create(), in order.
    // BOOTREF-A: private static билдеры — create() остаётся коротким оркестратором.
    static lv_obj_t* create_root_column(
        LvglBootScreen& self,
        uint16_t screen_width);

    static void create_logo(
        LvglBootScreen& self,
        lv_obj_t* parent,
        uint16_t screen_width);

    static void create_status(
        LvglBootScreen& self,
        lv_obj_t* parent,
        uint16_t screen_width,
        int32_t frame_padding);

    static void create_indeterminate_bar(
        LvglBootScreen& self,
        lv_obj_t* parent,
        uint16_t screen_width);

    // Animation
    static void shuttleAnimExec(void* var, int32_t x);
    void startIndeterminateAnim();

    lv_obj_t* _screen          = nullptr;
    lv_obj_t* _root            = nullptr; // Root flex container / корневой flex-контейнер
    lv_obj_t* _img_logo        = nullptr;
    lv_obj_t* _lbl_status      = nullptr;
    char      _status_text[128] = {0};    // Stable label storage / стабильный буфер текста статуса
    lv_obj_t* _prog_track      = nullptr;
    lv_obj_t* _prog_glow       = nullptr;
    lv_obj_t* _prog_shuttle    = nullptr;
};

} // namespace lvgl_ui

#endif
