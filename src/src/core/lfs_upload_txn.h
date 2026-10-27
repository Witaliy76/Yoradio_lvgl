// LittleFS upload transaction that spans several HTTP chunk callbacks (S01-5C-2, Class B).
// Транзакция загрузки в LittleFS на несколько chunk-callback'ов HTTP (S01-5C-2, класс B).
//
// Why: an upload mutates flash across many callbacks (stdio 4 KiB buffer, 512 B littlefs cache,
// block erases and metadata commits are not aligned to HTTP chunks), so the RGB flash guard must
// be held from the first mutation at chunk 0 until the final close/rename/remove, and must be
// returned on every end: success, error, client abort, or a request that never sees its final
// chunk. A stack rgb_flash_guard::Scope cannot outlive one callback, so this owns one heap Scope
// tied to an owner (the AsyncWebServerRequest*). No PCLK logic here: the global guard decides.
// Зачем: загрузка меняет flash на протяжении многих callback'ов, поэтому RGB flash guard держится
// от первой мутации на chunk 0 до финальных close/rename/remove и возвращается на любом исходе:
// успех, ошибка, обрыв клиента, запрос без финального chunk. Стековый Scope не переживает один
// callback, поэтому здесь один Scope в куче, привязанный к владельцу (AsyncWebServerRequest*).
// Логики PCLK здесь нет: решает глобальный guard.
//
// Contract / контракт:
//   begin(owner, source, cleanup) at chunk 0, BEFORE the first mutating FS call; false = Denied:
//       touch nothing, answer HTTP 503.
//   activeFor(owner) gates later chunks.
//   finish(outcome) after the last FS call of the transaction returned (inline cleanup included).
//   abort(reason) runs cleanup(owner) while the guard is still held, then releases.
//   ownerGone(owner) from the request's onDisconnect hook (fires once on every request end):
//       aborts only if this owner's transaction is still open, so it is a no-op after finish().
// All calls for one slot must come from one task (async_tcp for HTTP uploads). Not copyable.
// Все вызовы одного слота — из одной задачи (async_tcp для HTTP-загрузок). Не копируется.
//
// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef YORADIO_LFS_UPLOAD_TXN_H
#define YORADIO_LFS_UPLOAD_TXN_H

#include <Arduino.h>
#include <new>

#include "rgb_flash_guard.h"

namespace lfs_upload {

using CleanupFn = void (*)(void* owner);

class Txn {
 public:
  Txn() = default;
  ~Txn() { abort("destroyed"); }
  Txn(const Txn&) = delete;
  Txn& operator=(const Txn&) = delete;

  bool begin(const void* owner, const char* source, CleanupFn cleanup) {
    if (scope_) {
      abort("superseded");  // a stale transaction of an older request: clean it up first
    }
    const uint32_t t_req = millis();
    rgb_flash_guard::Scope* s = new (std::nothrow) rgb_flash_guard::Scope(source);
    if (!s || !s->ok()) {
      delete s;  // Denied scope: its destructor does not release
      Serial.printf("[LFS-UPLOAD] abort source=%s reason=%s bytes=0 hold_ms=0\n", source,
                    s ? "flash_busy" : "no_memory");
      return false;
    }
    scope_ = s;
    owner_ = owner;
    source_ = source;
    cleanup_ = cleanup;
    bytes_ = 0;
    t_active_ = millis();
    Serial.printf("[LFS-UPLOAD] begin source=%s guard=%s wait_ms=%lu\n", source_,
                  rgb_flash_guard::grantName(s->grant()),
                  static_cast<unsigned long>(t_active_ - t_req));
    return true;
  }

  bool activeFor(const void* owner) const { return scope_ != nullptr && owner_ == owner; }
  bool active() const { return scope_ != nullptr; }
  void addBytes(size_t n) { bytes_ += n; }

  void finish(const char* outcome) {
    if (!scope_) return;
    Serial.printf("[LFS-UPLOAD] complete source=%s outcome=%s bytes=%lu hold_ms=%lu\n", source_,
                  outcome, static_cast<unsigned long>(bytes_),
                  static_cast<unsigned long>(millis() - t_active_));
    end();
  }

  void abort(const char* reason) {
    if (!scope_) return;
    if (cleanup_) {
      cleanup_(const_cast<void*>(owner_));  // still under the guard
    }
    Serial.printf("[LFS-UPLOAD] abort source=%s reason=%s bytes=%lu hold_ms=%lu\n", source_, reason,
                  static_cast<unsigned long>(bytes_),
                  static_cast<unsigned long>(millis() - t_active_));
    end();
  }

  void ownerGone(const void* owner) {
    if (activeFor(owner)) abort("disconnect");
  }

 private:
  void end() {
    rgb_flash_guard::Scope* s = scope_;
    scope_ = nullptr;
    owner_ = nullptr;
    cleanup_ = nullptr;
    delete s;  // Scope destructor = exactly one release
  }

  rgb_flash_guard::Scope* scope_ = nullptr;
  const void* owner_ = nullptr;
  const char* source_ = "";
  CleanupFn cleanup_ = nullptr;
  size_t bytes_ = 0;
  uint32_t t_active_ = 0;
};

// Ends the transaction with finish(outcome) when the enclosing scope exits, after every inline
// cleanup on that path returned. Used for the final-chunk block with its many return paths.
// Завершает транзакцию finish(outcome) при выходе из области — после inline-очистки на этом пути.
class FinishOnExit {
 public:
  FinishOnExit(Txn& txn, const char* outcome) : txn_(txn), outcome_(outcome) {}
  ~FinishOnExit() { txn_.finish(outcome_); }
  void outcome(const char* o) { outcome_ = o; }
  FinishOnExit(const FinishOnExit&) = delete;
  FinishOnExit& operator=(const FinishOnExit&) = delete;

 private:
  Txn& txn_;
  const char* outcome_;
};

// Hold-time log for a synchronous Class B stage guarded by a stack rgb_flash_guard::Scope
// (playlist commit / index rebuild on loopTask). Declare it right AFTER the Scope and BEFORE any
// File, so on exit the files close first, then this logs, then the Scope releases.
// Лог времени удержания для синхронного этапа класса B под стековым Scope. Объявлять сразу ПОСЛЕ
// Scope и ДО File: на выходе сначала закрываются файлы, затем лог, затем release.
class StageTimer {
 public:
  StageTimer(const char* source, const char* outcome)
      : source_(source), outcome_(outcome), t0_(millis()) {}
  ~StageTimer() {
    Serial.printf("[LFS-UPLOAD] complete source=%s outcome=%s bytes=%lu hold_ms=%lu\n", source_,
                  outcome_, static_cast<unsigned long>(bytes_),
                  static_cast<unsigned long>(millis() - t0_));
  }
  void outcome(const char* o) { outcome_ = o; }
  void addBytes(size_t n) { bytes_ += n; }
  StageTimer(const StageTimer&) = delete;
  StageTimer& operator=(const StageTimer&) = delete;

 private:
  const char* source_;
  const char* outcome_;
  uint32_t t0_;
  size_t bytes_ = 0;
};

}  // namespace lfs_upload

#endif  // YORADIO_LFS_UPLOAD_TXN_H
