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
    static constexpr int VISUAL_INDEX = 2;   // W2: explicit (was bare literal 2 in registration) / явный индекс
    static constexpr int STATION_INDEX = 3;
    static constexpr int WEATHER_INDEX = 4;  // W2: real Weather page slot (was bare literal 4) / слот Weather
    static constexpr int SETTINGS_INDEX = 5;

    void registerPage(int index, ILvglScreen* page);
    // create + enter start page; cold load without animation.
    // create + enter стартовой страницы; холодная загрузка без анимации.
    void init();

    // Ignored while Temporary / Boot / RebootRequired active. Skips null slots.
    // Игнорируются при активном Temporary / Boot / RebootRequired. Пропускают пустые слоты.
    void swipeLeft();
    void swipeRight();
    void goTo(int index);

    // Block 8-E5C: carousel slide animation policy (runtime; default from YORADIO_LVGL_PAGE_TRANSITION_ANIM_DEFAULT).
    // Not persisted — future Settings / faster boards (e.g. ESP32-P4) may enable. Boot fade unchanged.
    // Block 8-E5C: политика анимации карусели; не в NVS; Settings позже.
    static void setCarouselTransitionAnimationEnabled(bool enabled);
    static bool isCarouselTransitionAnimationEnabled();

    // Temporary = Preset only (product); timeout default 20s → dismissTemporary → Main.
    // Temporary — только Preset; таймаут по умолчанию 20 с → Main.
    void showTemporary(ILvglScreen* scr, uint32_t timeout_ms = 20000);
    // Always returns to Main (MAIN_INDEX), not previous carousel page.
    // Всегда возврат на Main (MAIN_INDEX), не на предыдущую страницу карусели.
    void dismissTemporary();

    void showBoot(ILvglScreen* scr);
    // Boot → Main handoff (Main create/enter + preload path). Not for Wi‑Fi Recovery from Boot failure / только Boot→Main.
    void dismissBoot();
    // Boot → RebootRequired (Wi‑Fi shell): same LVGL fade as dismissBoot but skips Main — avoids BG/player preload offline / без Main preload офлайн.
    void dismissBootThenShowRebootRequired(ILvglScreen* scr);
    // Wi-Fi setup: full-screen RebootRequired; no back-nav, exit = reboot (Stage 7+ UI).
    // Wi-Fi: полноэкранный RebootRequired; без назад, выход = перезагрузка (UI в Stage 7+).
    void showRebootRequired(ILvglScreen* scr);
    // Wi-Fi 3A+: leave RebootRequired shell, restore carousel Main (sync load; no broad refactor).
    // Wi-Fi 3A+: выход из RebootRequired, возврат карусели на Main.
    void dismissRebootRequired();

    // Wi‑Fi 4C: RebootRequired shell is active for this screen pointer (LVGL Wi‑Fi flow only today).
    // Wi‑Fi 4C: активен полноэкранный RebootRequired для данного экрана (сейчас только Wi‑Fi).
    bool isRebootRequiredActiveFor(const ILvglScreen* scr) const;

    int currentIndex() const;
    ILvglScreen* currentPage() const;
    bool isTemporaryActive() const;

    void tick();
    void onActivity(); // Screensaver hook (Stage 5) / заглушка под screensaver (этап 5)

    // Stage 6.6R-C: live palette on carousel pages that already called create() (screen() != nullptr).
    // Этап 6.6R-C: live reapply темы на созданных страницах карусели.
    void reapplyThemeToCreatedPages();

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

    // W2F: re-entrancy guard for goTo() — prevents a gesture from starting a second carousel
    // switch while one is in progress. Instant (ANIM_NONE) path is synchronous, but the guard is
    // cheap and future-safe (e.g. if an animated/auto_del path is added later).
    // W2F: защита от повторного входа в goTo() во время перехода (жест поверх жеста).
    bool _transitionActive = false;
};

} // namespace lvgl_ui

#endif // LV_PAGE_CHAIN_H
