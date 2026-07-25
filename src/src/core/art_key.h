#pragma once
/*
 * Station artwork filename-key normalization shared by the Web UI and Main screen.
 * Нормализация ключа имени файла station art для Web UI и экрана Main.
 *
 * Single source of truth: artNormalizeKey() must be used by BOTH netserver.cpp AND scr_main.cpp
 * to guarantee that the key computed for save/remove and for lookup are always identical.
 *
 * Единственное место нормализации: и netserver.cpp, и scr_main.cpp включают этот заголовок —
 * ключ при сохранении и при lookup всегда совпадает.
 *
 * Key format: "<safe_prefix>__<8hex>"
 *   safe_prefix — ASCII-safe lowercase prefix (max 32 chars, from station name).
 *   8hex        — FNV-1a 32-bit hash of the raw (unmodified) station name, hex-encoded.
 *   The hash guarantees uniqueness even for all-Cyrillic / all-non-ASCII names that produce
 *   identical prefixes (e.g., "unnamed" fallback) — each raw name yields a different hash.
 *
 * Формат ключа: "<safe_prefix>__<8hex>"
 *   safe_prefix — ASCII-безопасный lowercase-prefix (до 32 символов).
 *   8hex        — FNV-1a 32-bit hash от сырого имени станции; гарантирует уникальность даже
 *                 для кириллических имён, дающих одинаковый prefix ("unnamed").
 *
 * Examples:
 *   "Rock FM"        → "rock_fm__<hash>"
 *   "BBC: Radio 1"   → "bbc_radio_1__<hash>"
 *   "Радио Джаз"     → "unnamed__<hash>"    (distinct hash per distinct raw name)
 *   "Радио Jazz"     → "jazz__<hash>"       (jazz is ASCII; hash differs from above)
 *
 * Backward compatibility note:
 *   Keys from the pre-hash format (e.g., "rock_fm.bin") are NOT compatible with the new format.
 *   Any art files saved before this change must be re-uploaded.
 *
 * Key length: max 42 chars (prefix 32 + "__" 2 + hash 8).
 * Path: "/logo/<key>.bin" — max 52 chars; well within LittleFS path limits.
 * Recommended dst_sz: 68 (covers max key + NUL + margin).
 *
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// FNV-1a 32-bit hash — deterministic, no external deps, stable across platforms.
// FNV-1a 32-bit — детерминированный, без внешних зависимостей, стабилен на всех платформах.
static inline uint32_t artFnv1a32(const char* s) {
    uint32_t h = 2166136261u; // FNV offset basis
    for (; *s; ++s) {
        h ^= (uint8_t)*s;
        h *= 16777619u;       // FNV prime
    }
    return h;
}

// artNormalizeKey: station name → unique stable LittleFS filename key.
// Format: "<safe_ascii_prefix>__<8hex_hash>"
// artNormalizeKey: имя станции → уникальный стабильный ключ для имени файла LittleFS.
static inline void artNormalizeKey(const char* src, char* dst, size_t dst_sz) {
    if (!src || !dst || dst_sz < 18u) { // min: "unnamed__XXXXXXXX\0" = 18
        if (dst && dst_sz > 0) dst[0] = '\0';
        return;
    }

    // Step 1: safe ASCII prefix — max 32 chars, same rules as before.
    // Шаг 1: ASCII-безопасный prefix — до 32 символов.
    char prefix[33] = {};
    size_t pre_out = 0;
    static const size_t k_pre_max = 32u;
    for (size_t i = 0u; src[i] && pre_out < k_pre_max; i++) {
        unsigned char c = (unsigned char)src[i];
        char mapped;
        if (c >= 'A' && c <= 'Z') {
            mapped = (char)(c + 32u); // uppercase → lowercase
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-') {
            mapped = (char)c;
        } else {
            mapped = '_'; // space, colon, slash, non-ASCII (kiriллица), etc.
        }
        if (mapped == '_' && pre_out > 0u && prefix[pre_out - 1u] == '_') {
            continue; // collapse consecutive underscores
        }
        prefix[pre_out++] = mapped;
    }
    prefix[pre_out] = '\0';
    // Trim trailing underscores
    while (pre_out > 0u && prefix[pre_out - 1u] == '_') {
        prefix[--pre_out] = '\0';
    }
    // Trim leading underscores (all-Cyrillic names produce leading '_')
    if (pre_out > 0u && prefix[0] == '_') {
        size_t skip = 0u;
        while (skip < pre_out && prefix[skip] == '_') skip++;
        if (skip > 0u) {
            memmove(prefix, prefix + skip, pre_out - skip + 1u);
            pre_out -= skip;
        }
    }
    // Empty prefix fallback
    if (pre_out == 0u) {
        memcpy(prefix, "unnamed", 7u);
        prefix[7u] = '\0';
    }

    // Step 2: FNV-1a 32-bit hash of the raw (unmodified) source string.
    // Includes all bytes — Cyrillic, spaces, punctuation — guarantees uniqueness per raw name.
    // Шаг 2: FNV-1a хэш сырого имени (все байты) — уникальность для любого входа.
    const uint32_t h = artFnv1a32(src);

    // Step 3: assemble "<prefix>__<8hex>"
    // snprintf is available in both Arduino (ESP-IDF) and desktop builds.
    snprintf(dst, dst_sz, "%s__%08x", prefix, (unsigned)h);
}
