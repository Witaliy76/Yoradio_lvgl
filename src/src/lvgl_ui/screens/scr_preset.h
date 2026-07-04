#ifndef SCR_PRESET_H
#define SCR_PRESET_H

#include <stddef.h>
#include <stdint.h>

#include "../lv_screen.h"

struct _lv_timer_t;
typedef struct _lv_timer_t lv_timer_t;

#ifdef __cplusplus
extern "C" {
struct _lv_event_t;
typedef struct _lv_event_t lv_event_t;
}
#endif

namespace lvgl_ui {

// PRESETREF-A: forward declaration — builders take const YoRadioPalette& without theme header in .h.
// PRESETREF-A: forward-декларация — билдеры принимают const YoRadioPalette& без include темы в .h.
struct YoRadioPalette;

// LvglPresetScreen — Temporary preset selector and saver (8 positional slots).
// Short tap: play occupied preset and dismiss. Long press: save, screen stays open.
// Footer: countdown/feedback surface; click dismisses to origin. DspTask-only lv_*.
// LvglPresetScreen — временный экран Preset. Тап = play+dismiss; long press = save без закрытия;
// footer = отсчёт/feedback, tap закрывает на origin. Только DspTask.

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
    void liveReapplyTheme() override;

private:
    void _nullHandles();
    void _updateRowContent(uint8_t slot);
    void _applyRowColors(uint8_t slot, const YoRadioPalette& pal);
    void _applyAllColors();
    void _setHelperDefault();
    void _setHelperMessage(const char* text, bool success);
    void _cancelFeedbackTimer();
    void _scheduleHelperRestore(uint32_t delay_ms);
    void _startCountdownTimer();
    void _cancelCountdownTimer();
    void _updateCountdownHelper();
    void _onRowPressed(uint8_t slot);
    void _onRowShortClicked(uint8_t slot);
    void _onRowLongPressed(uint8_t slot);
    void _onRowReleased();

    // PRESETREF-A: private static layout builders — create() stays a short orchestrator.
    // PRESETREF-A: private static билдеры — create() остаётся коротким оркестратором.
    static void create_title(LvglPresetScreen& self, const YoRadioPalette& pal);
    static void create_preset_rows(LvglPresetScreen& self, const YoRadioPalette& pal);
    static void create_preset_row(LvglPresetScreen& self, uint8_t slot, const YoRadioPalette& pal);
    static void create_footer(LvglPresetScreen& self, const YoRadioPalette& pal);

    static void _rowEventCb(lv_event_t* e);
    static void _helperEventCb(lv_event_t* e);
    static void _feedbackTimerCb(lv_timer_t* timer);
    static void _countdownTimerCb(lv_timer_t* timer);

    lv_obj_t* _screen      = nullptr;
    lv_obj_t* _title       = nullptr;
    lv_obj_t* _helper_box  = nullptr;
    lv_obj_t* _helper      = nullptr;
    lv_obj_t* _rows[kSlotCount]        = {};
    lv_obj_t* _vdiv_lines[kSlotCount]  = {};
    lv_obj_t* _slot_labels[kSlotCount] = {};
    lv_obj_t* _num_labels[kSlotCount]  = {};
    lv_obj_t* _name_labels[kSlotCount] = {};
    lv_timer_t* _feedback_timer  = nullptr;
    lv_timer_t* _countdown_timer = nullptr;
    int16_t _lastCountdownSecond = -1;
    bool _feedbackActive      = false;
    bool _long_press_handled  = false;
};

} // namespace lvgl_ui

#endif
