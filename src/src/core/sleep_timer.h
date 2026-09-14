#ifndef SLEEP_TIMER_H
#define SLEEP_TIMER_H

#include <stdint.h>
#include <time.h>

// Unified relative timers for radio stop/start and managed Deep Sleep.
// Единые относительные таймеры остановки/запуска радио и управляемого Deep Sleep.
// Author: Witaliy76 - https://github.com/Witaliy76

static constexpr uint16_t kTimerMinMinutes = 0;
static constexpr uint16_t kTimerMaxMinutes = 24u * 60u + 59u;  // 1499 = 24h59m

enum class TimerPlanKind : uint8_t {
  None = 0,
  Radio,
  DeepSleep,
};

enum class TimerPreset : uint8_t {
  RadioStop = 0,
  RadioStart,
  DeepSleepAfter,
  DeepSleepWakeAfter,
};

enum class TimerCommandResult : uint8_t {
  Applied = 0,
  Cancelled,
  InvalidMinutes,
  EmptyPlan,
  WakeIntervalRequired,
  RadioStartNotAfterStop,
  ConflictRadioActive,
  ConflictDeepSleepActive,
  ShutdownInProgress,
};

// Explicit wake-source contract: a caller either snapshots the persisted Deep Sleep preset or
// supplies a one-shot value (including explicit 0 = no RTC wake). No magic integer sentinel.
// Явный контракт wake: либо снимок сохранённого пресета, либо разовое значение, включая 0.
struct DeepSleepWakeRequest {
  enum class Source : uint8_t {
    PersistentPreset = 0,
    ExplicitMinutes,
  };

  Source source = Source::PersistentPreset;
  uint16_t minutes = 0;

  static DeepSleepWakeRequest persistent() {
    return DeepSleepWakeRequest{};
  }
  static DeepSleepWakeRequest explicitMinutes(uint16_t value) {
    DeepSleepWakeRequest request;
    request.source = Source::ExplicitMinutes;
    request.minutes = value;
    return request;
  }
};

// Read-only runtime snapshot used by Settings. Active snapshots are distinct from persisted
// presets: editing a preset never retargets an already running timer.
// Read-only снимок для Settings. Активные интервалы отделены от сохранённых пресетов.
struct TimerRuntimeSnapshot {
  TimerPlanKind plan = TimerPlanKind::None;
  bool radio_stop_active = false;
  bool radio_start_active = false;
  bool deep_sleep_active = false;
  bool shutdown_active = false;
  uint16_t radio_stop_minutes = 0;
  uint16_t radio_start_minutes = 0;
  uint16_t deep_sleep_after_minutes = 0;
  uint16_t deep_sleep_wake_after_minutes = 0;
  uint32_t radio_stop_remaining_seconds = 0;
  uint32_t radio_start_remaining_seconds = 0;
  uint32_t deep_sleep_remaining_seconds = 0;
  time_t radio_stop_at = 0;
  time_t radio_start_at = 0;
  time_t deep_sleep_at = 0;
  time_t deep_sleep_wake_at = 0;
};

void sleep_timer_init();
void sleep_timer_loop();

uint16_t timer_sanitize_minutes(uint16_t raw_minutes);
uint16_t timer_preset_minutes(TimerPreset preset);
void timer_preset_set_minutes(TimerPreset preset, uint16_t total_minutes);
bool timer_preset_enabled(TimerPreset preset);
void timer_preset_set_enabled(TimerPreset preset, bool enabled);

TimerCommandResult timer_schedule_radio_plan(uint16_t stop_after_minutes,
                                             uint16_t start_after_minutes);
TimerCommandResult timer_schedule_radio_stop(uint16_t minutes);
TimerCommandResult timer_schedule_radio_start(uint16_t minutes);
void timer_cancel_radio_stop();
void timer_cancel_radio_start();
void timer_cancel_radio_plan();

TimerCommandResult timer_schedule_deep_sleep(uint16_t after_minutes,
                                             DeepSleepWakeRequest wake_request);
TimerCommandResult timer_cancel_deep_sleep();

// Safe from telnet/Serial, NetServer, controls, or UI: snapshots the wake request and publishes
// pending state. DspTask accepts it from sleep_timer_loop(); no display-queue self-send is used.
// Безопасно из любого caller: фиксирует wake и pending; принимает его sleep_timer_loop на DspTask.
TimerCommandResult timer_request_deep_sleep_now(DeepSleepWakeRequest wake_request);

TimerPlanKind timer_active_plan();
TimerRuntimeSnapshot timer_runtime_snapshot();

// True only for a plausibly synchronized system clock. Execution never depends on this clock.
// Истина только для синхронизированных системных часов; исполнение от них не зависит.
bool timer_local_clock_now(time_t* out_now);

// DspTask-only pending consumer. Starts the sole managed shutdown pipeline
// (WaitPlayer -> Flush -> DisplayOff -> Settle -> Enter).
// Только DspTask: запускает единственный managed shutdown pipeline.
void sleep_timer_request_device_sleep();

bool sleep_timer_is_shutdown_active();

// Compatibility contract for wgt_status_line: these represent Radio Stop only.
// Контракт status line: эти функции означают только countdown Radio Stop.
bool sleep_timer_active();
uint32_t sleep_timer_remaining_seconds();
uint16_t sleep_timer_selected_minutes();

// Must run at the beginning of setup(), before panel/config initialization.
// Вызывать в начале setup(), до инициализации панели/конфига.
void sleep_wakeup_early_init();

// Sole GPIO wake registration, called immediately before esp_deep_sleep_start().
// Единственная регистрация GPIO-wake непосредственно перед входом в deep sleep.
void sleep_configure_wakeup_pin();

#endif
