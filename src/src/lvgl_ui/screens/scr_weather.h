#ifndef SCR_WEATHER_H
#define SCR_WEATHER_H

#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// Weather W2 — LVGL-native 480×480 Weather Page (read-only consumer of core WeatherState).
// Reads weatherGetStateSnapshot() only; never triggers network fetch (that stays in W1 / doSync).
// Renders: hero (icon + temp + condition + feels), metrics (wind/humidity/pressure/rain),
// hourly strip (4), daily strip (3), footer status. Handles valid / waiting / unavailable states.
//
// Weather W2 — нативная LVGL-страница погоды 480×480 (только чтение core WeatherState).
// Читает weatherGetStateSnapshot(); сеть из UI не дёргает (fetch остаётся в W1 / doSync).
// Рисует hero / метрики / почасовую (4) / посуточную (3) ленты / футер; состояния valid/waiting/unavailable.
//
// W2F: this ~76-object page is freed by the unified PageChain auto-delete on every carousel switch
// (no per-page non-resident flag). The weather snapshot lives in core WeatherState, so create()
// rebuilds the page cleanly on re-entry. ~15 KB of LVGL pool returns to the pool after leaving.
// W2F: страницу (~76 объектов) освобождает общий auto-delete PageChain при любом переходе карусели;
// данные живут в core WeatherState, поэтому create() пересобирает страницу при следующем визите.
class LvglWeatherPage final : public ILvglScreen {
public:
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

    static constexpr int kHourlyCells = 4;  // visible hourly cells / видимых почасовых ячеек
    static constexpr int kDailyCells  = 3;  // visible daily cells / видимых посуточных ячеек

    // One hourly cell: time / icon / temp / pop labels. / Одна почасовая ячейка.
    struct HourlyCell {
        lv_obj_t* cont = nullptr;
        lv_obj_t* time = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* temp = nullptr;
        lv_obj_t* pop  = nullptr;
    };

    // One daily cell: day / icon / min-max / pop labels. / Одна посуточная ячейка.
    struct DailyCell {
        lv_obj_t* cont  = nullptr;
        lv_obj_t* day   = nullptr;
        lv_obj_t* icon  = nullptr;
        lv_obj_t* range = nullptr;
        lv_obj_t* pop   = nullptr;
    };

    lv_obj_t* _screen = nullptr;
    wgt_status_line::Instance _status_line{};

    lv_obj_t* _content = nullptr;

    // W2C: body grows; footer pinned at bottom of content area (Station _hint_area visual language).
    // W2C: body растёт; футер прижат к низу content (визуал как _hint_area на Station).
    lv_obj_t* _body_area = nullptr;

    // W2A: data block vs empty-state block (toggle visibility; no object recreation in update).
    // W2A: блок данных и блок пустого состояния — переключение видимости, без пересоздания в update.
    lv_obj_t* _cont_data  = nullptr;  // hero + metrics + divider + strips
    lv_obj_t* _div_mid    = nullptr;  // divider between metrics and hourly (inside _cont_data)
    lv_obj_t* _cont_empty_center = nullptr;  // flex-grow centered message area (inside _body_area)
    lv_obj_t* _lbl_message = nullptr; // waiting / unavailable body message

    // W2C: bottom status pill — always at bottom of _content; text reflects page state.
    // W2C: нижняя status-pill — всегда внизу _content; текст по состоянию страницы.
    lv_obj_t* _cont_footer = nullptr;
    lv_obj_t* _footer_box  = nullptr;
    lv_obj_t* _lbl_footer  = nullptr;

    // Hero block / Блок hero
    lv_obj_t* _cont_hero    = nullptr;
    lv_obj_t* _lbl_hero_icon = nullptr;
    lv_obj_t* _lbl_hero_temp = nullptr;
    lv_obj_t* _lbl_hero_cond = nullptr;
    lv_obj_t* _lbl_hero_feels = nullptr;

    // Metrics block / Блок метрик
    lv_obj_t* _cont_metrics = nullptr;
    lv_obj_t* _val_wind     = nullptr;
    lv_obj_t* _val_humidity = nullptr;
    lv_obj_t* _val_pressure = nullptr;
    lv_obj_t* _val_rain     = nullptr;

    lv_obj_t* _cont_hourly = nullptr;
    HourlyCell _hourly[kHourlyCells]{};

    lv_obj_t* _cont_daily = nullptr;
    DailyCell _daily[kDailyCells]{};
};

} // namespace lvgl_ui

#endif
