#ifndef LV_PAGE_CHAIN_H
#define LV_PAGE_CHAIN_H

#include <stdint.h>
#include "lv_screen.h"

namespace lvgl_ui {

// PageChain — Stage 4.6 frozen navigation (see docs/stage_4_6_spec_freeze.md).
// Six page slots: 0=Info, 1=Main, 2=Visual, 3=Station, 4=Weather, 5=Settings.
// Wi-Fi (RebootRequired) is NOT a page slot — use showRebootRequired().
// Horizontal swipe is blocked while Temporary / Boot / RebootRequired is active.
// PageChain — навигация Stage 4.6 freeze. Wi-Fi не слот карусели — showRebootRequired.

// Horizontal carousel + Boot / Temporary / Reboot-required full screens.
// Горизонтальная карусель + полноэкранные Boot / Temporary / Reboot-required.
class PageChain {
public:
    // Fixed slot order: Info, Main, Visual, Station, Weather, Settings.
    // Фиксированный порядок слотов: Info, Main, Visual, Station, Weather, Settings.
    static constexpr int PAGE_COUNT = 6;
    static constexpr int INFO_INDEX = 0;
    static constexpr int MAIN_INDEX = 1;
    static constexpr int STATION_INDEX = 3;

    void registerPage(int index, ILvglScreen* page);
    // create + enter start page; cold load without animation.
    // create + enter стартовой страницы; холодная загрузка без анимации.
    void init();

    // Ignored while Temporary / Boot / RebootRequired active. Skips null slots.
    // Игнорируются при активном Temporary / Boot / RebootRequired. Пропускают пустые слоты.
    void swipeLeft();
    void swipeRight();
    void goTo(int index);

    // Temporary = Preset only (product); timeout default 20s → dismissTemporary → Main.
    // Temporary — только Preset; таймаут по умолчанию 20 с → Main.
    void showTemporary(ILvglScreen* scr, uint32_t timeout_ms = 20000);
    // Always returns to Main (MAIN_INDEX), not previous carousel page.
    // Всегда возврат на Main (MAIN_INDEX), не на предыдущую страницу карусели.
    void dismissTemporary();

    void showBoot(ILvglScreen* scr);
    // Tear down Boot special mode only; does not load Main — onModeChanged/goTo does (Stage 5.4).
    // Снимает только Boot; Main не грузит — дальше onModeChanged/goTo (этап 5.4).
    void dismissBoot();
    // Wi-Fi setup: full-screen RebootRequired; no back-nav, exit = reboot (Stage 7+ UI).
    // Wi-Fi: полноэкранный RebootRequired; без назад, выход = перезагрузка (UI в Stage 7+).
    void showRebootRequired(ILvglScreen* scr);
    // Wi-Fi 3A+: leave RebootRequired shell, restore carousel Main (sync load; no broad refactor).
    // Wi-Fi 3A+: выход из RebootRequired, возврат карусели на Main.
    void dismissRebootRequired();

    int currentIndex() const;
    ILvglScreen* currentPage() const;
    bool isTemporaryActive() const;

    void tick();
    void onActivity(); // Screensaver hook (Stage 5) / заглушка под screensaver (этап 5)

private:
    enum class SpecialMode : uint8_t {
        None,
        Temporary,
        Boot,
        RebootRequired
    };

    int findNextPageIndex(int from) const;
    int findPrevPageIndex(int from) const;
    int resolveStartIndex() const;
    ILvglScreen* activeScreen() const;
    bool navigationBlocked() const;

    ILvglScreen* _pages[PAGE_COUNT] = {};
    int _currentIndex = -1;

    ILvglScreen* _tempScreen = nullptr;
    uint32_t _tempTimeoutMs = 20000;
    uint32_t _tempStartMillis = 0;

    ILvglScreen* _bootScreen = nullptr;
    ILvglScreen* _rebootScreen = nullptr;

    SpecialMode _special = SpecialMode::None;
};

} // namespace lvgl_ui

#endif // LV_PAGE_CHAIN_H
