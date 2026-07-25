// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_PROFILE_TYPES_H
#define LV_PROFILE_TYPES_H

#include <cstdint>

// Compile-time LVGL display profile: one struct per board (DSP_MODEL).
// Профиль дисплея LVGL на этапе компиляции: одна структура на плату (DSP_MODEL).

struct LvglDisplayProfile {
    // --- Logical panel size (pixels) / Логический размер панели (пиксели) ---
    // a) Native UI resolution for LVGL layout. b) Not read at runtime yet for driver init
    //    (display still passes dsp.width()/height()). c) Reserved so screens use LV_ACTIVE_PROFILE
    //    instead of scattered literals. d) Foundation + future-facing.
    uint16_t width;
    uint16_t height;

    // --- Display driver hints / Подсказки драйверу дисплея ---
    // a) LVGL rotation enum value (0 = default). b) Not used yet. c) Stage 4.5: slot for lv_disp_set_rotation.
    // d) Future-facing (driver init may move here later).
    uint8_t default_rotation;
    // a) Swap RGB565 bytes if panel bus needs it. b) Not used yet. c) Reserved for lv_disp_drv / color format.
    // d) Future-facing.
    bool color_swap;

    // --- Touch → LVGL input (Stage 5.2+) / Тач → ввод LVGL (этап 5.2+) ---
    // a) Map raw touch to LVGL coords (swap/invert). b) Not used; legacy touchscreen.cpp unchanged per plan.
    // c) Stage 4.5: profile shape from approved plan; values placeholder false until indev wiring.
    // d) Foundation reserve for Stage 5.2, not active behavior now.
    bool touch_swap_xy;
    bool touch_invert_x;
    bool touch_invert_y;
    // a) Swap horizontal carousel: LV_DIR_LEFT ↔ PageChain swipe direction vs default. b) Board/driver specific
    //    (e.g. GT911 vs CST826 vs AXS) may invert gesture dir vs UX. c) Stage 5.3 carousel. d) Used in lvgl_ui gesture map.
    // a) Поменять местами горизонталь карусели: LV_DIR_LEFT ↔ направление swipe. b) Зависит от платы/драйвера.
    bool touch_swap_horizontal_carousel;

    // --- LVGL draw buffer / Полосовой буфер отрисовки LVGL ---
    // a) Strip height in lines (partial buffer). b) USED: lvgl_ui::initDisplayDriver reads buf_lines.
    // c) Centralizes former magic constant (40). d) Profile foundation, active.
    uint16_t buf_lines;
    // a) If true, buffer should live in PSRAM. b) Not read; code always allocates via ps_malloc today.
    // c) Documents intent; future: SRAM fallback or policy switch. d) Future-facing / documentation.
    bool buf_in_psram;

    // --- Font slots (LVGL const lv_font_t* at runtime) / Слоты шрифтов ---
    // a) Tiered fonts for UI (plan: small/normal/large/clock/header). b) All nullptr; INFO uses lv_font_default().
    // c) Stage 4.5 per plan ("NULL until Stage 5.1"). d) Foundation reserve for Stage 5.1.
    const void* font_small;
    const void* font_normal;
    const void* font_large;
    const void* font_clock;
    const void* font_header;

    // --- Margins / Поля ---
    // a) Edge inset like legacy TFT_FRAMEWDT (~8 px). b) USED: LvglInfoPage layout (marginLeft = padding*3).
    // c) Replaces hardcoded INFO margin. d) Profile foundation, active (INFO only so far).
    uint16_t frame_padding;

    // --- Spectrum / VU bar hints (LVGL Main / visual widgets, NOT the audio FFT class) ---
    // a) Nominal bar width, gap, and vertical band height for a future LVGL spectrum/VU strip.
    // b) Not read anywhere yet. c) Plan explicitly lists spectrum_* in profile; values derived from legacy
    //    VUBandsConfig (display*conf.h) as board-specific hints — avoids pulling Canvas config into lvgl_ui.
    // d) Future-facing for Main screen / Visual page; not the C++ SpectrumAnalyzer module — only layout hints.
    uint16_t spectrum_bar_width;
    uint16_t spectrum_bar_gap;
    uint16_t spectrum_height;
};

#endif
