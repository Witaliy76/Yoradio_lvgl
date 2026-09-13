/**
 * MemWatchdog implementation. Enable via MEM_WATCHDOG_AUTOREBOOT in myoptions.h.
 * Author: Witaliy76 - https://github.com/Witaliy76
 */
#include "options.h"
#include "mem_watchdog.h"
#include "../audioI2S/AudioEx.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "Arduino.h"
#include <cstring>

MemWatchdog memWatchdog;

#ifdef MEM_WATCHDOG_AUTOREBOOT

static const uint32_t INT_CURRENT_FREE_THRESHOLD   = 4096;
static const uint32_t INT_LARGEST_BLOCK_THRESHOLD  = 4096;
static const uint32_t INT_EPISODE_MIN_THRESHOLD    = 4096;
static const uint32_t WINDOW_MS                    = 60000;
static const uint8_t  FAILS_TO_REBOOT              = 3;
static const uint8_t  FAILS_TO_FUNCTIONAL_REBOOT   = 6;
static const uint32_t STABLE_RUN_MS                = 600000;

RTC_DATA_ATTR static uint32_t s_mw_boot_count = 0;

static const char* mwEventTag(MWEvent ev) {
  switch (ev) {
    case MWEvent::HTTP_FAIL: return "HTTP_FAIL";
    case MWEvent::CONN_LOST: return "CONN_LOST";
    case MWEvent::TLS_FAIL:  return "TLS_FAIL";
    case MWEvent::HEADER_OVERFLOW: return "HEADER_OVERFLOW";
    case MWEvent::PLAYBACK_STOP: return "PLAYBACK_STOP";
    default:                 return "UNKNOWN";
  }
}

static const char* mwArmReasonTag(MWArmReason reason) {
  switch (reason) {
    case MWArmReason::FREE:        return "FREE";
    case MWArmReason::FRAGMENTED:  return "FRAGMENTED";
    case MWArmReason::FREE_FRAG:   return "FREE+FRAGMENTED";
    case MWArmReason::EPISODE_LOW: return "EPISODE_LOW";
    case MWArmReason::FAIL_STORM:  return "FAIL_STORM";
    default:                       return "NONE";
  }
}

static const char* mwAudioTerminalReasonTag(AudioTerminalReason reason) {
  switch (reason) {
    case AudioTerminalReason::HEADER_RETRY_EXHAUSTED:   return "HEADER_RETRY_EXHAUSTED";
    case AudioTerminalReason::UNSTABLE_STREAM_EXHAUSTED: return "UNSTABLE_STREAM_EXHAUSTED";
    default:                                          return "NONE";
  }
}

static MWArmReason mwMemoryArmReason(const MWSnapshot& s) {
  if (s.current_free_critical && s.current_largest_critical) return MWArmReason::FREE_FRAG;
  if (s.current_free_critical) return MWArmReason::FREE;
  if (s.current_largest_critical) return MWArmReason::FRAGMENTED;
  return MWArmReason::NONE;
}

static void mwFillCurrentHealth(MWSnapshot& s) {
  s.current_free_critical = (s.int_free < INT_CURRENT_FREE_THRESHOLD);
  s.current_largest_critical = (s.int_largest < INT_LARGEST_BLOCK_THRESHOLD);
  s.current_memory_critical = s.current_free_critical || s.current_largest_critical;
}

