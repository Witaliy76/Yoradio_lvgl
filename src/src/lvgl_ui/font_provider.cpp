#include "font_provider.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstddef>
#include <cstring>

#include "../core/config.h"
#include "../core/user_ttf.h"
#include "fonts/lv_fonts.h"

// PlatformIO board_build.embed_files symbols for the two production TTF assets.
// Символы PlatformIO board_build.embed_files для двух production TTF.
extern "C" {
extern const uint8_t _binary_src_src_lvgl_ui_fonts_yoradio_factory_font_ttf_start[];
extern const uint8_t _binary_src_src_lvgl_ui_fonts_yoradio_factory_font_ttf_end[];
extern const uint8_t _binary_src_src_lvgl_ui_fonts_yoradio_tabler_ttf_start[];
extern const uint8_t _binary_src_src_lvgl_ui_fonts_yoradio_tabler_ttf_end[];
}

namespace lvgl_ui {
namespace {

constexpr size_t kTextGlyphCache = 64;
constexpr size_t kIconGlyphCache = 8;

// These are current 480x480 product requests, not TinyTTF or TTF capabilities.
// Текущие запросы профиля 480x480, не ограничения TTF.
constexpr uint16_t kCurrentTextRequests[] = {12, 14, 16, 18, 20, 22, 32, 40};
constexpr uint16_t kCurrentIconRequests[] = {20, 22, 26, 28, 36, 64};
constexpr size_t kMaxLiveTextInstances = 16;
constexpr size_t kMaxLiveIconInstances = 16;

struct FontInstance {
    uint16_t px = 0;
    lv_font_t* font = nullptr;
};

FontInstance s_text_instances[kMaxLiveTextInstances];
size_t s_text_count = 0;
FontInstance s_icon_instances[kMaxLiveIconInstances];
size_t s_icon_count = 0;
FontInstance s_user_text_instances[kMaxLiveTextInstances];
size_t s_user_text_count = 0;
bool s_begin_attempted = false;
bool s_primary_available = false;
bool s_icons_available = false;
bool s_user_active = false;
UserFontRuntime s_user_runtime = UserFontRuntime::Factory;
uint8_t* s_user_src = nullptr;
size_t s_user_src_bytes = 0;
bool s_boot_file_present = false;
uint32_t s_boot_file_size = 0;
uint8_t s_boot_file_hdr[12] = {};

const lv_font_t* emergency_font() {
    return &lv_font_yora_montserrat_16_cyr;
}

const uint8_t* factory_ttf() {
    return _binary_src_src_lvgl_ui_fonts_yoradio_factory_font_ttf_start;
}

size_t factory_ttf_bytes() {
    return static_cast<size_t>(
        _binary_src_src_lvgl_ui_fonts_yoradio_factory_font_ttf_end -
        _binary_src_src_lvgl_ui_fonts_yoradio_factory_font_ttf_start);
}

const uint8_t* tabler_ttf() {
    return _binary_src_src_lvgl_ui_fonts_yoradio_tabler_ttf_start;
}

size_t tabler_ttf_bytes() {
    return static_cast<size_t>(
        _binary_src_src_lvgl_ui_fonts_yoradio_tabler_ttf_end -
        _binary_src_src_lvgl_ui_fonts_yoradio_tabler_ttf_start);
}

lv_font_t* find_instance(FontInstance* table, size_t count, uint16_t px) {
    for (size_t i = 0; i < count; ++i) {
        if (table[i].px == px) return table[i].font;
    }
    return nullptr;
}

void release_table(FontInstance* table, size_t& count) {
    for (size_t i = 0; i < count; ++i) {
        if (table[i].font) lv_tiny_ttf_destroy(table[i].font);
        table[i] = FontInstance{};
    }
    count = 0;
}

void release_user_source() {
    if (s_user_src) {
        free(s_user_src);
        s_user_src = nullptr;
    }
    s_user_src_bytes = 0;
}

void release_user_fonts() {
    release_table(s_user_text_instances, s_user_text_count);
    s_user_active = false;
}

void snapshot_boot_file(bool present, uint32_t size, const uint8_t* hdr12) {
    s_boot_file_present = present;
    s_boot_file_size = size;
    memset(s_boot_file_hdr, 0, sizeof(s_boot_file_hdr));
    if (present && hdr12) {
        memcpy(s_boot_file_hdr, hdr12, sizeof(s_boot_file_hdr));
    }
}

void read_live_file_meta(bool* present, uint32_t* size, uint8_t hdr12[12]) {
    *present = false;
    *size = 0;
    memset(hdr12, 0, 12);
    if (!fsIsReady() || !LittleFS.exists(yoradio::kUserTtfPath)) return;
    File f = LittleFS.open(yoradio::kUserTtfPath, "r");
    if (!f) return;
    *present = true;
    *size = static_cast<uint32_t>(f.size());
    (void)f.read(hdr12, 12);
    f.close();
}

lv_font_t* create_text_font(uint16_t px) {
    if (px == 0 || s_text_count >= kMaxLiveTextInstances) return nullptr;
    lv_font_t* font = lv_tiny_ttf_create_data_ex(
        factory_ttf(),
        factory_ttf_bytes(),
        px,
        LV_FONT_KERNING_NORMAL,
        kTextGlyphCache);
    if (!font) return nullptr;
    font->fallback = emergency_font();
    s_text_instances[s_text_count++] = FontInstance{px, font};
    return font;
}

lv_font_t* create_user_text_font(uint16_t px) {
    if (px == 0 || s_user_src == nullptr || s_user_src_bytes == 0) return nullptr;
    if (s_user_text_count >= kMaxLiveTextInstances) return nullptr;
    lv_font_t* factory = find_instance(s_text_instances, s_text_count, px);
    if (!factory) return nullptr;
    lv_font_t* font = lv_tiny_ttf_create_data_ex(
        s_user_src,
        s_user_src_bytes,
        px,
        LV_FONT_KERNING_NONE,
        kTextGlyphCache);
    if (!font) return nullptr;
    font->fallback = factory;
    s_user_text_instances[s_user_text_count++] = FontInstance{px, font};
    return font;
}

lv_font_t* create_icon_font(uint16_t px) {
    if (px == 0 || s_icon_count >= kMaxLiveIconInstances) return nullptr;
    lv_font_t* font = lv_tiny_ttf_create_data_ex(
        tabler_ttf(),
        tabler_ttf_bytes(),
        px,
        LV_FONT_KERNING_NONE,
        kIconGlyphCache);
    if (!font) return nullptr;
    s_icon_instances[s_icon_count++] = FontInstance{px, font};
    return font;
}

bool init_icon_fonts() {
    for (const uint16_t px : kCurrentIconRequests) {
        if (!create_icon_font(px)) {
            release_table(s_icon_instances, s_icon_count);
            Serial.printf("[FontProvider] icon init failed at %u px; text primary remains\n",
                          static_cast<unsigned>(px));
            return false;
        }
    }
    return true;
}

void reject_user(yoradio::UserTtfReject why, bool file_present, uint32_t file_size, const uint8_t* hdr12) {
    release_user_fonts();
    release_user_source();
    s_user_runtime = UserFontRuntime::Rejected;
    snapshot_boot_file(file_present, file_size, hdr12);
    Serial.printf("[UserFont] rejected (%s); factory text active\n",
                  yoradio::user_ttf_reject_cstr(why));
}

void init_user_text_fonts() {
    s_user_runtime = UserFontRuntime::Factory;
    snapshot_boot_file(false, 0, nullptr);
    if (!fsIsReady() || !LittleFS.exists(yoradio::kUserTtfPath)) {
        Serial.println("[UserFont] factory active (no /fonts/user.ttf)");
        return;
    }

    File f = LittleFS.open(yoradio::kUserTtfPath, "r");
    if (!f) {
        uint8_t z[12] = {};
        reject_user(yoradio::UserTtfReject::Io, true, 0, z);
        return;
    }
    const size_t file_sz = static_cast<size_t>(f.size());
    uint8_t hdr[12] = {};
    (void)f.read(hdr, sizeof(hdr));
    if (file_sz == 0) {
        f.close();
        reject_user(yoradio::UserTtfReject::Empty, true, 0, hdr);
        return;
    }
    if (file_sz > yoradio::kUserTtfMaxBytes) {
        f.close();
        reject_user(yoradio::UserTtfReject::TooLarge, true, static_cast<uint32_t>(file_sz), hdr);
        return;
    }

    s_user_src = static_cast<uint8_t*>(ps_malloc(file_sz));
    if (!s_user_src) {
        f.close();
        reject_user(yoradio::UserTtfReject::Io, true, static_cast<uint32_t>(file_sz), hdr);
        Serial.println("[UserFont] PSRAM alloc failed");
        return;
    }
    s_user_src_bytes = file_sz;
    if (!f.seek(0, SeekSet)) {
        f.close();
        reject_user(yoradio::UserTtfReject::Io, true, static_cast<uint32_t>(file_sz), hdr);
        return;
    }
    const int nread = f.read(s_user_src, file_sz);
    f.close();
    if (nread < 0 || static_cast<size_t>(nread) != file_sz) {
        reject_user(yoradio::UserTtfReject::Io, true, static_cast<uint32_t>(file_sz), hdr);
        return;
    }

    const yoradio::UserTtfReject v = yoradio::user_ttf_validate(s_user_src, s_user_src_bytes);
    if (v != yoradio::UserTtfReject::Ok) {
        reject_user(v, true, static_cast<uint32_t>(file_sz), hdr);
        return;
    }

    for (const uint16_t px : kCurrentTextRequests) {
        if (!create_user_text_font(px)) {
            reject_user(yoradio::UserTtfReject::Io, true, static_cast<uint32_t>(file_sz), hdr);
            Serial.printf("[UserFont] TinyTTF create failed at %u px\n", static_cast<unsigned>(px));
            return;
        }
    }

    s_user_active = true;
    s_user_runtime = UserFontRuntime::UserActive;
    snapshot_boot_file(true, static_cast<uint32_t>(file_sz), hdr);
    Serial.printf("[UserFont] active bytes=%u\n",
                  static_cast<unsigned>(s_user_src_bytes));
}

} // namespace

bool FontProvider::begin() {
    if (s_begin_attempted) return s_primary_available;
    s_begin_attempted = true;

    for (const uint16_t px : kCurrentTextRequests) {
        if (!create_text_font(px)) {
            release_table(s_text_instances, s_text_count);
            release_table(s_icon_instances, s_icon_count);
            release_user_fonts();
            release_user_source();
            s_user_runtime = UserFontRuntime::Emergency;
            Serial.printf("[FontProvider] primary init failed at %u px; emergency 16 px active\n",
                          static_cast<unsigned>(px));
            return false;
        }
    }

    s_primary_available = true;
    s_icons_available = init_icon_fonts();
    init_user_text_fonts();
    Serial.printf(
        "[FontProvider] TinyTTF factory ready text_bytes=%u cache=P%u "
        "tabler_bytes=%u icon_cache=P%u icons=%s pool=%uKiB/PSRAM "
        "text=12,14,16,18,20,22,32,40 icon=20,22,26,28,36,64\n",
        static_cast<unsigned>(factory_ttf_bytes()),
        static_cast<unsigned>(kTextGlyphCache),
        static_cast<unsigned>(tabler_ttf_bytes()),
        static_cast<unsigned>(kIconGlyphCache),
        s_icons_available ? "ready" : "unavailable",
        static_cast<unsigned>(YORADIO_LVGL_POOL_SIZE_KIB));
    return true;
}

const lv_font_t* FontProvider::text(uint16_t px) {
    if (!s_primary_available || px == 0) return emergency_font();
    if (s_user_active) {
        if (lv_font_t* user = find_instance(s_user_text_instances, s_user_text_count, px)) return user;
        if (lv_font_t* user = create_user_text_font(px)) return user;
    }
    if (lv_font_t* font = find_instance(s_text_instances, s_text_count, px)) return font;
    if (lv_font_t* font = create_text_font(px)) return font;
    return emergency_font();
}

const lv_font_t* FontProvider::icon(uint16_t px) {
    // Icons are not a recovery dependency: never return a dangling TinyTTF pointer.
    // Иконки не нужны для recovery: не возвращаем dangling TinyTTF-указатель.
    if (!s_primary_available || !s_icons_available || px == 0) return emergency_font();
    if (lv_font_t* font = find_instance(s_icon_instances, s_icon_count, px)) return font;
    if (lv_font_t* font = create_icon_font(px)) return font;
    return emergency_font();
}

bool FontProvider::primaryAvailable() {
    return s_primary_available;
}

const char* FontProvider::userFontRuntimeCstr(UserFontRuntime runtime) {
    switch (runtime) {
        case UserFontRuntime::Emergency: return "emergency";
        case UserFontRuntime::UserActive: return "user";
        case UserFontRuntime::Rejected: return "rejected";
        case UserFontRuntime::Factory:
        default: return "factory";
    }
}

UserFontWebStatus FontProvider::userFontWebStatus() {
    UserFontWebStatus st;
    st.provider_ready = s_begin_attempted;
    st.runtime = s_begin_attempted ? s_user_runtime : UserFontRuntime::Factory;
    uint8_t hdr[12] = {};
    bool present = false;
    uint32_t size = 0;
    read_live_file_meta(&present, &size, hdr);
    st.file_present = present;
    st.file_size = size;
    if (!s_begin_attempted) {
        st.reboot_required = false;
        return st;
    }
    const bool hdr_diff = present && s_boot_file_present &&
                          (memcmp(hdr, s_boot_file_hdr, sizeof(hdr)) != 0);
    st.reboot_required = (present != s_boot_file_present) ||
                         (present && size != s_boot_file_size) ||
                         hdr_diff;
    return st;
}

} // namespace lvgl_ui
