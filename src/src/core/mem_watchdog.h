/**
 * MemWatchdog: fail-safe reboot when internal RAM degrades (TLS/fragmentation).
 * MemWatchdog: автоперезапуск при деградации внутренней RAM (TLS/фрагментация).
 * MEM_WATCHDOG_AUTOREBOOT: enable in myoptions.h (disable: comment out the define).
 * Author: Witaliy76 - https://github.com/Witaliy76
 */
#ifndef MEM_WATCHDOG_H
#define MEM_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

enum class MWEvent : uint8_t {
  TLS_FAIL,
  HTTP_FAIL,
  CONN_LOST,
  HEADER_OVERFLOW,
  PLAYBACK_STOP,
};

// E36MEM0D1: player health faults — fixed enum, no error strings in watchdog
enum class MWPlaybackFault : uint8_t {
  GENERIC_ERROR = 0,
  HEADER_OVERFLOW,
};

enum class MWArmReason : uint8_t {
  NONE = 0,
  FREE,
  FRAGMENTED,
  FREE_FRAG,
  EPISODE_LOW,
  FAIL_STORM,
};

struct MWDecision {
  bool     trigger;
  uint32_t int_min_global;
  uint32_t int_min_episode;
  uint32_t int_free;
  uint8_t  fails;
};

struct MWSnapshot {
  uint8_t  fails;
  uint8_t  fails_threshold;
  uint32_t window_age_ms;
  uint32_t window_ms;
  MWEvent  last_event;
  uint32_t int_free;
  uint32_t int_min_global;
  uint32_t int_min_episode;
  uint32_t int_largest;
  uint32_t psram_free;
  uint32_t psram_largest;
  uint32_t boot_count;
  bool     suppressed;
  bool     reboot_armed;
  bool     current_free_critical;
  bool     current_largest_critical;
  bool     current_memory_critical;
  MWArmReason arm_reason;
};

#ifdef MEM_WATCHDOG_AUTOREBOOT
constexpr uint32_t MW_PLAYBACK_RECOVER_MS = 15000;
#endif

enum class AudioTerminalReason : uint8_t;

class MemWatchdog {
public:
  void reset();
  void tick();
  void record(MWEvent ev);
  MWDecision evaluate() const;
  MWSnapshot snapshot() const;

  void armReboot();
  bool rebootArmed() const;
  MWArmReason armReason() const;
  bool isFunctionalReboot() const;

  void onBoot();
  bool isSuppressed() const;
  void onStableRun();

  // E36MEM0D: new playback/connect episode — start local-min monitor once / эпизод попытки воспроизведения
  void onPlaybackAttemptStarted();
  void onPlaybackRecovered();
  // E36MEM0D1: every non-empty setError() / automatic degraded stop
  void onPlaybackHealthFault(MWPlaybackFault fault);
  void onPlaybackAutoStopped(bool hasPlayerError);
  // E36REC1B: station-local Audio terminal — no ARM/reboot / локальный STOP без деградации
  void onStationLocalStop(AudioTerminalReason reason);
  bool canAcceptPlaybackRecovery() const;
  bool takeRecoveryTimerReset();

  void printRebootDiagnostic() const;

private:
  void mwMaybeExpireWindow();
  void mwClearEpisodeHealthFlags();
  void mwCountFunctionalFailure(MWEvent ev);
  void mwStartEpisodeIfInactive();
  void mwEndEpisode();
  uint32_t mwEpisodeIntMin() const;
  uint32_t mwGlobalIntMin() const;
  void mwEpisodeTrackSample() const;

  uint8_t  m_fails        = 0;
  uint32_t m_first_ts     = 0;
  bool     m_reboot_armed = false;
  bool     m_suppressed   = false;
  MWEvent  m_last_event   = MWEvent::HTTP_FAIL;
  MWArmReason m_arm_reason = MWArmReason::NONE;
  mutable bool m_hold_logged = false;
  bool     m_episode_active = false;
  bool     m_episode_monitoring = false;
  uint32_t m_int_min_global_cached = 0;
  mutable uint32_t m_episode_int_min_tracked = UINT32_MAX;
  volatile bool m_recovery_reset_pending = false;
  bool m_header_overflow_counted = false;
  bool m_header_overflow_observed = false;
  bool m_auto_stop_counted = false;
  bool m_player_error_observed = false;
};

extern MemWatchdog memWatchdog;

#endif
