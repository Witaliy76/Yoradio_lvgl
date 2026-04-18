#ifndef SCR_MAIN_H
#define SCR_MAIN_H

/*
 * scr_main.h — LVGL Main (now-playing) page: ILvglScreen implementation.
 * scr_main.h — главный экран плеера: реализация ILvglScreen.
 *
 * Stage 6.1: flex column on _screen; data only from config.station / config.store (no legacy display path).
 * Этап 6.1: flex-колонка; данные только из config — без привязки к legacy Display::_title.
 *
 * Threading: all lv_* only from DspTask via PageChain + refreshMainScreen() → update().
 * Потоки: только DspTask для lv_*.
 */

#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"

namespace lvgl_ui {

// Main player screen — station / track / artist; meta row in lower stack (6.1E-c+); volume (6.1D+).
// Главный экран: текст; meta в нижнем stack; громкость.
class LvglMainScreen final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;

private:
    lv_obj_t* _screen = nullptr;

    // Stage 6.1F-b: optional file-backed background (bottom Z, floating — not in flex). / Фон Main из LittleFS.
    lv_obj_t* _bg_img = nullptr;
    // F-c: optional scrim above file bg — ThemePreset::Dark only, hidden if no bg file. / Scrim только Dark при наличии фона.
    lv_obj_t* _bg_scrim = nullptr;

    // PSRAM-preloaded bg: eliminates per-frame LittleFS reads on DspTask (WDT fix, Stage 6 diag).
    // Предзагруженный фон в PSRAM — нет LittleFS-чтений на DspTask при каждом кадре (фикс WDT).
    uint8_t*     _bg_psram_buf = nullptr;
    lv_img_dsc_t _bg_psram_dsc = {};
    uint8_t      _bg_last_slot = 255; // 255 = not loaded / не загружен

    // Apply bg from PSRAM (force=true: reload; force=false: skip if slot unchanged and buf present).
    // Применить фон из PSRAM. force=true — перезагрузить; false — пропустить если слот и буфер не изменились.
    void _applyBgTheme(bool force);

    // Top status strip: Wi‑Fi + weather glance + clock (experimental). / Верх: Wi‑Fi, погода, часы.
    wgt_status_line::Instance _status_line{};

    // Meta row (below control band): preset # + bitrate / Meta под полосой кнопок
    lv_obj_t* _lbl_station_num = nullptr;
    lv_obj_t* _lbl_bitrate = nullptr;

    // Center text stack: name → track → artist (track/artist may be HIDDEN) / Стек: имя → трек → артист
    lv_obj_t* _lbl_station_name = nullptr;
    lv_obj_t* _lbl_track = nullptr;
    lv_obj_t* _lbl_artist = nullptr;

    // Center transport: play/stop label — glyph synced from player.status() in update() / Глиф play↔stop из статуса плеера.
    lv_obj_t* _lbl_transport_play_stop = nullptr;

    // Bottom: volume row + lower divider/meter + AI line (6.1D) / Низ: громкость, нижний divider/meter, AI
    lv_obj_t* _lbl_volume = nullptr;
    lv_obj_t* _bar_volume = nullptr;
    // Invisible touch zone over volume bar — wider hit area for finger (6.1D-b).
    // Невидимая touch-зона над volume bar — шире для пальца (6.1D-b).
    lv_obj_t* _vol_touch_zone = nullptr;
    // Below volume strip: absorbs gestures so horizontal swipe does not reach carousel (6.1D-b guard).
    // Под полосой громкости: гасит жесты — свайп не уходит в карусель.
    lv_obj_t* _vol_gesture_guard = nullptr;
    // Covers bottom gap between zone_bottom and _screen bottom (e.g. if coords leave 1px); absorbs gestures.
    // Закрывает зазор между низом zone_bottom и низом экрана — жест не на голом _screen.
    lv_obj_t* _screen_bottom_carousel_guard = nullptr;
    // Temporary floating label showing volume value during drag/tap (6.1D-b).
    // Временный label с числом громкости во время drag/tap (6.1D-b).
    lv_obj_t* _lbl_vol_popup = nullptr;
    // Lower divider that becomes a buffer meter when config.store.audioinfo == true (6.1D-a).
    // Нижний разделитель, превращающийся в meter буфера при audioinfo == true.
    lv_obj_t* _bar_buffer = nullptr;
    lv_obj_t* _lbl_ai_line = nullptr;
};

} // namespace lvgl_ui

#endif
