#ifndef AUDIO_RETRY_BUDGET_H
#define AUDIO_RETRY_BUDGET_H

#include <stdint.h>

// Shared accounting for the unstable-stream reconnect budget.
// Общий учёт бюджета переподключений нестабильного потока.
//
// Contract: one *physical* connect attempt consumes exactly one budget unit.
// The clean connecttohost() fallback that follows a failed buffered httpPrint()
// is a second physical attempt and therefore consumes its own unit; it must not
// ride on the unit already spent by the buffered attempt.
// Контракт: одна физическая попытка подключения списывает ровно одну единицу.
// Clean fallback после неудачного buffered-подключения — вторая физическая
// попытка, поэтому списывает собственную единицу, а не использует чужую.
//
// The budget is reset by the existing stability/session owners only
// (pollStreamStability, beginPlaybackSession, finishUnstableStreamExhausted) —
// this helper never resets it.
// Сброс бюджета остаётся за существующими владельцами состояния; helper его
// никогда не сбрасывает.
namespace audio_safe {

constexpr bool retryBudgetTryConsume(uint8_t& used, uint8_t max) {
    if(used >= max) return false;
    ++used;
    return true;
}

} // namespace audio_safe

#endif
