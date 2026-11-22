#pragma once
#include "../core/options.h"

// The display host is chosen by the board profile, not by a user selector.
// Only the ST7701 RGB host exists until S07_bis-5 adds platform/composition.h.
// Хост дисплея выбирает профиль платы, а не пользовательский селектор.
// До S07_bis-5 (platform/composition.h) существует только хост ST7701 RGB.
#include "displayST7701.h"
#ifdef __cplusplus
static_assert(yoradio::kActiveBoard.display.controller == yoradio::PanelController::St7701,
              "YoRadio: the selected board's panel has no display host yet (S07_bis-5 composition).");
#endif
