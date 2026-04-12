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

// Main player screen — station / track / artist hierarchy, meta row, volume (Stage 6.1B+).
// Главный экран: иерархия текста, meta, громкость.
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

    // Top status strip: Wi‑Fi + weather glance + clock (experimental). / Верх: Wi‑Fi, погода, часы.
    wgt_status_line::Instance _status_line{};

    // Meta row: preset index + bitrate (token meta_row_text) / # пресета и битрейт
    lv_obj_t* _lbl_station_num = nullptr;
    lv_obj_t* _lbl_bitrate = nullptr;

    // Center text stack: name → track → artist (track/artist may be HIDDEN) / Стек: имя → трек → артист
    lv_obj_t* _lbl_station_name = nullptr;
    lv_obj_t* _lbl_track = nullptr;
    lv_obj_t* _lbl_artist = nullptr;

    // Bottom: volume row + lower divider/meter + AI line (6.1D) / Низ: громкость, нижний divider/meter, AI
    lv_obj_t* _lbl_volume = nullptr;
    lv_obj_t* _bar_volume = nullptr;
    // Invisible touch zone over volume bar — wider hit area for finger (6.1D-b).
    // Невидимая touch-зона над volume bar — шире для пальца (6.1D-b).
    lv_obj_t* _vol_touch_zone = nullptr;
    // Temporary floating label showing volume value during drag/tap (6.1D-b).
    // Временный label с числом громкости во время drag/tap (6.1D-b).
    lv_obj_t* _lbl_vol_popup = nullptr;
    // Lower divider that becomes a buffer meter when config.store.audioinfo == true (6.1D-a).
    // Нижний разделитель, превращающийся в meter буфера при audioinfo == true.
    lv_obj_t* _bar_buffer = nullptr;
    lv_obj_t* _lbl_ai_line = nullptr;

    // Transparent tap target; LV_OBJ_FLAG_FLOATING so flex layout ignores it / Прозрачная зона тапа play
    lv_obj_t* _hit_play = nullptr;

    // 6.1D-b: volume touch state
    bool _vol_touch_active = false;
};

} // namespace lvgl_ui

#endif
