// Legacy Canvas Pager/Page removed in Block 8-E18D. LVGL is the only product UI.
// Legacy Canvas Pager/Page удалены в Block 8-E18D. Продуктовый UI — только LVGL.

#include "../dspcore.h"
#if DSP_MODEL != DSP_DUMMY

#include "pages.h"
#include "widgets.h"

void Pager::begin() {}
void Pager::loop() {}

Page& Pager::addPage(Page* page, bool setNow) {
    _pages.add(page);
    if (setNow) setPage(page);
    return *page;
}

bool Pager::removePage(Page* page, bool clear_display) {
    (void)clear_display;
    page->setActive(false);
    return _pages.remove(page);
}

void Pager::setPage(Page* page, bool black) {
    (void)black;
    for (const auto& p : _pages) p->setActive(false);
    page->setActive(true);
}

Page* Pager::getActivePage() const {
    for (const auto& p : _pages) {
        if (p->isActive()) return p;
    }
    return nullptr;
}

Page::Page()
    : _widgets(LinkedList<Widget*>([](Widget* wd) { delete wd; })),
      _pages(LinkedList<Page*>([](Page* pg) { delete pg; })) {
    _active = false;
}

Page::~Page() {}

void Page::loop() {
    for (const auto& w : _widgets) {
        if (_active) w->loop();
    }
    for (const auto& p : _pages) {
        if (_active) p->loop();
    }
}

Widget& Page::addWidget(Widget* widget) {
    _widgets.add(widget);
    return *widget;
}

bool Page::removeWidget(Widget* widget) { return _widgets.remove(widget); }

Page& Page::addPage(Page* page) {
    _pages.add(page);
    return *page;
}

bool Page::removePage(Page* page) { return _pages.remove(page); }

void Page::setActive(bool act) {
    _active = act;
    for (const auto& w : _widgets) w->setActive(act);
    for (const auto& p : _pages) p->setActive(act);
}

bool Page::isActive() { return _active; }

int16_t Page::getScrollWidgetIndex(void* widget) const {
    (void)widget;
    return -1;
}

int16_t Page::getScrollableCount() { return 0; }

#endif // DSP_MODEL != DSP_DUMMY
