#include "user_ttf.h"

namespace yoradio {
namespace {

constexpr uint32_t kScalerTrueType = 0x00010000u;
constexpr uint16_t kMaxTables = 64;
constexpr uint16_t kMaxGlyphs = 8192;
constexpr uint16_t kPlatformUnicode = 0;
constexpr uint16_t kPlatformMicrosoft = 3;
constexpr uint16_t kMsUnicodeBmp = 1;
constexpr uint16_t kMsUnicodeFull = 10;
constexpr uint16_t kCompArgWords = 0x0001;
constexpr uint16_t kCompHaveScale = 0x0008;
constexpr uint16_t kCompMore = 0x0020;
constexpr uint16_t kCompHaveXyScale = 0x0040;
constexpr uint16_t kCompHave2x2 = 0x0080;
constexpr uint16_t kCompHaveInstr = 0x0100;
constexpr uint32_t kMaxCompositeParts = 128;

uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool u32_add(uint32_t a, uint32_t b, uint32_t* out) {
    if (a > 0xFFFFFFFFu - b) return false;
    *out = a + b;
    return true;
}

bool span_in(uint32_t off, uint32_t len, size_t file_sz) {
    uint32_t end = 0;
    if (!u32_add(off, len, &end)) return false;
    return static_cast<size_t>(end) <= file_sz;
}

uint32_t make_tag(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

struct TableRef {
    uint32_t off = 0;
    uint32_t len = 0;
    bool present = false;
};

bool find_table(const uint8_t* dir, uint16_t num_tables, uint32_t tag, TableRef* out) {
    for (uint16_t i = 0; i < num_tables; ++i) {
        const uint8_t* rec = dir + static_cast<size_t>(i) * 16u;
        if (be32(rec) == tag) {
            out->off = be32(rec + 8);
            out->len = be32(rec + 12);
            out->present = true;
            return true;
        }
    }
    return false;
}

UserTtfReject check_loca(const uint8_t* loca, uint32_t loca_len, uint16_t num_glyphs,
                         uint16_t loca_fmt, uint32_t glyf_len) {
    const uint32_t entries = static_cast<uint32_t>(num_glyphs) + 1u;
    const uint32_t stride = (loca_fmt == 0) ? 2u : 4u;
    uint32_t need = 0;
    if (entries > (0xFFFFFFFFu / stride)) return UserTtfReject::LocaBad;
    need = entries * stride;
    if (need != loca_len) return UserTtfReject::LocaBad;

    uint32_t prev = 0;
    for (uint32_t i = 0; i < entries; ++i) {
        uint32_t raw = (loca_fmt == 0) ? be16(loca + i * 2u) : be32(loca + i * 4u);
        uint32_t glyf_off = (loca_fmt == 0) ? (raw * 2u) : raw;
        if (loca_fmt == 0 && raw > (0xFFFFFFFFu / 2u)) return UserTtfReject::LocaBad;
        if (glyf_off > glyf_len) return UserTtfReject::LocaBad;
        if (i > 0 && glyf_off < prev) return UserTtfReject::LocaBad;
        prev = glyf_off;
    }
    return UserTtfReject::Ok;
}

UserTtfReject check_cmap(const uint8_t* cmap, uint32_t cmap_len, uint16_t num_glyphs) {
    if (cmap_len < 4) return UserTtfReject::CmapShort;
    const uint16_t rec_count = be16(cmap + 2);
    uint32_t recs_bytes = 0;
    if (!u32_add(4u, static_cast<uint32_t>(rec_count) * 8u, &recs_bytes) || recs_bytes > cmap_len) {
        return UserTtfReject::CmapBad;
    }

    uint32_t chosen_rel = 0;
    bool found = false;
    for (uint16_t i = 0; i < rec_count; ++i) {
        const uint8_t* rec = cmap + 4u + static_cast<uint32_t>(i) * 8u;
        const uint16_t plat = be16(rec);
        const uint16_t enc = be16(rec + 2);
        const uint32_t rel = be32(rec + 4);
        bool take = false;
        if (plat == kPlatformMicrosoft && (enc == kMsUnicodeBmp || enc == kMsUnicodeFull)) {
            take = true;
        } else if (plat == kPlatformUnicode) {
            take = true;
        }
        if (take) {
            chosen_rel = rel;
            found = true;
        }
    }
    if (!found) return UserTtfReject::CmapBad;
    if (chosen_rel > cmap_len || (cmap_len - chosen_rel) < 6u) return UserTtfReject::CmapBad;

    const uint8_t* sub = cmap + chosen_rel;
    const uint16_t format = be16(sub);
    if (format == 4) {
        const uint16_t length = be16(sub + 2);
        if (length < 16 || static_cast<uint32_t>(length) > (cmap_len - chosen_rel)) {
            return UserTtfReject::CmapBad;
        }
        const uint16_t seg_x2 = be16(sub + 6);
        if ((seg_x2 & 1u) != 0) return UserTtfReject::CmapBad;
        const uint16_t seg = static_cast<uint16_t>(seg_x2 / 2u);
        uint32_t min_len = 16u + static_cast<uint32_t>(seg) * 8u;
        if (static_cast<uint32_t>(length) < min_len) return UserTtfReject::CmapBad;
        const uint8_t* id_range = sub + 16u + static_cast<uint32_t>(seg) * 6u;
        for (uint16_t s = 0; s < seg; ++s) {
            const uint16_t start = be16(sub + 16u + static_cast<uint32_t>(seg) * 2u + static_cast<uint32_t>(s) * 2u);
            const uint16_t endc = be16(sub + 14u + static_cast<uint32_t>(s) * 2u);
            if (endc < start) return UserTtfReject::CmapBad;
            const uint16_t ro = be16(id_range + static_cast<uint32_t>(s) * 2u);
            if (ro != 0) {
                const uint32_t field_at = 16u + static_cast<uint32_t>(seg) * 6u + static_cast<uint32_t>(s) * 2u;
                uint32_t abs_index = 0;
                if (!u32_add(field_at, ro, &abs_index)) return UserTtfReject::CmapBad;
                if (abs_index + 2u > length) return UserTtfReject::CmapBad;
            }
        }
        (void)num_glyphs;
        return UserTtfReject::Ok;
    }
    if (format == 12) {
        if ((cmap_len - chosen_rel) < 16u) return UserTtfReject::CmapBad;
        const uint32_t length = be32(sub + 4);
        if (length < 16u || length > (cmap_len - chosen_rel)) return UserTtfReject::CmapBad;
        const uint32_t ngroups = be32(sub + 12);
        uint32_t groups_bytes = 0;
        if (ngroups > (0xFFFFFFFFu / 12u)) return UserTtfReject::CmapBad;
        groups_bytes = ngroups * 12u;
        uint32_t need = 0;
        if (!u32_add(16u, groups_bytes, &need) || need > length) return UserTtfReject::CmapBad;
        uint32_t prev_end = 0;
        for (uint32_t g = 0; g < ngroups; ++g) {
            const uint8_t* rec = sub + 16u + g * 12u;
            const uint32_t start_c = be32(rec);
            const uint32_t end_c = be32(rec + 4);
            const uint32_t start_g = be32(rec + 8);
            if (end_c < start_c) return UserTtfReject::CmapBad;
            if (g > 0 && start_c <= prev_end) return UserTtfReject::CmapBad;
            uint32_t span = end_c - start_c;
            uint32_t last_g = 0;
            if (!u32_add(start_g, span, &last_g)) return UserTtfReject::CmapBad;
            if (start_g >= num_glyphs || last_g >= num_glyphs) return UserTtfReject::CmapBad;
            prev_end = end_c;
        }
        return UserTtfReject::Ok;
    }
    return UserTtfReject::CmapBad;
}

UserTtfReject check_simple_or_composite(const uint8_t* glyf, uint32_t start, uint32_t end,
                                        uint16_t num_glyphs) {
    if (end < start) return UserTtfReject::GlyfSpan;
    const uint32_t span = end - start;
    if (span == 0) return UserTtfReject::Ok;
    if (span < 10u) return UserTtfReject::GlyfSpan;
    const uint8_t* g = glyf + start;
    const int16_t contours = static_cast<int16_t>(be16(g));
    uint32_t cursor = 10u;
    if (contours >= 0) {
        uint32_t ep = static_cast<uint32_t>(static_cast<uint16_t>(contours)) * 2u;
        uint32_t after_ep = 0;
        if (!u32_add(cursor, ep, &after_ep) || after_ep + 2u > span) return UserTtfReject::GlyfSpan;
        cursor = after_ep;
        const uint16_t instr = be16(g + cursor);
        cursor += 2u;
        uint32_t after_instr = 0;
        if (!u32_add(cursor, instr, &after_instr) || after_instr > span) return UserTtfReject::GlyfSpan;
        return UserTtfReject::Ok;
    }
    uint32_t parts = 0;
    while (parts < kMaxCompositeParts) {
        if (cursor + 4u > span) return UserTtfReject::GlyfSpan;
        const uint16_t flags = be16(g + cursor);
        const uint16_t gindex = be16(g + cursor + 2);
        cursor += 4u;
        if (gindex >= num_glyphs) return UserTtfReject::GlyfSpan;
        if (flags & kCompArgWords) {
            if (cursor + 4u > span) return UserTtfReject::GlyfSpan;
            cursor += 4u;
        } else {
            if (cursor + 2u > span) return UserTtfReject::GlyfSpan;
            cursor += 2u;
        }
        if (flags & kCompHaveScale) {
            if (cursor + 2u > span) return UserTtfReject::GlyfSpan;
            cursor += 2u;
        } else if (flags & kCompHaveXyScale) {
            if (cursor + 4u > span) return UserTtfReject::GlyfSpan;
            cursor += 4u;
        } else if (flags & kCompHave2x2) {
            if (cursor + 8u > span) return UserTtfReject::GlyfSpan;
            cursor += 8u;
        }
        ++parts;
        if ((flags & kCompMore) == 0) {
            if (flags & kCompHaveInstr) {
                if (cursor + 2u > span) return UserTtfReject::GlyfSpan;
                const uint16_t instr = be16(g + cursor);
                cursor += 2u;
                uint32_t after = 0;
                if (!u32_add(cursor, instr, &after) || after > span) return UserTtfReject::GlyfSpan;
            }
            return UserTtfReject::Ok;
        }
    }
    return UserTtfReject::GlyfSpan;
}

UserTtfReject check_glyf_spans(const uint8_t* loca, uint16_t loca_fmt, uint16_t num_glyphs,
                               const uint8_t* glyf, uint32_t glyf_len) {
    auto loca_val = [&](uint32_t i) -> uint32_t {
        if (loca_fmt == 0) {
            return static_cast<uint32_t>(be16(loca + i * 2u)) * 2u;
        }
        return be32(loca + i * 4u);
    };
    for (uint16_t i = 0; i < num_glyphs; ++i) {
        const uint32_t a = loca_val(i);
        const uint32_t b = loca_val(static_cast<uint32_t>(i) + 1u);
        if (b > glyf_len || a > b) return UserTtfReject::GlyfSpan;
        const UserTtfReject r = check_simple_or_composite(glyf, a, b, num_glyphs);
        if (r != UserTtfReject::Ok) return r;
    }
    return UserTtfReject::Ok;
}

} // namespace

const char* user_ttf_reject_cstr(UserTtfReject why) {
    switch (why) {
        case UserTtfReject::Ok: return "ok";
        case UserTtfReject::Empty: return "empty";
        case UserTtfReject::TooLarge: return "too_large";
        case UserTtfReject::Io: return "io";
        case UserTtfReject::BadScaler: return "bad_scaler";
        case UserTtfReject::BadNumTables: return "bad_num_tables";
        case UserTtfReject::DirectoryOob: return "directory_oob";
        case UserTtfReject::TableOob: return "table_oob";
        case UserTtfReject::ForbiddenTable: return "forbidden_table";
        case UserTtfReject::MissingRequired: return "missing_required";
        case UserTtfReject::HeadShort: return "head_short";
        case UserTtfReject::HheaShort: return "hhea_short";
        case UserTtfReject::MaxpShort: return "maxp_short";
        case UserTtfReject::CmapShort: return "cmap_short";
        case UserTtfReject::BadLocaFormat: return "bad_loca_format";
        case UserTtfReject::BadNumGlyphs: return "bad_num_glyphs";
        case UserTtfReject::BadHmtx: return "bad_hmtx";
        case UserTtfReject::LocaBad: return "loca_bad";
        case UserTtfReject::CmapBad: return "cmap_bad";
        case UserTtfReject::GlyfSpan: return "glyf_span";
        default: return "unknown";
    }
}

UserTtfReject user_ttf_validate(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) return UserTtfReject::Empty;
    if (size > kUserTtfMaxBytes) return UserTtfReject::TooLarge;
    if (size < 12) return UserTtfReject::DirectoryOob;
    if (be32(data) != kScalerTrueType) return UserTtfReject::BadScaler;

