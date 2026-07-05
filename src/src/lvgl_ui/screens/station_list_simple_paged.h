#ifndef STATION_LIST_SIMPLE_PAGED_H
#define STATION_LIST_SIMPLE_PAGED_H

/*
 * station_list_simple_paged — instant page-flip Station list renderer (DspTask-only).
 * station_list_simple_paged — постраничный renderer Station без pixel scroll (только DspTask).
 *
 * Owns: list_area subtree, short page text buffer, focus/current overlays, swipe/tap pipeline.
 * Does NOT own: page chrome, header count, footer, PageChain gestures.
 */

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#include "../adapters/station_list_adapter.h"

namespace lvgl_ui {

struct YoRadioPalette;

namespace station_list_simple_paged {

struct Instance {
    lv_obj_t* list_area = nullptr;
    lv_obj_t* lbl_page = nullptr;
    lv_obj_t* focus_row_bg = nullptr;
    lv_obj_t* focus_row_accent = nullptr;
    lv_obj_t* current_marker = nullptr;

    lv_obj_t* parent_screen = nullptr;

    uint16_t page_index = 0;
    uint16_t page_count = 0;
    uint16_t rows_per_page = 0;
    uint16_t station_total = 0;
    uint16_t focus_station_num = 0;

    char* page_text = nullptr;
    size_t page_text_cap = 0;

    station_list_adapter::StationListSignature list_sig_cache{};
    bool list_sig_cache_valid = false;

    lv_point_t press_pt{};
    int32_t stroke_max_dx = 0;
    int32_t stroke_max_dy = 0;
    bool arm_suppress_next_tap = false;
    bool completed_vertical_page_gesture = false;
};

enum class RefreshOnActivateResult {
    MarkerOnly,
    Rebuilt,
};

bool create(Instance& instance, lv_obj_t* parent_screen, const YoRadioPalette& palette);

void populate(Instance& instance);

RefreshOnActivateResult refreshOnActivate(Instance& instance);

void onEnter(Instance& instance);

void refreshCurrentStationVisuals(Instance& instance);

void liveReapplyTheme(Instance& instance, const YoRadioPalette& palette);

void releaseAfterTreeDelete(Instance& instance);

} // namespace station_list_simple_paged
} // namespace lvgl_ui

#endif // STATION_LIST_SIMPLE_PAGED_H
