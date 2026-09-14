// Unified radio/deep-sleep relative timers and managed shutdown.
// Единые относительные radio/deep-sleep таймеры и управляемый shutdown.
// Author: Witaliy76 - https://github.com/Witaliy76
#include "sleep_timer.h"

#include "config.h"
#include "display.h"
#include "options.h"
#include "player.h"
#include "save_manager.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

namespace {

struct TimerPhase {
  bool active = false;
  uint16_t snapshot_minutes = 0;
  uint32_t start_ms = 0;
  uint32_t duration_ms = 0;
  time_t event_at = 0;
};

struct TimerRuntimeState {
  TimerPhase radio_stop;
  TimerPhase radio_start;
  TimerPhase deep_sleep;
  uint16_t deep_sleep_wake_minutes = 0;
  time_t deep_sleep_wake_at = 0;
  bool sleep_request_pending = false;
  uint16_t requested_wake_minutes = 0;
  bool shutdown_active = false;
};

enum class SleepShutdownPhase : uint8_t {
  None = 0,
  WaitPlayer,
  Flush,
  DisplayOff,
  Settle,
  Enter,
};

// All countdown state has one owner module. Short critical sections make runtime commands from
// telnet/Serial/NetServer safe while expiry remains polled on DspTask; no background task exists.
// Всё состояние countdown принадлежит модулю. Короткие critical section защищают команды из
// telnet/Serial/NetServer, а expiry по-прежнему опрашивается на DspTask без фоновой задачи.
portMUX_TYPE s_timer_mux = portMUX_INITIALIZER_UNLOCKED;
TimerRuntimeState s_runtime;

SleepShutdownPhase s_shutdown = SleepShutdownPhase::None;
uint32_t s_shutdown_deadline_ms = 0;
uint32_t s_shutdown_settle_ms = 0;
bool s_resume_after_wake = false;
uint16_t s_wake_after_minutes_snapshot = 0;

uint32_t elapsed_ms(const TimerPhase& phase, uint32_t now) {
  // Unsigned subtraction is wrap-safe for all supported durations (< 2^31 ms).
  // Беззнаковое вычитание корректно переживает wrap millis() для наших интервалов.
  return static_cast<uint32_t>(now - phase.start_ms);
}

uint32_t remaining_seconds(const TimerPhase& phase, uint32_t now) {
  if (!phase.active || phase.duration_ms == 0) return 0;
  const uint32_t elapsed = elapsed_ms(phase, now);
  if (elapsed >= phase.duration_ms) return 0;
  return (phase.duration_ms - elapsed + 999u) / 1000u;
}

uint32_t phase_deadline_ms(const TimerPhase& phase) {
  return phase.start_ms + phase.duration_ms;
}

bool phase_is_later(const TimerPhase& later, const TimerPhase& earlier) {
  // Both active intervals are shorter than 2^31 ms, so signed deadline distance stays
  // wrap-safe. / Оба интервала короче 2^31 мс, поэтому signed-разность переживает wrap.
  return static_cast<int32_t>(phase_deadline_ms(later) - phase_deadline_ms(earlier)) > 0;
}

void clear_phase(TimerPhase& phase) {
  phase = TimerPhase{};
}

void clear_all_countdowns_locked() {
  clear_phase(s_runtime.radio_stop);
  clear_phase(s_runtime.radio_start);
  clear_phase(s_runtime.deep_sleep);
  s_runtime.deep_sleep_wake_minutes = 0;
  s_runtime.deep_sleep_wake_at = 0;
}

TimerPlanKind active_plan_locked() {
  if (s_runtime.deep_sleep.active || s_runtime.sleep_request_pending ||
      s_runtime.shutdown_active) {
    return TimerPlanKind::DeepSleep;
  }
  if (s_runtime.radio_stop.active || s_runtime.radio_start.active) {
    return TimerPlanKind::Radio;
  }
  return TimerPlanKind::None;
}

bool raw_minutes_valid(uint16_t minutes) {
  return minutes <= kTimerMaxMinutes;
}

time_t event_epoch(time_t now, uint16_t minutes) {
  return now > 0 && minutes > 0 ? now + static_cast<time_t>(minutes) * 60 : 0;
}

time_t event_epoch_seconds(time_t now, uint32_t seconds) {
  return now > 0 ? now + static_cast<time_t>(seconds) : 0;
}

TimerPhase make_phase(uint16_t minutes, uint32_t now_ms, time_t wall_now) {
  TimerPhase phase;
  if (minutes == 0) return phase;
  phase.active = true;
  phase.snapshot_minutes = minutes;
  phase.start_ms = now_ms;
  phase.duration_ms = static_cast<uint32_t>(minutes) * 60u * 1000u;
  phase.event_at = event_epoch(wall_now, minutes);
  return phase;
}

TimerCommandResult resolve_wake_request(DeepSleepWakeRequest request, uint16_t* out_minutes) {
  if (!out_minutes) return TimerCommandResult::InvalidMinutes;
  if (request.source == DeepSleepWakeRequest::Source::PersistentPreset) {
    if (!timer_preset_enabled(TimerPreset::DeepSleepWakeAfter)) {
      *out_minutes = 0;
      return TimerCommandResult::Applied;
    }
    *out_minutes = timer_preset_minutes(TimerPreset::DeepSleepWakeAfter);
    return *out_minutes > 0 ? TimerCommandResult::Applied
                            : TimerCommandResult::WakeIntervalRequired;
  }
  if (!raw_minutes_valid(request.minutes)) return TimerCommandResult::InvalidMinutes;
  *out_minutes = request.minutes;
  return TimerCommandResult::Applied;
}

bool publish_managed_sleep_request(uint16_t wake_minutes) {
  time_t wall_now = 0;
  timer_local_clock_now(&wall_now);
  portENTER_CRITICAL(&s_timer_mux);
  if (s_runtime.shutdown_active || s_runtime.sleep_request_pending) {
    portEXIT_CRITICAL(&s_timer_mux);
    return false;
  }
  clear_all_countdowns_locked();
  s_runtime.deep_sleep_wake_minutes = wake_minutes;
  s_runtime.deep_sleep_wake_at = event_epoch(wall_now, wake_minutes);
  s_runtime.sleep_request_pending = true;
  s_runtime.requested_wake_minutes = wake_minutes;
  portEXIT_CRITICAL(&s_timer_mux);
  // pending is the cross-task ingress. DspTask accepts it from sleep_timer_loop(); no producer
  // can block DspTask by sending to its own full display queue.
  // pending — межзадачный вход. DspTask принимает его в sleep_timer_loop(), без self-send.
  return true;
}

static void shutdown_display_off() {
#ifdef ENABLE_BRIGHTNESS_CONTROL
  dsp.setBrightness(0);
#endif
  config.setDspOn(false, false);
}

static void log_deep_sleep_wake_plan(uint16_t wake_minutes) {
  time_t now = 0;
  if (timer_local_clock_now(&now)) {
    const time_t wake_at = now + static_cast<time_t>(wake_minutes) * 60;
    struct tm wake_tm {};
    char local_time[24];
    if (localtime_r(&wake_at, &wake_tm) != nullptr &&
        strftime(local_time, sizeof(local_time), "%Y-%m-%d %H:%M", &wake_tm) > 0) {
      Serial.printf("[SLEEP] entering deep sleep; RTC wake in %u min at %s local\n",
                    static_cast<unsigned>(wake_minutes), local_time);
      return;
    }
  }
  Serial.printf("[SLEEP] entering deep sleep; RTC wake in %u min (local clock not synced)\n",
                static_cast<unsigned>(wake_minutes));
}

static void begin_accepted_sleep_shutdown(uint16_t wake_after_minutes) {
  s_shutdown = SleepShutdownPhase::WaitPlayer;
  s_shutdown_deadline_ms = millis() + 2500u;

  // Snapshot before PR_STOP: Player::_stop()/stopInfo() clears smartstart. Restore transient
  // resume intent before flush, but never overwrite the user's reserved smartstart == 2 choice.
  // Снимок до PR_STOP: stopInfo() обнулит smartstart. Возвращаем transient resume перед flush,
  // но никогда не перезаписываем пользовательское smartstart == 2.
  s_resume_after_wake = (player.status() != STOPPED);

  // WAKE AFTER SLEEP starts only after actual deep-sleep entry. This fixed snapshot is converted
  // to the RTC interval immediately before esp_deep_sleep_start(), never at plan-arm time.
  // WAKE AFTER SLEEP отсчитывается от фактического входа в сон; здесь хранится только snapshot.
  s_wake_after_minutes_snapshot = wake_after_minutes;

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
      if (player.status() == STOPPED ||
          static_cast<int32_t>(now - s_shutdown_deadline_ms) >= 0) {
        s_shutdown = SleepShutdownPhase::Flush;
      }
      break;
    case SleepShutdownPhase::Flush:
      if (s_resume_after_wake && config.store.smartstart < 2) {
        config.setSmartStart(1);
      }
      sm::flushSync();
      s_shutdown = SleepShutdownPhase::DisplayOff;
      break;
    case SleepShutdownPhase::DisplayOff:
      shutdown_display_off();
      s_shutdown_settle_ms = now;
      s_shutdown = SleepShutdownPhase::Settle;
      break;
    case SleepShutdownPhase::Settle:
      if (static_cast<uint32_t>(now - s_shutdown_settle_ms) < 50u) return;
      s_shutdown = SleepShutdownPhase::Enter;
      break;
    case SleepShutdownPhase::Enter: {
      sleep_configure_wakeup_pin();
      if (s_wake_after_minutes_snapshot > 0) {
        // Sole RTC timer registration. A zero wake interval simply skips this call; never disable
        // the TIMER source here because that broke the device-tested baseline.
        // Единственная регистрация RTC timer. При нуле вызов пропускается; TIMER здесь не выключаем.
        const esp_err_t err = esp_sleep_enable_timer_wakeup(
            static_cast<uint64_t>(s_wake_after_minutes_snapshot) * 60ULL * 1000000ULL);
        if (err == ESP_OK) {
          log_deep_sleep_wake_plan(s_wake_after_minutes_snapshot);
          Serial.printf("[WAKE] timer=%umin\n",
                        static_cast<unsigned>(s_wake_after_minutes_snapshot));
        } else {
          Serial.printf("[WAKE] timer enable failed err=%d\n", static_cast<int>(err));
        }
      } else {
        Serial.println("[SLEEP] entering deep sleep; RTC wake disabled (GPIO/Reset/power only)");
        Serial.println("[WAKE] timer=disabled");
      }
      esp_deep_sleep_start();
      break;
    }
    default:
      break;
  }
}

