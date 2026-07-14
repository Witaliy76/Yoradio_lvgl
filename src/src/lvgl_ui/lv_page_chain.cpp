#include "lv_page_chain.h"

#include "lvgl.h"
#include "Arduino.h"
#include "../core/config.h"
#include "../core/autodim.h"
#include "../core/options.h"   // pulls myoptions.h → YORADIO_WEATHER_UI_DIAG

// W2D: gated page-transition diagnostics (carousel mem/object pressure investigation).
// Default OFF; enable via YORADIO_WEATHER_UI_DIAG=1 in myoptions.h (local, not committed).
// W2D: gated диагностика переходов карусели; по умолчанию выкл.
#ifndef YORADIO_WEATHER_UI_DIAG
#define YORADIO_WEATHER_UI_DIAG 0
#endif

#if YORADIO_WEATHER_UI_DIAG
#include <esp_heap_caps.h>
#endif

namespace lvgl_ui {

namespace {

#if YORADIO_WEATHER_UI_DIAG
const char* pageName(int index) {
    switch (index) {
        case PageChain::INFO_INDEX:     return "Info";
        case PageChain::MAIN_INDEX:     return "Main";
        case PageChain::VISUAL_INDEX:   return "Visual";
        case PageChain::STATION_INDEX:  return "Station";
        case PageChain::WEATHER_INDEX:  return "Weather";
        case PageChain::SETTINGS_INDEX: return "Settings";
        default:                        return "?";
    }
}

uint32_t diagCountObjs(lv_obj_t* root) {
    if (!root) return 0;
    uint32_t n = 1;
    const uint32_t c = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < c; ++i) n += diagCountObjs(lv_obj_get_child(root, i));
    return n;
}

// LVGL pool monitor (gradients alloc here: LV_MEM_SIZE 48K, LV_GRAD_CACHE_DEF_SIZE 0) + heaps.
// Монитор пула LVGL (тут аллоцируются градиенты) + кучи.
void diagDump(const char* tag, int idx, lv_obj_t* root) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("[WX_UI_DIAG] %s page=%s(%d) objs=%lu | lvpool free=%lu biggest=%lu "
                  "used=%u%% frag=%u%% | int_free=%u int_big=%u psram_free=%u psram_big=%u\n",
                  tag, pageName(idx), idx, (unsigned long)diagCountObjs(root),
                  (unsigned long)mon.free_size, (unsigned long)mon.free_biggest_size,
                  (unsigned)mon.used_pct, (unsigned)mon.frag_pct,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}
#endif // YORADIO_WEATHER_UI_DIAG

constexpr uint32_t kPageAnimMs = 300;
// Boot → Main only (dismissBoot); carousel goTo uses policy below when animation enabled.
// Только Boot → Main (dismissBoot); карусель goTo — по политике ниже.
constexpr uint32_t kBootHandoffFadeMs = 600;

// Block 8-E5C: compile-time default + runtime override (no NVS yet).
// Block 8-E5C: дефолт из options.h; runtime без сохранения в config.
bool g_carousel_transition_anim_enabled =
    (YORADIO_LVGL_PAGE_TRANSITION_ANIM_DEFAULT != 0);

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
    _temporaryOriginIndex = -1;
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

void PageChain::setCarouselTransitionAnimationEnabled(bool enabled) {
    g_carousel_transition_anim_enabled = enabled;
}

bool PageChain::isCarouselTransitionAnimationEnabled() {
    return g_carousel_transition_anim_enabled;
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
    // W2F: ignore a swipe/goTo while a transition is already running (re-entry from gestures).
    // W2F: игнорируем свайп/goTo, пока идёт переход (повторный вход из жестов).
    if (_transitionActive) return;
    ILvglScreen* next = _pages[index];
    if (!next) return;

    // Skip only if this page is already the active LVGL screen.
    // Пропуск только если эта страница уже активный экран LVGL.
    if (index == _currentIndex && next->screen() && lv_scr_act() == next->screen()) return;

    // RAII guard: clears _transitionActive on every return path below (instant path is synchronous,
    // but this keeps the flag correct even on early-return failure cases).
    // RAII-страж: сбрасывает флаг на любом return ниже.
    _transitionActive = true;
    struct TransitionGuard { bool& flag; ~TransitionGuard() { flag = false; } } _tg{_transitionActive};

    ILvglScreen* prev = nullptr;
    if (_currentIndex >= 0 && _currentIndex < PAGE_COUNT) prev = _pages[_currentIndex];

    const lv_scr_load_anim_t anim = (_currentIndex < 0 || index > _currentIndex)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT
        : LV_SCR_LOAD_ANIM_MOVE_RIGHT;

#if YORADIO_WEATHER_UI_DIAG
    // Before transition: previous page still active (e.g. before entering Weather/Main).
    diagDump("goTo_before", _currentIndex, prev ? prev->screen() : nullptr);
#endif

    // Create the next screen while prev is still the active LVGL screen (peak = old + new trees).
    // Создаём следующий экран, пока prev ещё активен (пик памяти = старое + новое дерево).
    if (prev) prev->exit();
    next->create();
    next->enter();
    _currentIndex = index;

#if YORADIO_WEATHER_UI_DIAG
    // After next->create()/enter(): destination objects exist (e.g. after Weather create).
    diagDump("goTo_after_create", index, next->screen());
#endif

    lv_obj_t* next_scr = next->screen();
    if (!next_scr) return; // create() failed (e.g. LVGL pool exhausted) — guard clears the flag.

    // W2F unified auto-delete: prev is deleted by LVGL on load, so its object tree returns to the
    // 48 KB pool before the next page redraws (e.g. Main gradients). No per-page non-resident flag.
    // W2F: prev удаляется LVGL при загрузке — дерево возвращается в пул до отрисовки следующей страницы.
    const bool autoDeletePrev = (prev && prev->screen() && prev->screen() != next_scr);

    if (g_carousel_transition_anim_enabled) {
        // Animation path is disabled by policy (slow on partial buffer) and is NOT wired to auto_del
        // here — keeping it would leave prev resident. Default build never takes this branch.
        // Анимация выключена политикой и не связана с auto_del — дефолтная сборка сюда не заходит.
        loadScreenAnim(next_scr, anim, kPageAnimMs);
    } else if (autoDeletePrev) {
        // 1) stop prev's non-tree resources (e.g. Main PresenceRail lv_timer) BEFORE deletion;
        // 2) lv_scr_load_anim(ANIM_NONE, time=0, delay=0, auto_del=true) loads next and deletes the
        //    old active screen SYNCHRONOUSLY (LVGL shortcut path), so it is safe to release prev's
        //    C++ pointers immediately after the call returns.
        // 1) гасим ресурсы prev вне дерева (таймер rail) ДО удаления;
        // 2) auto_del=true с ANIM_NONE/0/0 — синхронная загрузка+удаление старого экрана.
        prev->prepareForAutoDelete();
        loadScreenAnimAutoDel(next_scr, LV_SCR_LOAD_ANIM_NONE, 0);
        prev->releaseAfterAutoDelete();
#if YORADIO_WEATHER_UI_DIAG
        diagDump("goTo_after_auto_delete", index, next_scr);
#endif
    } else {
        // First load (no prev) or prev already == next_scr — just load, nothing to auto-delete.
        // Первая загрузка (нет prev) или prev уже == next_scr — просто грузим.
        lv_scr_load(next_scr);
    }

#if YORADIO_WEATHER_UI_DIAG
    // After load: destination is the active LVGL screen (e.g. after Main becomes active).
    diagDump("goTo_after_load", index, next_scr);
#endif
    autodim_notify_activity("navigation");
}

void PageChain::showTemporary(ILvglScreen* scr, uint32_t timeout_ms) {
    if (!scr || navigationBlocked()) return;

    // Capture origin before exit — return-to-origin on dismiss / timeout (Stage 6.4A).
    // Запоминаем origin до exit — возврат при dismiss / timeout (этап 6.4A).
    _temporaryOriginIndex = (_currentIndex >= 0 && _currentIndex < PAGE_COUNT) ? static_cast<int8_t>(_currentIndex) : -1;

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

    int idx = static_cast<int>(_temporaryOriginIndex);
    ILvglScreen* origin = nullptr;
    if (idx >= 0 && idx < PAGE_COUNT) {
        origin = _pages[idx];
    }
    if (!origin) {
        idx = MAIN_INDEX;
        origin = _pages[MAIN_INDEX];
    }

    ILvglScreen* temp = _tempScreen;

    if (!origin) {
        temp->exit();
        temp->destroy();
        _tempScreen = nullptr;
        _special = SpecialMode::None;
        _temporaryOriginIndex = -1;
        _tempTimeoutMs = 20000;
        _tempStartMillis = 0;
        return;
    }

    // Approved lifecycle: temp exits before origin re-enters; destroy temp only after origin load.
    // Утверждённый порядок: temp exit → origin create/enter/load → temp destroy.
    temp->exit();

    origin->create();
    origin->enter();

    lv_obj_t* origin_scr = origin->screen();
    if (origin_scr) {
        lv_scr_load(origin_scr);
    }

    temp->destroy();

    _currentIndex = idx;

    _tempScreen = nullptr;
    _special = SpecialMode::None;
    _temporaryOriginIndex = -1;
    _tempTimeoutMs = 20000;
    _tempStartMillis = 0;
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

void PageChain::showWifiServiceOneWay(ILvglScreen* scr) {
    if (!scr) return;
    // Not gated by navigationBlocked() — forced one-way exit from any carousel state.
    // Не проверяем navigationBlocked() — принудительный односторонний выход из любого состояния карусели.

    ILvglScreen* cur = (_currentIndex >= 0 && _currentIndex < PAGE_COUNT)
                       ? _pages[_currentIndex] : nullptr;

    // Phase 1: destroy origin carousel page via blank intermediary so LVGL always
    // has a valid active screen while the old object tree is freed.
    // Фаза 1: удалить origin carousel через blank intermediary — LVGL требует действующий
    // active screen во время удаления старого дерева объектов.
    lv_obj_t* blank = nullptr;
    if (cur && cur->screen()) {
        cur->exit();
        // Minimal blank: no widgets, no callbacks — purely a technical active-screen placeholder.
        // Minimal blank: без виджетов и callback'ов — технический placeholder для active screen.
        blank = lv_obj_create(nullptr);
        if (!blank) {
            // Cannot swap active screen safely — controlled reboot into normal Main boot.
            // Нельзя безопасно сменить active screen — контролируемый reboot в Main boot.
            ESP.restart();
            return;
        }
        lv_scr_load(blank);     // blank is now lv_scr_act()
        cur->destroy();         // LVGL tree freed; pool reclaimed. cur->screen() → nullptr
    }

    // Phase 2: set PageChain state BEFORE create/enter (mirrors showRebootRequired ordering).
    // Фаза 2: PageChain state ДО create/enter (порядок как в showRebootRequired).
    _currentIndex = -1;
    _rebootScreen = scr;
    _special = SpecialMode::RebootRequired;

    // Phase 3: create service screen on freed pool, then synchronously auto-delete blank.
    // Фаза 3: создать service screen на освобождённом пуле, затем синхронно auto-delete blank.
    scr->create();
    lv_obj_t* svc_scr = scr->screen();

    if (svc_scr) {
        if (blank) {
            // auto_del=true + ANIM_NONE/0/0: LVGL synchronously loads svc_scr and deletes blank.
            // auto_del=true + ANIM_NONE/0/0: LVGL синхронно загружает svc_scr и удаляет blank.
            loadScreenAnimAutoDel(svc_scr, LV_SCR_LOAD_ANIM_NONE, 0);
        } else {
            lv_scr_load(svc_scr);
        }
        scr->enter();
    } else {
        // create() failed — OOM even on freed pool; controlled reboot into normal Main boot.
        // create() провалился — OOM даже на освобождённом пуле; контролируемый reboot в Main boot.
        ESP.restart();
    }
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

bool PageChain::isTemporaryActiveFor(const ILvglScreen* scr) const {
    if (!scr) return false;
    return (_special == SpecialMode::Temporary) && (_tempScreen == scr);
}

void PageChain::refreshTemporaryTimeout() {
    if (_special != SpecialMode::Temporary || !_tempScreen) return;
    _tempStartMillis = millis();
}

uint32_t PageChain::temporaryRemainingMs() const {
    if (_special != SpecialMode::Temporary || !_tempScreen) return 0u;
    const uint32_t elapsed = millis() - _tempStartMillis;
    if (elapsed >= _tempTimeoutMs) return 0u;
    return _tempTimeoutMs - elapsed;
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

