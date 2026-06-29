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

// Stage 6.4C: Preset Temporary — visual polish. Card rows, column layout, centered title.
// Title always static. Feedback goes to bottom helper label, not the title.
// Этап 6.4C: polish. Карточки-строки с колончатым макетом, статичный заголовок по центру.
// Feedback отображается в нижней подсказке, заголовок всегда «PRESETS».

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
    // Updates text content and per-row colors for one slot (call from enter + after save).
    // Обновляет текст и цвета одной строки (при enter и после сохранения).
    void _updateRowContent(uint8_t slot);
    // Applies palette colors to one card row and its column labels based on slot state.
    // Применяет цвета карточки и колонок по состоянию слота.
    void _applyRowColors(uint8_t slot, const struct YoRadioPalette& pal);
    // Reapplies palette to all elements (liveReapplyTheme + theme parity). DspTask only.
    // Применяет палитру ко всем элементам; только DspTask.
    void _applyAllColors();
    // Helper label: default hint text + secondary color.
    // Helper label: restore to countdown display; clears _feedbackActive.
    void _setHelperDefault();
    // Helper label: feedback message text + semantic color. success=true → accent, false → secondary.
    void _setHelperMessage(const char* text, bool success);
    void _cancelFeedbackTimer();
    void _scheduleHelperRestore(uint32_t delay_ms);
    // Countdown timer — recurring ~333 ms; updates helper when second changes.
    // Recurring таймер; обновляет helper при изменении отображаемых секунд.
    void _startCountdownTimer();
    void _cancelCountdownTimer();
    void _updateCountdownHelper();  // render remaining seconds into helper label
    void _onRowPressed(uint8_t slot);
    void _onRowShortClicked(uint8_t slot);
    void _onRowLongPressed(uint8_t slot);
    void _onRowReleased();

    static void _rowEventCb(lv_event_t* e);
    static void _feedbackTimerCb(lv_timer_t* timer);
    static void _countdownTimerCb(lv_timer_t* timer);

    lv_obj_t* _screen      = nullptr;
    lv_obj_t* _title       = nullptr;               // "PRESETS" — always static / всегда статичный
    lv_obj_t* _helper      = nullptr;               // bottom hint + feedback / нижняя подсказка/feedback
    lv_obj_t* _rows[kSlotCount]        = {};        // clickable card containers / карточки-кнопки
    lv_obj_t* _vdiv_lines[kSlotCount]  = {};        // 1px vertical dividers / разделители
    lv_obj_t* _slot_labels[kSlotCount] = {};        // "1".."8"
    lv_obj_t* _num_labels[kSlotCount]  = {};        // "#257" / station numbers (accent)
    lv_obj_t* _name_labels[kSlotCount] = {};        // station name or status (flex-grow)
    lv_timer_t* _feedback_timer  = nullptr;
    lv_timer_t* _countdown_timer = nullptr;         // recurring 333 ms countdown / периодический
    int16_t _lastCountdownSecond = -1;              // avoids redundant label updates / без лишних обновлений
    bool _feedbackActive      = false;              // countdown paused while feedback visible
    bool _long_press_handled  = false;
};

} // namespace lvgl_ui

#endif
