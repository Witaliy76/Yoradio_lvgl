#pragma once
#include "../core/options.h"

#if DSP_MODEL == DSP_ST7701
#include "displayST7701.h"
#else
#error "Unsupported DSP_MODEL: this LVGL-only build currently supports DSP_ST7701 only"
#endif