constexpr int kWakeLevelLow = LOW;
constexpr int kWakeLevelHigh = HIGH;
static_assert(kWakeLevelLow == 0, "EXT0 LOW is level 0");
static_assert(kWakeLevelHigh == 1, "EXT0 HIGH is level 1");

const char* wakeup_cause_name(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0: return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1: return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
    case ESP_SLEEP_WAKEUP_GPIO: return "GPIO";
    default: return nullptr;
  }
}

}  // namespace

void sleep_wakeup_early_init() {
  // After any real deep-sleep wake GPIO0 may still belong to RTC IO. Release it before the
  // shared ST7701 RGB line is initialized, for both EXT0 and TIMER wake causes.
  // После любого deep-sleep wake GPIO0 ещё может принадлежать RTC IO. Освобождаем его до RGB.
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const char* cause_name = wakeup_cause_name(cause);
  if (cause_name) Serial.printf("[WAKE] cause=%s\n", cause_name);

#if SOC_PM_SUPPORT_EXT0_WAKEUP
  if (WAKE_PIN == 255 || cause == ESP_SLEEP_WAKEUP_UNDEFINED) return;
  const gpio_num_t pin = static_cast<gpio_num_t>(WAKE_PIN);
  if (!esp_sleep_is_valid_wakeup_gpio(pin)) {
    Serial.printf("[WAKE] pin=%d unsupported\n", static_cast<int>(WAKE_PIN));
    return;
  }
  const esp_err_t err = rtc_gpio_deinit(pin);
  if (err != ESP_OK) {
    Serial.printf("[WAKE] pin=%d rtc_gpio_deinit failed err=%d\n",
                  static_cast<int>(WAKE_PIN), static_cast<int>(err));
    return;
  }
  Serial.printf("[WAKE] pin=%d restored to digital GPIO\n", static_cast<int>(WAKE_PIN));
#endif
}