static void mwPrintSnapshotLine(const char* tag, const MWSnapshot& s, bool include_window) {
  const bool showReason = (strcmp(tag, "ARM") == 0 || strcmp(tag, "REBOOT") == 0);
  const char* reason = showReason ? mwArmReasonTag(s.arm_reason) : nullptr;
  const unsigned failsThreshold = (s.arm_reason == MWArmReason::FAIL_STORM)
      ? FAILS_TO_FUNCTIONAL_REBOOT
      : FAILS_TO_REBOOT;
  // E-MW2: FAIL_STORM is judged on spontaneous failures only, so report those.
  // E-MW2: FAIL_STORM судит только по самопроизвольным отказам — их и показываем.
  const unsigned failsValue = (s.arm_reason == MWArmReason::FAIL_STORM)
      ? s.auto_fails
      : s.fails;

  if (include_window) {
    if (reason) {
      Serial.printf(
        "[MEMWATCH] %s reason=%s event=%s fails=%u/%u window=%lu/%lu int_free=%lu int_min_global=%lu int_min_episode=%lu int_largest=%lu psram_free=%lu psram_largest=%lu boot=%lu suppressed=%u\n",
        tag, reason,
        mwEventTag(s.last_event),
        failsValue,
        failsThreshold,
        (unsigned long)s.window_age_ms,
        (unsigned long)s.window_ms,
        (unsigned long)s.int_free,
        (unsigned long)s.int_min_global,
        (unsigned long)s.int_min_episode,
        (unsigned long)s.int_largest,
        (unsigned long)s.psram_free,
        (unsigned long)s.psram_largest,
        (unsigned long)s.boot_count,
        (unsigned)s.suppressed);
    } else {
      Serial.printf(
        "[MEMWATCH] %s event=%s fails=%u/%u window=%lu/%lu int_free=%lu int_min_global=%lu int_min_episode=%lu int_largest=%lu psram_free=%lu psram_largest=%lu boot=%lu suppressed=%u\n",
        tag,
        mwEventTag(s.last_event),
        failsValue,
        failsThreshold,
        (unsigned long)s.window_age_ms,
        (unsigned long)s.window_ms,
        (unsigned long)s.int_free,
        (unsigned long)s.int_min_global,
        (unsigned long)s.int_min_episode,
        (unsigned long)s.int_largest,
        (unsigned long)s.psram_free,
        (unsigned long)s.psram_largest,
        (unsigned long)s.boot_count,
        (unsigned)s.suppressed);
    }
  } else if (reason) {
    Serial.printf(
      "[MEMWATCH] %s reason=%s event=%s fails=%u/%u int_free=%lu int_min_global=%lu int_min_episode=%lu int_largest=%lu psram_free=%lu psram_largest=%lu boot=%lu suppressed=%u\n",
      tag, reason,
      mwEventTag(s.last_event),
      failsValue,
      failsThreshold,
      (unsigned long)s.int_free,
      (unsigned long)s.int_min_global,
      (unsigned long)s.int_min_episode,
      (unsigned long)s.int_largest,
      (unsigned long)s.psram_free,
      (unsigned long)s.psram_largest,
      (unsigned long)s.boot_count,
      (unsigned)s.suppressed);
  } else {
    Serial.printf(
      "[MEMWATCH] %s event=%s fails=%u/%u int_free=%lu int_min_global=%lu int_min_episode=%lu int_largest=%lu psram_free=%lu psram_largest=%lu boot=%lu suppressed=%u\n",
      tag,
      mwEventTag(s.last_event),
      failsValue,
      failsThreshold,
      (unsigned long)s.int_free,
      (unsigned long)s.int_min_global,
      (unsigned long)s.int_min_episode,
      (unsigned long)s.int_largest,
      (unsigned long)s.psram_free,
      (unsigned long)s.psram_largest,
      (unsigned long)s.boot_count,
      (unsigned)s.suppressed);
  }
}

void MemWatchdog::mwClearEpisodeHealthFlags() {
  m_header_overflow_counted = false;
  m_header_overflow_observed = false;
  m_auto_stop_counted = false;
  m_player_error_observed = false;
}

void MemWatchdog::mwEndEpisode() {
  if (m_episode_monitoring) {
    heap_caps_monitor_local_minimum_free_size_stop();
    m_episode_monitoring = false;
  }
  m_episode_active = false;
  m_episode_int_min_tracked = UINT32_MAX;
  m_int_min_global_cached = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  mwClearEpisodeHealthFlags();
}

void MemWatchdog::mwStartEpisodeIfInactive() {
  if (m_episode_active) return;

  if (m_episode_monitoring) {
    heap_caps_monitor_local_minimum_free_size_stop();
    m_episode_monitoring = false;
  }

  m_episode_active = true;
  m_episode_int_min_tracked = UINT32_MAX;
  m_int_min_global_cached = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  // ESP-IDF local-minimum monitor — episode dip vs boot lifetime / локальный минимум эпизода
  if (heap_caps_monitor_local_minimum_free_size_start() == ESP_OK) {
    m_episode_monitoring = true;
  } else {
    const uint32_t freeNow = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    m_episode_int_min_tracked = freeNow;
  }
}

void MemWatchdog::mwEpisodeTrackSample() const {
  if (!m_episode_active || m_episode_monitoring) return;
  const uint32_t freeNow = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (m_episode_int_min_tracked == UINT32_MAX || freeNow < m_episode_int_min_tracked) {
    m_episode_int_min_tracked = freeNow;
  }
}

