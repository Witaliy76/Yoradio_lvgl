#include "font_provider.h"

#include <Arduino.h>
#include <cstddef>

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
bool s_begin_attempted = false;
bool s_primary_available = false;
bool s_icons_available = false;

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

lv_font_t* create_text_font(uint16_t px) {
    if (px == 0 || s_text_count >= kMaxLiveTextInstances) return nullptr;
    lv_font_t* font = lv_tiny_ttf_create_data_ex(
        factory_ttf(),
        factory_ttf_bytes(),
        px,
        LV_FONT_KERNING_NORMAL,
        kTextGlyphCache);
    if (!font) return nullptr;
    s_text_instances[s_text_count++] = FontInstance{px, font};
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

} // namespace

bool FontProvider::begin() {
    if (s_begin_attempted) return s_primary_available;
    s_begin_attempted = true;

    for (const uint16_t px : kCurrentTextRequests) {
        if (!create_text_font(px)) {
            release_table(s_text_instances, s_text_count);
            release_table(s_icon_instances, s_icon_count);
            Serial.printf("[FontProvider] primary init failed at %u px; emergency 16 px active\n",
                          static_cast<unsigned>(px));
            return false;
        }
    }

    s_primary_available = true;
    s_icons_available = init_icon_fonts();
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

} // namespace lvgl_ui