void sleep_configure_wakeup_pin() {
#if SOC_PM_SUPPORT_EXT0_WAKEUP
  static_assert(WAKE_LEVEL == LOW || WAKE_LEVEL == HIGH,
                "WAKE_LEVEL must be LOW or HIGH");
  if (WAKE_PIN == 255) return;

  const gpio_num_t pin = static_cast<gpio_num_t>(WAKE_PIN);
  const int level = (WAKE_LEVEL == HIGH) ? kWakeLevelHigh : kWakeLevelLow;
  if (!esp_sleep_is_valid_wakeup_gpio(pin)) {
    Serial.printf("[WAKE] pin=%d unsupported\n", static_cast<int>(WAKE_PIN));
    return;
  }
  const esp_err_t err = esp_sleep_enable_ext0_wakeup(pin, level);
  if (err != ESP_OK) {
    Serial.printf("[WAKE] pin=%d enable failed err=%d\n",
                  static_cast<int>(WAKE_PIN), static_cast<int>(err));
    return;
  }
  Serial.printf("[WAKE] pin=%d level=%s\n",
                static_cast<int>(WAKE_PIN), (level == kWakeLevelHigh) ? "HIGH" : "LOW");
#else
  if (WAKE_PIN != 255) {
    Serial.printf("[WAKE] pin=%d unsupported\n", static_cast<int>(WAKE_PIN));
  }
#endif
}

