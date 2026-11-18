// Bottom source selector. DspTask owns every lv_* call. Sound stays in Player::activate.
// Нижний выбор источника. Все lv_* только на DspTask. Звук остаётся в Player::activate.
#pragma once

#include <stdint.h>

#include "source_overlay_policy.h"

namespace lvgl_ui {

bool sourceOverlayIsOpen();
// Creates the panel outside LVGL event dispatch. False when this place cannot open it.
// Создаёт панель вне диспетчеризации событий LVGL. false, если здесь её открывать нельзя.
bool sourceOverlayOpen(OverlayPlace place);
// Hides the panel. Does not cancel an activation Player already accepted.
// Прячет панель. Не отменяет активацию, которую Player уже принял.
void sourceOverlayDismiss();
// Poll Player::activation and apply a deferred open/close. Call after lv_timer_handler().
// Опрос Player::activation и отложенное открытие/закрытие. После lv_timer_handler().
OverlayCommand sourceOverlayService(OverlayPlace place);
// Future encoder / IR. DspTask only. Moving focus does not activate a source.
// Будущие энкодер и ИК. Только DspTask. Сдвиг фокуса не активирует источник.
void sourceOverlayFocusPrevious();
void sourceOverlayFocusNext();
void sourceOverlayFocusSelect();

}  // namespace lvgl_ui
