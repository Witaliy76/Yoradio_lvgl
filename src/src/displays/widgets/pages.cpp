#include "../dspcore.h"
#if DSP_MODEL!=DSP_DUMMY

#include "pages.h"
#include "widgets.h"
#include <vector>
#include <algorithm>

void Pager::begin(){

}

void Pager::loop(){
  for(const auto& p: _pages)
    if(p->isActive()) p->loop();
}

Page& Pager::addPage(Page* page, bool setNow){
  _pages.add(page);
  if(setNow) setPage(page);
  return *page;
}

bool Pager::removePage(Page* page, bool clear_display){
  page->setActive(false);
  // clearDsp() fills the whole Arduino_Canvas; skip when LVGL owns the same buffer (Boot/Main handoff).
  // clearDsp() заливает весь Arduino_Canvas; не вызываем, если LVGL рисует в тот же буфер (Boot/Main).
  if (clear_display) {
    dsp.clearDsp();
  }
  return _pages.remove(page);
}

void Pager::setPage(Page* page, bool black){
  for(const auto& p: _pages) p->setActive(false);
  dsp.clearDsp(black);
  page->setActive(true);
}

Page* Pager::getActivePage() const {
  for(const auto& p: _pages) {
    if(p->isActive()) return p;
  }
  return nullptr;
}


/*******************************************************/

Page::Page() : _widgets(LinkedList<Widget * >([](Widget * wd) { delete wd;})), _pages(LinkedList<Page*>([](Page* pg){ delete pg; })) {
  _active = false;
}

Page::~Page() {
  // Безопасная очистка: используем free() вместо range-for, чтобы избежать UB
  // Safe cleanup: use free() instead of range-for to avoid UB
  // free() вызывает _onRemove (delete) для каждого элемента и очищает список
  // free() calls _onRemove (delete) for each element and clears the list
  _widgets.free();
  _pages.free();
}

void Page::loop() {
  if(_active) for (const auto& w : _widgets) w->loop();
}

Widget& Page::addWidget(Widget* widget) {
  _widgets.add(widget);
  widget->setActive(_active, _active);
  return *widget;
}

bool Page::removeWidget(Widget* widget){
  if (!widget) return false; // Защита от nullptr / nullptr protection
  widget->setActive(false, _active);
  return _widgets.remove(widget);
}

Page& Page::addPage(Page* page){
  _pages.add(page);
  return *page;
}

bool Page::removePage(Page* page){
  if (!page) return false; // Защита от nullptr / nullptr protection
  return _pages.remove(page);
}

void Page::setActive(bool act) {
  for(const auto& w: _widgets) w->setActive(act);
  for(const auto& p: _pages) p->setActive(act);
  _active = act;
}

bool Page::isActive() {
  return _active;
}

// Получить порядковый индекс ScrollWidget'а среди всех ScrollWidget'ов в этой странице
// Считает только ScrollWidget'ы, которые могут скроллиться (через canParticipateInScroll)
// Сортировка по top-down (сверху вниз, слева направо) / Top-down sorting (top to bottom, left to right)
int16_t Page::getScrollWidgetIndex(void* widget) const {
  // Собираем список eligible ScrollWidget* / Collect eligible ScrollWidget* list
  std::vector<ScrollWidget*> eligible;
  for (const auto& w : _widgets) {
    // Безопасная проверка типа через виртуальный метод isScrollWidget()
    if (!w || !w->isScrollWidget()) {
      continue; // Пропускаем виджеты, которые не являются ScrollWidget'ами
    }
    // Теперь безопасно приводим к ScrollWidget*
    ScrollWidget* sw = static_cast<ScrollWidget*>(w);
    if (sw && sw->canParticipateInScroll()) {
      eligible.push_back(sw);
    }
  }
  
  // Сортируем по top-down: сначала по top (меньше → раньше), затем по left (меньше → раньше)
  // Sort by top-down: first by top (less → earlier), then by left (less → earlier)
  std::sort(eligible.begin(), eligible.end(), [](ScrollWidget* a, ScrollWidget* b) {
    if (a->top() != b->top()) {
      return a->top() < b->top(); // Меньше top → раньше / Less top → earlier
    }
    return a->left() < b->left(); // При равном top: меньше left → раньше / Equal top: less left → earlier
  });
  
  // Ищем виджет в отсортированном списке / Find widget in sorted list
  for (size_t i = 0; i < eligible.size(); i++) {
    if (eligible[i] == widget) {
      return static_cast<int16_t>(i);
    }
  }
  
  return -1; // Виджет не найден или не может скроллиться
}

// Получить количество ScrollWidget'ов, которые могут скроллиться
// Сортировка по top-down (сверху вниз, слева направо) / Top-down sorting (top to bottom, left to right)
int16_t Page::getScrollableCount() {
  // Собираем список eligible ScrollWidget* / Collect eligible ScrollWidget* list
  std::vector<ScrollWidget*> eligible;
  for (const auto& w : _widgets) {
    // Безопасная проверка типа через виртуальный метод isScrollWidget()
    if (!w || !w->isScrollWidget()) {
      continue; // Пропускаем виджеты, которые не являются ScrollWidget'ами
    }
    // Теперь безопасно приводим к ScrollWidget*
    ScrollWidget* sw = static_cast<ScrollWidget*>(w);
    if (sw && sw->canParticipateInScroll()) {
      eligible.push_back(sw);
    }
  }
  
  // Сортируем по top-down: сначала по top (меньше → раньше), затем по left (меньше → раньше)
  // Sort by top-down: first by top (less → earlier), then by left (less → earlier)
  std::sort(eligible.begin(), eligible.end(), [](ScrollWidget* a, ScrollWidget* b) {
    if (a->top() != b->top()) {
      return a->top() < b->top(); // Меньше top → раньше / Less top → earlier
    }
    return a->left() < b->left(); // При равном top: меньше left → раньше / Equal top: less left → earlier
  });
  
  return static_cast<int16_t>(eligible.size());
}

#endif // #if DSP_MODEL!=DSP_DUMMY
