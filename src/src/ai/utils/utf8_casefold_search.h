#ifndef UTF8_CASEFOLD_SEARCH_H
#define UTF8_CASEFOLD_SEARCH_H

/**
 * utf8_casefold_search.h - Case-insensitive UTF-8 string search utilities
 * Description: Case-insensitive pattern search in UTF-8 strings without memory allocation (RU+EN)
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include <stddef.h>
#include <stdbool.h>
#include <cstdint>

/**
 * UTF-8 Case-Insensitive Search Utilities
 * Утилиты для регистронезависимого поиска в UTF-8 строках
 * 
 * Поддерживает русский (кириллица) и английский (ASCII) без выделения памяти.
 * Supports Russian (Cyrillic) and English (ASCII) without memory allocation.
 */

/**
 * Декодирует следующий UTF-8 символ и возвращает codepoint.
 * Decodes next UTF-8 character and returns codepoint.
 * 
 * @param str указатель на начало UTF-8 строки / pointer to start of UTF-8 string
 * @param len длина строки в байтах / string length in bytes
 * @param out_codepoint выходной codepoint / output codepoint
 * @return количество прочитанных байт (0 если конец строки или ошибка) / bytes read (0 if end or error)
 */
size_t utf8_decode_char(const char* str, size_t len, uint32_t* out_codepoint);

/**
 * Приводит Unicode codepoint к нижнему регистру (casefold).
 * Converts Unicode codepoint to lowercase (casefold).
 * 
 * Поддерживает:
 * - ASCII: A-Z (U+0041-U+005A) → a-z (U+0061-U+007A)
 * - Кириллица: А-Я (U+0410-U+042F) → а-я (U+0430-U+044F)
 * - Ё (U+0401) → ё (U+0451)
 * 
 * Supports:
 * - ASCII: A-Z (U+0041-U+005A) → a-z (U+0061-U+007A)
 * - Cyrillic: А-Я (U+0410-U+042F) → а-я (U+0430-U+044F)
 * - Ё (U+0401) → ё (U+0451)
 */
uint32_t utf8_casefold(uint32_t codepoint);

/**
 * Проверяет, содержит ли haystack любой из needles (регистронезависимо, UTF-8 RU+EN).
 * Checks if haystack contains any of needles (case-insensitive, UTF-8 RU+EN).
 * 
 * @param haystack строка для поиска / string to search in
 * @param needles массив указателей на строки-паттерны / array of pattern string pointers
 * @param n количество паттернов / number of patterns
 * @return true если найден хотя бы один паттерн / true if any pattern found
 */
bool utf8_contains_any_ci(const char* haystack, const char* const* needles, size_t n);

#endif // UTF8_CASEFOLD_SEARCH_H

