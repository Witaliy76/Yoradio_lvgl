// Author: Witaliy76 - https://github.com/Witaliy76
#include "lv_img_disk_header.h"

namespace lvgl_ui {

void imgDiskHeaderParse(const uint8_t raw[4], ImgDiskHeader& out) {
    const uint32_t packed = static_cast<uint32_t>(raw[0]) |
                             (static_cast<uint32_t>(raw[1]) << 8) |
                             (static_cast<uint32_t>(raw[2]) << 16) |
                             (static_cast<uint32_t>(raw[3]) << 24);
    out.cf = static_cast<uint8_t>(packed & 0x1Fu);
    out.w  = static_cast<uint16_t>((packed >> 10) & 0x7FFu);
    out.h  = static_cast<uint16_t>((packed >> 21) & 0x7FFu);
}

bool imgDiskHeaderToLvHeader(const ImgDiskHeader& disk, lv_image_header_t& out) {
    lv_color_format_t cf;
    switch (disk.cf) {
        case 4:  // LV_IMG_CF_TRUE_COLOR (v8)
            cf = LV_COLOR_FORMAT_RGB565;
            break;
        case 5:  // LV_IMG_CF_TRUE_COLOR_ALPHA (v8)
            cf = LV_COLOR_FORMAT_RGB565A8;
            break;
        default:
            return false;
    }

    out.magic      = LV_IMAGE_HEADER_MAGIC;
    out.cf         = static_cast<uint32_t>(cf);
    out.flags      = 0;
    out.w          = disk.w;
    out.h          = disk.h;
    out.stride     = static_cast<uint32_t>(disk.w) * 2u;
    out.reserved_2 = 0;
    return true;
}

}  // namespace lvgl_ui
