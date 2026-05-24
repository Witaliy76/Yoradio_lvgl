/**
 * ai_prompt.cpp - AI prompt loader implementation
 * Description: AI prompt loader from filesystem with RAM caching
 * Author: W76W, 4pda.to
 * Date: 02.01.2026
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "ai_prompt.h"
#include "ai_log.h"  // AI Layer logging macros
#include <LittleFS.h>
#include "../core/config.h"   // Для fsIsReady() / For fsIsReady()

// Maximum prompt file size in bytes / Максимальный размер файла промпта в байтах
// Single source of truth for prompt size limit / Единый источник истины для лимита размера промпта
#define AI_PROMPT_MAX_LEN 20480

// Prompt file path / Путь к файлу промпта
#define AI_PROMPT_PATH "/ai/ai_prompt.txt"

// RAM cache for prompt / RAM кеш для промпта
// Cache only successful loads from filesystem / Кешируются только успешные загрузки из filesystem
static String g_prompt;
static bool g_prompt_loaded = false;

// Load prompt from filesystem / Загрузить промпт из filesystem
// Returns true only if file successfully loaded / Возвращает true только если файл успешно загружен
// No fallback - strict mode / Без fallback - строгий режим
static bool loadPromptFromFS(String& outPrompt) {
    // Проверка готовности filesystem / Check filesystem readiness
    if (!fsIsReady()) {
        return false;
    }
    
    if (!LittleFS.exists(AI_PROMPT_PATH)) {
        AI_LOG("[AI] Prompt missing: %s", AI_PROMPT_PATH);
        return false;
    }
    
    File file = LittleFS.open(AI_PROMPT_PATH, "r");
    if (!file || file.isDirectory()) {
        AI_LOG("[AI] Failed to open prompt: %s", AI_PROMPT_PATH);
        return false;
    }
    
    size_t size = file.size();
    if (size == 0 || size > AI_PROMPT_MAX_LEN) {
        AI_LOG("[AI] Prompt invalid size: %s size=%u max=%u", AI_PROMPT_PATH, size, AI_PROMPT_MAX_LEN);
        file.close();
        return false;
    }
    
    outPrompt = file.readString();
    size_t file_size = file.size();  // Save file size before close / Сохранить размер файла до закрытия
    file.close();
    
    outPrompt.trim();
    if (outPrompt.length() == 0) {
        AI_LOG("[AI] Prompt empty after trim: %s", AI_PROMPT_PATH);
        return false;
    }
    
    // Log both file size and string length for diagnostics / Логировать и размер файла, и длину строки для диагностики
    AI_LOG("[AI] Loaded prompt from %s (file=%u, text=%u)", AI_PROMPT_PATH, file_size, outPrompt.length());
    return true;
}

// Get AI prompt from filesystem / Получить AI промпт из filesystem
// STRICT MODE: returns true ONLY if loaded from file / СТРОГИЙ РЕЖИМ: возвращает true ТОЛЬКО если загружен из файла
// If file not available, returns false and clears outPrompt / Если файл недоступен, возвращает false и очищает outPrompt
bool aiPromptGet(String& outPrompt) {
    outPrompt = "";  // Clear output / Очистить выход
    
    // Check cache / Проверяем кеш
    if (g_prompt_loaded) {
        outPrompt = g_prompt;
        return true;
    }
    
    // Try to load from filesystem / Пытаемся загрузить из filesystem
    if (loadPromptFromFS(g_prompt)) {
        g_prompt_loaded = true;
        outPrompt = g_prompt;
        return true;
    } else {
        // File not available / Файл недоступен
        outPrompt = "";
        return false;
    }
}

// Reset prompt cache (forces reload from filesystem on next request) / Сбросить кеш промпта
// Resets only successfully loaded prompts / Сбрасывает только успешно загруженные промпты
void aiPromptResetCache() {
    g_prompt_loaded = false;
    g_prompt = "";
    AI_LOG("[AI] Prompt cache reset");
}

// Check if prompt file is available (strict mode, no fallback) / Проверить, доступен ли файл промпта (строгий режим, без fallback)
// Returns true only if all conditions met: FS ready, file exists, valid size / Возвращает true только если все условия выполнены
bool aiPromptIsAvailable() {
    // Check filesystem readiness / Проверка готовности filesystem
    if (!fsIsReady()) {
        return false;
    }
    
    // Check file existence / Проверяем существование файла
    if (!LittleFS.exists(AI_PROMPT_PATH)) {
        return false;
    }
    
    // Check file size / Проверяем размер файла
    File file = LittleFS.open(AI_PROMPT_PATH, "r");
    if (!file || file.isDirectory()) {
        return false;
    }
    
    size_t size = file.size();
    file.close();
    
    // File must be non-empty and within size limit / Файл должен быть непустым и в пределах лимита
    if (size == 0 || size > AI_PROMPT_MAX_LEN) {
        return false;
    }
    
    return true;
}

// Get prompt file size in bytes / Получить размер файла промпта в байтах
// Returns 0 if file not available or invalid / Возвращает 0 если файл недоступен или невалиден
size_t aiPromptGetSize() {
    if (!fsIsReady()) {
        return 0;
    }
    
    if (!LittleFS.exists(AI_PROMPT_PATH)) {
        return 0;
    }
    
    File file = LittleFS.open(AI_PROMPT_PATH, "r");
    if (!file || file.isDirectory()) {
        return 0;
    }
    
    size_t size = file.size();
    file.close();
    
    // Return size only if valid / Возвращаем размер только если валиден
    if (size == 0 || size > AI_PROMPT_MAX_LEN) {
        return 0;
    }
    
    return size;
}

// Get maximum allowed prompt file size / Получить максимальный разрешённый размер файла промпта
size_t aiPromptGetMaxLen() {
    return AI_PROMPT_MAX_LEN;
}

