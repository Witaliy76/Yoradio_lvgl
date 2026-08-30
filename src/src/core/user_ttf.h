// User text-font product contract: path, size cap, bounded TTF validator.
// Контракт пользовательского TTF: путь, лимит, ограниченный структурный валидатор.
// Not a general OpenType parser and not a security boundary for hostile fonts.
// Не общий OpenType-парсер и не граница доверия против враждебных шрифтов.
#ifndef YORADIO_USER_TTF_H
#define YORADIO_USER_TTF_H

#include <cstddef>
#include <cstdint>

namespace yoradio {

inline constexpr size_t kUserTtfMaxBytes = 524288u; // 512 KiB / 512 КиБ
inline constexpr char kUserTtfPath[] = "/fonts/user.ttf";
inline constexpr char kUserTtfTmpPath[] = "/fonts/.upload_user.tmp";

enum class UserTtfReject : uint8_t {
    Ok = 0,
    Empty,
    TooLarge,
    Io,
    BadScaler,
    BadNumTables,
    DirectoryOob,
    TableOob,
    ForbiddenTable,
    MissingRequired,
    HeadShort,
    HheaShort,
    MaxpShort,
    CmapShort,
    BadLocaFormat,
    BadNumGlyphs,
    BadHmtx,
    LocaBad,
    CmapBad,
    GlyfSpan,
};

const char* user_ttf_reject_cstr(UserTtfReject why);

// Bounded TrueType glyf/loca check on an in-memory file. Does not call TinyTTF/stb.
// Ограниченная проверка TrueType glyf/loca в памяти. TinyTTF/stb не вызываются.
UserTtfReject user_ttf_validate(const uint8_t* data, size_t size);

} // namespace yoradio

#endif