uint16_t timer_sanitize_minutes(uint16_t raw_minutes) {
  return raw_minutes > kTimerMaxMinutes ? kTimerMaxMinutes : raw_minutes;
}

uint16_t timer_preset_minutes(TimerPreset preset) {
  uint16_t raw = 0;
  switch (preset) {
    case TimerPreset::RadioStop: raw = config.store.radio_stop_after_minutes; break;
    case TimerPreset::RadioStart: raw = config.store.radio_start_after_minutes; break;
    case TimerPreset::DeepSleepAfter: raw = config.store.deep_sleep_after_minutes; break;
    case TimerPreset::DeepSleepWakeAfter:
      raw = config.store.deep_sleep_wake_after_minutes;
      break;
  }
  return timer_sanitize_minutes(raw);
}

void timer_preset_set_minutes(TimerPreset preset, uint16_t total_minutes) {
  const uint16_t clean = timer_sanitize_minutes(total_minutes);
  switch (preset) {
    case TimerPreset::RadioStop:
      config.saveValue(&config.store.radio_stop_after_minutes, clean);
      break;
    case TimerPreset::RadioStart:
      config.saveValue(&config.store.radio_start_after_minutes, clean);
      break;
    case TimerPreset::DeepSleepAfter:
      config.saveValue(&config.store.deep_sleep_after_minutes, clean);
      break;
    case TimerPreset::DeepSleepWakeAfter:
      config.saveValue(&config.store.deep_sleep_wake_after_minutes, clean);
      break;
  }
}

bool timer_preset_enabled(TimerPreset preset) {
  switch (preset) {
    case TimerPreset::RadioStop: return config.store.radio_stop_timer_enabled;
    case TimerPreset::RadioStart: return config.store.radio_start_timer_enabled;
    case TimerPreset::DeepSleepAfter: return config.store.deep_sleep_timer_enabled;
    case TimerPreset::DeepSleepWakeAfter: return config.store.deep_sleep_wake_timer_enabled;
  }
  return false;
}

void timer_preset_set_enabled(TimerPreset preset, bool enabled) {
  switch (preset) {
    case TimerPreset::RadioStop:
      config.saveValue(&config.store.radio_stop_timer_enabled, enabled);
      break;
    case TimerPreset::RadioStart:
      config.saveValue(&config.store.radio_start_timer_enabled, enabled);
      break;
    case TimerPreset::DeepSleepAfter:
      config.saveValue(&config.store.deep_sleep_timer_enabled, enabled);
      break;
    case TimerPreset::DeepSleepWakeAfter:
      config.saveValue(&config.store.deep_sleep_wake_timer_enabled, enabled);
      break;
  }
}

bool timer_local_clock_now(time_t* out_now) {
  if (!out_now) return false;
  const time_t now = time(nullptr);
  struct tm local_tm {};
  if (now <= 0 || localtime_r(&now, &local_tm) == nullptr || local_tm.tm_year < 120) {
    *out_now = 0;
    return false;
  }
  *out_now = now;
  return true;
}