    const uint16_t num_tables = be16(data + 4);
    if (num_tables < 1 || num_tables > kMaxTables) return UserTtfReject::BadNumTables;
    const uint32_t dir_bytes = 12u + static_cast<uint32_t>(num_tables) * 16u;
    if (dir_bytes > size) return UserTtfReject::DirectoryOob;
    const uint8_t* dir = data + 12;

    for (uint16_t i = 0; i < num_tables; ++i) {
        const uint8_t* rec = dir + static_cast<size_t>(i) * 16u;
        const uint32_t off = be32(rec + 8);
        const uint32_t len = be32(rec + 12);
        if (!span_in(off, len, size)) return UserTtfReject::TableOob;
        const uint32_t tag = be32(rec);
        if (tag == make_tag('C', 'F', 'F', ' ') || tag == make_tag('C', 'F', 'F', '2') ||
            tag == make_tag('f', 'v', 'a', 'r') || tag == make_tag('g', 'v', 'a', 'r') ||
            tag == make_tag('a', 'v', 'a', 'r')) {
            return UserTtfReject::ForbiddenTable;
        }
    }

    TableRef cmap{}, head{}, hhea{}, hmtx{}, maxp{}, loca{}, glyf{};
    if (!find_table(dir, num_tables, make_tag('c', 'm', 'a', 'p'), &cmap) ||
        !find_table(dir, num_tables, make_tag('h', 'e', 'a', 'd'), &head) ||
        !find_table(dir, num_tables, make_tag('h', 'h', 'e', 'a'), &hhea) ||
        !find_table(dir, num_tables, make_tag('h', 'm', 't', 'x'), &hmtx) ||
        !find_table(dir, num_tables, make_tag('m', 'a', 'x', 'p'), &maxp) ||
        !find_table(dir, num_tables, make_tag('l', 'o', 'c', 'a'), &loca) ||
        !find_table(dir, num_tables, make_tag('g', 'l', 'y', 'f'), &glyf)) {
        return UserTtfReject::MissingRequired;
    }

