#ifndef SLEEP_TIMER_H
#define SLEEP_TIMER_H

#include <stdint.h>

// Persistent expiry action / постоянное действие по истечению (NVS via config).
enum class SleepTimerAction : uint8_t {
    StopRadio = 0,
    SleepDevice = 1,
};

void sleep_timer_init();
void sleep_timer_loop();

SleepTimerAction sleep_timer_action();
void sleep_timer_set_action(SleepTimerAction action);

bool sleep_timer_is_shutdown_active();

void sleep_timer_set_minutes(uint16_t minutes);
void sleep_timer_cancel();

bool sleep_timer_active();
uint32_t sleep_timer_remaining_seconds();
uint16_t sleep_timer_selected_minutes();

#endif
