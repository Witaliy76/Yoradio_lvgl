/**
 * ai_log.h - AI Layer logging macros
 * Description: Logging macros for AI Layer with compile-time debug flag support
 * Author: W76W, 4pda.to
 * Date: 20.01.2026
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#pragma once
#include <Arduino.h>
#include "../core/options.h"  // Для доступа к myoptions.h / For access to myoptions.h

#ifndef AI_LAYER_DEBUG
  #define AI_LAYER_DEBUG 0
#endif

// Boot gate: AI_DLOG не печатается до завершения boot banner / Boot gate: AI_DLOG not printed until boot banner is complete
// По умолчанию false (boot не завершён) / Default false (boot not complete)
extern volatile bool g_ai_boot_done;

/**
 * aiLogSetBootDone - Установить флаг завершения boot / Set boot completion flag
 * aiLogSetBootDone - Set boot completion flag
 * 
 * Usage: aiLogSetBootDone(true); // После завершения boot banner / After boot banner is complete
 */
inline void aiLogSetBootDone(bool v) {
    g_ai_boot_done = v;
}

/**
 * AI_LOG - Always print (class A logs)
 * AI_LOG - Всегда печатать (логи класса A)
 * 
 * Usage: AI_LOG("[AISubsystem] Track changed, new track_id: %u", track_id);
 * 
 * Note: Long strings (>200 chars) are truncated to prevent Serial buffer overflow / Примечание: Длинные строки (>200 символов) обрезаются для предотвращения переполнения буфера Serial
 */
#define AI_LOG(fmt, ...) do { \
    char _ai_log_buf[256]; \
    int _ai_log_len = snprintf(_ai_log_buf, sizeof(_ai_log_buf) - 1, fmt, ##__VA_ARGS__); \
    if (_ai_log_len < 0) { \
        _ai_log_buf[0] = '\0'; \
    } else if (_ai_log_len >= (int)(sizeof(_ai_log_buf) - 1)) { \
        _ai_log_buf[sizeof(_ai_log_buf) - 4] = '.'; \
        _ai_log_buf[sizeof(_ai_log_buf) - 3] = '.'; \
        _ai_log_buf[sizeof(_ai_log_buf) - 2] = '.'; \
        _ai_log_buf[sizeof(_ai_log_buf) - 1] = '\0'; \
    } \
    Serial.printf("%s\n", _ai_log_buf); \
} while(0)

/**
 * AI_DLOG - Print only if AI_LAYER_DEBUG == 1 AND boot is done (class B logs)
 * AI_DLOG - Печатать только если AI_LAYER_DEBUG == 1 И boot завершён (логи класса B)
 * 
 * Usage: AI_DLOG("[AISubsystem] TT score details: %s", breakdown.c_str());
 * 
 * Note: Compiles to nothing when AI_LAYER_DEBUG == 0 (no overhead)
 *       Suppressed during boot banner to avoid interleaving
 * Примечание: Компилируется в "ничего" при AI_LAYER_DEBUG == 0 (без накладных расходов)
 *             Подавлен во время boot banner чтобы избежать вклинивания
 */
#if AI_LAYER_DEBUG
  #define AI_DLOG(fmt, ...) do { if (g_ai_boot_done) Serial.printf((fmt "\n"), ##__VA_ARGS__); } while(0)
#else
  #define AI_DLOG(fmt, ...) do {} while(0)
#endif