TimerCommandResult timer_schedule_radio_plan(uint16_t stop_after_minutes,
                                             uint16_t start_after_minutes) {
  if (!raw_minutes_valid(stop_after_minutes) || !raw_minutes_valid(start_after_minutes)) {
    return TimerCommandResult::InvalidMinutes;
  }
  if (stop_after_minutes == 0 && start_after_minutes == 0) {
    return TimerCommandResult::EmptyPlan;
  }
  if (stop_after_minutes > 0 && start_after_minutes > 0 &&
      start_after_minutes <= stop_after_minutes) {
    return TimerCommandResult::RadioStartNotAfterStop;
  }

  time_t wall_now = 0;
  timer_local_clock_now(&wall_now);
  portENTER_CRITICAL(&s_timer_mux);
  const TimerPlanKind active = active_plan_locked();
  if (active == TimerPlanKind::DeepSleep) {
    const bool shutdown = s_runtime.shutdown_active || s_runtime.sleep_request_pending;
    portEXIT_CRITICAL(&s_timer_mux);
    return shutdown ? TimerCommandResult::ShutdownInProgress
                    : TimerCommandResult::ConflictDeepSleepActive;
  }
  const uint32_t now_ms = millis();
  s_runtime.radio_stop = make_phase(stop_after_minutes, now_ms, wall_now);
  s_runtime.radio_start = make_phase(start_after_minutes, now_ms, wall_now);
  portEXIT_CRITICAL(&s_timer_mux);
  return TimerCommandResult::Applied;
}

TimerCommandResult timer_schedule_radio_stop(uint16_t minutes) {
  if (!raw_minutes_valid(minutes)) return TimerCommandResult::InvalidMinutes;
  if (minutes == 0) {
    timer_cancel_radio_stop();
    return TimerCommandResult::Cancelled;
  }
  time_t wall_now = 0;
  timer_local_clock_now(&wall_now);
  portENTER_CRITICAL(&s_timer_mux);
  const TimerPlanKind active = active_plan_locked();
  if (active == TimerPlanKind::DeepSleep) {
    const bool shutdown = s_runtime.shutdown_active || s_runtime.sleep_request_pending;
    portEXIT_CRITICAL(&s_timer_mux);
    return shutdown ? TimerCommandResult::ShutdownInProgress
                    : TimerCommandResult::ConflictDeepSleepActive;
  }
  const uint32_t now_ms = millis();
  const TimerPhase next_stop = make_phase(minutes, now_ms, wall_now);
  if (s_runtime.radio_start.active && !phase_is_later(s_runtime.radio_start, next_stop)) {
    portEXIT_CRITICAL(&s_timer_mux);
    return TimerCommandResult::RadioStartNotAfterStop;
  }
  s_runtime.radio_stop = next_stop;
  portEXIT_CRITICAL(&s_timer_mux);
  return TimerCommandResult::Applied;
}

TimerCommandResult timer_schedule_radio_start(uint16_t minutes) {
  if (!raw_minutes_valid(minutes)) return TimerCommandResult::InvalidMinutes;
  if (minutes == 0) {
    timer_cancel_radio_start();
    return TimerCommandResult::Cancelled;
  }
  time_t wall_now = 0;
  timer_local_clock_now(&wall_now);
  portENTER_CRITICAL(&s_timer_mux);
  const TimerPlanKind active = active_plan_locked();
  if (active == TimerPlanKind::DeepSleep) {
    const bool shutdown = s_runtime.shutdown_active || s_runtime.sleep_request_pending;
    portEXIT_CRITICAL(&s_timer_mux);
    return shutdown ? TimerCommandResult::ShutdownInProgress
                    : TimerCommandResult::ConflictDeepSleepActive;
  }
  const uint32_t now_ms = millis();
  const TimerPhase next_start = make_phase(minutes, now_ms, wall_now);
  if (s_runtime.radio_stop.active && !phase_is_later(next_start, s_runtime.radio_stop)) {
    portEXIT_CRITICAL(&s_timer_mux);
    return TimerCommandResult::RadioStartNotAfterStop;
  }
  s_runtime.radio_start = next_start;
  portEXIT_CRITICAL(&s_timer_mux);
  return TimerCommandResult::Applied;
}

void timer_cancel_radio_stop() {
  portENTER_CRITICAL(&s_timer_mux);
  clear_phase(s_runtime.radio_stop);
  portEXIT_CRITICAL(&s_timer_mux);
}

void timer_cancel_radio_start() {
  portENTER_CRITICAL(&s_timer_mux);
  clear_phase(s_runtime.radio_start);
  portEXIT_CRITICAL(&s_timer_mux);
}

void timer_cancel_radio_plan() {
  portENTER_CRITICAL(&s_timer_mux);
  clear_phase(s_runtime.radio_stop);
  clear_phase(s_runtime.radio_start);
  portEXIT_CRITICAL(&s_timer_mux);
}

