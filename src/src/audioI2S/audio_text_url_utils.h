#ifndef AUDIO_TEXT_URL_UTILS_H
#define AUDIO_TEXT_URL_UTILS_H

// Small boundary helpers for audio metadata and HTTP stream URLs.
// Небольшие boundary-helper'ы для audio metadata и URL HTTP-потоков.
// They validate bytes they receive; source-encoding detection remains the caller's policy.
// Они проверяют полученные байты; определение исходной кодировки остаётся политикой caller'а.

#include <cstddef>
#include <cstdint>

namespace audio_safe {

struct Utf8Unit {
    bool valid;
    uint8_t width;
    uint32_t codepoint;
};

constexpr size_t cstrlen(const char* text) {
    if(!text) return 0;
    size_t len = 0;
    while(text[len] != '\0') ++len;
    return len;
}

// Historical fallback helper: every non-ASCII input byte is treated as Latin-1.
// Исторический fallback: каждый non-ASCII байт трактуется как Latin-1.
// It intentionally does not try to distinguish Latin-1 from damaged UTF-8.
// Он намеренно не пытается отличить Latin-1 от повреждённого UTF-8.
constexpr bool latin1ToUtf8(const char* src, char* dst, size_t capacity) {
    if(!src || !dst || capacity == 0) return false;
    const size_t sourceLength = cstrlen(src);
    if(sourceLength > (static_cast<size_t>(-1) - 1) / 2 || sourceLength * 2 + 1 > capacity) return false;

    size_t out = 0;
    for(size_t in = 0; in < sourceLength; ++in) {
        const uint8_t ch = static_cast<uint8_t>(src[in]);
        if(ch < 0x80) dst[out++] = static_cast<char>(ch);
        else {
            dst[out++] = static_cast<char>(0xC0 | (ch >> 6));
            dst[out++] = static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    dst[out] = '\0';
    return true;
}

constexpr uint8_t asciiLower(uint8_t ch) {
    return (ch >= 'A' && ch <= 'Z') ? static_cast<uint8_t>(ch + ('a' - 'A')) : ch;
}

constexpr bool startsWithIcase(const char* text, const char* prefix) {
    if(!text || !prefix) return false;
    for(size_t i = 0; prefix[i] != '\0'; ++i) {
        if(text[i] == '\0' || asciiLower(static_cast<uint8_t>(text[i])) !=
           asciiLower(static_cast<uint8_t>(prefix[i]))) return false;
    }
    return true;
}

constexpr bool endsWithIcase(const char* text, const char* suffix) {
    const size_t textLen = cstrlen(text);
    const size_t suffixLen = cstrlen(suffix);
    if(!text || !suffix || suffixLen > textLen) return false;
    for(size_t i = 0; i < suffixLen; ++i) {
        if(asciiLower(static_cast<uint8_t>(text[textLen - suffixLen + i])) !=
           asciiLower(static_cast<uint8_t>(suffix[i]))) return false;
    }
    return true;
}

constexpr bool containsIcase(const char* text, const char* needle) {
    if(!text || !needle) return false;
    if(needle[0] == '\0') return true;
    for(size_t i = 0; text[i] != '\0'; ++i) {
        size_t j = 0;
        while(needle[j] != '\0' && text[i + j] != '\0' &&
              asciiLower(static_cast<uint8_t>(text[i + j])) ==
              asciiLower(static_cast<uint8_t>(needle[j]))) ++j;
        if(needle[j] == '\0') return true;
    }
    return false;
}

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
                                               ((b1 & 0x3F) << 6) |
                                               (b2 & 0x3F))};
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
                                               ((b2 & 0x3F) << 6) |
                                               (b3 & 0x3F))};
    }
    return {false, 0, 0};
}

constexpr bool isMetadataControl(uint32_t codepoint) {
    return codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F);
}

constexpr size_t bomLength(const char* text, size_t len) {
    return text && len >= 3 && static_cast<uint8_t>(text[0]) == 0xEF &&
           static_cast<uint8_t>(text[1]) == 0xBB && static_cast<uint8_t>(text[2]) == 0xBF ? 3 : 0;
}

constexpr bool isValidUtf8(const char* text, bool rejectMetadataControls = false) {
    if(!text) return false;
    const size_t len = cstrlen(text);
    size_t pos = bomLength(text, len);
    while(pos < len) {
        const Utf8Unit unit = decodeUtf8(text, len, pos);
        if(!unit.valid || (rejectMetadataControls && isMetadataControl(unit.codepoint))) return false;
        pos += unit.width;
    }
    return true;
}

