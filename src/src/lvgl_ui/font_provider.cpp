#include "font_provider.h"

#include <Arduino.h>
#include <cstddef>

#include "fonts/lv_fonts.h"

#ifndef YORADIO_TEXT_GLYPH_CACHE
#define YORADIO_TEXT_GLYPH_CACHE 64
#endif

extern "C" {
extern const uint8_t yoradio_text_ttf_data[];
extern const uint8_t yoradio_text_ttf_data_end[];
}

namespace lvgl_ui {
namespace {

// These are current 480x480 product requests, not TinyTTF capabilities.
constexpr uint16_t kCurrentProfileRequests[] = {12, 14, 16, 18, 20, 22, 32, 40};
constexpr size_t kMaxLiveTextInstances = 16;

struct FontInstance {
    uint16_t px = 0;
    lv_font_t* font = nullptr;
};

FontInstance s_instances[kMaxLiveTextInstances];
size_t s_instance_count = 0;
bool s_begin_attempted = false;
bool s_primary_available = false;

const lv_font_t* emergency_font() {
    return &lv_font_yora_montserrat_16_cyr;
}

size_t embedded_bytes() {
    return static_cast<size_t>(yoradio_text_ttf_data_end - yoradio_text_ttf_data);
}

lv_font_t* find_font(uint16_t px) {
    for (size_t i = 0; i < s_instance_count; ++i) {
        if (s_instances[i].px == px) return s_instances[i].font;
    }
    return nullptr;
}

lv_font_t* create_font(uint16_t px) {
    if (px == 0 || s_instance_count >= kMaxLiveTextInstances) return nullptr;
    lv_font_t* font = lv_tiny_ttf_create_data_ex(
        yoradio_text_ttf_data,
        embedded_bytes(),
        px,
        LV_FONT_KERNING_NORMAL,
        static_cast<size_t>(YORADIO_TEXT_GLYPH_CACHE));
    if (!font) return nullptr;
    s_instances[s_instance_count++] = FontInstance{px, font};
    return font;
}

void release_all() {
    for (size_t i = 0; i < s_instance_count; ++i) {
        if (s_instances[i].font) lv_tiny_ttf_destroy(s_instances[i].font);
        s_instances[i] = FontInstance{};
    }
    s_instance_count = 0;
}

} // namespace

bool FontProvider::begin() {
    if (s_begin_attempted) return s_primary_available;
    s_begin_attempted = true;

    for (const uint16_t px : kCurrentProfileRequests) {
        if (!create_font(px)) {
            release_all();
            Serial.printf("[FontProvider] primary init failed at %u px; emergency 16 px active\n",
                          static_cast<unsigned>(px));
            return false;
        }
    }

    s_primary_available = true;
    Serial.printf(
        "[FontProvider] TinyTTF factory ready bytes=%u cache=P%u pool=%uKiB/PSRAM "
        "requests=12,14,16,18,20,22,32,40\n",
        static_cast<unsigned>(embedded_bytes()),
        static_cast<unsigned>(YORADIO_TEXT_GLYPH_CACHE),
        static_cast<unsigned>(YORADIO_LVGL_POOL_SIZE_KIB));
    return true;
}

const lv_font_t* FontProvider::text(uint16_t px) {
    if (!s_primary_available || px == 0) return emergency_font();
    if (lv_font_t* font = find_font(px)) return font;
    if (lv_font_t* font = create_font(px)) return font;
    return emergency_font();
}

bool FontProvider::primaryAvailable() {
    return s_primary_available;
}

} // namespace lvgl_ui
