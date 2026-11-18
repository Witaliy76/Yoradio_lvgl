// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef SCR_STATION_H
#define SCR_STATION_H

/*
 * scr_station.h — Stations page as a source browser: Sources, Radio, SD.
 * scr_station.h — страница Stations как браузер источников: Sources, Radio, SD.
 *
 * The page owns the open level, folder path requests and their lifetime.
 * The list renderer only draws the visible page. It does not read the card.
 * Страница владеет уровнем, запросами папок и их временем жизни.
 * Renderer рисует только видимую страницу и карту не читает.
 */

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#include "../adapters/browser_provider.h"
#include "../lv_screen.h"
#include "../widgets/wgt_status_line.h"
#include "station_list_renderer_select.h"

namespace lvgl_ui {

struct YoRadioPalette;

class LvglStationPage final : public ILvglScreen {
public:
    ScreenType screenType() const override;
    void create() override;
    void enter() override;
    void update() override;
    void exit() override;
    void destroy() override;
    lv_obj_t* screen() override;
    void liveReapplyTheme() override;
    void releaseAfterAutoDelete() override;

    void refreshCurrentStationVisuals();
    void armBrowser(yoradio::source::SourceId id);
    void cancelArmedBrowser();
    // While an SD folder request is pending, poll it without waiting for the 1 Hz page refresh.
    // Пока запрос папки SD в Pending, опрашивать его не дожидаясь секундного refresh страницы.
    void serviceSdPending();

private:
    void _nullHandles();

    static void create_status_chrome(LvglStationPage& self, const YoRadioPalette& pal);
    static void create_header(LvglStationPage& self, const YoRadioPalette& pal);
    static void create_hint_band(LvglStationPage& self, const YoRadioPalette& pal);

    static void _hintAreaClickedEvt(lv_event_t* e);
    void _onHintAreaClicked(lv_event_t* e);

    void _applyEntry();
    void _showSources();
    void _showRadio(bool jump_to_sounding);
    void _showSd(bool playing_folder, bool jump_to_sounding);
    void _redisplaySd(bool jump_to_sounding);
    uint32_t _nextRequest();
    void _releaseProvider();
    void _syncChrome();
    void _bindFeed(const station_list_active::BrowserRowFeed& feed, bool jump_to_sounding);
    void _pollOpenFolder(bool allow_idle_refresh);

    static uint16_t _sourcesCount(void* ctx);
    static bool _sourcesPageText(void* ctx, uint16_t first, uint16_t last,
                                 char* out, size_t cap, size_t name_limit);
    static int32_t _sourcesSounding(void* ctx);
    static void _sourcesTap(void* ctx, uint16_t row);
    static uint32_t _sourcesRevision(void* ctx);

    static uint16_t _radioCount(void* ctx);
    static bool _radioPageText(void* ctx, uint16_t first, uint16_t last,
                               char* out, size_t cap, size_t name_limit);
    static int32_t _radioSounding(void* ctx);
    static void _radioTap(void* ctx, uint16_t row);
    static bool _radioPressFocus(void* ctx, uint16_t row);
    static uint32_t _radioRevision(void* ctx);

    static uint16_t _sdCount(void* ctx);
    static bool _sdPageText(void* ctx, uint16_t first, uint16_t last,
                            char* out, size_t cap, size_t name_limit);
    static int32_t _sdSounding(void* ctx);
    static void _sdTap(void* ctx, uint16_t row);
    static bool _sdPressFocus(void* ctx, uint16_t row);
    static uint32_t _sdRevision(void* ctx);

    static void _headerClickedEvt(lv_event_t* e);

    enum class BrowserLevel : uint8_t { Sources, Radio, Sd };

    lv_obj_t* _screen = nullptr;
    wgt_status_line::Instance _status_line{};

    lv_obj_t* _header = nullptr;
    lv_obj_t* _lbl_title = nullptr;
    lv_obj_t* _lbl_count = nullptr;
    lv_obj_t* _lbl_hint_icon = nullptr;
    lv_obj_t* _lbl_hint_text = nullptr;
    lv_obj_t* _hint_area = nullptr;

    BrowserLevel _level = BrowserLevel::Sources;
    const BrowserProvider* _provider = nullptr;
    uint32_t _request_id = 0;
    uint64_t _generation = 0;
    bool _sd_jump = false;
    bool _sd_loading_row = false;
    enum class ArmedBrowser : uint8_t { None, Radio, Sd };
    ArmedBrowser _armed = ArmedBrowser::None;

    station_list_active::Instance _list{};
};

} // namespace lvgl_ui

#endif // SCR_STATION_H
