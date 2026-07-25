// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef WGT_FOOTER_PILL_H
#define WGT_FOOTER_PILL_H

/*
 * wgt_footer_pill — shared visual contract for full-width clickable footer pills.
 * Shared visual contract для кликабельных footer-pill (Weather, Station, Preset).
 *
 * Owns: fixed normal/pressed visual states and palette-dependent colors.
 * Не владеет: geometry, padding, text, callbacks, navigation, gesture flags.
 *
 * DspTask-only — all lv_* calls.
 */

#include "lvgl.h"

namespace lvgl_ui {

// Forward declaration — full definition in lv_theme_yoradio.h.
// Используется только как const ref; не нужен полный include в этом header.
struct YoRadioPalette;

namespace wgt_footer_pill {

// Apply fixed visual contract to a pill surface object.
// Sets: gradient off, shadow off, radius, border width/opacity, normal/pressed bg opacity,
//       CLICKABLE, not SCROLLABLE.
// Does NOT set: size, padding, flex, parent, alignment, callbacks, text, gesture flags.
//
// Применить fixed visual contract к объекту pill.
// Устанавливает: gradient off, shadow off, radius, border width/opacity, normal/pressed bg opacity,
//               CLICKABLE, not SCROLLABLE.
// Не устанавливает: размер, padding, flex, parent, alignment, callbacks, текст, gesture flags.
void prepare_surface(lv_obj_t* obj);

// Apply palette-dependent colors to a pill surface object.
// Must be called after prepare_surface() on create and on liveReapplyTheme().
// Does NOT touch fixed opacities, radius, border width, padding, flags or callbacks.
//
// Применить palette-зависимые цвета к объекту pill.
// Вызывать после prepare_surface() при create и при liveReapplyTheme().
// Не трогает fixed opacity, radius, border width, padding, flags, callbacks.
void apply_palette(lv_obj_t* obj, const YoRadioPalette& pal);

// Mark a child object as non-clickable and non-scrollable so the pill surface remains the tap target.
// Пометить child как некликабельный и non-scrollable — чтобы pill surface оставался таргетом тапа.
void make_child_passive(lv_obj_t* child);

} // namespace wgt_footer_pill
} // namespace lvgl_ui

#endif // WGT_FOOTER_PILL_H