TimerCommandResult timer_schedule_deep_sleep(uint16_t after_minutes,
                                             DeepSleepWakeRequest wake_request) {
  if (!raw_minutes_valid(after_minutes)) return TimerCommandResult::InvalidMinutes;
  if (after_minutes == 0) return TimerCommandResult::EmptyPlan;
  uint16_t wake_minutes = 0;
  const TimerCommandResult wake_result = resolve_wake_request(wake_request, &wake_minutes);
  if (wake_result != TimerCommandResult::Applied) return wake_result;

  time_t wall_now = 0;
  timer_local_clock_now(&wall_now);
  portENTER_CRITICAL(&s_timer_mux);
  const TimerPlanKind active = active_plan_locked();
  if (active == TimerPlanKind::Radio) {
    portEXIT_CRITICAL(&s_timer_mux);
    return TimerCommandResult::ConflictRadioActive;
  }
  if (s_runtime.shutdown_active || s_runtime.sleep_request_pending) {
    portEXIT_CRITICAL(&s_timer_mux);
    return TimerCommandResult::ShutdownInProgress;
  }
  const uint32_t now_ms = millis();
  s_runtime.deep_sleep = make_phase(after_minutes, now_ms, wall_now);
  s_runtime.deep_sleep_wake_minutes = wake_minutes;
  s_runtime.deep_sleep_wake_at =
      s_runtime.deep_sleep.event_at > 0
          ? event_epoch(s_runtime.deep_sleep.event_at, wake_minutes)
          : 0;
  portEXIT_CRITICAL(&s_timer_mux);
  return TimerCommandResult::Applied;
}

TimerCommandResult timer_cancel_deep_sleep() {
  portENTER_CRITICAL(&s_timer_mux);
  if (s_runtime.shutdown_active) {
    portEXIT_CRITICAL(&s_timer_mux);
    return TimerCommandResult::ShutdownInProgress;
  }
  clear_phase(s_runtime.deep_sleep);
  s_runtime.deep_sleep_wake_minutes = 0;
  s_runtime.deep_sleep_wake_at = 0;
  s_runtime.sleep_request_pending = false;
  s_runtime.requested_wake_minutes = 0;
  portEXIT_CRITICAL(&s_timer_mux);
  return TimerCommandResult::Cancelled;
}

TimerCommandResult timer_request_deep_sleep_now(DeepSleepWakeRequest wake_request) {
  uint16_t wake_minutes = 0;
  const TimerCommandResult wake_result = resolve_wake_request(wake_request, &wake_minutes);
  if (wake_result != TimerCommandResult::Applied) return wake_result;
  // An explicit immediate sleep is allowed to cancel a Radio plan; nothing is cancelled silently
  // for delayed cross-plan requests. / Явный немедленный сон может отменить Radio plan.
  return publish_managed_sleep_request(wake_minutes) ? TimerCommandResult::Applied
                                                      : TimerCommandResult::ShutdownInProgress;
}

TimerPlanKind timer_active_plan() {
  portENTER_CRITICAL(&s_timer_mux);
  const TimerPlanKind plan = active_plan_locked();
  portEXIT_CRITICAL(&s_timer_mux);
  return plan;
}

