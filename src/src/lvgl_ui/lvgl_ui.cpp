#include "lvgl_ui.h"
#include "lv_ui_events.h"
#include "profiles/lv_profile_select.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
#include "lvgl.h"
#include "esp_timer.h"
#include "Arduino.h"
#include "../displays/tools/GFX_Canvas_screen.h"

// External canvas instance from display subsystem / Внешний экземпляр canvas из подсистемы дисплея
extern Arduino_Canvas* gfx;

// Lightweight LVGL tick callback: only lv_tick_inc()
// Лёгкий callback тиков LVGL: только lv_tick_inc()
static void lvgl_tick_cb(void *arg) {
    (void)arg;
    lv_tick_inc(1);
}

static esp_timer_handle_t s_lv_tick_timer = nullptr;

// LVGL display driver state / Состояние драйвера дисплея LVGL
static lv_disp_draw_buf_t s_disp_draw_buf;
static lv_color_t*        s_disp_buf1 = nullptr;
static lv_disp_drv_t      s_disp_drv;
static lv_disp_t*         s_disp = nullptr;

// Flush callback: blit LVGL area into Arduino_Canvas and mark frame dirty.
// Flush callback: копирует область LVGL в Arduino_Canvas и помечает кадр как грязный.
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    (void)drv;

    if (!gfx || !color_p || !area) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    if (x2 < x1 || y2 < y1) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;

    // Use existing Canvas helper which clips to bounds and marks frame dirty.
    // Используем существующий helper Canvas, который клипует по границам и помечает кадр грязным.
    gfxDrawBitmap(gfx, x1, y1, reinterpret_cast<const uint16_t*>(color_p), w, h);

    lv_disp_flush_ready(drv);
}

#endif

// Stage 0: stub — confirms LVGL library is compiled into the build
// Stage 0: заглушка — подтверждает, что библиотека LVGL скомпилирована в сборку

bool lvgl_ui::isCompiled() {
    return true;
}

void lvgl_ui::initRuntime() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    static bool s_inited = false;
    if (s_inited) return;
    lv_init();
    s_inited = true;
#endif
}

void lvgl_ui::initTick() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (s_lv_tick_timer) return;

    esp_timer_create_args_t args = {};
    args.callback = lvgl_tick_cb;
    args.name = "lv_tick";

    if (esp_timer_create(&args, &s_lv_tick_timer) != ESP_OK || !s_lv_tick_timer) {
        // Timer create failed: keep LVGL tick disabled to avoid undefined behavior.
        // Ошибка создания таймера: оставляем tick LVGL выключенным, чтобы избежать некорректного поведения.
        s_lv_tick_timer = nullptr;
        return;
    }

    if (esp_timer_start_periodic(s_lv_tick_timer, 1000) != ESP_OK) {
        // Start failed: clean up timer and keep LVGL tick disabled.
        // Ошибка запуска: очищаем таймер и оставляем tick LVGL выключенным.
        esp_timer_delete(s_lv_tick_timer);
        s_lv_tick_timer = nullptr;
        return;
    }
#endif
}

void lvgl_ui::initDisplayDriver(uint16_t hor_res, uint16_t ver_res) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (s_disp) return;
    if (hor_res == 0 || ver_res == 0) return;

    // Small strip buffer in PSRAM: width x LINES
    // Небольшой полосовый буфер в PSRAM: ширина x LINES
    const uint16_t buf_lines = 40;
    uint32_t lines = buf_lines;
    if (lines > ver_res) lines = ver_res;

    uint32_t px_count = static_cast<uint32_t>(hor_res) * lines;
    s_disp_buf1 = static_cast<lv_color_t*>(ps_malloc(px_count * sizeof(lv_color_t)));
    if (!s_disp_buf1) {
        // If allocation fails, skip driver registration to keep system stable.
        // При неудаче аллокации не регистрируем драйвер, чтобы не рисковать стабильностью.
        Serial.println("[LVGL] draw buffer PSRAM alloc failed, LVGL display disabled");
        return;
    }

    lv_disp_draw_buf_init(&s_disp_draw_buf, s_disp_buf1, nullptr, px_count);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = hor_res;
    s_disp_drv.ver_res = ver_res;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_disp_draw_buf;

    s_disp = lv_disp_drv_register(&s_disp_drv);
    if (!s_disp) {
        Serial.println("[LVGL] lv_disp_drv_register failed, LVGL display disabled");
        return;
    }
#endif
}

void lvgl_ui::taskHandler() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    lv_timer_handler();
#endif
}

void lvgl_ui::createTestOverlay() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    static bool s_created = false;
    if (s_created) return;

    lv_obj_t* scr = lv_scr_act();
    if (!scr) return;

    lv_obj_t* label = lv_label_create(scr);
    if (!label) return;

    lv_label_set_text(label, "LVGL OK");
    lv_obj_set_pos(label, 8, 8);

    s_created = true;
#endif
}

// Stage 3.1: stub — forward display events to LVGL layer; no behavior yet.
void lvgl_ui::onDisplayEvent(const DisplayEvent& evt) {
    (void)evt;
}

// Stage 3.2: backend selection stub — always LegacyCanvas for now.
lvgl_ui::UiBackend lvgl_ui::getPreferredBackend(displayMode_e mode) {
    (void)mode;
    return UiBackend::LegacyCanvas;
}

// Stage 3.2: mode-change hook stub — no behavior yet.
void lvgl_ui::onModeChanged(displayMode_e mode, UiBackend backend) {
    (void)mode;
    (void)backend;
}