constexpr bool copyMetadataUtf8(char* dst, size_t capacity, const char* src) {
    if(!dst || capacity == 0) return false;
    dst[0] = '\0';
    if(!src) return false;

    // Validate the complete source before callers replace state. Output truncation is
    // only at a codepoint boundary. / Вся строка проверяется до замены состояния;
    // усечение результата выполняется только на границе codepoint.
    const size_t len = cstrlen(src);
    size_t pos = bomLength(src, len);
    size_t out = 0;
    bool truncated = false;
    while(pos < len) {
        const Utf8Unit unit = decodeUtf8(src, len, pos);
        if(!unit.valid || isMetadataControl(unit.codepoint)) {
            dst[0] = '\0';
            return false;
        }
        if(!truncated && out + unit.width < capacity) {
            for(uint8_t i = 0; i < unit.width; ++i) dst[out + i] = src[pos + i];
            out += unit.width;
        } else truncated = true;
        pos += unit.width;
    }
    dst[out] = '\0';
    return true;
}

inline void truncateAtInvalidUtf8(char* text) {
    if(!text) return;
    const size_t len = cstrlen(text);
    size_t pos = 0;
    while(pos < len) {
        const Utf8Unit unit = decodeUtf8(text, len, pos);
        if(!unit.valid) {
            text[pos] = '\0';
            return;
        }
        pos += unit.width;
    }
}

constexpr bool parseUnsignedDecimal(const char* text, uint32_t& value) {
    if(!text) return false;
    size_t pos = 0;
    while(text[pos] == ' ' || text[pos] == '\t') ++pos;
    if(text[pos] == '+') ++pos;
    if(text[pos] < '0' || text[pos] > '9') return false;
    uint32_t result = 0;
    while(text[pos] >= '0' && text[pos] <= '9') {
        const uint32_t digit = static_cast<uint32_t>(text[pos] - '0');
        if(result > (0xFFFFFFFFu - digit) / 10u) result = 0xFFFFFFFFu;
        else if(result != 0xFFFFFFFFu) result = result * 10u + digit;
        ++pos;
    }
    value = result;
    return true;
}

struct TextSlice {
    const char* data;
    size_t length;
    bool valid;
};

constexpr bool isMetadataKeyChar(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
}

constexpr TextSlice streamTitleValue(const char* metadata) {
    constexpr char prefix[] = "StreamTitle='";
    if(!startsWithIcase(metadata, prefix)) return {nullptr, 0, false};
    const size_t len = cstrlen(metadata);
    const size_t start = sizeof(prefix) - 1;
    for(size_t pos = start; pos + 1 < len; ++pos) {
        if(metadata[pos] != '\'' || metadata[pos + 1] != ';') continue;
        size_t next = pos + 2;
        while(next < len && metadata[next] == ' ') ++next;
        if(next == len) return {metadata + start, pos - start, true};
        const size_t keyStart = next;
        while(next < len && isMetadataKeyChar(metadata[next])) ++next;
        if(next > keyStart && next < len && metadata[next] == '=') {
            return {metadata + start, pos - start, true};
        }
    }
    return {nullptr, 0, false};
}

struct UrlParts {
    bool valid;
    bool ssl;
    const char* host;
    size_t hostLength;
    const char* authority;
    size_t authorityLength;
    uint16_t port;
    bool explicitPort;
    const char* path;
    size_t pathLength;
    const char* query;
    size_t queryLength;
};

constexpr bool isUrlControl(uint8_t ch) {
    return ch < 0x20 || ch == 0x7F;
}

