#include "lvgl_ui.h"
#include "lv_ui_events.h"
#include "profiles/lv_profile_select.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
#include "lvgl.h"
#include "esp_timer.h"
#include "Arduino.h"
#include "WiFi.h"
#include "Esp.h"
#include "../core/network.h"
#include "../core/options.h"
#include "../displays/tools/GFX_Canvas_screen.h"

// External canvas instance from display subsystem / Внешний экземпляр canvas из подсистемы дисплея
extern Arduino_Canvas* gfx;

#define INFO_REFRESH_SET(lbl, str) do { if ((lbl)) lv_label_set_text((lbl), (str)); } while(0)

// Stage 4.2/4.4b: INFO screen (internal; loaded when mode == INFO).
static lv_obj_t* s_info_screen = nullptr;
// Stage 4.3: default screen; restored when leaving INFO.
static lv_obj_t* s_default_screen = nullptr;

// Patch 4.4b: VALUE labels for future runtime updates (placeholders only for now).
// NETWORK
static lv_obj_t* s_val_ssid    = nullptr;
static lv_obj_t* s_val_ip      = nullptr;
static lv_obj_t* s_val_rssi    = nullptr;
static lv_obj_t* s_val_status  = nullptr;
// SYSTEM
static lv_obj_t* s_val_firmware = nullptr;
static lv_obj_t* s_val_uptime   = nullptr;
static lv_obj_t* s_val_heap     = nullptr;
static lv_obj_t* s_val_psram   = nullptr;
static lv_obj_t* s_val_cpu_freq = nullptr;
static lv_obj_t* s_val_chip     = nullptr;
static lv_obj_t* s_val_build   = nullptr;

// 480x480 layout constants (readable, conservative spacing).
static const int32_t INFO_MARGIN_LEFT = 24;
static const int32_t INFO_VALUE_X     = 180;
static const int32_t INFO_ROW_H       = 26;
static const int32_t INFO_SECTION_GAP = 20;

static lv_obj_t* addRow(lv_obj_t* parent, int32_t y, const char* name, const char* value, lv_obj_t** outVal) {
    lv_obj_t* lblName = lv_label_create(parent);
    if (lblName) {
        lv_label_set_text(lblName, name);
        lv_obj_set_pos(lblName, INFO_MARGIN_LEFT, y);
    }
    lv_obj_t* lblVal = lv_label_create(parent);
    if (lblVal) {
        lv_label_set_text(lblVal, value);
        lv_obj_set_pos(lblVal, INFO_VALUE_X, y);
        if (outVal) *outVal = lblVal;
    }
    return lblVal;
}

static void ensureInfoScreen() {
    if (s_info_screen) return;

    s_info_screen = lv_obj_create(NULL);
    if (!s_info_screen) return;

    int32_t y = 16;

    // Title
    lv_obj_t* title = lv_label_create(s_info_screen);
    if (title) {
        lv_label_set_text(title, "INFO");
        lv_obj_set_pos(title, INFO_MARGIN_LEFT, y);
    }
    y += INFO_ROW_H + INFO_SECTION_GAP;

    // --- NETWORK ---
    lv_obj_t* secNet = lv_label_create(s_info_screen);
    if (secNet) {
        lv_label_set_text(secNet, "NETWORK");
        lv_obj_set_pos(secNet, INFO_MARGIN_LEFT, y);
    }
    y += INFO_ROW_H;

    addRow(s_info_screen, y, "SSID:",    "--", &s_val_ssid);   y += INFO_ROW_H;
    addRow(s_info_screen, y, "IP:",      "--", &s_val_ip);     y += INFO_ROW_H;
    addRow(s_info_screen, y, "RSSI:",    "--", &s_val_rssi);   y += INFO_ROW_H;
    addRow(s_info_screen, y, "Status:",  "--", &s_val_status); y += INFO_ROW_H;

    y += INFO_SECTION_GAP;

    // --- SYSTEM ---
    lv_obj_t* secSys = lv_label_create(s_info_screen);
    if (secSys) {
        lv_label_set_text(secSys, "SYSTEM");
        lv_obj_set_pos(secSys, INFO_MARGIN_LEFT, y);
    }
    y += INFO_ROW_H;

    addRow(s_info_screen, y, "Firmware:",  "--", &s_val_firmware); y += INFO_ROW_H;
    addRow(s_info_screen, y, "Uptime:",    "--", &s_val_uptime);   y += INFO_ROW_H;
    addRow(s_info_screen, y, "Free heap:", "--", &s_val_heap);    y += INFO_ROW_H;
    addRow(s_info_screen, y, "Free PSRAM:", "--", &s_val_psram);  y += INFO_ROW_H;
    addRow(s_info_screen, y, "CPU freq:",  "--", &s_val_cpu_freq); y += INFO_ROW_H;
    addRow(s_info_screen, y, "Chip:",     "--", &s_val_chip);     y += INFO_ROW_H;
    addRow(s_info_screen, y, "Build:",     "--", &s_val_build);    y += INFO_ROW_H;
}

