#ifndef LV_UI_EVENTS_H
#define LV_UI_EVENTS_H

#include "../core/common.h"

// Stage 3.1: minimal event abstraction from displayQueue into LVGL layer.
// Preserves access to the full original request (type + payload, future fields).
struct DisplayEvent {
    displayRequestType_e type;
    const requestParams_t* request;
    displayMode_e mode;
};

namespace lvgl_ui {
void onDisplayEvent(const DisplayEvent& evt);
}

#endif
