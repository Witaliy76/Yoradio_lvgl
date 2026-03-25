#include "lv_page_chain.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "Arduino.h"

namespace lvgl_ui {

namespace {

constexpr uint32_t kPageAnimMs = 300;

void loadScreenAnim(lv_obj_t* scr, lv_scr_load_anim_t anim, uint32_t time_ms) {
    if (!scr) return;
    lv_scr_load_anim(scr, anim, time_ms, 0, false);
}

} // namespace

void PageChain::registerPage(int index, ILvglScreen* page) {
    if (index < 0 || index >= PAGE_COUNT) return;
    _pages[index] = page;
}

int PageChain::resolveStartIndex() const {
    if (_pages[MAIN_INDEX]) return MAIN_INDEX;
    for (int i = 0; i < PAGE_COUNT; ++i) {
        if (_pages[i]) return i;
    }
    return -1;
}

int PageChain::findNextPageIndex(int from) const {
    for (int i = from + 1; i < PAGE_COUNT; ++i) {
        if (_pages[i]) return i;
    }
    return -1;
}

int PageChain::findPrevPageIndex(int from) const {
    for (int i = from - 1; i >= 0; --i) {
        if (_pages[i]) return i;
    }
    return -1;
}

bool PageChain::navigationBlocked() const {
    return _special != SpecialMode::None;
}

ILvglScreen* PageChain::activeScreen() const {
    switch (_special) {
        case SpecialMode::Temporary:
            return _tempScreen;
        case SpecialMode::Boot:
            return _bootScreen;
        case SpecialMode::RebootRequired:
            return _rebootScreen;
        case SpecialMode::None:
        default:
            break;
    }
    if (_currentIndex >= 0 && _currentIndex < PAGE_COUNT) return _pages[_currentIndex];
    return nullptr;
}

void PageChain::init() {
    _special = SpecialMode::None;
    _tempScreen = nullptr;
    _bootScreen = nullptr;
    _rebootScreen = nullptr;

    const int start = resolveStartIndex();
    if (start < 0) return;

    _currentIndex = start;
    ILvglScreen* p = _pages[start];
    if (!p) return;

    p->create();
    p->enter();
    lv_obj_t* s = p->screen();
    if (s) lv_scr_load(s);
}

void PageChain::swipeLeft() {
    if (navigationBlocked()) return;
    const int n = findNextPageIndex(_currentIndex);
    if (n < 0) return;
    goTo(n);
}

void PageChain::swipeRight() {
    if (navigationBlocked()) return;
    const int p = findPrevPageIndex(_currentIndex);
    if (p < 0) return;
    goTo(p);
}

void PageChain::goTo(int index) {
    if (navigationBlocked()) return;
    if (index < 0 || index >= PAGE_COUNT) return;
    ILvglScreen* next = _pages[index];
    if (!next) return;

    // Skip only if this page is already the active LVGL screen.
    // Пропуск только если эта страница уже активный экран LVGL.
    if (index == _currentIndex && next->screen() && lv_scr_act() == next->screen()) return;

    ILvglScreen* prev = nullptr;
    if (_currentIndex >= 0 && _currentIndex < PAGE_COUNT) prev = _pages[_currentIndex];

    const lv_scr_load_anim_t anim = (_currentIndex < 0 || index > _currentIndex)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT
        : LV_SCR_LOAD_ANIM_MOVE_RIGHT;

    if (prev) prev->exit();
    next->create();
    next->enter();
    _currentIndex = index;
    loadScreenAnim(next->screen(), anim, kPageAnimMs);
}

void PageChain::showTemporary(ILvglScreen* scr, uint32_t timeout_ms) {
    if (!scr || navigationBlocked()) return;

    ILvglScreen* cur = _pages[_currentIndex];
    if (cur) cur->exit();

    _tempScreen = scr;
    _tempTimeoutMs = timeout_ms;
    _tempStartMillis = millis();
    _special = SpecialMode::Temporary;

    scr->create();
    scr->enter();
    loadScreenAnim(scr->screen(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, kPageAnimMs);
}

void PageChain::dismissTemporary() {
    if (_special != SpecialMode::Temporary || !_tempScreen) return;

    _tempScreen->exit();
    _tempScreen->destroy();
    _tempScreen = nullptr;
    _special = SpecialMode::None;

    ILvglScreen* main = _pages[MAIN_INDEX];
    if (!main) return;

    main->create();
    main->enter();
    _currentIndex = MAIN_INDEX;
    loadScreenAnim(main->screen(), LV_SCR_LOAD_ANIM_MOVE_TOP, kPageAnimMs);
}

void PageChain::showBoot(ILvglScreen* scr) {
    if (!scr || navigationBlocked()) return;

    ILvglScreen* cur = _pages[_currentIndex];
    if (cur) cur->exit();

    _bootScreen = scr;
    _special = SpecialMode::Boot;

    scr->create();
    scr->enter();
    loadScreenAnim(scr->screen(), LV_SCR_LOAD_ANIM_FADE_IN, kPageAnimMs);
}

void PageChain::showRebootRequired(ILvglScreen* scr) {
    if (!scr || navigationBlocked()) return;

    ILvglScreen* cur = _pages[_currentIndex];
    if (cur) cur->exit();

    _rebootScreen = scr;
    _special = SpecialMode::RebootRequired;

    scr->create();
    scr->enter();
    loadScreenAnim(scr->screen(), LV_SCR_LOAD_ANIM_FADE_IN, kPageAnimMs);
}

int PageChain::currentIndex() const {
    return _currentIndex;
}

ILvglScreen* PageChain::currentPage() const {
    return activeScreen();
}

bool PageChain::isTemporaryActive() const {
    return _special == SpecialMode::Temporary;
}

void PageChain::tick() {
    if (_special != SpecialMode::Temporary || !_tempScreen) return;
    const uint32_t now = millis();
    if ((now - _tempStartMillis) >= _tempTimeoutMs) dismissTemporary();
}

void PageChain::onActivity() {
    // Reserved for screensaver idle tracking (Stage 5).
    // Зарезервировано под учёт простоя screensaver (этап 5).
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
