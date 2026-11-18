// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef STATION_LIST_SIMPLE_PAGED_H
#define STATION_LIST_SIMPLE_PAGED_H

/*
 * station_list_simple_paged — instant page-flip Station list renderer (DspTask-only).
 * station_list_simple_paged — постраничный renderer Station без pixel scroll (только DspTask).
 *
 * Owns: list_area subtree, short page text buffer, focus/current overlays, swipe/tap pipeline.
 * Does NOT own: page chrome, header count, footer, PageChain gestures, or which source is open.
 * Rows are 0-based and come from BrowserRowFeed. This file does not read the playlist.
 */

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

namespace lvgl_ui {

struct YoRadioPalette;

namespace station_list_simple_paged {

// Visible rows of the open browser level. The page fills this; the renderer does not.
// Видимые строки открытого уровня. Страница заполняет feed; renderer его не выбирает.
struct BrowserRowFeed {
    uint16_t (*count)(void* ctx) = nullptr;
    bool (*page_text)(void* ctx, uint16_t first, uint16_t last, char* out, size_t cap,
                      size_t name_limit) = nullptr;
    int32_t (*sounding_row)(void* ctx) = nullptr;
    void (*on_tap)(void* ctx, uint16_t row) = nullptr;
    // True when a press on this row should show the existing focus chrome before the click.
    // True: нажатие на строку сразу показывает уже существующую подсветку фокуса.
    bool (*press_focus)(void* ctx, uint16_t row) = nullptr;
    uint32_t (*revision)(void* ctx) = nullptr;
    void* ctx = nullptr;
};

struct Instance {
    lv_obj_t* list_area = nullptr;
    lv_obj_t* lbl_page = nullptr;
    lv_obj_t* focus_row_bg = nullptr;
    lv_obj_t* focus_row_accent = nullptr;
    lv_obj_t* current_marker = nullptr;

    lv_obj_t* parent_screen = nullptr;

    BrowserRowFeed feed{};
    uint16_t page_index = 0;
    uint16_t page_count = 0;
    uint16_t rows_per_page = 0;
    uint16_t row_count = 0;
    uint16_t focus_row = 0;
    uint32_t applied_revision = 0;
    bool revision_valid = false;
    bool jump_to_sounding = false;

    char* page_text = nullptr;
    size_t page_text_cap = 0;

    bool page_filled_for_pending_enter = false;

    lv_point_t press_pt{};
    int32_t stroke_max_dx = 0;
    int32_t stroke_max_dy = 0;
    bool arm_suppress_next_tap = false;
    bool completed_vertical_page_gesture = false;
    uint16_t focus_before_press = 0;
    bool press_focus_active = false;
    uint32_t content_version = 1;
    uint32_t press_content_version = 0;
    uint32_t press_revision = 0;
};

enum class RefreshOnActivateResult {
    MarkerOnly,
    Rebuilt,
};

bool create(Instance& instance, lv_obj_t* parent_screen, const YoRadioPalette& palette);

void setFeed(Instance& instance, const BrowserRowFeed& feed);

void populate(Instance& instance);

RefreshOnActivateResult refreshOnActivate(Instance& instance);

void onEnter(Instance& instance);

void refreshCurrentStationVisuals(Instance& instance);

void liveReapplyTheme(Instance& instance, const YoRadioPalette& palette);

void releaseAfterTreeDelete(Instance& instance);

} // namespace station_list_simple_paged
} // namespace lvgl_ui

#endif // STATION_LIST_SIMPLE_PAGED_H
