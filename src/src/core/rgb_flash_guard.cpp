// RGB flash guard implementation (S01-5B). See rgb_flash_guard.h for ownership.
// Реализация RGB flash guard (S01-5B). Владение описано в rgb_flash_guard.h.
//
// Why: on direct-T0 ST7701 (one PSRAM framebuffer, bounce 0, restart-in-VSYNC off) a physical
// flash write at PCLK 10 MHz can leave the scanout permanently shifted. Device evidence
// S01-4X-P (P1) showed the same NVS writes stay clean at a confirmed 6 MHz, without restart.
// Зачем: на direct-T0 ST7701 физическая запись во flash при PCLK 10 МГц может навсегда сдвинуть
// scanout. Доказательство S01-4X-P (P1): те же записи NVS чисты при подтверждённых 6 МГц.
//
// esp_lcd (IDF 5.5.5) applies a new PCLK asynchronously: set_pclk() only records it, and the
// VSYNC ISR programs the divider AFTER the application on_vsync callback. The barrier snapshots
// the VSYNC count when set_pclk() returns and confirms after two further counts, so the
// divider update is already in the past.
// esp_lcd применяет PCLK асинхронно: set_pclk() только записывает, а VSYNC ISR программирует
// делитель ПОСЛЕ on_vsync. Барьер снимает счётчик после возврата set_pclk() и подтверждает
// переход после двух следующих отсчётов.
//
// The RGB ISR is not IRAM-safe here, so VSYNC stops counting while flash disables the cache.
// That is safe because a guarded write only starts after ACTIVE_6M is confirmed, the guard
// stays ACTIVE_6M (no VSYNC needed) while writes run, and the return to 10 MHz is requested
// only after the writer released its holder, i.e. after its synchronous flash calls returned.
// RGB ISR не IRAM-safe, поэтому во время flash-операций VSYNC стоит. Это безопасно: запись
// начинается только после подтверждённого ACTIVE_6M, во время записи guard остаётся
// ACTIVE_6M, а возврат к 10 МГц запрашивается лишь после release(), т.е. после возврата
// синхронных flash-вызовов.
//
// Author: Witaliy76 - https://github.com/Witaliy76

#include "rgb_flash_guard.h"

#include <Arduino.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "../displays/display_port.h"

