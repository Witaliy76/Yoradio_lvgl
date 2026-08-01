#ifndef AI_PROMPT_H
#define AI_PROMPT_H

/**
 * ai_prompt.h - AI prompt loader header
 * Description: AI prompt loader from LittleFS with RAM caching
 * Author: Witaliy76 - https://github.com/Witaliy76
 * Date: 02.01.2026
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include <Arduino.h>

/**
 * Get AI prompt from LittleFS (`/ai/ai_prompt.txt`)
 * Получить AI промпт из LittleFS (`/ai/ai_prompt.txt`)
 *
 * Uses RAM cache - reads from LittleFS only once
 * Использует RAM кеш - читает из LittleFS только один раз
 *
 * STRICT MODE: Returns true ONLY if prompt loaded from file / СТРОГИЙ РЕЖИМ: Возвращает true ТОЛЬКО если промпт загружен из файла
 *
 * @param outPrompt Output string for prompt / Выходная строка для промпта
 * @return true if prompt loaded successfully from file, false otherwise / true если промпт успешно загружен из файла, false иначе
 */
bool aiPromptGet(String& outPrompt);

/**
 * Reset prompt cache (forces reload from LittleFS on next request)
 * Сбросить кеш промпта (принудительно перезагрузить из LittleFS при следующем запросе)
 *
 * Useful after LittleFS files are updated / Полезно после обновления файлов LittleFS
 */
void aiPromptResetCache();

/**
 * Check if prompt file is available (without fallback)
 * Проверить, доступен ли файл промпта (без fallback)
 * 
 * Returns true only if file exists and is non-empty / Возвращает true только если файл существует и не пустой
 * Does not use cache or fallback / Не использует кеш или fallback
 * 
 * @return true if prompt file exists and is valid, false otherwise / true если файл промпта существует и валиден, false иначе
 */
bool aiPromptIsAvailable();

/**
 * Get prompt file size in bytes
 * Получить размер файла промпта в байтах
 * 
 * Returns 0 if file not available or invalid / Возвращает 0 если файл недоступен или невалиден
 * 
 * @return Size in bytes, or 0 if unavailable / Размер в байтах, или 0 если недоступен
 */
size_t aiPromptGetSize();

/**
 * Get maximum allowed prompt file size
 * Получить максимальный разрешённый размер файла промпта
 * 
 * @return Maximum size in bytes / Максимальный размер в байтах
 */
size_t aiPromptGetMaxLen();

#endif // AI_PROMPT_H