    if (head.len < 54) return UserTtfReject::HeadShort;
    if (hhea.len < 36) return UserTtfReject::HheaShort;
    if (maxp.len < 6) return UserTtfReject::MaxpShort;
    if (cmap.len < 4) return UserTtfReject::CmapShort;

    const uint8_t* head_p = data + head.off;
    const uint8_t* hhea_p = data + hhea.off;
    const uint8_t* maxp_p = data + maxp.off;
    const uint8_t* hmtx_p = data + hmtx.off;
    const uint8_t* loca_p = data + loca.off;
    const uint8_t* glyf_p = data + glyf.off;
    const uint8_t* cmap_p = data + cmap.off;

    const uint16_t loca_fmt = be16(head_p + 50);
    if (loca_fmt > 1) return UserTtfReject::BadLocaFormat;

    const uint16_t num_glyphs = be16(maxp_p + 4);
    if (num_glyphs < 1 || num_glyphs > kMaxGlyphs) return UserTtfReject::BadNumGlyphs;

    const uint16_t n_hmtx = be16(hhea_p + 34);
    if (n_hmtx < 1 || n_hmtx > num_glyphs) return UserTtfReject::BadHmtx;
    const uint32_t lsb_extra = static_cast<uint32_t>(num_glyphs - n_hmtx) * 2u;
    const uint32_t hmtx_need = static_cast<uint32_t>(n_hmtx) * 4u + lsb_extra;
    if (hmtx.len < hmtx_need) return UserTtfReject::BadHmtx;
    (void)hmtx_p;

    const UserTtfReject loca_r = check_loca(loca_p, loca.len, num_glyphs, loca_fmt, glyf.len);
    if (loca_r != UserTtfReject::Ok) return loca_r;

    const UserTtfReject cmap_r = check_cmap(cmap_p, cmap.len, num_glyphs);
    if (cmap_r != UserTtfReject::Ok) return cmap_r;

    return check_glyf_spans(loca_p, loca_fmt, num_glyphs, glyf_p, glyf.len);
}

} // namespace yoradio