TimerRuntimeSnapshot timer_runtime_snapshot() {
  TimerRuntimeSnapshot snapshot;
  portENTER_CRITICAL(&s_timer_mux);
  // Sample millis only after the protected state is stable. A concurrent setter can no longer
  // publish start_ms newer than this sample and look instantly expired after unsigned subtract.
  // millis читается после захвата state: новый start_ms не окажется новее снимка now.
  const uint32_t now = millis();
  snapshot.plan = active_plan_locked();
  snapshot.radio_stop_active = s_runtime.radio_stop.active;
  snapshot.radio_start_active = s_runtime.radio_start.active;
  snapshot.deep_sleep_active = s_runtime.deep_sleep.active;
  snapshot.shutdown_active = s_runtime.shutdown_active || s_runtime.sleep_request_pending;
  snapshot.radio_stop_minutes = s_runtime.radio_stop.snapshot_minutes;
  snapshot.radio_start_minutes = s_runtime.radio_start.snapshot_minutes;
  snapshot.deep_sleep_after_minutes = s_runtime.deep_sleep.snapshot_minutes;
  snapshot.deep_sleep_wake_after_minutes =
      s_runtime.sleep_request_pending ? s_runtime.requested_wake_minutes
                                      : s_runtime.deep_sleep_wake_minutes;
  snapshot.radio_stop_remaining_seconds = remaining_seconds(s_runtime.radio_stop, now);
  snapshot.radio_start_remaining_seconds = remaining_seconds(s_runtime.radio_start, now);
  snapshot.deep_sleep_remaining_seconds = remaining_seconds(s_runtime.deep_sleep, now);
  portEXIT_CRITICAL(&s_timer_mux);

  // Event epochs are display projections, rebuilt from current wall time plus the immutable
  // monotonic remainder. They recover after late clock sync and follow later clock corrections
  // without restarting or retargeting a timer. / Epoch для UI строится из актуальных часов и
  // monotonic remainder; синхронизация/коррекция часов не меняет сам countdown.
  time_t wall_now = 0;
  if (timer_local_clock_now(&wall_now)) {
    if (snapshot.radio_stop_active) {
      snapshot.radio_stop_at =
          event_epoch_seconds(wall_now, snapshot.radio_stop_remaining_seconds);
    }
    if (snapshot.radio_start_active) {
      snapshot.radio_start_at =
          event_epoch_seconds(wall_now, snapshot.radio_start_remaining_seconds);
    }
    if (snapshot.deep_sleep_active) {
      snapshot.deep_sleep_at =
          event_epoch_seconds(wall_now, snapshot.deep_sleep_remaining_seconds);
    }
    if (snapshot.deep_sleep_wake_after_minutes > 0 &&
        (snapshot.deep_sleep_active || snapshot.shutdown_active)) {
      const uint32_t until_sleep_seconds =
          snapshot.deep_sleep_active ? snapshot.deep_sleep_remaining_seconds : 0u;
      snapshot.deep_sleep_wake_at = event_epoch_seconds(
          wall_now,
          until_sleep_seconds +
              static_cast<uint32_t>(snapshot.deep_sleep_wake_after_minutes) * 60u);
    }
  }
  return snapshot;
}

void sleep_timer_request_device_sleep() {
  if (s_shutdown != SleepShutdownPhase::None) return;

  uint16_t wake_minutes = 0;
  portENTER_CRITICAL(&s_timer_mux);
  if (!s_runtime.sleep_request_pending || s_runtime.shutdown_active) {
    portEXIT_CRITICAL(&s_timer_mux);
    return;
  }

  // Accept pending atomically: a cancellation that wins this lock prevents sleep; after this
  // transition cancellation sees shutdown_active and must report ShutdownInProgress. No Player,
  // storage, logging, LVGL, or peripheral work is done under the mux.
  // Принимаем pending атомарно: отмена до этого lock предотвращает сон, после — видит shutdown.
  wake_minutes = s_runtime.requested_wake_minutes;
  clear_all_countdowns_locked();
  s_runtime.deep_sleep_wake_minutes = wake_minutes;
  s_runtime.sleep_request_pending = false;
  s_runtime.requested_wake_minutes = 0;
  s_runtime.shutdown_active = true;
  portEXIT_CRITICAL(&s_timer_mux);
  begin_accepted_sleep_shutdown(wake_minutes);
}

bool sleep_timer_is_shutdown_active() {
  portENTER_CRITICAL(&s_timer_mux);
  const bool active = s_runtime.shutdown_active || s_runtime.sleep_request_pending;
  portEXIT_CRITICAL(&s_timer_mux);
  return active;
}

bool sleep_timer_active() {
  portENTER_CRITICAL(&s_timer_mux);
  const bool active = s_runtime.radio_stop.active;
  portEXIT_CRITICAL(&s_timer_mux);
  return active;
}

uint32_t sleep_timer_remaining_seconds() {
  portENTER_CRITICAL(&s_timer_mux);
  const uint32_t now = millis();
  const uint32_t seconds = remaining_seconds(s_runtime.radio_stop, now);
  portEXIT_CRITICAL(&s_timer_mux);
  return seconds;
}

uint16_t sleep_timer_selected_minutes() {
  portENTER_CRITICAL(&s_timer_mux);
  const uint16_t minutes = s_runtime.radio_stop.snapshot_minutes;
  portEXIT_CRITICAL(&s_timer_mux);
  return minutes;
}

