#ifndef LV_OVERLAY_H
#define LV_OVERLAY_H

// Stage 5.7: modal overlays on lv_layer_top() — LOST / UPDATING (not pages).
// Этап 5.7: модальные оверлеи на lv_layer_top() — LOST / UPDATING (не страницы).
// DspTask only; no ownership in Main screen class.
// S6V8A + S6V9I: LOST overlay extended with a status sub-label (reconnect + Recovery copy).
// S6V8A + S6V9I: оверлей LOST — вторая строка (reconnect + текст эскалации Recovery).

namespace lvgl_ui {

void overlayHideAll();
void overlayShowLost();
void overlayShowUpdating();
// S6V8A: update LOST overlay status line (safe when overlay not active; strcmp guard inside).
// S6V8A: обновить статусную строку LOST overlay (безопасно если overlay не показан; strcmp внутри).
void overlayLostSetStatusText(const char* text);

} // namespace lvgl_ui

#endif
