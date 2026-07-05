#ifndef STATION_LIST_LEGACY_SCROLL_H
#define STATION_LIST_LEGACY_SCROLL_H

/*
 * station_list_legacy_scroll — continuous-scroll Station list renderer (DspTask-only).
 * station_list_legacy_scroll — renderer списка Station с непрерывной прокруткой (только DspTask).
 *
 * Owns: _list_area subtree, PSRAM text buffer, focus/current overlays, touch pipeline.
 * Does NOT own: page chrome, header count label, footer, PageChain gestures.
 * Владеет: subtree списка, буфер текста, оверлеи, touch pipeline. Не владеет chrome страницы.
 */

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#include "../adapters/station_list_adapter.h"

namespace lvgl_ui {

struct YoRadioPalette;

namespace station_list_legacy_scroll {

struct Instance {
    lv_obj_t* list_area = nullptr;
    lv_obj_t* lbl_list = nullptr;
    lv_obj_t* focus_row_bg = nullptr;
    lv_obj_t* focus_row_accent = nullptr;
    lv_obj_t* current_marker = nullptr;

    lv_obj_t* parent_screen = nullptr;

    uint16_t focus_station_num = 0;
    uint16_t station_total = 0;
    char* list_text = nullptr;
    size_t list_text_cap = 0;

    station_list_adapter::StationListSignature list_sig_cache{};
    bool list_sig_cache_valid = false;

    lv_point_t list_press_pt{};
    lv_coord_t list_press_scroll_y = 0;
    int32_t list_stroke_max_manhattan = 0;
    int32_t list_stroke_max_scroll_y_abs = 0;
    bool list_arm_suppress_next_focus = false;
};

// Result of enter-time signature check / результат проверки сигнатуры при enter.
enum class RefreshOnActivateResult {
    MarkerOnly,  // signature unchanged or read failed — page should refresh count+marker / только marker+count
    Rebuilt,     // list repopulated — page should refresh count only / список пересобран
};

// Creates scrollable list_area under parent_screen (flex_grow slot). Does not populate rows.
// Создаёт прокручиваемый list_area под parent_screen. Строки не заполняет.
bool create(Instance& instance, lv_obj_t* parent_screen, const YoRadioPalette& palette);

// Initial fill after create(), or full rebuild when playlist signature changes.
// Первичное заполнение или полный rebuild при смене сигнатуры плейлиста.
void populate(Instance& instance);

// enter() path: signature check; rebuild or defer marker refresh to page.
// enter(): проверка сигнатуры; rebuild или marker refresh делегируется page.
RefreshOnActivateResult refreshOnActivate(Instance& instance);

// enter() only: center scroll on current station (no call from NEWSTATION).
// Только enter(): центрирование на текущей станции (не из NEWSTATION).
void onEnter(Instance& instance);

// NEWSTATION / marker-only refresh — does not rebuild list or scroll.
// NEWSTATION / только маркер — без rebuild и без scroll jump.
void refreshCurrentStationVisuals(Instance& instance);

void liveReapplyTheme(Instance& instance, const YoRadioPalette& palette);

// After lv_obj_del(parent_screen) or PageChain auto-delete: null handles + free list_text. No lv_obj_del.
// После удаления дерева: обнулить handles + free list_text. Без lv_obj_del.
void releaseAfterTreeDelete(Instance& instance);

} // namespace station_list_legacy_scroll
} // namespace lvgl_ui

#endif // STATION_LIST_LEGACY_SCROLL_H
