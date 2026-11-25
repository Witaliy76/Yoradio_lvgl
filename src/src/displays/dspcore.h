#pragma once
#include "../core/options.h"

// platform/composition.h chooses the display host from the board profile.
// DspCore (display_rgb_panel.*) drives the Rgb3Wire host (rgb_3wire_backend.*).
// Хост дисплея выбирает platform/composition.h по профилю платы.
// DspCore (display_rgb_panel.*) работает с хостом Rgb3Wire (rgb_3wire_backend.*).
#include "../platform/composition.h"
#include "display_rgb_panel.h"
#ifdef __cplusplus
static_assert(yoradio::composition::kDisplayHost == yoradio::composition::DisplayHost::Rgb3Wire,
              "YoRadio: DspCore implements only the Rgb3Wire display host chosen by platform/composition.h.");
#endif
