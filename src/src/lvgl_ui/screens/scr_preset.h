#ifndef SCR_PRESET_H
#define SCR_PRESET_H

#include "../lv_screen.h"

namespace lvgl_ui {

// Stage 6.4A: Preset Temporary screen skeleton (8 fixed stub slots, no persist/play).
// Этап 6.4A: скелет Preset Temporary (8 заглушек, без persist/play).

class LvglPresetScreen final : public ILvglScreen {
public:
    static constexpr int kSlotCount = 8;

    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;

private:
    void _nullHandles();

    lv_obj_t* _screen = nullptr;
    lv_obj_t* _title = nullptr;
    lv_obj_t* _rows[kSlotCount] = {};
};

} // namespace lvgl_ui

#endif
