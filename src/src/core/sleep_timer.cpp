// Sleep Timer implementation (stop radio or deep sleep).
// Author: Witaliy76 - https://github.com/Witaliy76
#include "sleep_timer.h"

#include "config.h"
#include "display.h"
#include "options.h"
#include "player.h"
#include "save_manager.h"

#include <Arduino.h>
#include <esp_sleep.h>

namespace {

struct SleepTimerState {
  bool active = false;
  uint16_t selected_minutes = 0;
  uint32_t start_ms = 0;
  uint32_t duration_ms = 0;
};

enum class SleepShutdownPhase : uint8_t {
  None = 0,
  WaitPlayer,
  Flush,
  DisplayOff,
  Settle,
  Enter,
};

SleepTimerState s_sleep_timer;
SleepShutdownPhase s_shutdown = SleepShutdownPhase::None;
uint32_t s_shutdown_deadline_ms = 0;
uint32_t s_shutdown_settle_ms = 0;

uint32_t elapsed_ms(uint32_t now) {
  return static_cast<uint32_t>(now - s_sleep_timer.start_ms);
}

void clear_countdown_state() {
  s_sleep_timer.active = false;
  s_sleep_timer.selected_minutes = 0;
  s_sleep_timer.start_ms = 0;
  s_sleep_timer.duration_ms = 0;
}

SleepTimerAction sanitize_action(uint8_t raw) {
  if (raw == static_cast<uint8_t>(SleepTimerAction::SleepDevice)) {
    return SleepTimerAction::SleepDevice;
  }
  return SleepTimerAction::StopRadio;
}

static void shutdown_display_off() {
#ifdef ENABLE_BRIGHTNESS_CONTROL
  dsp.setBrightness(0);
#endif
  config.setDspOn(false, false);
}

static void begin_sleep_device_shutdown() {
  clear_countdown_state();
  s_shutdown = SleepShutdownPhase::WaitPlayer;
  s_shutdown_deadline_ms = millis() + 2500u;
  if (player.status() != STOPPED) {
    player.sendCommand({PR_STOP, 0});
  } else {
    s_shutdown = SleepShutdownPhase::Flush;
  }
}

static void run_shutdown_steps() {
  const uint32_t now = millis();
  switch (s_shutdown) {
    case SleepShutdownPhase::WaitPlayer:
      if (player.status() == STOPPED || now >= s_shutdown_deadline_ms) {
        s_shutdown = SleepShutdownPhase::Flush;
      }
      break;
    case SleepShutdownPhase::Flush:
      sm::flushSync();
      s_shutdown = SleepShutdownPhase::DisplayOff;
      break;
    case SleepShutdownPhase::DisplayOff:
      shutdown_display_off();
      s_shutdown_settle_ms = now;
      s_shutdown = SleepShutdownPhase::Settle;
      break;
    case SleepShutdownPhase::Settle:
      if (now - s_shutdown_settle_ms < 50u) {
        return;
      }
      s_shutdown = SleepShutdownPhase::Enter;
      break;
    case SleepShutdownPhase::Enter:
      esp_deep_sleep_start();
      break;
    default:
      break;
  }
}

}  // namespace

void sleep_timer_init() {
  s_sleep_timer = SleepTimerState{};
  s_shutdown = SleepShutdownPhase::None;
  s_shutdown_deadline_ms = 0;
  s_shutdown_settle_ms = 0;

  const uint8_t raw = config.store.sleep_timer_action;
  if (raw > static_cast<uint8_t>(SleepTimerAction::SleepDevice)) {
    config.saveValue(&config.store.sleep_timer_action,
                     static_cast<uint8_t>(SleepTimerAction::StopRadio));
  }
}

SleepTimerAction sleep_timer_action() {
  return sanitize_action(config.store.sleep_timer_action);
}

void sleep_timer_set_action(SleepTimerAction action) {
  const uint8_t raw = static_cast<uint8_t>(sanitize_action(static_cast<uint8_t>(action)));
  config.saveValue(&config.store.sleep_timer_action, raw);
}

bool sleep_timer_is_shutdown_active() {
  return s_shutdown != SleepShutdownPhase::None;
}

void sleep_timer_set_minutes(uint16_t minutes) {
  if (minutes == 0) {
    sleep_timer_cancel();
    return;
  }

  s_sleep_timer.selected_minutes = minutes;
  s_sleep_timer.start_ms = millis();
  s_sleep_timer.duration_ms = static_cast<uint32_t>(minutes) * 60UL * 1000UL;
  s_sleep_timer.active = true;
}

void sleep_timer_cancel() {
  clear_countdown_state();
}

bool sleep_timer_active() {
  return s_sleep_timer.active;
}

uint32_t sleep_timer_remaining_seconds() {
  if (!s_sleep_timer.active || s_sleep_timer.duration_ms == 0) {
    return 0;
  }

  const uint32_t elapsed = elapsed_ms(millis());
  if (elapsed >= s_sleep_timer.duration_ms) {
    return 0;
  }

  const uint32_t remaining_ms = s_sleep_timer.duration_ms - elapsed;
  return (remaining_ms + 999UL) / 1000UL;
}

uint16_t sleep_timer_selected_minutes() {
  return s_sleep_timer.selected_minutes;
}

void sleep_timer_loop() {
  if (s_shutdown != SleepShutdownPhase::None) {
    run_shutdown_steps();
    return;
  }

  if (!s_sleep_timer.active || s_sleep_timer.duration_ms == 0) {
    return;
  }

  if (elapsed_ms(millis()) < s_sleep_timer.duration_ms) {
    return;
  }

  const SleepTimerAction action = sleep_timer_action();

  if (action == SleepTimerAction::SleepDevice) {
    begin_sleep_device_shutdown();
    return;
  }

  clear_countdown_state();
  if (player.status() != STOPPED) {
    player.sendCommand({PR_STOP, 0});
  }
}
