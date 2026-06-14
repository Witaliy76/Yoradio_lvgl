#ifndef SCR_STUB_H
#define SCR_STUB_H

#include "../lv_screen.h"

namespace lvgl_ui {

// Minimal carousel placeholder (Stage 5.3). Label + dark bg only; no product features.
// Минимальная заглушка карусели (этап 5.3): только лейбл и тёмный фон.

class LvglStubPage final : public ILvglScreen {
public:
    explicit LvglStubPage(const char* titleUtf8);

    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;
    void liveReapplyTheme() override;
    // W2F: LVGL tree already auto-deleted by PageChain → only null handles. / Только обнулить указатели.
    void releaseAfterAutoDelete() override;

private:
    // W2F: shared handle nulling (no lv_obj_del) used by destroy() and releaseAfterAutoDelete().
    // W2F: общий сброс указателей (без lv_obj_del) для destroy() и releaseAfterAutoDelete().
    void _nullHandles();

    const char* _titleUtf8;
    lv_obj_t* _screen = nullptr;
    lv_obj_t* _lbl = nullptr;
};

} // namespace lvgl_ui

#endif
