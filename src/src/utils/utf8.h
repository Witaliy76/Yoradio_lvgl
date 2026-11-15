// Byte validation shared by filesystem paths and audio text.
// Общая проверка байтов путей файловой системы и аудиотекста.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace yoradio { namespace text {
struct Utf8Unit {
    bool valid;
    uint8_t width;
    uint32_t codepoint;
};

constexpr Utf8Unit decodeUtf8(const char* text, size_t len, size_t pos) {
    if(!text || pos >= len) return {false, 0, 0};
    const uint8_t b0 = static_cast<uint8_t>(text[pos]);
    if(b0 <= 0x7F) return {true, 1, b0};
    if(b0 >= 0xC2 && b0 <= 0xDF) {
        if(pos + 1 >= len) return {false, 0, 0};
        const uint8_t b1 = static_cast<uint8_t>(text[pos + 1]);
        if((b1 & 0xC0) != 0x80) return {false, 0, 0};
        return {true, 2, static_cast<uint32_t>(((b0 & 0x1F) << 6) | (b1 & 0x3F))};
    }
    if(b0 >= 0xE0 && b0 <= 0xEF) {
        if(pos + 2 >= len) return {false, 0, 0};
        const uint8_t b1 = static_cast<uint8_t>(text[pos + 1]);
        const uint8_t b2 = static_cast<uint8_t>(text[pos + 2]);
        if((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return {false, 0, 0};
        if(b0 == 0xE0 && b1 < 0xA0) return {false, 0, 0};
        if(b0 == 0xED && b1 >= 0xA0) return {false, 0, 0};
        return {true, 3, static_cast<uint32_t>(((b0 & 0x0F) << 12) |
                                               ((b1 & 0x3F) << 6) | (b2 & 0x3F))};
    }
    if(b0 >= 0xF0 && b0 <= 0xF4) {
        if(pos + 3 >= len) return {false, 0, 0};
        const uint8_t b1 = static_cast<uint8_t>(text[pos + 1]);
        const uint8_t b2 = static_cast<uint8_t>(text[pos + 2]);
        const uint8_t b3 = static_cast<uint8_t>(text[pos + 3]);
        if((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return {false, 0, 0};
        if(b0 == 0xF0 && b1 < 0x90) return {false, 0, 0};
        if(b0 == 0xF4 && b1 > 0x8F) return {false, 0, 0};
        return {true, 4, static_cast<uint32_t>(((b0 & 0x07) << 18) |
                                               ((b1 & 0x3F) << 12) |
                                               ((b2 & 0x3F) << 6) | (b3 & 0x3F))};
    }
    return {false, 0, 0};
}

constexpr bool isControl(uint32_t codepoint) {
    return codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F);
}

constexpr bool isValidUtf8(const char* text, size_t len, bool rejectControls = false) {
    if(!text) return false;
    for(size_t pos = 0; pos < len;) {
        const auto unit = decodeUtf8(text, len, pos);
        if(!unit.valid || (rejectControls && isControl(unit.codepoint))) return false;
        pos += unit.width;
    }
    return true;
}
}}