uint32_t MemWatchdog::mwGlobalIntMin() const {
  if (m_episode_monitoring) return m_int_min_global_cached;
  return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

uint32_t MemWatchdog::mwEpisodeIntMin() const {
  if (!m_episode_active) {
    return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (m_episode_monitoring) {
    return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (m_episode_int_min_tracked != UINT32_MAX) return m_episode_int_min_tracked;
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void MemWatchdog::reset() {
  mwEndEpisode();
  m_fails = 0;
  m_auto_fails = 0;
  m_connect_fail_ack_pending = false;
  m_first_ts = 0;
  m_reboot_armed = false;
  m_hold_logged = false;
  m_arm_reason = MWArmReason::NONE;
  m_recovery_reset_pending = false;
}

void MemWatchdog::mwMaybeExpireWindow() {
  if (m_suppressed || m_first_ts == 0) return;
  if ((millis() - m_first_ts) > WINDOW_MS) {
    m_fails = 0;
    m_auto_fails = 0;   // E-MW2
    m_first_ts = 0;
    m_hold_logged = false;
    mwEndEpisode();
  }
}

void MemWatchdog::mwCountFunctionalFailure(MWEvent ev) {
  if (m_suppressed || m_reboot_armed) return;
  mwMaybeExpireWindow();
  if (!m_episode_active) mwStartEpisodeIfInactive();
  mwEpisodeTrackSample();
  m_last_event = ev;
  const uint32_t now = millis();
  if (m_first_ts == 0) m_first_ts = now;
  // E-MW2: see record() - a failure that concludes a user-initiated attempt is
  // evidence about the station, not about this device.
  // E-MW2: см. record() - отказ, завершающий начатую пользователем попытку, —
  // свидетельство о станции, а не о состоянии устройства.
  const bool spontaneous = !m_user_attempt_pending;
  m_user_attempt_pending = false;
  m_fails++;
  if (spontaneous) m_auto_fails++;
  m_recovery_reset_pending = true;
  const MWDecision d = evaluate();
  if (d.trigger) armReboot();
}

void MemWatchdog::tick() {
  mwMaybeExpireWindow();
}

void MemWatchdog::record(MWEvent ev) {
  if (m_suppressed) return;
  mwMaybeExpireWindow();
  if (!m_episode_active) mwStartEpisodeIfInactive();
  mwEpisodeTrackSample();
  m_last_event = ev;
  const uint32_t now = millis();
  if (m_first_ts == 0) m_first_ts = now;

  // E-MW2: classify before counting. A failure that concludes a user-initiated
  // playback attempt only tells us the station did not answer - the device may be
  // perfectly healthy - so it must not feed the FAIL_STORM reboot, which fires
  // precisely when memory is fine. It still feeds m_fails, because the memory
  // triggers want every failure regardless of who started the attempt.
  // Anything else (stream lost mid-playback, internal reconnect) is spontaneous
  // and is exactly the evidence FAIL_STORM was meant to act on.
  // E-MW2: классифицируем до подсчёта. Отказ, завершающий начатую пользователем
  // попытку, говорит лишь о том, что станция не ответила — устройство может быть
  // полностью исправно, — поэтому он не кормит перезагрузку FAIL_STORM, которая
  // срабатывает как раз при здоровой памяти. В m_fails он по-прежнему попадает:
  // памятным триггерам нужен любой отказ, независимо от инициатора.
  // Всё остальное (поток развалился на ходу, внутренний reconnect) —
  // самопроизвольное, и это ровно то, ради чего FAIL_STORM задумывался.
  const bool spontaneous = !m_user_attempt_pending;
  m_user_attempt_pending = false;

  m_fails++;
  if (spontaneous) m_auto_fails++;
  // E-MW1: Player logs the same connect failure one level up. Tell it this one is
  // already counted, otherwise a single dead station costs two units and the
  // threshold of 6 behaves like 3.
  // E-MW1: Player сообщает об этом же отказе уровнем выше. Помечаем, что он уже
  // учтён, иначе одна мёртвая станция стоит двух единиц и порог 6 ведёт себя как 3.
  if (ev == MWEvent::HTTP_FAIL) m_connect_fail_ack_pending = true;
  m_recovery_reset_pending = true;
}

bool MemWatchdog::takeConnectFailureAck() {
  const bool pending = m_connect_fail_ack_pending;
  m_connect_fail_ack_pending = false;
  return pending;
}

MWDecision MemWatchdog::evaluate() const {
  MWDecision d = { false, 0, 0, 0, m_fails };
  uint32_t now = millis();

  if (m_first_ts != 0 && (now - m_first_ts) > WINDOW_MS) {
    d.fails = 0;
    return d;
  }

  mwEpisodeTrackSample();

  d.int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  d.int_min_global = mwGlobalIntMin();
  d.int_min_episode = mwEpisodeIntMin();
  const uint32_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  const bool currentMemoryCritical =
      (d.int_free < INT_CURRENT_FREE_THRESHOLD) ||
      (int_largest < INT_LARGEST_BLOCK_THRESHOLD);

  const bool episodeLow =
      m_episode_active &&
      (d.int_min_episode < INT_EPISODE_MIN_THRESHOLD);

  const bool memoryTrigger = (m_fails >= FAILS_TO_REBOOT) && currentMemoryCritical;
  const bool episodeLowTrigger = (m_fails >= FAILS_TO_REBOOT) && !currentMemoryCritical && episodeLow;
  // E-MW2: spontaneous failures only / только самопроизвольные отказы
  const bool stormTrigger =
      (m_auto_fails >= FAILS_TO_FUNCTIONAL_REBOOT) && !currentMemoryCritical && !episodeLow;

  if (memoryTrigger || episodeLowTrigger || stormTrigger)
    d.trigger = true;

  if (m_fails >= FAILS_TO_REBOOT &&
      m_fails < FAILS_TO_FUNCTIONAL_REBOOT &&
      !currentMemoryCritical &&
      !episodeLow &&
      !d.trigger &&
      !m_hold_logged) {
    m_hold_logged = true;
    Serial.printf("[MEMWATCH] HOLD fails=%u/%u int_free=%lu int_min_global=%lu int_min_episode=%lu int_largest=%lu\n",
                  (unsigned)m_fails,
                  (unsigned)FAILS_TO_REBOOT,
                  (unsigned long)d.int_free,
                  (unsigned long)d.int_min_global,
                  (unsigned long)d.int_min_episode,
                  (unsigned long)int_largest);
  }

  return d;
}

MWSnapshot MemWatchdog::snapshot() const {
  MWSnapshot s = {};
  uint32_t now = millis();
  s.fails = m_fails;
  s.auto_fails = m_auto_fails;   // E-MW2
  s.fails_threshold = FAILS_TO_REBOOT;
  s.window_ms = WINDOW_MS;
  s.window_age_ms = (m_first_ts == 0) ? 0 : (now - m_first_ts);
  s.last_event = m_last_event;
  s.int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s.int_min_global = mwGlobalIntMin();
  s.int_min_episode = mwEpisodeIntMin();
  s.int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s.psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s.boot_count = s_mw_boot_count;
  s.suppressed = m_suppressed;
  s.reboot_armed = m_reboot_armed;
  s.arm_reason = m_arm_reason;
  mwFillCurrentHealth(s);
  return s;
}

void MemWatchdog::armReboot() {
  if (m_reboot_armed) return;
  m_reboot_armed = true;
  MWSnapshot s = snapshot();
  if (m_fails >= FAILS_TO_REBOOT && s.current_memory_critical) {
    m_arm_reason = mwMemoryArmReason(s);
  } else if (m_fails >= FAILS_TO_REBOOT && s.int_min_episode < INT_EPISODE_MIN_THRESHOLD) {
    m_arm_reason = MWArmReason::EPISODE_LOW;
  } else if (m_auto_fails >= FAILS_TO_FUNCTIONAL_REBOOT) {   // E-MW2
    m_arm_reason = MWArmReason::FAIL_STORM;
  } else {
    m_arm_reason = MWArmReason::NONE;
  }
  s.arm_reason = m_arm_reason;
  mwPrintSnapshotLine("ARM", s, true);
}

bool MemWatchdog::rebootArmed() const {
  return m_reboot_armed;
}

MWArmReason MemWatchdog::armReason() const {
  return m_arm_reason;
}

bool MemWatchdog::isFunctionalReboot() const {
  return m_arm_reason == MWArmReason::FAIL_STORM || m_arm_reason == MWArmReason::EPISODE_LOW;
}

void MemWatchdog::onPlaybackAttemptStarted() {
  if (m_suppressed) return;
  m_recovery_reset_pending = true;
  // E-MW1/E-MW2: only Player calls this, and only for a user-initiated play.
  // An internal reconnect never does, which is what makes the two cases
  // distinguishable at all.
  // E-MW1/E-MW2: сюда заходит только Player и только при запуске по команде
  // пользователя; внутренний reconnect этого не делает — на этом и строится
  // различение двух случаев.
  m_user_attempt_pending = true;
  m_connect_fail_ack_pending = false;
  mwStartEpisodeIfInactive();
}

void MemWatchdog::onPlaybackHealthFault(MWPlaybackFault fault) {
  if (m_suppressed) return;
  mwMaybeExpireWindow();
  if (!m_episode_active) mwStartEpisodeIfInactive();
  m_recovery_reset_pending = true;
  m_player_error_observed = true;

  if (fault == MWPlaybackFault::HEADER_OVERFLOW) {
    m_header_overflow_observed = true;
    if (!m_header_overflow_counted) {
      m_header_overflow_counted = true;
      mwCountFunctionalFailure(MWEvent::HEADER_OVERFLOW);
      Serial.printf("[MEMWATCH] FAULT event=HEADER_OVERFLOW counted=1 fails=%u\n",
                    (unsigned)m_fails);
    }
    return;
  }
  // GENERIC_ERROR: recovery timer only — no m_fails increment / только сброс recovery
}

void MemWatchdog::onPlaybackAutoStopped(bool hasPlayerError) {
  if (m_suppressed || m_reboot_armed) return;
  m_recovery_reset_pending = true;
  const bool degraded =
      (m_fails > 0) || m_header_overflow_observed || hasPlayerError || m_player_error_observed;
  if (!degraded || m_auto_stop_counted) return;
  m_auto_stop_counted = true;
  mwCountFunctionalFailure(MWEvent::PLAYBACK_STOP);
  Serial.printf("[MEMWATCH] AUTO_STOP counted=1 fails=%u\n", (unsigned)m_fails);
}

void MemWatchdog::onStationLocalStop(AudioTerminalReason reason) {
  if (m_suppressed) return;
  m_recovery_reset_pending = true;
  m_hold_logged = false;
  mwEndEpisode();
  Serial.printf("[MEMWATCH] STATION_STOP reason=%s\n", mwAudioTerminalReasonTag(reason));
}

bool MemWatchdog::takeRecoveryTimerReset() {
  if (!m_recovery_reset_pending) return false;
  m_recovery_reset_pending = false;
  return true;
}

bool MemWatchdog::canAcceptPlaybackRecovery() const {
  if (m_reboot_armed || m_suppressed) return false;
  if (!m_episode_active) return true;
  return mwEpisodeIntMin() >= INT_EPISODE_MIN_THRESHOLD;
}

void MemWatchdog::onPlaybackRecovered() {
  if (m_suppressed) return;
  const uint8_t prevFails = m_fails;
  const uint32_t episodeMin = mwEpisodeIntMin();
  m_fails = 0;
  m_auto_fails = 0;              // E-MW2
  m_user_attempt_pending = false;  // the attempt ended in healthy audio / попытка закончилась звуком
  m_first_ts = 0;
  m_hold_logged = false;
  m_recovery_reset_pending = false;
  mwEndEpisode();
  if (prevFails > 0) {
    Serial.printf("[MEMWATCH] RECOVER fails=%u healthy_ms=%lu int_min_episode=%lu\n",
                  (unsigned)prevFails,
                  (unsigned long)MW_PLAYBACK_RECOVER_MS,
                  (unsigned long)episodeMin);
  }
}

void MemWatchdog::onBoot() {
  s_mw_boot_count++;
  if (s_mw_boot_count >= 3)
    m_suppressed = true;
}

bool MemWatchdog::isSuppressed() const {
  return m_suppressed;
}

void MemWatchdog::onStableRun() {
  if (millis() > STABLE_RUN_MS)
    s_mw_boot_count = 0;
}

void MemWatchdog::printRebootDiagnostic() const {
  MWSnapshot s = snapshot();
  mwPrintSnapshotLine("REBOOT", s, false);
}

#else

void MemWatchdog::reset() {}
void MemWatchdog::tick() {}
void MemWatchdog::record(MWEvent) {}
MWDecision MemWatchdog::evaluate() const { MWDecision d = {false, 0, 0, 0, 0}; return d; }
MWSnapshot MemWatchdog::snapshot() const { MWSnapshot s = {}; return s; }
void MemWatchdog::armReboot() {}
bool MemWatchdog::rebootArmed() const { return false; }
MWArmReason MemWatchdog::armReason() const { return MWArmReason::NONE; }
bool MemWatchdog::isFunctionalReboot() const { return false; }
void MemWatchdog::onBoot() {}
bool MemWatchdog::isSuppressed() const { return false; }
void MemWatchdog::onStableRun() {}
void MemWatchdog::onPlaybackAttemptStarted() {}
void MemWatchdog::onPlaybackRecovered() {}
void MemWatchdog::onPlaybackHealthFault(MWPlaybackFault) {}
void MemWatchdog::onPlaybackAutoStopped(bool) {}
void MemWatchdog::onStationLocalStop(AudioTerminalReason) {}
bool MemWatchdog::canAcceptPlaybackRecovery() const { return false; }
bool MemWatchdog::takeRecoveryTimerReset() { return false; }
bool MemWatchdog::takeConnectFailureAck() { return false; }
void MemWatchdog::printRebootDiagnostic() const {}

#endif
