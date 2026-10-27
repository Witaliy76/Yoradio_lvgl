// RGB flash guard: keeps the direct-T0 RGB pixel clock low while flash is written (S01-5B).
// RGB flash guard: держит пониженный PCLK direct-T0 RGB, пока идёт запись во flash (S01-5B).
//
// Ownership / владение:
//   storage code   -> acquire() / release()        ("I need a protected flash episode")
//   Display::loop  -> poll() on DspTask              (sole caller of DisplayPort::requestRgbPclkHz)
//   DisplayPort    -> ST7701 backend -> esp_lcd_rgb_panel_set_pclk()
// The guard knows no setting names, no NVS/Preferences details and no audio state.
// Guard не знает имён настроек, деталей NVS/Preferences и состояния аудио.
//
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_RGB_FLASH_GUARD_H
#define YORADIO_RGB_FLASH_GUARD_H

#include <stddef.h>
#include <stdint.h>
#include "options.h"

// Single product switch; set it in myoptions.h. 1 = guard active. 0 = flash is written at the
// nominal PCLK (diagnostic comparison only).
// Единый переключатель продукта (myoptions.h). 1 = guard активен. 0 = запись во flash на
// номинальном PCLK (только для диагностического сравнения).
#ifndef YORADIO_RGB_FLASH_GUARD
#define YORADIO_RGB_FLASH_GUARD 1
#endif

namespace rgb_flash_guard {

enum class Grant : uint8_t {
  // Flash-guard PCLK confirmed by the VSYNC barrier. The caller holds one holder and must
  // release() it after its last physical write has returned.
  // PCLK guard подтверждён барьером VSYNC. Вызывающий держит один holder и обязан вызвать
  // release() после возврата последней физической записи.
  Protected = 0,
  // Guard switched off, panel not up, bounce topology or no VSYNC counter. No holder taken.
  // Guard выключен, панель не поднята, bounce-топология или нет счётчика VSYNC. Holder не взят.
  NotApplicable = 1,
  // Flash-guard PCLK not confirmed within the DOWN timeout. No holder kept.
  // The caller must NOT write flash now; it keeps its data dirty and retries later.
  // PCLK guard не подтверждён за DOWN-таймаут. Holder не сохраняется.
  // Вызывающий НЕ пишет flash сейчас; данные остаются dirty, повтор позже.
  Denied = 2
};

// Blocks until the flash-guard PCLK is confirmed (+2 VSYNC after the request) or the DOWN
// timeout expires. Safe from any task. On DspTask (the PCLK owner) it drives the transition
// itself, so a synchronous flush that runs on DspTask cannot deadlock. Never call it while
// holding a lock that DspTask may wait for, and never from an ISR or a timer callback.
// Блокирует до подтверждения PCLK guard (+2 VSYNC после запроса) или DOWN-таймаута.
// Безопасен из любой задачи. На DspTask (владелец PCLK) сам ведёт переход, поэтому
// синхронный flush на DspTask не даёт deadlock. Не вызывать под блокировкой, которую может
// ждать DspTask, и никогда из ISR или callback таймера.
Grant acquire(const char* source);

// Ends the holder of a Protected acquire(). No-op for any other grant.
// Завершает holder после Protected acquire(). Для других grant — no-op.
void release(Grant grant, const char* source);

// DspTask only (Display::loop). Advances DOWN/UP transitions and the release linger.
// Non-blocking. The first call registers DspTask as the PCLK owner.
// Только DspTask (Display::loop). Ведёт переходы DOWN/UP и задержку отпускания.
// Не блокирует. Первый вызов регистрирует DspTask как владельца PCLK.
void poll();

// One status line without a trailing newline. Returns the length written.
// Одна строка статуса без перевода строки. Возвращает записанную длину.
size_t formatStatus(char* out, size_t cap);

const char* grantName(Grant grant);

// One guarded flash transaction, scoped (S01-5C-1). The constructor is acquire(); the destructor
// is release(), which acts only on a Protected grant, so a Denied or NotApplicable scope never
// releases and a Protected one releases exactly once on every exit path. Same global guard,
// holder count, linger and DspTask owner path as SaveManager. Not copyable, not movable.
// Construct it BEFORE the first mutating filesystem call of the transaction (open for write,
// remove, rename, mkdir) and before any lock that DspTask may wait for. With LittleFS every
// open-for-write already programs flash inside open(). Declare File objects AFTER the scope so
// they close before it releases.
// Одна защищённая flash-транзакция в области видимости (S01-5C-1). Конструктор — acquire(),
// деструктор — release(), который действует только при Protected: Denied/NotApplicable не
// отпускают, Protected отпускается ровно один раз на любом выходе. Тот же глобальный guard,
// счётчик holder'ов, linger и owner-путь DspTask, что и у SaveManager. Создавать ДО первого
// мутирующего вызова ФС (open на запись, remove, rename, mkdir) и до любой блокировки, которую
// может ждать DspTask. File объявлять ПОСЛЕ scope, чтобы он закрылся до release.
class Scope {
 public:
  explicit Scope(const char* source) : source_(source), grant_(acquire(source)) {}
  ~Scope() { release(grant_, source_); }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  Scope(Scope&&) = delete;
  Scope& operator=(Scope&&) = delete;

  // True when the transaction may mutate flash: 6 MHz confirmed, or the guard does not apply
  // (switched off, panel not up, bounce topology). False = Denied: do not touch flash.
  // True — можно менять flash: 6 МГц подтверждены или guard неприменим. False = Denied.
  bool ok() const { return grant_ != Grant::Denied; }
  Grant grant() const { return grant_; }

 private:
  const char* source_;
  Grant grant_;
};

}  // namespace rgb_flash_guard

#endif  // YORADIO_RGB_FLASH_GUARD_H
