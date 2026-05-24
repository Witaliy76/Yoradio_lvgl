/**
 * utf8_casefold_search.cpp - Case-insensitive UTF-8 string search utilities implementation
 * Description: Case-insensitive pattern search in UTF-8 strings without memory allocation (RU+EN)
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "utf8_casefold_search.h"
#include <string.h>

/**
 * Декодирует следующий UTF-8 символ и возвращает codepoint.
 */
size_t utf8_decode_char(const char* str, size_t len, uint32_t* out_codepoint) {
    if (!str || len == 0 || !out_codepoint) {
        return 0;
    }
    
    unsigned char first = (unsigned char)str[0];
    
    // ASCII (0x00-0x7F) - 1 байт
    if (first < 0x80) {
        *out_codepoint = first;
        return 1;
    }
    
    // 2-byte sequence (0xC0-0xDF) - нужен 1 continuation byte
    if ((first & 0xE0) == 0xC0) {
        if (len < 2) {
            *out_codepoint = first;
            return 1;
        }
        unsigned char second = (unsigned char)str[1];
        if ((second & 0xC0) != 0x80) {
            // Невалидный continuation byte - трактуем lead byte как одиночный байт
            *out_codepoint = first;
            return 1;
        }
        *out_codepoint = ((first & 0x1F) << 6) | (second & 0x3F);
        return 2;
    }
    
    // 3-byte sequence (0xE0-0xEF) - нужны 2 continuation bytes
    if ((first & 0xF0) == 0xE0) {
        if (len < 3) {
            *out_codepoint = first;
            return 1;
        }
        unsigned char second = (unsigned char)str[1];
        unsigned char third = (unsigned char)str[2];
        if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80) {
            // Невалидные continuation bytes - трактуем lead byte как одиночный байт
            *out_codepoint = first;
            return 1;
        }
        *out_codepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
        return 3;
    }
    
    // 4-byte sequence (0xF0-0xF4) - нужны 3 continuation bytes
    // Строго по UTF-8 валидный максимум — 0xF4 (0xF5..0xF7 вне Unicode)
    if ((first & 0xF8) == 0xF0 && first <= 0xF4) {
        if (len < 4) {
            *out_codepoint = first;
            return 1;
        }
        unsigned char second = (unsigned char)str[1];
        unsigned char third = (unsigned char)str[2];
        unsigned char fourth = (unsigned char)str[3];
        if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80 || (fourth & 0xC0) != 0x80) {
            // Невалидные continuation bytes - трактуем lead byte как одиночный байт
            *out_codepoint = first;
            return 1;
        }
        *out_codepoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
        return 4;
    }
    
    // Невалидный lead byte (0xF5..0xFF или другие) - трактуем как одиночный байт
    *out_codepoint = first;
    return 1;
}

/**
 * Приводит Unicode codepoint к нижнему регистру (casefold).
 */
uint32_t utf8_casefold(uint32_t codepoint) {
    // ASCII: A-Z (U+0041-U+005A) → a-z (U+0061-U+007A)
    if (codepoint >= 0x0041 && codepoint <= 0x005A) {
        return codepoint + 32;
    }
    
    // Кириллица: А-Я (U+0410-U+042F) → а-я (U+0430-U+044F)
    if (codepoint >= 0x0410 && codepoint <= 0x042F) {
        return codepoint + 32;
    }
    
    // Ё (U+0401) → ё (U+0451)
    if (codepoint == 0x0401) {
        return 0x0451;
    }
    
    // Остальные символы без изменений
    return codepoint;
}

/**
 * Проверяет, содержит ли haystack подстроку needle (регистронезависимо, UTF-8 RU+EN).
 */
static bool utf8_contains_ci(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) {
        return false;
    }
    
    size_t haystack_len = strlen(haystack);
    size_t needle_len = strlen(needle);
    
    if (needle_len > haystack_len) {
        return false;
    }
    
    // Идём по haystack позиция за позицией
    for (size_t i = 0; i <= haystack_len - needle_len; ) {
        size_t haystack_pos = i;
        size_t needle_pos = 0;
        bool match = true;
        
        // Сравниваем посимвольно
        while (needle_pos < needle_len && haystack_pos < haystack_len) {
            uint32_t haystack_cp, needle_cp;
            
            // Декодируем символы из haystack и needle
            size_t haystack_bytes = utf8_decode_char(haystack + haystack_pos, haystack_len - haystack_pos, &haystack_cp);
            size_t needle_bytes = utf8_decode_char(needle + needle_pos, needle_len - needle_pos, &needle_cp);
            
            // utf8_decode_char теперь всегда возвращает >= 1, но проверим на всякий случай
            if (haystack_bytes == 0 || needle_bytes == 0) {
                match = false;
                break;
            }
            
            // Приводим к нижнему регистру и сравниваем
            haystack_cp = utf8_casefold(haystack_cp);
            needle_cp = utf8_casefold(needle_cp);
            
            if (haystack_cp != needle_cp) {
                match = false;
                break;
            }
            
            haystack_pos += haystack_bytes;
            needle_pos += needle_bytes;
        }
        
        if (match && needle_pos >= needle_len) {
            // Найдено совпадение
            return true;
        }
        
        // Переходим к следующей позиции в haystack
        uint32_t dummy;
        size_t bytes = utf8_decode_char(haystack + i, haystack_len - i, &dummy);
        // utf8_decode_char теперь всегда возвращает >= 1, но проверим на всякий случай
        if (bytes == 0) {
            bytes = 1; // Пропускаем невалидный байт (страховка)
        }
        i += bytes;
    }
    
    return false;
}

/**
 * Проверяет, содержит ли haystack любой из needles (регистронезависимо, UTF-8 RU+EN).
 */
bool utf8_contains_any_ci(const char* haystack, const char* const* needles, size_t n) {
    if (!haystack || !needles || n == 0) {
        return false;
    }
    
    for (size_t i = 0; i < n; ++i) {
        if (needles[i] && utf8_contains_ci(haystack, needles[i])) {
            return true;
        }
    }
    
    return false;
}

