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
#include "../widgets/wgt_presence_rail.h"

namespace lvgl_ui {

// Forward declaration sufficient for const-ref parameter in private static builder signatures.
// Full definition is in lv_theme_yoradio.h, included by scr_main.cpp.
// Предварительное объявление — достаточно для const-ref в сигнатурах builder-методов.
struct YoRadioPalette;

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

    // WebUI replaced LittleFS .bin for a slot — force PSRAM reload (call from DspTask only).
    // Веб перезаписал .bin слота — принудительно перезагрузить PSRAM (только DspTask).
    void reloadFileBackgroundFromLittlefs();

    // Station Art MVP: WebUI committed upload_art / remove_art — force art reload (DspTask only).
    // Station Art MVP: после upload_art / remove_art — принудительно перезагрузить арт (только DspTask).
    void reloadStationArtFromLittlefs();

    // Stage 6.6R-B: update all palette-bound colors on already-created Main (DspTask only).
    // Does not recreate the screen; layout/structure is preserved.
    // Glow gradient chrome is a known follow-up (static local in create()).
    // Этап 6.6R-B: обновить palette-цвета уже созданного Main без пересоздания. Только DspTask.
    void liveReapplyTheme() override;

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

    // Stage 6.6R-B1/B2: full-bleed bg/scrim — compensate _screen frame_padding + restore size after lv_img_set_src.
    // Этап 6.6R-B1/B2: фон на весь экран — offset −frame_padding, размер после смены src.
    void _syncBgImgLayout();

    // Top status strip: Wi‑Fi + weather glance + clock (experimental). / Верх: Wi‑Fi, погода, часы.
    wgt_status_line::Instance _status_line{};

    // Stage 8 E22A: Sound Presence Rail — procedural decorative life-indicator hosted in zone_visual.
    // Этап 8 E22A: Sound Presence Rail — процедурный декоративный индикатор в зоне zone_visual.
    wgt_presence_rail::Instance _presence_rail{};

    // Flex spacers around cont_mid (equal by default; with left art — asymmetric grow, see create()).
    // Спейсеры вокруг cont_mid: по умолчанию равны; с left art — асимметричный grow.
    lv_obj_t* _spacer_top    = nullptr;
    lv_obj_t* _spacer_bottom  = nullptr;

    // Meta row (below control band): composed stream-info line (6.1E stream facts).
    // Мета-строка: один label — факты потока.
    lv_obj_t* _lbl_stream_info = nullptr;

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

    // Stage 6.6R-B: control band (transport shelf) — stored for live chrome reapply.
    // Этап 6.6R-B: полка управления — для обновления chrome при смене темы.
    lv_obj_t* _control_band = nullptr;

    // Stage 6.6R-GA: rim glow strips — stored so liveReapplyTheme() can refresh gradient stops.
    // Gradient descriptors live in a file-scope static (must outlive create()); these are just the objects.
    // Этап 6.6R-GA: полоски rim glow — для обновления стопов градиента при смене темы (fix stale glow).
    lv_obj_t* _edge_glow_top = nullptr;
    lv_obj_t* _edge_glow_bot = nullptr;

    // Stage 6.6R-B1: control icon buttons — colors set only in create(); reapply on theme switch.
    // Этап 6.6R-B1: кнопки полки — цвета иконок обновляются в liveReapplyTheme().
    lv_obj_t* _ctrl_btn_list     = nullptr;
    lv_obj_t* _ctrl_btn_prev     = nullptr;
    lv_obj_t* _ctrl_btn_play     = nullptr;
    lv_obj_t* _ctrl_btn_next     = nullptr;
    lv_obj_t* _ctrl_btn_settings = nullptr;

    // Left Art slot (6.1E-visual v1): hidden when no local asset (Mode A); visible when asset present (Mode B).
    // Dynamic collapse: LVGL v8 flex skips HIDDEN children — cont_text auto-expands in Mode A.
    // art_slot скрыт по умолчанию (Mode A); показывается при наличии локального asset (Mode B).
    lv_obj_t* _art_slot  = nullptr;
    lv_obj_t* _art_img   = nullptr;
    // Flex containers whose alignment changes when art appears / disappears (CENTER ↔ START).
    // Контейнеры, выравнивание которых меняется при появлении/исчезновении арта.
    lv_obj_t* _cont_mid  = nullptr; // outer column (row host)
    lv_obj_t* _cont_text = nullptr; // text column (station name / track / artist)

    // Station Art MVP: runtime reload state.
    // _art_last_station_num: 0xFFFF = uninitialized (force reload on first update() call).
    // _art_reload_forced: set by reloadStationArtFromLittlefs() to re-check even if num unchanged.
    // _art_current_key: normalized key of the last loaded asset (empty = none loaded).
    // Station Art MVP: состояние runtime-перезагрузки.
    uint16_t _art_last_station_num = 0xFFFF; // sentinel: force reload on first call
    bool     _art_reload_forced    = false;
    char     _art_current_key[68]  = {};

    // Reload station art from LittleFS for the current station.
    // Must be called only from DspTask (same thread as all lv_* calls).
    // Key source: stationByNum(config.lastStation()) — never config.station.name.
    // Перезагрузка арта для текущей станции из LittleFS (только DspTask).
    // Источник ключа: stationByNum() — не config.station.name.
    void _reloadArtIfNeeded();

    // Layout builders — private static to keep create() a short skeleton while retaining
    // full access to private members via self. Called only from create().
    // Строители секций — private static для краткости create() + доступа к private-членам через self.
    // create_status_line returns the status divider (needed by create_bottom_zone for symmetry calc).
    // create_status_line возвращает divider статуса (нужен create_bottom_zone для симметрии).
    static lv_obj_t* create_status_line(LvglMainScreen& self, const YoRadioPalette& pal);
    static void      create_mid_block(LvglMainScreen& self, const YoRadioPalette& pal);
    static void      create_visual_rail(LvglMainScreen& self, const YoRadioPalette& pal);
    static void      create_bottom_zone(LvglMainScreen& self, const YoRadioPalette& pal, lv_obj_t* status_divider);
};

} // namespace lvgl_ui

#endif