namespace rgb_flash_guard {
namespace {

// Policy constants. Not user settings.
// Константы политики. Не пользовательские настройки.
constexpr uint32_t kReleaseLingerMs = 1500u;        // stay at 6 MHz after the last release
constexpr uint32_t kDownConfirmTimeoutMs = 1000u;   // acquire() gives up -> Denied
constexpr uint32_t kUpConfirmTimeoutMs = 1000u;     // UP stall is reported, never assumed done
constexpr uint32_t kMaxHoldWarnMs = 10000u;         // ACTIVE_6M with holders: report once
constexpr uint32_t kVsyncBarrier = 2u;              // counts after set_pclk() returned
constexpr uint32_t kSetPclkRetryMs = 100u;          // back-off after a rejected set_pclk()

enum State : uint8_t {
  kIdle10M = 0,
  kDownRequested = 1,
  kActive6M = 2,
  kUpRequested = 3
};

enum Action : uint8_t { kActNone = 0, kActLow, kActHigh };

enum Event : uint8_t {
  kEvNone = 0,
  kEvActive,
  kEvIdle,
  kEvDownStall,
  kEvUpStall,
  kEvMaxHold
};

const char* stateName(uint8_t s) {
  switch (s) {
    case kIdle10M: return "IDLE_10M";
    case kDownRequested: return "DOWN_REQUESTED";
    case kActive6M: return "ACTIVE_6M";
    case kUpRequested: return "UP_REQUESTED";
    default: return "?";
  }
}

// Two levels (S01-FINAL-PREP). GUARD_LOG is always on and carries only what a service engineer
// must never miss: denials, stalls, the max-hold warning, a failed set_pclk, a release underflow
// and the one-line boot summary. GUARD_TRACE carries the routine per-transition chatter
// (acquire/release, down/up request, active_6m/idle_10m, bypass) that was essential while the
// guard was being developed and is pure noise in production - an ordinary NVS save emitted six
// lines. It is compiled out by default; nothing is lost for diagnosis because every one of those
// events is also counted in formatStatus(), which `s01guard status` prints on demand.
// Dva urovnya (S01-FINAL-PREP). GUARD_LOG vsegda vklyuchen i soderzhit tolko to, chto servisnyy
// inzhener ne dolzhen propustit. GUARD_TRACE - rutinnye stroki perehodov, bespoleznyy shum v
// produkte: obychnaya zapis NVS davala shest strok. Po umolchaniyu vyklyuchen; nichego ne
// teryaetsya, tak kak vse eti sobytiya schitayutsya v formatStatus().
#ifndef YORADIO_RGB_GUARD_VERBOSE
#define YORADIO_RGB_GUARD_VERBOSE 0
#endif
constexpr bool kGuardTrace = (YORADIO_RGB_GUARD_VERBOSE != 0);

#define GUARD_LOG(fmt, ...) Serial.printf("[RGB-GUARD] " fmt "\n", ##__VA_ARGS__)
#define GUARD_TRACE(fmt, ...)                                                \
  do {                                                                       \
    if (kGuardTrace) Serial.printf("[RGB-GUARD] " fmt "\n", ##__VA_ARGS__);  \
  } while (0)

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// Shared, under s_mux. s_state is written only by the owner task (DspTask); waiters read it.
// Общие, под s_mux. s_state пишет только задача-владелец (DspTask); ожидающие читают.
volatile uint8_t s_state = kIdle10M;
uint32_t s_holders = 0;
bool s_linger_armed = false;
uint32_t s_linger_start_ms = 0;
uint32_t s_base = 0;
uint32_t s_transition_ms = 0;
uint32_t s_requested_hz = 0;

// Owner task only.
// Только задача-владелец.
TaskHandle_t volatile s_owner = nullptr;
uint32_t s_active_since_ms = 0;
uint32_t s_retry_after_ms = 0;
bool s_stall_reported = false;
bool s_max_hold_reported = false;

// Diagnostic counters.
// Диагностические счётчики.
uint32_t c_downReq = 0;
uint32_t c_downOk = 0;
uint32_t c_downStall = 0;
uint32_t c_upReq = 0;
uint32_t c_upOk = 0;
uint32_t c_upStall = 0;
uint32_t c_acquire = 0;
uint32_t c_release = 0;
uint32_t c_denied = 0;
uint32_t c_bypass = 0;
uint32_t c_lingerCancel = 0;
uint32_t c_maxHoldWarn = 0;
uint32_t c_setPclkFail = 0;
uint32_t c_releaseUnderflow = 0;

const char* restartInVsyncName() {
#if CONFIG_LCD_RGB_RESTART_IN_VSYNC
  return "ON";
#else
  return "OFF";
#endif
}

#if YORADIO_RGB_FLASH_GUARD
// Null when the guard applies; otherwise the reason it does not.
// Null, если guard применим; иначе причина, почему нет.
const char* bypassReason(const DisplayPort::RgbScanoutStatus& st) {
  if (!st.panelReady) return "panel_not_ready";
  if (st.bounceBufferPx > 0) return "bounce";
  if (!st.vsyncCounterReady) return "no_vsync_counter";
  return nullptr;
}

// One state-machine step. Owner task (DspTask) only: from poll() or an owner acquire().
// Один шаг автомата. Только задача-владелец (DspTask): из poll() или owner acquire().
void stepOwner() {
  const uint32_t now = millis();
  const uint32_t seq = DisplayPort::rgbVsyncSeq();
  Action action = kActNone;
  Event ev = kEvNone;
  uint32_t ev_dt = 0;
  uint32_t holders = 0;

  portENTER_CRITICAL(&s_mux);
  switch (s_state) {
    case kIdle10M:
      if (s_holders > 0 && static_cast<int32_t>(now - s_retry_after_ms) >= 0) {
        s_state = kDownRequested;
        action = kActLow;
      }
      break;

    case kDownRequested:
      if (static_cast<uint32_t>(seq - s_base) >= kVsyncBarrier) {
        s_state = kActive6M;
        ++c_downOk;
        ev = kEvActive;
        ev_dt = now - s_transition_ms;
        s_active_since_ms = now;
        s_stall_reported = false;
        s_max_hold_reported = false;
        if (s_holders == 0) {
          // Every waiter timed out meanwhile: start the linger from here.
          // Все ожидающие уже ушли по таймауту: задержка отсчитывается отсюда.
          s_linger_armed = true;
          s_linger_start_ms = now;
        }
      } else if (!s_stall_reported &&
                 static_cast<uint32_t>(now - s_transition_ms) >= kDownConfirmTimeoutMs) {
        s_stall_reported = true;
        ++c_downStall;
        ev = kEvDownStall;
        ev_dt = now - s_transition_ms;
      }
      break;

    case kActive6M:
      if (s_holders > 0) {
        if (s_linger_armed) {
          s_linger_armed = false;
          ++c_lingerCancel;
        }
        if (!s_max_hold_reported &&
            static_cast<uint32_t>(now - s_active_since_ms) >= kMaxHoldWarnMs) {
          // Report only. Never force 10 MHz while a holder may be writing flash.
          // Только отчёт. Пока holder может писать flash, 10 МГц не форсируется.
          s_max_hold_reported = true;
          ++c_maxHoldWarn;
          ev = kEvMaxHold;
          ev_dt = now - s_active_since_ms;
        }
      } else if (!s_linger_armed) {
        s_linger_armed = true;
        s_linger_start_ms = now;
      } else if (static_cast<uint32_t>(now - s_linger_start_ms) >= kReleaseLingerMs &&
                 static_cast<int32_t>(now - s_retry_after_ms) >= 0) {
        s_linger_armed = false;
        s_state = kUpRequested;
        action = kActHigh;
      }
      break;

    case kUpRequested:
      if (static_cast<uint32_t>(seq - s_base) >= kVsyncBarrier) {
        s_state = kIdle10M;
        ++c_upOk;
        ev = kEvIdle;
        ev_dt = now - s_transition_ms;
        s_stall_reported = false;
        if (s_holders > 0) {
          // Work arrived while returning to 10 MHz. The UP transition is now confirmed,
          // so lowering again starts from a known clock.
          // Работа пришла во время возврата к 10 МГц. UP уже подтверждён, поэтому новое
          // понижение начинается с известной частоты.
          s_state = kDownRequested;
          action = kActLow;
        }
      } else if (!s_stall_reported &&
                 static_cast<uint32_t>(now - s_transition_ms) >= kUpConfirmTimeoutMs) {
        // Not assumed done. Writers that arrive now wait and, if needed, are Denied.
        // Не считается завершённым. Пришедшие сейчас ждут и при необходимости получают Denied.
        s_stall_reported = true;
        ++c_upStall;
        ev = kEvUpStall;
        ev_dt = now - s_transition_ms;
      }
      break;

    default:
      break;
  }
  holders = s_holders;
  portEXIT_CRITICAL(&s_mux);

  switch (ev) {
    case kEvActive:
      GUARD_TRACE("active_6m vsync=%lu dt_ms=%lu holders=%lu", (unsigned long)seq,
                (unsigned long)ev_dt, (unsigned long)holders);
      break;
    case kEvIdle:
      GUARD_TRACE("idle_10m vsync=%lu dt_ms=%lu holders=%lu", (unsigned long)seq,
                (unsigned long)ev_dt, (unsigned long)holders);
      break;
    case kEvDownStall:
      GUARD_LOG("down_stall vsync=%lu base=%lu dt_ms=%lu holders=%lu", (unsigned long)seq,
                (unsigned long)s_base, (unsigned long)ev_dt, (unsigned long)holders);
      break;
    case kEvUpStall:
      GUARD_LOG("up_stall vsync=%lu base=%lu dt_ms=%lu state=UP_REQUESTED (not assumed 10 MHz)",
                (unsigned long)seq, (unsigned long)s_base, (unsigned long)ev_dt);
      break;
    case kEvMaxHold:
      GUARD_LOG("max_hold_warn held_ms=%lu holders=%lu (protection kept)", (unsigned long)ev_dt,
                (unsigned long)holders);
      break;
    default:
      break;
  }

  if (action == kActNone) {
    return;
  }

  const DisplayPort::RgbScanoutStatus st = DisplayPort::rgbScanoutStatus();
  const bool low = (action == kActLow);
  const uint32_t hz = low ? st.flashGuardPclkHz : st.nominalPclkHz;
  const bool ok = DisplayPort::requestRgbPclkHz(hz);
  // Snapshot after the API returned: the first callback after this may still precede the
  // divider update in that same ISR, the second cannot.
  // Снимок после возврата API: первый callback ещё может опередить смену делителя в том же
  // ISR, второй — уже нет.
  const uint32_t base = DisplayPort::rgbVsyncSeq();

  portENTER_CRITICAL(&s_mux);
  if (ok) {
    s_base = base;
    s_transition_ms = now;
    s_requested_hz = hz;
    s_stall_reported = false;
    if (low) {
      ++c_downReq;
    } else {
      ++c_upReq;
    }
  } else {
    // Nothing reached the driver; the hardware clock is unchanged. Retry after a back-off.
    // До драйвера ничего не дошло; частота не изменилась. Повтор после паузы.
    ++c_setPclkFail;
    s_state = low ? kIdle10M : kActive6M;
    s_retry_after_ms = now + kSetPclkRetryMs;
  }
  holders = s_holders;
  portEXIT_CRITICAL(&s_mux);

  if (!ok) {
    GUARD_LOG("set_pclk_fail hz=%lu (state unchanged, retry)", (unsigned long)hz);
  } else if (low) {
    GUARD_TRACE("down_request hz=%lu vsync=%lu holders=%lu", (unsigned long)hz,
              (unsigned long)base, (unsigned long)holders);
  } else {
    GUARD_TRACE("up_request hz=%lu vsync=%lu", (unsigned long)hz, (unsigned long)base);
  }
}
#endif  // YORADIO_RGB_FLASH_GUARD

}  // namespace

const char* grantName(Grant grant) {
  switch (grant) {
    case Grant::Protected: return "PROTECTED";
    case Grant::NotApplicable: return "NOT_APPLICABLE";
    case Grant::Denied: return "DENIED";
    default: return "?";
  }
}

Grant acquire(const char* source) {
  const char* src = source ? source : "?";
#if !YORADIO_RGB_FLASH_GUARD
  (void)src;
  __atomic_fetch_add(&c_bypass, 1u, __ATOMIC_RELAXED);
  return Grant::NotApplicable;
#else
  const DisplayPort::RgbScanoutStatus st = DisplayPort::rgbScanoutStatus();
  const char* reason = bypassReason(st);
  if (reason) {
    __atomic_fetch_add(&c_bypass, 1u, __ATOMIC_RELAXED);
    GUARD_TRACE("bypass source=%s reason=%s", src, reason);
    return Grant::NotApplicable;
  }

  const bool owner = (s_owner != nullptr) && (xTaskGetCurrentTaskHandle() == s_owner);
  bool linger_cancelled = false;
  uint32_t holders = 0;
  uint8_t state = kIdle10M;
  portENTER_CRITICAL(&s_mux);
  ++s_holders;
  ++c_acquire;
  if (s_state == kActive6M && s_linger_armed) {
    s_linger_armed = false;
    ++c_lingerCancel;
    linger_cancelled = true;
  }
  holders = s_holders;
  state = s_state;
  portEXIT_CRITICAL(&s_mux);
  GUARD_TRACE("acquire source=%s holders=%lu state=%s%s%s", src, (unsigned long)holders,
            stateName(state), owner ? " owner=1" : "", linger_cancelled ? " linger_cancel=1" : "");

  // While this holder is counted, ACTIVE_6M cannot be left (UP needs zero holders), so
  // seeing ACTIVE_6M once is enough. The owner path advances the machine itself and never
  // waits for a future Display::loop(); other tasks wait for DspTask's poll().
  // Пока holder учтён, из ACTIVE_6M не выйти (UP требует ноль holder'ов), поэтому достаточно
  // увидеть ACTIVE_6M один раз. Owner-путь сам двигает автомат и не ждёт будущего
  // Display::loop(); остальные задачи ждут poll() на DspTask.
  const uint32_t start = millis();
  for (;;) {
    if (owner) {
      stepOwner();
    }
    if (s_state == kActive6M) {
      return Grant::Protected;
    }
    if (static_cast<uint32_t>(millis() - start) >= kDownConfirmTimeoutMs) {
      break;
    }
    vTaskDelay(owner ? 1 : pdMS_TO_TICKS(2));
  }

  // Fail closed: the holder is dropped and the caller must not write flash now.
  // Fail closed: holder снимается, вызывающий сейчас не пишет flash.
  portENTER_CRITICAL(&s_mux);
  if (s_holders > 0) {
    --s_holders;
  } else {
    ++c_releaseUnderflow;
  }
  ++c_denied;
  holders = s_holders;
  state = s_state;
  portEXIT_CRITICAL(&s_mux);
  GUARD_LOG("acquire_denied source=%s waited_ms=%lu state=%s holders=%lu", src,
            (unsigned long)(millis() - start), stateName(state), (unsigned long)holders);
  return Grant::Denied;
#endif
}

void release(Grant grant, const char* source) {
  if (grant != Grant::Protected) {
    return;
  }
  const char* src = source ? source : "?";
  const uint32_t now = millis();
  bool underflow = false;
  bool linger = false;
  uint32_t holders = 0;
  portENTER_CRITICAL(&s_mux);
  ++c_release;
  if (s_holders == 0) {
    underflow = true;
    ++c_releaseUnderflow;
  } else {
    --s_holders;
  }
  holders = s_holders;
  if (holders == 0 && s_state == kActive6M) {
    s_linger_armed = true;
    s_linger_start_ms = now;
    linger = true;
  }
  portEXIT_CRITICAL(&s_mux);
  if (underflow) {
    // Bookkeeping error: logged and counted, holder count stays 0. No crash.
    // Ошибка учёта: лог и счётчик, число holder'ов остаётся 0. Без падения.
    GUARD_LOG("release_underflow source=%s", src);
    return;
  }
  if (linger) {
    GUARD_TRACE("release source=%s holders=0 linger_ms=%lu", src, (unsigned long)kReleaseLingerMs);
  } else {
    GUARD_TRACE("release source=%s holders=%lu", src, (unsigned long)holders);
  }
}

void poll() {
  if (s_owner == nullptr) {
    s_owner = xTaskGetCurrentTaskHandle();
    const DisplayPort::RgbScanoutStatus st = DisplayPort::rgbScanoutStatus();
    portENTER_CRITICAL(&s_mux);
    s_requested_hz = st.nominalPclkHz;
    portEXIT_CRITICAL(&s_mux);
    GUARD_LOG("boot enabled=%d state=%s holders=0 nominalHz=%lu guardHz=%lu bounce=%u "
              "vsync_cb=%d vsyncSeq=%lu restart_in_vsync=%s auto_resync=REMOVED "
              "linger_ms=%lu down_timeout_ms=%lu up_timeout_ms=%lu",
              YORADIO_RGB_FLASH_GUARD ? 1 : 0, stateName(s_state),
              (unsigned long)st.nominalPclkHz, (unsigned long)st.flashGuardPclkHz,
              (unsigned)st.bounceBufferPx, st.vsyncCounterReady ? 1 : 0,
              (unsigned long)st.vsyncSeq, restartInVsyncName(),
              (unsigned long)kReleaseLingerMs, (unsigned long)kDownConfirmTimeoutMs,
              (unsigned long)kUpConfirmTimeoutMs);
  }
#if YORADIO_RGB_FLASH_GUARD
  stepOwner();
#endif
}

size_t formatStatus(char* out, size_t cap) {
  if (!out || cap == 0) {
    return 0;
  }
  const DisplayPort::RgbScanoutStatus st = DisplayPort::rgbScanoutStatus();
  uint8_t state;
  uint32_t holders, requested_hz;
  bool linger;
  portENTER_CRITICAL(&s_mux);
  state = s_state;
  holders = s_holders;
  requested_hz = s_requested_hz;
  linger = s_linger_armed;
  portEXIT_CRITICAL(&s_mux);
  const int n = snprintf(
      out, cap,
      "[RGB-GUARD] status enabled=%d state=%s holders=%lu requestedHz=%lu nominalHz=%lu "
      "guardHz=%lu vsyncSeq=%lu linger=%d downReq=%lu downOk=%lu "
      "downStall=%lu upReq=%lu upOk=%lu upStall=%lu acquire=%lu release=%lu denied=%lu "
      "bypass=%lu lingerCancel=%lu maxHoldWarn=%lu setPclkFail=%lu releaseUnderflow=%lu "
      "restartExec=%lu restartRejectedBounce=%lu bounce=%u vsync_cb=%d "
      "restart_in_vsync=%s",
      YORADIO_RGB_FLASH_GUARD ? 1 : 0, stateName(state), (unsigned long)holders,
      (unsigned long)requested_hz, (unsigned long)st.nominalPclkHz,
      (unsigned long)st.flashGuardPclkHz, (unsigned long)st.vsyncSeq,
      linger ? 1 : 0, (unsigned long)c_downReq,
      (unsigned long)c_downOk, (unsigned long)c_downStall, (unsigned long)c_upReq,
      (unsigned long)c_upOk, (unsigned long)c_upStall, (unsigned long)c_acquire,
      (unsigned long)c_release, (unsigned long)c_denied, (unsigned long)c_bypass,
      (unsigned long)c_lingerCancel, (unsigned long)c_maxHoldWarn,
      (unsigned long)c_setPclkFail, (unsigned long)c_releaseUnderflow,
      (unsigned long)st.restartExec,
      (unsigned long)st.restartRejectedBounce, (unsigned)st.bounceBufferPx,
      st.vsyncCounterReady ? 1 : 0, restartInVsyncName());

  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  return (static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : cap - 1;
}

}  // namespace rgb_flash_guard