constexpr UrlParts parseUrl(const char* url) {
    UrlParts result{};
    if(!url || url[0] == '\0') return result;

    // Keep path/query bytes raw: no %xx decoding, normalization, fragment or IPv6 handling.
    // Path/query сохраняются как есть: без %xx decode, normalization, fragment и IPv6 logic.
    for(size_t i = 0; url[i] != '\0'; ++i) {
        if(isUrlControl(static_cast<uint8_t>(url[i]))) return result;
    }

    const char* authority = url;
    if(startsWithIcase(url, "https://")) {
        result.ssl = true;
        authority += 8;
    } else if(startsWithIcase(url, "http://")) {
        authority += 7;
    } else if(url[0] == '/' && url[1] == '/') {
        authority += 2;
    }

    size_t authorityLength = 0;
    while(authority[authorityLength] != '\0' && authority[authorityLength] != '/' &&
          authority[authorityLength] != '?') ++authorityLength;
    if(authorityLength == 0) return result;
    for(size_t i = 0; i < authorityLength; ++i) {
        if(authority[i] == ' ') return result;
    }

    size_t colon = authorityLength;
    for(size_t i = 0; i < authorityLength; ++i) {
        if(authority[i] == ':') { colon = i; break; }
    }
    if(colon == 0) return result;

    result.port = result.ssl ? 443 : 80;
    if(colon < authorityLength) {
        uint32_t parsedPort = 0;
        if(colon + 1 == authorityLength) return UrlParts{};
        for(size_t i = colon + 1; i < authorityLength; ++i) {
            if(authority[i] < '0' || authority[i] > '9') return UrlParts{};
            parsedPort = parsedPort * 10u + static_cast<uint32_t>(authority[i] - '0');
            if(parsedPort > 65535u) return UrlParts{};
        }
        if(parsedPort == 0) return UrlParts{};
        result.port = static_cast<uint16_t>(parsedPort);
        result.explicitPort = true;
    }

    const char* afterAuthority = authority + authorityLength;
    const char* query = nullptr;
    const char* path = nullptr;
    size_t pathLength = 0;
    if(*afterAuthority == '/') {
        path = afterAuthority + 1;
        const char* cursor = path;
        while(*cursor != '\0' && *cursor != '?') ++cursor;
        pathLength = static_cast<size_t>(cursor - path);
        if(*cursor == '?') query = cursor + 1;
    } else if(*afterAuthority == '?') {
        query = afterAuthority + 1;
    }

    result.valid = true;
    result.host = authority;
    result.hostLength = colon;
    result.authority = authority;
    result.authorityLength = authorityLength;
    result.path = path;
    result.pathLength = pathLength;
    result.query = query;
    result.queryLength = query ? cstrlen(query) : 0;
    return result;
}

constexpr bool resolveRedirect(const char* currentUrl, const char* location, char* dst, size_t capacity) {
    if(!dst || capacity == 0) return false;
    dst[0] = '\0';
    if(!location) return false;
    while(*location == ' ' || *location == '\t') ++location;

    const char* prefix = "";
    if(location[0] == '/' && location[1] == '/') {
        prefix = startsWithIcase(currentUrl, "https://") ? "https:" : "http:";
    } else if(!startsWithIcase(location, "http://") && !startsWithIcase(location, "https://")) {
        return false;
    }

    const size_t prefixLen = cstrlen(prefix);
    size_t locationLen = cstrlen(location);
    while(locationLen > 0 && (location[locationLen - 1] == ' ' || location[locationLen - 1] == '\t')) --locationLen;
    if(prefixLen + locationLen + 1 > capacity) return false;
    size_t out = 0;
    for(size_t i = 0; i < prefixLen; ++i) dst[out++] = prefix[i];
    for(size_t i = 0; i < locationLen; ++i) dst[out++] = location[i];
    dst[out] = '\0';
    return parseUrl(dst).valid;
}

// ---------------------------------------------------------------------------
// Endpoint identity for HTTP connection reuse.
// Идентичность endpoint для повторного использования HTTP-соединения.
//
// A live socket may be reused only when hostname, scheme AND effective port all
// match. Hostname alone is not enough: the same host can serve a second port,
// and http <-> https on one host needs a different transport object entirely.
// Сокет можно переиспользовать только при совпадении hostname, схемы и
// эффективного порта: одного hostname недостаточно (другой порт, http<->https).
// ---------------------------------------------------------------------------

// The transport functions coerce an https URL that still carries port 80 to 443
// just before connecting, so endpoint identity must apply the same rule.
// Транспортные функции приводят https с портом 80 к 443 перед connect — сравнение
// endpoint обязано использовать то же правило.
constexpr uint16_t effectiveEndpointPort(bool ssl, uint16_t port) {
    return (ssl && port == 80u) ? 443u : port;
}

// Host bytes are compared exactly, as the previous hostname-only check did:
// no case folding, no IDN and no trailing-dot normalization is introduced here.
// Байты hostname сравниваются точно, как и в прежней проверке по hostname.
constexpr bool sameHttpEndpoint(const char* urlA, const char* urlB) {
    const UrlParts a = parseUrl(urlA);
    const UrlParts b = parseUrl(urlB);
    if(!a.valid || !b.valid) return false;
    if(a.ssl != b.ssl) return false;
    if(effectiveEndpointPort(a.ssl, a.port) != effectiveEndpointPort(b.ssl, b.port)) return false;
    if(a.hostLength != b.hostLength) return false;
    for(size_t i = 0; i < a.hostLength; ++i) {
        if(a.host[i] != b.host[i]) return false;
    }
    return true;
}

} // namespace audio_safe

#endif
