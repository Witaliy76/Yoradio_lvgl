// Stage 5.6: full-screen screensaver overlay (lv_layer_top), not ILvglScreen / not PageChain.
// Этап 5.6: полноэкранный оверлей screensaver (lv_layer_top), не ILvglScreen / не PageChain.

#ifndef LV_SCREENSAVER_H
#define LV_SCREENSAVER_H

namespace lvgl_ui {

void screensaverHide();
void screensaverShow();
void screensaverRefreshClock();
bool screensaverIsVisible();

} // namespace lvgl_ui

#endif
