#ifndef WIFI_SIGNAL_MAP_H
#define WIFI_SIGNAL_MAP_H

// RSSI → Wi-Fi icon level + UTF-8 for Tabler subset font (Main status line only).
// Маппинг RSSI → уровень 0..4 + UTF-8 глифов Tabler (только Main status line).

namespace lvgl_ui {

// Thresholds (dBm): max ≥-35, min ≤-68; middle three bands evenly between -68 and -35 (step 11 dB).
// Пороги: максимум ≥-35, минимум ≤-68; три промежуточных уровня равномерно.

// Map RSSI to 0..4 (stronger signal → higher level).
inline int wifi_rssi_to_level(int rssi_dbm) {
    if (rssi_dbm <= -68) return 0;
    if (rssi_dbm <= -57) return 1;
    if (rssi_dbm <= -46) return 2;
    if (rssi_dbm < -35) return 3;
    return 4;
}

// Tabler 3.26: wifi-0, wifi-1, wifi-2, wifi (full); no separate wifi-3/4 — top two levels use full.
// UTF-8 BMP PUA: U+EBA3, U+EBA4, U+EBA5, U+EB52 (×2 for levels 3 and 4).
inline const char* wifi_status_glyph_utf8_for_level(int level_0_to_4) {
    static const char* const kGlyphs[5] = {
        reinterpret_cast<const char*>(u8"\uEBA3"),
        reinterpret_cast<const char*>(u8"\uEBA4"),
        reinterpret_cast<const char*>(u8"\uEBA5"),
        reinterpret_cast<const char*>(u8"\uEB52"),
        reinterpret_cast<const char*>(u8"\uEB52"),
    };
    if (level_0_to_4 < 0) level_0_to_4 = 0;
    if (level_0_to_4 > 4) level_0_to_4 = 4;
    return kGlyphs[level_0_to_4];
}

// Tabler: wifi-off U+ECFA
inline const char* wifi_status_glyph_utf8_disconnected() {
    return reinterpret_cast<const char*>(u8"\uECFA");
}

} // namespace lvgl_ui

#endif
