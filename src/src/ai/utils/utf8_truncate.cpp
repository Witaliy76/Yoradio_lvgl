/**
 * utf8_truncate.cpp - UTF-8 string truncation utilities implementation
 * Description: Safe UTF-8 string copying to fixed buffers with character boundary awareness
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "utf8_truncate.h"
#include <string.h>

/**
 * Вычисляет длину корректного UTF-8 префикса не больше maxBytes.
 * Returns the length of a valid UTF-8 prefix not exceeding maxBytes.
 */
size_t utf8_valid_prefix_len(const uint8_t* s, size_t maxBytes) {
    if (!s || maxBytes == 0) {
        return 0;
    }
    
    size_t i = 0;
    while (i < maxBytes && s[i] != 0) {
        uint8_t c = s[i];
        size_t need = 0;
        
        // Определяем сколько байт нужно для символа
        // Determine how many bytes are needed for the character
        if (c < 0x80) {
            // ASCII (0x00-0x7F) - 1 байт
            need = 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence (0xC0-0xDF) - нужен 1 continuation byte
            need = 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte sequence (0xE0-0xEF) - нужны 2 continuation bytes
            need = 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte sequence (0xF0-0xF7) - нужны 3 continuation bytes
            need = 4;
        } else {
            // Невалидный lead byte - останавливаемся перед ним
            // Invalid lead byte - stop before it
            break;
        }
        
        // Проверяем, поместится ли весь символ
        // Check if the entire character fits
        if (i + need > maxBytes) {
            // Символ не поместится - останавливаемся перед lead byte
            // Character won't fit - stop before lead byte
            break;
        }
        
        // Проверяем continuation bytes для многобайтовых символов
        // Verify continuation bytes for multibyte characters
        bool valid = true;
        for (size_t k = 1; k < need; k++) {
            uint8_t cc = s[i + k];
            if ((cc & 0xC0) != 0x80) {
                // Невалидный continuation byte - останавливаемся перед символом
                // Invalid continuation byte - stop before character
                valid = false;
                break;
            }
        }
        
        if (!valid) {
            // Невалидный символ - останавливаемся перед ним
            // Invalid character - stop before it
            break;
        }
        
        // Символ валиден, переходим к следующему
        // Character is valid, move to next
        i += need;
    }
    
    return i;
}

/**
 * Копирует UTF-8 строку в буфер с корректной обрезкой на границе символа.
 * Copies UTF-8 string to buffer with correct truncation at character boundary.
 */
void utf8_copy_trunc(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    
    if (!src) {
        dst[0] = '\0';
        return;
    }
    
    // Копируем сырые данные до dst_size-1 байт (оставляем место для \0)
    // Copy raw data up to dst_size-1 bytes (leave room for \0)
    size_t src_len = strlen(src);
    size_t raw_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;
    if (raw_len > 0) {
        memcpy(dst, src, raw_len);
    }
    dst[raw_len] = '\0';
    
    // Исправляем обрезку на границе символа
    // Fix truncation at character boundary
    size_t valid_len = utf8_valid_prefix_len((const uint8_t*)dst, raw_len);
    dst[valid_len] = '\0';
}