// Populate INFO value labels from runtime data. Throttled; call only when INFO is active.
static void refreshInfoScreenImpl() {
    if (!s_val_ssid) return;  // Screen not built yet

    static char buf[64];

    // NETWORK
    if (WiFi.status() == WL_CONNECTED) {
        INFO_REFRESH_SET(s_val_ssid, WiFi.SSID().c_str());
        INFO_REFRESH_SET(s_val_ip,   WiFi.localIP().toString().c_str());
        snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
        INFO_REFRESH_SET(s_val_rssi, buf);
    } else {
        INFO_REFRESH_SET(s_val_ssid, "--");
        INFO_REFRESH_SET(s_val_ip,   "--");
        INFO_REFRESH_SET(s_val_rssi, "--");
    }

    switch (network.status) {
        case CONNECTED: INFO_REFRESH_SET(s_val_status, "Connected"); break;
        case SOFT_AP:   INFO_REFRESH_SET(s_val_status, "Soft AP");   break;
        case FAILED:    INFO_REFRESH_SET(s_val_status, "Failed");   break;
        case SDREADY:   INFO_REFRESH_SET(s_val_status, "SD Ready");  break;
        default:        INFO_REFRESH_SET(s_val_status, "--");       break;
    }

    // SYSTEM
    INFO_REFRESH_SET(s_val_firmware, YOVERSION);

    uint32_t sec = (uint32_t)(millis() / 1000u);
    uint32_t h = sec / 3600u;
    uint32_t m = (sec % 3600u) / 60u;
    uint32_t s = sec % 60u;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    INFO_REFRESH_SET(s_val_uptime, buf);

    uint32_t heap = ESP.getFreeHeap();
    if (heap >= 1024u * 1024u)
        snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(heap / (1024u * 1024u)));
    else
        snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(heap / 1024u));
    INFO_REFRESH_SET(s_val_heap, buf);

    size_t psram = ESP.getFreePsram();
    if (psram > 0) {
        if (psram >= 1024u * 1024u)
            snprintf(buf, sizeof(buf), "%lu MB", (unsigned long)(psram / (1024u * 1024u)));
        else
            snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(psram / 1024u));
        INFO_REFRESH_SET(s_val_psram, buf);
    } else {
        INFO_REFRESH_SET(s_val_psram, "--");
    }

    snprintf(buf, sizeof(buf), "%u MHz", (unsigned)ESP.getCpuFreqMHz());
    INFO_REFRESH_SET(s_val_cpu_freq, buf);

    snprintf(buf, sizeof(buf), "%s rev.%d", ESP.getChipModel(), ESP.getChipRevision());
    INFO_REFRESH_SET(s_val_chip, buf);

    INFO_REFRESH_SET(s_val_build, YOVERSION);
}

void lvgl_ui::refreshInfoScreen() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    refreshInfoScreenImpl();
#endif
}

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

// Сохраняем дефолтный (пустой) экран LVGL для переключения при выходе из INFO.
// Пустой — без виджетов, чтобы не ломать boot/legacy и не рисовать лишнее поверх плейера.
// Позже сюда можно перенести LVGL boot/overlay при миграции.
void lvgl_ui::createTestOverlay() {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    static bool s_created = false;
    if (s_created) return;

    lv_obj_t* scr = lv_scr_act();
    if (!scr) return;

    if (!s_default_screen) s_default_screen = scr;

    s_created = true;
#endif
}

// Stage 3.1: stub — forward display events to LVGL layer; no behavior yet.
void lvgl_ui::onDisplayEvent(const DisplayEvent& evt) {
    (void)evt;
}

// Stage 4.3: INFO is the first mode owned by LVGL; all others remain LegacyCanvas.
lvgl_ui::UiBackend lvgl_ui::getPreferredBackend(displayMode_e mode) {
    if (mode == INFO) return UiBackend::Lvgl;
    return UiBackend::LegacyCanvas;
}

// Stage 4.3: mode-change hook — activate INFO LVGL screen when INFO is Lvgl-owned; restore default when LegacyCanvas.
void lvgl_ui::onModeChanged(displayMode_e mode, UiBackend backend) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (mode == INFO && backend == UiBackend::Lvgl) {
        ensureInfoScreen();
        if (s_info_screen) lv_scr_load(s_info_screen);
    } else if (backend == UiBackend::LegacyCanvas) {
        if (s_default_screen) lv_scr_load(s_default_screen);
    }
#else
    (void)mode;
    (void)backend;
#endif
}
