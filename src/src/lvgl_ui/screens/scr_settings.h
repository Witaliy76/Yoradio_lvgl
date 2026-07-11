#ifndef SCR_SETTINGS_H
#define SCR_SETTINGS_H

#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// Settings main page: row-unit layout, status line, footer; detail views inside same ILvglScreen.
// Главная Settings: row-unit layout, status line, footer; detail views внутри одного экрана.
class LvglSettingsPage final : public ILvglScreen {
public:
    // Row widget handles for theme reapply / хэндлы строк для перекраски темы
    struct RowChrome {
        lv_obj_t* icon    = nullptr;
        lv_obj_t* label   = nullptr;
        lv_obj_t* value   = nullptr;
        lv_obj_t* chevron = nullptr;
    };

    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;
    void liveReapplyTheme() override;
    void releaseAfterAutoDelete() override;

    // Footer tap → Main carousel slot (PageChain contract, same as Station hint band).
    // Тап по footer → слот Main (как hint band на Station).
    static void footerClickedEvt(lv_event_t* e);

private:
    void _nullHandles();
    void _applyThemeColors();

    lv_obj_t* _screen           = nullptr;
    wgt_status_line::Instance   _status_line{};
    lv_obj_t* _cont_content     = nullptr;
    lv_obj_t* _footer_area      = nullptr;
    lv_obj_t* _lbl_footer       = nullptr;
    RowChrome   _row_display{};
    RowChrome   _row_music{};
    RowChrome   _row_ai{};
    RowChrome   _row_sleep{};
    RowChrome   _row_sleep_sub{};
    RowChrome   _row_wifi{};
};

} // namespace lvgl_ui

#endif
