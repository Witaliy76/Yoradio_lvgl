/*
 * LvglInfoPage — LVGL «Info» page (device / network / memory). Not stream metadata.
 * LvglInfoPage — страница LVGL «Info» (устройство / сеть / память). Не метаданные потока.
 *
 * Architecture hooks / связи с архитектурой:
 * - Implements ILvglScreen (see lv_screen.h): create → enter → update → exit → destroy.
 *   Реализует ILvglScreen: create → enter → update → exit → destroy.
 * - Registered in PageChain slot INFO_INDEX (0); only page wired in lvgl_ui.cpp until Stage 5.5+.
 *   Зарегистрирована в PageChain как INFO_INDEX (0); пока единственная page в lvgl_ui.cpp до этапов 5.5+.
 * - All lv_* calls run on DspTask (Core 0) via Display::loop → lvgl_ui::taskHandler — do not call from other tasks.
 *   Все lv_* только из DspTask (Core 0) через Display::loop → lvgl_ui::taskHandler — не вызывать из других задач.
 * - Layout uses LV_ACTIVE_PROFILE (width-independent constants: frame_padding, font_* pointers).
 *   Вёрстка через LV_ACTIVE_PROFILE (отступы, указатели font_*), без #ifdef по плате.
 * - update() is invoked from display when mode==INFO && backend==Lvgl (~1 Hz); keep work light.
 *   update() дергает display при mode==INFO и Lvgl backend (~1 Гц); держите работу лёгкой.
 */

#include "scr_info.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "Arduino.h"
#include "WiFi.h"
#include "Esp.h"
#include "../profiles/lv_profile_select.h"
#include "lvgl_ui.h"
#include "../../core/network.h"
#include "../../core/options.h"

namespace lvgl_ui {

// Safe label text refresh / безопасное обновление текста label (может быть nullptr до create).
#define INFO_REFRESH_SET(lbl, str) do { if ((lbl)) lv_label_set_text((lbl), (str)); } while (0)

// Profile stores font pointers as const void*; cast at use site / В профиле шрифты как const void*, применяем cast здесь.
static void info_set_font(lv_obj_t* obj, const void* font_slot) {
    if (!obj || !font_slot) return;
    lv_obj_set_style_text_font(obj, static_cast<const lv_font_t*>(font_slot), LV_PART_MAIN);
}

// One row: left caption + right value column (fixed x); used for dense INFO layout.
// Строка: подпись слева + значение справа (фикс. X); плотная вёрстка INFO.
static lv_obj_t* addInfoRow(lv_obj_t* parent, int32_t y, const char* name, const char* value, lv_obj_t** outVal) {
    const int32_t marginLeft = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding) * 3;
    const int32_t valueX = 180;

    lv_obj_t* lblName = lv_label_create(parent);
    if (lblName) {
        lv_label_set_text(lblName, name);
        lv_obj_set_pos(lblName, marginLeft, y);
        info_set_font(lblName, LV_ACTIVE_PROFILE.font_normal);
    }
    lv_obj_t* lblVal = lv_label_create(parent);
    if (lblVal) {
        lv_label_set_text(lblVal, value);
        lv_obj_set_pos(lblVal, valueX, y);
        info_set_font(lblVal, LV_ACTIVE_PROFILE.font_normal);
        if (outVal) *outVal = lblVal;
    }
    return lblVal;
}

ScreenType LvglInfoPage::screenType() const {
    return ScreenType::Page;
}

void LvglInfoPage::create() {
    // Idempotent: PageChain may call create() multiple times across navigations.
    // Идемпотентно: PageChain может вызывать create() при повторных переходах.
    if (_screen) return;

    const int32_t rowH = 26;
    const int32_t sectionGap = 20;
    const int32_t marginLeft = static_cast<int32_t>(LV_ACTIVE_PROFILE.frame_padding) * 3;

    _screen = lv_obj_create(nullptr);
    if (!_screen) return;

    int32_t y = 16;

    // Title: English short label + large tier font / Заголовок короткий EN + крупный шрифт профиля.
    lv_obj_t* title = lv_label_create(_screen);
    if (title) {
        lv_label_set_text(title, "INFO");
        lv_obj_set_pos(title, marginLeft, y);
        info_set_font(title, LV_ACTIVE_PROFILE.font_large);
    }
    y += rowH + sectionGap;

    // Section headers: font_header (e.g. 14px Cyrillic) / Заголовки секций: font_header.
    lv_obj_t* secNet = lv_label_create(_screen);
    if (secNet) {
        lv_label_set_text(secNet, "СЕТЬ"); // UTF-8 smoke + NETWORK / проверка UTF-8 + «сеть»
        lv_obj_set_pos(secNet, marginLeft, y);
        info_set_font(secNet, LV_ACTIVE_PROFILE.font_header);
    }
    y += rowH;

    addInfoRow(_screen, y, "SSID:", "--", &_val_ssid);
    y += rowH;
    addInfoRow(_screen, y, "IP:", "--", &_val_ip);
    y += rowH;
    addInfoRow(_screen, y, "RSSI:", "--", &_val_rssi);
    y += rowH;
    addInfoRow(_screen, y, "Статус:", "--", &_val_status);
    y += sectionGap;

    lv_obj_t* secSys = lv_label_create(_screen);
    if (secSys) {
        lv_label_set_text(secSys, "СИСТЕМА");
        lv_obj_set_pos(secSys, marginLeft, y);
        info_set_font(secSys, LV_ACTIVE_PROFILE.font_header);
    }
    y += rowH;

    addInfoRow(_screen, y, "Прошивка:", "--", &_val_firmware);
    y += rowH;
    addInfoRow(_screen, y, "Uptime:", "--", &_val_uptime);
    y += rowH;
    addInfoRow(_screen, y, "Free heap:", "--", &_val_heap);
    y += rowH;
    addInfoRow(_screen, y, "Free PSRAM:", "--", &_val_psram);
    y += rowH;
    addInfoRow(_screen, y, "CPU freq:", "--", &_val_cpu_freq);
    y += rowH;
    addInfoRow(_screen, y, "Chip:", "--", &_val_chip);
    y += rowH;
    addInfoRow(_screen, y, "Build:", "--", &_val_build);

    // Stage 5.3: horizontal carousel gestures on page root (not used on Boot).
    // Этап 5.3: жесты карусели на корне страницы (Boot не подключаем).
    installCarouselGesturesOnPageRoot(_screen);
}

