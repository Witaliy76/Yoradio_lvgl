#ifndef UTF8_TRUNCATE_H
#define UTF8_TRUNCATE_H

/**
 * utf8_truncate.h - UTF-8 string truncation utilities
 * Description: Safe UTF-8 string copying to fixed buffers with character boundary awareness
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include <stddef.h>
#include <stdint.h>

/**
 * UTF-8 Truncate Utilities
 * Утилиты для корректной обрезки UTF-8 строк
 * 
 * Функции для безопасного копирования UTF-8 строк в фиксированные буферы
 * с учётом границ символов, чтобы избежать появления символа.
 */

/**
 * Вычисляет длину корректного UTF-8 префикса не больше maxBytes.
 * Returns the length of a valid UTF-8 prefix not exceeding maxBytes.
 * 
 * Обрабатывает оба случая:
 * A) последний байт - continuation byte -> откатывается до lead byte
 * B) последний байт - lead byte без continuation -> удаляется lead byte
 */
size_t utf8_valid_prefix_len(const uint8_t* s, size_t maxBytes);

/**
 * Копирует UTF-8 строку в буфер с корректной обрезкой на границе символа.
 * Copies UTF-8 string to buffer with correct truncation at character boundary.
 * 
 * Гарантирует, что результат всегда заканчивается корректным UTF-8 символом
 * и никогда не содержит незавершённых кодовых точек.
 */
void utf8_copy_trunc(char* dst, size_t dst_size, const char* src);

#endif // UTF8_TRUNCATE_H

