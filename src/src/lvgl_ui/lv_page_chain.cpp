#include "lv_page_chain.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"
#include "Arduino.h"
#include "../core/config.h"
#include "../core/options.h"

namespace lvgl_ui {

namespace {

constexpr uint32_t kPageAnimMs = 300;
// Boot → Main only (dismissBoot); other transitions keep kPageAnimMs.
// Только Boot → Main (dismissBoot); остальные переходы — kPageAnimMs.
constexpr uint32_t kBootHandoffFadeMs = 600;

void loadScreenAnim(lv_obj_t* scr, lv_scr_load_anim_t anim, uint32_t time_ms) {
    if (!scr) return;
    lv_scr_load_anim(scr, anim, time_ms, 0, false);
}

void loadScreenAnimAutoDel(lv_obj_t* scr, lv_scr_load_anim_t anim, uint32_t time_ms) {
    if (!scr) return;
    // auto_del=true: LVGL deletes the previous active screen after the transition.
    // auto_del=true: LVGL удаляет предыдущий активный экран после перехода.
    lv_scr_load_anim(scr, anim, time_ms, 0, true);
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

    ILvglScreen* cur = nullptr;
    if (_currentIndex >= 0 && _currentIndex < PAGE_COUNT) cur = _pages[_currentIndex];
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

    // Must load a valid screen BEFORE deleting the active temp screen.
    // LVGL warns / may crash if lv_scr_act() is deleted then lv_scr_load_anim runs.
    // Сначала переключаемся на Main, иначе удаление активного экрана ломает анимацию перехода.
    ILvglScreen* main = _pages[MAIN_INDEX];
    if (!main) return;

    main->create();
    main->enter();
    lv_obj_t* mainScr = main->screen();
    if (mainScr) lv_scr_load(mainScr);
    _currentIndex = MAIN_INDEX;

    _tempScreen->exit();
    _tempScreen->destroy();
    _tempScreen = nullptr;
    _special = SpecialMode::None;
}

void PageChain::showBoot(ILvglScreen* scr) {
    if (!scr || navigationBlocked()) return;

    ILvglScreen* cur = nullptr;
    if (_currentIndex >= 0 && _currentIndex < PAGE_COUNT) cur = _pages[_currentIndex];
    if (cur) cur->exit();

    _bootScreen = scr;
    _special = SpecialMode::Boot;

    scr->create();
    scr->enter();
    loadScreenAnim(scr->screen(), LV_SCR_LOAD_ANIM_FADE_IN, kPageAnimMs);
}

void PageChain::dismissBoot() {
    if (_special != SpecialMode::Boot || !_bootScreen) return;

    ILvglScreen* main = _pages[MAIN_INDEX];
    if (!main) return;

    main->create();
    main->enter();
    lv_obj_t* mainScr = main->screen();
    // Fade handoff restored: full-frame LVGL refresh fixed stripe tearing; FADE_ON should be safe again.
    // Вернули fade: полноэкранный refresh LVGL убрал полосы; FADE_ON снова безопасен для handoff.
    // auto_del deletes Boot screen after the transition; destroy() stops shuttle anim (no stale pointers).
    // auto_del удаляет Boot после перехода; destroy() гасит анимацию shuttle (без висячих указателей).
    if (mainScr) loadScreenAnimAutoDel(mainScr, LV_SCR_LOAD_ANIM_FADE_ON, kBootHandoffFadeMs);
    _currentIndex = MAIN_INDEX;

    _bootScreen->exit();
    // Must stop boot animation callback immediately to avoid stale-pointer access during fade handoff.
    // Нужно сразу остановить callback анимации Boot, чтобы не словить stale-pointer во время fade handoff.
    _bootScreen->destroy();
    _bootScreen = nullptr;
    _special = SpecialMode::None;
}

void PageChain::dismissBootThenShowRebootRequired(ILvglScreen* scr) {
    if (_special != SpecialMode::Boot || !_bootScreen || !scr) return;

    // Same ordering contract as showRebootRequired: special mode flags before create/load_anim()
    // Тот же порядок что showRebootRequired: режим до load_anim.
    _rebootScreen = scr;
    _special      = SpecialMode::RebootRequired;

    scr->create();
    scr->enter();
    lv_obj_t* svcScr = scr->screen();
    // Same auto_del fade as dismissBoot→Main; avoids Main::enter / BG preload when STA offline / без Main preload.
    if (svcScr) loadScreenAnimAutoDel(svcScr, LV_SCR_LOAD_ANIM_FADE_ON, kBootHandoffFadeMs);

    _bootScreen->exit();
    _bootScreen->destroy();
    _bootScreen = nullptr;
}

void PageChain::showRebootRequired(ILvglScreen* scr) {
    if (!scr || navigationBlocked()) return;

    ILvglScreen* cur = nullptr;
    if (_currentIndex >= 0 && _currentIndex < PAGE_COUNT) cur = _pages[_currentIndex];
    if (cur) cur->exit();

    _rebootScreen = scr;
    _special = SpecialMode::RebootRequired;

    scr->create();
    scr->enter();
    loadScreenAnim(scr->screen(), LV_SCR_LOAD_ANIM_FADE_IN, kPageAnimMs);
}

bool PageChain::isRebootRequiredActiveFor(const ILvglScreen* scr) const {
    if (!scr) return false;
    return (_special == SpecialMode::RebootRequired) && (_rebootScreen == scr);
}

void PageChain::dismissRebootRequired() {
    if (_special != SpecialMode::RebootRequired || !_rebootScreen) return;

    ILvglScreen* main = _pages[MAIN_INDEX];
    if (!main) return;

    // Load Main before destroying service screen — avoid lv_scr_act() dangling / Main до destroy сервиса.
    main->create();
    main->enter();
    lv_obj_t* mainScr = main->screen();
    if (mainScr) {
        lv_scr_load(mainScr);
    }
    _currentIndex = MAIN_INDEX;

    _rebootScreen->exit();
    _rebootScreen->destroy();
    _rebootScreen = nullptr;
    _special = SpecialMode::None;
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
    // Stage 5.6: bridge LVGL activity → legacy idle counters (single source of truth in network.ticks).
    // Этап 5.6: мост активности LVGL → legacy-счётчики простоя (единый источник в network.ticks).
    config.screensaverTicks = SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks = SCREENSAVERSTARTUPDELAY;
}

void PageChain::reapplyThemeToCreatedPages() {
    for (int i = 0; i < PAGE_COUNT; ++i) {
        ILvglScreen* page = _pages[i];
        if (!page) continue;
        if (page->screen() == nullptr) continue;
        page->liveReapplyTheme();
    }
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