void sleep_timer_init() {
  portENTER_CRITICAL(&s_timer_mux);
  s_runtime = TimerRuntimeState{};
  portEXIT_CRITICAL(&s_timer_mux);
  s_shutdown = SleepShutdownPhase::None;
  s_shutdown_deadline_ms = 0;
  s_shutdown_settle_ms = 0;
  s_resume_after_wake = false;
  s_wake_after_minutes_snapshot = 0;

  // Defensive sanitize: migration seeds only genuinely new fields; the renamed wake field keeps
  // its prior bytes/value. Persist only when corrupted data lies outside 0..1499.
  // Защитная санация: migration обнуляет только новые поля, переименованный wake сохраняет offset.
  const TimerPreset presets[] = {
      TimerPreset::RadioStop,
      TimerPreset::RadioStart,
      TimerPreset::DeepSleepAfter,
      TimerPreset::DeepSleepWakeAfter,
  };
  for (TimerPreset preset : presets) {
    const uint16_t raw = [&]() {
      switch (preset) {
        case TimerPreset::RadioStop: return config.store.radio_stop_after_minutes;
        case TimerPreset::RadioStart: return config.store.radio_start_after_minutes;
        case TimerPreset::DeepSleepAfter: return config.store.deep_sleep_after_minutes;
        case TimerPreset::DeepSleepWakeAfter:
          return config.store.deep_sleep_wake_after_minutes;
      }
      return static_cast<uint16_t>(0);
    }();
    const uint16_t clean = timer_sanitize_minutes(raw);
    if (clean != raw) timer_preset_set_minutes(preset, clean);
  }
}

void sleep_timer_loop() {
  if (s_shutdown != SleepShutdownPhase::None) {
    run_shutdown_steps();
    return;
  }

  // Non-display tasks and LVGL callbacks only publish pending. This DspTask safe point owns the
  // atomic accept and every shutdown side effect. / Другие task только ставят pending; принимает
  // и исполняет shutdown исключительно эта безопасная точка DspTask.
  sleep_timer_request_device_sleep();
  if (s_shutdown != SleepShutdownPhase::None) return;

  bool stop_expired = false;
  bool start_expired = false;
  bool deep_expired = false;
  uint32_t stop_deadline = 0;
  uint32_t start_deadline = 0;
  uint16_t wake_minutes = 0;

  portENTER_CRITICAL(&s_timer_mux);
  const uint32_t now = millis();
  if (s_runtime.radio_stop.active &&
      elapsed_ms(s_runtime.radio_stop, now) >= s_runtime.radio_stop.duration_ms) {
    stop_expired = true;
    stop_deadline = s_runtime.radio_stop.start_ms + s_runtime.radio_stop.duration_ms;
    clear_phase(s_runtime.radio_stop);
  }
  if (s_runtime.radio_start.active &&
      elapsed_ms(s_runtime.radio_start, now) >= s_runtime.radio_start.duration_ms) {
    start_expired = true;
    start_deadline = s_runtime.radio_start.start_ms + s_runtime.radio_start.duration_ms;
    clear_phase(s_runtime.radio_start);
  }
  if (s_runtime.deep_sleep.active &&
      elapsed_ms(s_runtime.deep_sleep, now) >= s_runtime.deep_sleep.duration_ms) {
    deep_expired = true;
    wake_minutes = s_runtime.deep_sleep_wake_minutes;
    clear_phase(s_runtime.deep_sleep);
    s_runtime.deep_sleep_wake_at = 0;
    s_runtime.sleep_request_pending = true;
    s_runtime.requested_wake_minutes = wake_minutes;
  }
  portEXIT_CRITICAL(&s_timer_mux);

  if (deep_expired) {
    // Cancellation may win after expiry published pending and before this accept. The consumer
    // rechecks under the same mux, so a cancelled/stale expiry cannot start shutdown.
    // Отмена может победить между publish и accept; повторная проверка не запустит старый запрос.
    sleep_timer_request_device_sleep();
    return;
  }

  // If DspTask was delayed long enough for both radio phases to expire in one poll, apply only
  // the chronologically later desired state. This preserves final intent without racing two
  // asynchronous Player commands. / При одновременном catch-up применяем итог более поздней фазы.
  if (stop_expired && start_expired) {
    const bool start_is_later = static_cast<int32_t>(start_deadline - stop_deadline) > 0;
    stop_expired = !start_is_later;
    start_expired = start_is_later;
  }

  if (stop_expired) {
    if (player.status() != STOPPED) player.sendCommand({PR_STOP, 0});
  } else if (start_expired) {
    if (player.status() == STOPPED) player.sendCommand({PR_PLAY, config.lastStation()});
  }
}
