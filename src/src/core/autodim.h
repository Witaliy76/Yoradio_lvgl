#ifndef AUTODIM_H
#define AUTODIM_H

#include <stdint.h>

// Runtime Auto Dim: temporary backlight reduction without overwriting config.store.brightness.
// Runtime Auto Dim: временное снижение яркости без перезаписи config.store.brightness.
// Author: Witaliy76 - https://github.com/Witaliy76

void autodim_notify_activity(const char* source);

void autodim_loop();
void autodim_on_disabled();
void autodim_on_dim_level_changed();
bool autodim_is_dimmed();

// DIM LEVEL max: max(1, brightness - 1) / верхняя граница DIM LEVEL.
uint8_t autodim_level_max_for_brightness(uint8_t brightness);

// Runtime: min(autodim_level, max(1, brightness - 1)), floor 1.
uint8_t autodim_effective_level();

// Boot/load sanity after partial Ai section overlay / проверка после загрузки.
bool autodim_sanitize_store();

#endif
