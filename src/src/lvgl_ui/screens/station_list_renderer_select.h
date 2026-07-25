// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef STATION_LIST_RENDERER_SELECT_H
#define STATION_LIST_RENDERER_SELECT_H

/*
 * station_list_renderer_select — compile-time Station list renderer dispatch (STATIONPAGED-2).
 * station_list_renderer_select — выбор renderer списка Station на этапе компиляции (STATIONPAGED-2).
 *
 * Define STATION_LIST_SIMPLE_PAGED in src/myoptions.h (via options.h include chain).
 * Undefined or 0 → legacy continuous scroll; 1 → paged instant flip.
 */

#include "../core/options.h"

#if defined(STATION_LIST_SIMPLE_PAGED) && STATION_LIST_SIMPLE_PAGED

#include "station_list_simple_paged.h"

namespace lvgl_ui {
namespace station_list_active = station_list_simple_paged;
} // namespace lvgl_ui

#else

#include "station_list_legacy_scroll.h"

namespace lvgl_ui {
namespace station_list_active = station_list_legacy_scroll;
} // namespace lvgl_ui

#endif

#endif // STATION_LIST_RENDERER_SELECT_H
