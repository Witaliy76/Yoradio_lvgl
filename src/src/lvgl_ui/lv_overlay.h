#ifndef LV_OVERLAY_H
#define LV_OVERLAY_H

// Stage 5.7: modal overlays on lv_layer_top() — LOST / UPDATING (not pages).
// Этап 5.7: модальные оверлеи на lv_layer_top() — LOST / UPDATING (не страницы).
// DspTask only; no ownership in Main screen class.

namespace lvgl_ui {

void overlayHideAll();
void overlayShowLost();
void overlayShowUpdating();

} // namespace lvgl_ui

#endif
