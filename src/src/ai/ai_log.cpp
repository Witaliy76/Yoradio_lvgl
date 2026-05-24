/**
 * ai_log.cpp - AI Layer logging implementation
 * Description: Implementation of global boot_done flag for AI debug logs
 * Author: W76W, 4pda.to
 * Date: 20.01.2026
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "ai_log.h"

// Boot gate: по умолчанию false (boot не завершён) / Boot gate: default false (boot not complete)
volatile bool g_ai_boot_done = false;