void LvglInfoPage::enter() {
    // Placeholder for future focus/animation hooks.
    // Зарезервировано под фокус/анимации.
}

void LvglInfoPage::exit() {
    // No retained focus state yet.
    // Пока нет состояния фокуса.
}

void LvglInfoPage::update() {
    if (!_val_ssid) return;

    static char buf[64];

    // WiFi.* = Arduino stack state; independent of YoRadio network.status enum.
    // WiFi.* — состояние стека Arduino; не то же самое, что enum network.status в YoRadio.
    if (WiFi.status() == WL_CONNECTED) {
        INFO_REFRESH_SET(_val_ssid, WiFi.SSID().c_str());
        INFO_REFRESH_SET(_val_ip, WiFi.localIP().toString().c_str());
        snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
        INFO_REFRESH_SET(_val_rssi, buf);
    } else {
        INFO_REFRESH_SET(_val_ssid, "--");
        INFO_REFRESH_SET(_val_ip, "--");
        INFO_REFRESH_SET(_val_rssi, "--");
    }

    switch (network.status) {
        case CONNECTED: INFO_REFRESH_SET(_val_status, "Connected"); break;
        case SOFT_AP:   INFO_REFRESH_SET(_val_status, "Soft AP");   break;
        case FAILED:    INFO_REFRESH_SET(_val_status, "Failed");   break;
        case SDREADY:   INFO_REFRESH_SET(_val_status, "SD Ready");  break;
        default:        INFO_REFRESH_SET(_val_status, "--");       break;
    }

    INFO_REFRESH_SET(_val_firmware, YOVERSION);

    uint32_t sec = static_cast<uint32_t>(millis() / 1000u);
    uint32_t h = sec / 3600u;
    uint32_t m = (sec % 3600u) / 60u;
    uint32_t s = sec % 60u;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", static_cast<unsigned long>(h), static_cast<unsigned long>(m), static_cast<unsigned long>(s));
    INFO_REFRESH_SET(_val_uptime, buf);

    uint32_t heap = ESP.getFreeHeap();
    if (heap >= 1024u * 1024u)
        snprintf(buf, sizeof(buf), "%lu MB", static_cast<unsigned long>(heap / (1024u * 1024u)));
    else
        snprintf(buf, sizeof(buf), "%lu KB", static_cast<unsigned long>(heap / 1024u));
    INFO_REFRESH_SET(_val_heap, buf);

    size_t psram = ESP.getFreePsram();
    if (psram > 0) {
        if (psram >= 1024u * 1024u)
            snprintf(buf, sizeof(buf), "%lu MB", static_cast<unsigned long>(psram / (1024u * 1024u)));
        else
            snprintf(buf, sizeof(buf), "%lu KB", static_cast<unsigned long>(psram / 1024u));
        INFO_REFRESH_SET(_val_psram, buf);
    } else {
        INFO_REFRESH_SET(_val_psram, "--");
    }

    snprintf(buf, sizeof(buf), "%u MHz", static_cast<unsigned>(ESP.getCpuFreqMHz()));
    INFO_REFRESH_SET(_val_cpu_freq, buf);

    snprintf(buf, sizeof(buf), "%s rev.%d", ESP.getChipModel(), ESP.getChipRevision());
    INFO_REFRESH_SET(_val_chip, buf);

    INFO_REFRESH_SET(_val_build, YOVERSION);
}

void LvglInfoPage::destroy() {
    if (_screen) {
        lv_obj_del(_screen);
        _screen = nullptr;
    }
    _val_ssid = _val_ip = _val_rssi = _val_status = nullptr;
    _val_firmware = _val_uptime = _val_heap = _val_psram = nullptr;
    _val_cpu_freq = _val_chip = _val_build = nullptr;
}

lv_obj_t* LvglInfoPage::screen() {
    return _screen;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
