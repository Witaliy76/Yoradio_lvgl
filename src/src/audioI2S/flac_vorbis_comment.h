#pragma once
// FLAC Vorbis comment field split.
// Разбор одного поля Vorbis-комментария FLAC.
//
// The value is copied into the caller buffer. No field owns another field's memory,
// and a missing ALBUM does not affect ARTIST or TITLE.
// Значение копируется в буфер вызывающего. Поля не владеют памятью друг друга,
// и отсутствие ALBUM не влияет на ARTIST и TITLE.

#include <cstddef>
#include <cstring>

enum FlacVorbisKey : int {
    kFlacVorbisOther = 0,
    kFlacVorbisAlbum,
    kFlacVorbisArtist,
    kFlacVorbisTitle
};

// Same bound as Config title/station buffers (BUFLEN).
// Тот же предел, что у буферов title/station в Config (BUFLEN).
constexpr size_t kFlacVorbisValueCap = 250;

struct FlacVorbisField {
    FlacVorbisKey key;
    bool fits;
};

inline bool flacVorbisKeyEqual(const char* key, size_t key_len, const char* name) {
    if (!key || !name) return false;
    size_t i = 0;
    for (; i < key_len && name[i] != '\0'; ++i) {
        char a = key[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return i == key_len && name[i] == '\0';
}

inline FlacVorbisKey flacVorbisKeyOf(const char* comment, const char** value_out) {
    if (value_out) *value_out = "";
    if (!comment) return kFlacVorbisOther;
    const char* eq = std::strchr(comment, '=');
    if (!eq) return kFlacVorbisOther;
    const size_t key_len = static_cast<size_t>(eq - comment);
    const char* value = eq + 1;
    FlacVorbisKey key = kFlacVorbisOther;
    if (flacVorbisKeyEqual(comment, key_len, "album")) key = kFlacVorbisAlbum;
    else if (flacVorbisKeyEqual(comment, key_len, "artist")) key = kFlacVorbisArtist;
    else if (flacVorbisKeyEqual(comment, key_len, "title")) key = kFlacVorbisTitle;
    if (value_out) *value_out = value;
    return key;
}

// Copies the value of ALBUM, ARTIST or TITLE. Other comments leave dst empty.
// Копирует значение ALBUM, ARTIST или TITLE. Остальные комментарии оставляют dst пустым.
inline FlacVorbisField flacVorbisCopy(const char* comment, char* dst, size_t capacity) {
    FlacVorbisField out{kFlacVorbisOther, false};
    if (dst && capacity > 0) dst[0] = '\0';
    const char* value = nullptr;
    out.key = flacVorbisKeyOf(comment, &value);
    if (out.key == kFlacVorbisOther || !dst || capacity == 0 || !value) return out;
    const size_t len = std::strlen(value);
    const size_t n = len < capacity ? len : capacity - 1;
    if (n > 0) std::memcpy(dst, value, n);
    dst[n] = '\0';
    out.fits = len < capacity;
    return out;
}
