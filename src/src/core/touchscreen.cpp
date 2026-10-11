/**
 * @file touchscreen.cpp
 * @brief Драйвер для работы с сенсорным экраном
 * @author W76W, 4pda.to
 * @version 2.0
 * @date октябрь 2025
 * 
 * Реализация обработки касаний и свайпов для сенсорного экрана
 * 
 * Поддерживаемые модели тачскринов:
 * - GT911 - емкостный тачскрин (I2C); geometry from kYoradioTouchGeometryCurrent
 *   / геометрия из kYoradioTouchGeometryCurrent
 * 
 * Функциональность:
 * - Одиночные касания (короткое/долгое)
 * - Свайпы влево/вправо для управления громкостью
 * - Свайпы вверх/вниз для управления списком станций
 * - Мультитач (два пальца) для переключения режимов
 * - Защиту от случайных касаний
 * - Улучшенную обработку последовательных свайпов
 * - Адаптивные пороги для разных типов тачскринов
 * - Условную компиляцию для GT911
 */

#include "options.h"
#if (TS_MODEL!=TS_MODEL_UNDEFINED)

#include "touch_geometry.h"
#include "touchscreen.h"
#include "config.h"
#include "controls.h"
#include "display.h"
#include "player.h"

#ifndef TS_X_MIN
  #define TS_X_MIN              400
#endif
#ifndef TS_X_MAX
  #define TS_X_MAX              3800
#endif
#ifndef TS_Y_MIN
  #define TS_Y_MIN              260
#endif
#ifndef TS_Y_MAX
  #define TS_Y_MAX              3800
#endif
#ifndef TS_STEPS
  #define TS_STEPS              40
#endif

#if TS_MODEL==TS_MODEL_GT911
  #include "../GT911_Touchscreen/TAMC_GT911.h"
  TAMC_GT911 ts = TAMC_GT911(TS_SDA, TS_SCL, TS_INT, TS_RST,
                             kYoradioTouchGeometryCurrent.panel_width,
                             kYoradioTouchGeometryCurrent.panel_height);
  typedef TP_Point TSPoint;
#endif

/*
 * Константы для настройки работы тачскрина:
 * 
 * SWIPE_THRESHOLD (15) - минимальное расстояние в пикселях для определения свайпа
 *    - Меньшее значение: более чувствительные свайпы, но больше ложных срабатываний
 *    - Большее значение: более точные свайпы, но требуется большее движение
 * 
 * SWIPE_MIN_DISTANCE (20) - минимальное общее расстояние для подтверждения свайпа
 *    - Влияет на то, насколько длинным должен быть свайп
 *    - Помогает отфильтровать случайные касания
 * 
 * SWIPE_MAX_TIME (300) - максимальное время в миллисекундах для выполнения свайпа
 *    - Если свайп длится дольше, он игнорируется
 *    - Помогает отличить свайп от долгого касания
 * 
 * SWIPE_DEADZONE (3) - зона нечувствительности в пикселях
 *    - Игнорирует случайные микродвижения
 *    - Уменьшает дребезг при касании
 * 
 * SWIPE_ANGLE_THRESHOLD (22.5) - угол в градусах для определения направления свайпа
 *    - Определяет сектора для каждого направления (вверх, вниз, влево, вправо)
 *    - Меньший угол: более точное определение направления
 *    - Больший угол: более свободное определение направления
 * 
 * MOVEMENT_THRESHOLD (5) - порог для определения значимого движения
 *    - Минимальное накопленное движение для срабатывания
 *    - Влияет на плавность изменения громкости и переключения станций
 * 
 * MOVEMENT_HISTORY_SIZE (3) - количество точек для сглаживания движения
 *    - Большее значение: более плавное, но медленное срабатывание
 *    - Меньшее значение: более быстрое, но менее плавное срабатывание
 * 
 * SWIPE_CLICK_COOLDOWN (400) - задержка после свайпа перед распознаванием клика (мс)
 *    - Защита от случайного клика сразу после свайпа
 *    - Если касание произошло раньше этого времени - оно игнорируется полностью
 *    - Увеличьте значение если часто происходят ложные клики после свайпа
 * 
 * SWIPE_PROXIMITY_TIME (600) - время проверки близости к концу свайпа (мс)
 *    - После свайпа дополнительно игнорируются касания близко к точке окончания
 *    - Работает совместно с SWIPE_PROXIMITY_DIST
 *    - Защищает от медленного листания с кликом в конце
 * 
 * SWIPE_PROXIMITY_DIST (100) - радиус защиты от касания у конца свайпа (пиксели)
 *    - Касания в этом радиусе игнорируются в течение SWIPE_PROXIMITY_TIME
 *    - Если слишком агрессивно - уменьшите расстояние
 *    - Если недостаточно - увеличьте время или расстояние
 */

// Определения констант
#define SWIPE_THRESHOLD        30    // Минимальное расстояние для определения свайпа
#define SWIPE_MIN_DISTANCE     35    // Минимальное общее расстояние для свайпа
#define SWIPE_MAX_TIME        300    // Максимальное время для выполнения свайпа
#define SWIPE_DEADZONE         3     // Зона нечувствительности
#define SWIPE_ANGLE_THRESHOLD  22.5  // Угол для определения направления
#define MOVEMENT_THRESHOLD     8     // Порог значимого движения
#define MOVEMENT_HISTORY_SIZE  5     // Размер истории для сглаживания
#define SWIPE_CLICK_COOLDOWN   400   // Задержка после свайпа до распознавания клика
#define SWIPE_PROXIMITY_TIME   600   // Время проверки близости к концу свайпа (мс)
#define SWIPE_PROXIMITY_DIST   100   // Расстояние близости к концу свайпа (px)

// GT911 swipe-distance thresholds (controller-model sensitivity, not panel literals).
// Пороги свайпа GT911 (чувствительность модели контроллера, не литералы панели).
#if TS_MODEL==TS_MODEL_GT911
  #define GT911_SWIPE_THRESHOLD      20    // was 30 / было 30
  #define GT911_SWIPE_MIN_DISTANCE   25    // was 35 / было 35
  #define GT911_MOVEMENT_THRESHOLD   6     // was 8 / было 8
  #define GT911_SWIPE_DEADZONE       2     // Было 3, уменьшаем для точности
  #define GT911_SWIPE_ANGLE_THRESHOLD 25.0 // Было 22.5, увеличиваем для стабильности
#else
  // Стандартные настройки для других тачскринов
  #define GT911_SWIPE_THRESHOLD      SWIPE_THRESHOLD
  #define GT911_SWIPE_MIN_DISTANCE   SWIPE_MIN_DISTANCE
  #define GT911_MOVEMENT_THRESHOLD   MOVEMENT_THRESHOLD
  #define GT911_SWIPE_DEADZONE       SWIPE_DEADZONE
  #define GT911_SWIPE_ANGLE_THRESHOLD SWIPE_ANGLE_THRESHOLD
#endif

#ifndef TOUCH_MULTI_DELAY
  #define TOUCH_MULTI_DELAY       1000  // Задержка между мультитач-событиями
#endif
#ifndef TOUCH_MODE_DELAY
  #define TOUCH_MODE_DELAY        100   // Задержка после смены режима
#endif
#ifndef SWIPE_COOLDOWN
  #define SWIPE_COOLDOWN         100   // Задержка между свайпами
#endif
#ifndef TOUCH_BUFFER_TIMEOUT
  #define TOUCH_BUFFER_TIMEOUT    100   // Таймаут для буфера касаний
#endif
#ifndef TOUCH_READ_DELAY
  #define TOUCH_READ_DELAY        20    // Задержка чтения тачскрина
#endif

// Объединенная структура для касаний и движений
struct TouchPoint {
    uint16_t x;
    uint16_t y;
    uint32_t timestamp;
    bool valid;
    int16_t movement[2];  // [dx, dy]
};

// Функция фильтрации координат GT911 / GT911 coordinate filter
#if TS_MODEL==TS_MODEL_GT911
bool TouchScreen::_filterGT911Coordinates(uint16_t rawX, uint16_t rawY) {
    // Generic filter: bounds come from the current-board contract, not a 480 literal.
    // Generic-фильтр: границы из контракта текущей платы, не литерал 480.
    return yoradio_touch_filter_raw(kYoradioTouchGeometryCurrent, rawX, rawY);
}
#endif

void TouchScreen::loop(){
  uint16_t touchX, touchY;
  static bool wastouched = true;
  static uint32_t touchLongPress;
  static tsDirection_e direct;
  static bool wasTwoFingerTouch = false;
    static uint32_t lastMultiTouchTime = 0;
    static uint32_t lastSwipeTime = 0;
  static bool wasSwiped = false;
    static uint32_t swipeStartTime = 0;
    static uint32_t lastSwipeEndTime = 0;  // Время окончания последнего свайпа / Last swipe end time
    static int16_t lastSwipeEndX = 0;      // Координата X конца последнего свайпа / Last swipe end X
    static int16_t lastSwipeEndY = 0;      // Координата Y конца последнего свайпа / Last swipe end Y
    static int16_t lastProcessedX = 0;
    static int16_t lastProcessedY = 0;
    static TouchPoint touchBuffer[3] = {0}; // Буфер для сглаживания касаний и движений
    static uint8_t bufferIndex = 0;
    static bool modeChangeInProgress = false;
    static uint8_t multiTouchCount = 0;  // Счетчик мультитач-событий

    if (!_checklpdelay(TOUCH_READ_DELAY, _touchdelay)) return;
    
#if TS_MODEL==TS_MODEL_GT911
  ts.read();
#endif
    
  bool istouched = _istouched();
  if(istouched){
    #if TS_MODEL==TS_MODEL_GT911
      TSPoint p = ts.points[0];
      
      // Применяем фильтрацию координат GT911
      if (!_filterGT911Coordinates(p.x, p.y)) {
          // Координаты не прошли фильтр - игнорируем
          return;
      }
      
      // Swap + clip from the current-board contract (legacy loop has no invert).
      // Swap + clip из контракта текущей платы (legacy loop без invert).
      yoradio_touch_swap_and_clip(kYoradioTouchGeometryCurrent, p.x, p.y, &touchX, &touchY);
    #endif
        
        // Буферизация касаний
        touchBuffer[bufferIndex].x = touchX;
        touchBuffer[bufferIndex].y = touchY;
        touchBuffer[bufferIndex].timestamp = millis();
        touchBuffer[bufferIndex].valid = true;
        bufferIndex = (bufferIndex + 1) % 3;
        
        // Вычисляем среднее значение из буфера с весами для GT911
        int32_t avgX = 0, avgY = 0;
        uint8_t validCount = 0;
        uint32_t currentTime = millis();
        
        #if TS_MODEL==TS_MODEL_GT911
        // Для GT911 используем весовую обработку (новые данные важнее)
        for (int i = 0; i < 3; i++) {
            if (touchBuffer[i].valid && (currentTime - touchBuffer[i].timestamp < TOUCH_BUFFER_TIMEOUT)) {
                // Новые данные получают больший вес
                uint8_t weight = (i == ((bufferIndex + 2) % 3)) ? 2 : 1;  // Самые новые данные
                avgX += touchBuffer[i].x * weight;
                avgY += touchBuffer[i].y * weight;
                validCount += weight;
            }
        }
        #else
        // Стандартная обработка для других тачскринов
        for (int i = 0; i < 3; i++) {
            if (touchBuffer[i].valid && (currentTime - touchBuffer[i].timestamp < TOUCH_BUFFER_TIMEOUT)) {
                avgX += touchBuffer[i].x;
                avgY += touchBuffer[i].y;
                validCount++;
            }
        }
        #endif
        
        if (validCount > 0) {
            touchX = avgX / validCount;
            touchY = avgY / validCount;
        }
        
        if (!wastouched) { /*     START TOUCH     */
      if (config.store.dbgtouch) {
        Serial.printf("[TS] START TOUCH: x=%d y=%d\n", touchX, touchY);
      }
      
      // Двухуровневая защита от случайного касания после свайпа
      // Two-level protection against accidental touch after swipe
      if (lastSwipeEndTime > 0) {
        uint32_t timeSinceLastSwipe = millis() - lastSwipeEndTime;
        int16_t distX = abs((int16_t)touchX - lastSwipeEndX);
        int16_t distY = abs((int16_t)touchY - lastSwipeEndY);
        
        // УРОВЕНЬ 1: В течение 400ms - блокируем ВСЕ касания
        // LEVEL 1: Within 400ms - block ALL touches
        bool tooSoon = timeSinceLastSwipe < SWIPE_CLICK_COOLDOWN;
        
        // УРОВЕНЬ 2: В течение 600ms - блокируем БЛИЗКИЕ касания (< 100px)
        // LEVEL 2: Within 600ms - block CLOSE touches (< 100px)
        bool tooClose = (timeSinceLastSwipe < SWIPE_PROXIMITY_TIME) && 
                        (distX < SWIPE_PROXIMITY_DIST) && 
                        (distY < SWIPE_PROXIMITY_DIST);
        
        if (tooSoon || tooClose) {
          if (config.store.dbgtouch) {
            if (tooSoon) {
              Serial.printf("[TS] Touch ignored: %dms after swipe (need %dms)\n", timeSinceLastSwipe, SWIPE_CLICK_COOLDOWN);
            } else {
              Serial.printf("[TS] Touch ignored: too close to swipe end (%dpx, %dpx)\n", distX, distY);
            }
          }
          return;  // Игнорируем касание сразу после свайпа / Ignore touch right after swipe
        }
      }
      
      _oldTouchX = touchX;
      _oldTouchY = touchY;
      direct = TDS_REQUEST;
      touchLongPress=millis();
      swipeStartTime = millis();
      wasSwiped = false;
            // Сброс буфера
            for (int i = 0; i < 3; i++) {
                touchBuffer[i].valid = false;
                touchBuffer[i].movement[0] = 0;
                touchBuffer[i].movement[1] = 0;
            }
            lastProcessedX = touchX;
            lastProcessedY = touchY;
        } else { /*     SWIPE TOUCH     */
            int16_t dX = touchX - lastProcessedX;
            int16_t dY = touchY - lastProcessedY;
            
            // Проверяем минимальное движение
            if (abs(dX) > SWIPE_DEADZONE || abs(dY) > SWIPE_DEADZONE) {
                // Сохраняем движение в буфер
                touchBuffer[bufferIndex].movement[0] = dX;
                touchBuffer[bufferIndex].movement[1] = dY;
                
                // Вычисляем накопленное движение
                int32_t totalX = 0, totalY = 0;
                for (int i = 0; i < 3; i++) {
                    if (touchBuffer[i].valid) {
                        totalX += touchBuffer[i].movement[0];
                        totalY += touchBuffer[i].movement[1];
                    }
                }
                
                if (abs(totalX) > MOVEMENT_THRESHOLD || abs(totalY) > MOVEMENT_THRESHOLD) {
                    if (config.store.dbgtouch) {
                      Serial.printf("[TS] Move: dX=%d dY=%d totalX=%d totalY=%d\n", dX, dY, totalX, totalY);
                    }
                    
                    uint32_t currentTime = millis();
                    if (currentTime - lastSwipeTime >= SWIPE_COOLDOWN) {
      direct = _tsDirection(touchX, touchY);
      
      if (config.store.dbgtouch) {
        const char* dirNames[] = {"STAY", "REQ", "LEFT", "RIGHT", "UP", "DOWN"};
        Serial.printf("[TS] Direction: %s\n", dirNames[direct]);
      }
                        
                        // Используем направление из _tsDirection
      switch (direct) {
        case TSD_LEFT:
        case TSD_RIGHT: {
            wasSwiped = true;
            touchLongPress = millis();
            if(display.mode()==PLAYER || display.mode()==VOL){
              display.putRequest(NEWMODE, VOL);
              // Влево-вправо для изменения громкости / Left-right for volume control
              bool volumeUp = totalX < 0;  // totalX < 0 = влево (громкость ВВЕРХ), totalX > 0 = вправо (громкость ВНИЗ)
              
              if (config.store.dbgtouch) {
                Serial.printf("[TS] Volume: %s (totalY=%d)\n", volumeUp ? "UP" : "DOWN", totalY);
              }
              
              controlsEvent(volumeUp);
              lastProcessedX = touchX;
              lastSwipeTime = currentTime;
            }
            break;
        }
        case TSD_UP:
        case TSD_DOWN: {
            wasSwiped = true;
            touchLongPress = millis();
            if(display.mode()==PLAYER || display.mode()==STATIONS){
                  // Вверх-вниз для выбора станций / Up-down for station selection
                  bool nextStation = totalY < 0;  // totalY < 0 = вверх (следующая станция), totalY > 0 = вниз (предыдущая станция)
                  
                  if (config.store.dbgtouch) {
                    Serial.printf("[TS] Station: %s (totalX=%d totalY=%d)\n", nextStation ? "NEXT" : "PREV", totalX, totalY);
                  }
                  // Block 8-E12A / 8-E19C-1: Main vertical swipe must NOT switch stations (no next/prev).
                  // Stage 6.4 Preset overlay — follow-up; safe no-op here.
                  // Block 8-E12A / 8-E19C-1: вертикальный свайп на Main не меняет станцию.
                  (void)nextStation;
                  if (config.store.dbgtouch) {
                    Serial.println("[TS] Station swipe ignored on LVGL Main (8-E12A no-op)");
                  }
                  lastProcessedY = touchY;
                  lastSwipeTime = currentTime;
            }
            break;
        }
        default:
            break;
      }
    }
                }
            }
    }
    } else {
        if (wastouched) { /* END TOUCH */
            if (config.store.dbgtouch) {
              Serial.printf("[TS] END TOUCH: time=%dms swiped=%d\n", millis() - touchLongPress, wasSwiped);
            }
            
            // Сохраняем время и координаты окончания ТОЛЬКО при свайпе (не при клике)
            // Нужно для защиты от случайных касаний близко к концу свайпа
            // Save swipe end time and coordinates ONLY for swipes (not for clicks)
            // Needed to prevent accidental touches close to swipe end point
            if (wasSwiped) {
                lastSwipeEndTime = millis();
                lastSwipeEndX = lastProcessedX;
                lastSwipeEndY = lastProcessedY;
            }
            
            if (!wasSwiped && direct == TDS_REQUEST) {
                uint32_t pressTicks = millis() - touchLongPress;
                if (pressTicks < BTN_PRESS_TICKS * 2) {
                    if (pressTicks > 50) {
                        if (config.store.dbgtouch) {
                            Serial.printf("[TS] CLICK detected: pressTicks=%dms\n", pressTicks);
                        }
                        onBtnClick(EVT_BTNCENTER);
                    }
                } else {
          lvgl_ui::toggleStationListUiFromProductInput();
        }
      }
      direct = TSD_STAY;
      wasSwiped = false;
            modeChangeInProgress = false;
            
            // Очищаем буфер при отпускании / Clear buffer on release
            for (int i = 0; i < 3; i++) {
                touchBuffer[i].valid = false;
                touchBuffer[i].movement[0] = 0;
                touchBuffer[i].movement[1] = 0;
            }
            bufferIndex = 0;  // Сброс индекса буфера / Reset buffer index
    }
  }
  wastouched = istouched;
}

bool TouchScreen::_checklpdelay(int m, uint32_t &tstamp) {
  if (millis() - tstamp > m) {
    tstamp = millis();
    return true;
  } else {
    return false;
  }
}

bool TouchScreen::_istouched(){
  #if TS_MODEL==TS_MODEL_GT911
    return ts.isTouched;
  #else
    return false;
  #endif
}

tsDirection_e TouchScreen::_tsDirection(uint16_t x, uint16_t y) {
    int16_t dX = x - _oldTouchX;
    int16_t dY = y - _oldTouchY;
    
    #if TS_MODEL==TS_MODEL_GT911
    // Специальная логика для GT911 на квадратном экране
    if (abs(dX) < GT911_SWIPE_DEADZONE && abs(dY) < GT911_SWIPE_DEADZONE) {
        return TDS_REQUEST;
    }
    
    // Для квадратного экрана используем более точную логику
    float angle = atan2(dY, dX) * 180.0 / PI;
    if (angle < 0) angle += 360.0;
    
    // Улучшенное определение направления для квадратного экрана
    if (abs(dX) > GT911_SWIPE_THRESHOLD || abs(dY) > GT911_SWIPE_THRESHOLD) {
        if (angle >= (360 - GT911_SWIPE_ANGLE_THRESHOLD) || angle < GT911_SWIPE_ANGLE_THRESHOLD) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_RIGHT;  // Вправо (0° ± 25°)
        } else if (angle >= (90 - GT911_SWIPE_ANGLE_THRESHOLD) && angle < (90 + GT911_SWIPE_ANGLE_THRESHOLD)) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_DOWN;   // Вниз (90° ± 25°)
        } else if (angle >= (180 - GT911_SWIPE_ANGLE_THRESHOLD) && angle < (180 + GT911_SWIPE_ANGLE_THRESHOLD)) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_LEFT;   // Влево (180° ± 25°)
        } else if (angle >= (270 - GT911_SWIPE_ANGLE_THRESHOLD) && angle < (270 + GT911_SWIPE_ANGLE_THRESHOLD)) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_UP;     // Вверх (270° ± 25°)
        }
    }
    #else
    // Стандартная логика для других тачскринов
    // Проверяем мертвую зону
    if (abs(dX) < SWIPE_DEADZONE && abs(dY) < SWIPE_DEADZONE) {
        return TDS_REQUEST;
    }
    
    // Вычисляем угол движения в градусах
    float angle = atan2(dY, dX) * 180.0 / PI;
    if (angle < 0) angle += 360.0;
    
    // Определяем направление на основе угла
    if (abs(dX) > SWIPE_THRESHOLD || abs(dY) > SWIPE_THRESHOLD) {
        if (angle >= (360 - SWIPE_ANGLE_THRESHOLD) || angle < SWIPE_ANGLE_THRESHOLD) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_DOWN;  // Вправо
        } else if (angle >= (90 - SWIPE_ANGLE_THRESHOLD) && angle < (90 + SWIPE_ANGLE_THRESHOLD)) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_RIGHT;  // Вниз
        } else if (angle >= (180 - SWIPE_ANGLE_THRESHOLD) && angle < (180 + SWIPE_ANGLE_THRESHOLD)) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_UP;  // Влево
        } else if (angle >= (270 - SWIPE_ANGLE_THRESHOLD) && angle < (270 + SWIPE_ANGLE_THRESHOLD)) {
            _oldTouchX = x;
            _oldTouchY = y;
            return TSD_LEFT;  // Вверх
        }
    }
    #endif
    
    return TDS_REQUEST;
}

void TouchScreen::init() {
    const YoradioTouchGeometry& geom = kYoradioTouchGeometryCurrent;
    _width  = geom.logical_width;
    _height = geom.logical_height;
    #if TS_MODEL==TS_MODEL_GT911
        ts.begin();
        ts.setRotation(yoradio_touch_rotation(geom, config.store.fliptouch));
        ts.setResolution(geom.panel_width, geom.panel_height);
    #endif
    _hwReady = true;
}

bool TouchScreen::readPointerForLvgl(uint16_t* outX, uint16_t* outY) {
    if (!outX || !outY || !_hwReady) {
        return false;
    }

#if TS_MODEL==TS_MODEL_GT911
    ts.read();
#endif

    if (!_istouched()) {
        return false;
    }

    uint16_t touchX = 0;
    uint16_t touchY = 0;

#if TS_MODEL==TS_MODEL_GT911
    {
        TSPoint p = ts.points[0];
        if (!_filterGT911Coordinates(p.x, p.y)) {
            return false;
        }
        // LVGL path: swap → clip → invert from the current-board contract.
        // Путь LVGL: swap → clip → invert из контракта текущей платы.
        yoradio_touch_map_for_lvgl(kYoradioTouchGeometryCurrent, p.x, p.y, &touchX, &touchY);
    }
#endif

    *outX = touchX;
    *outY = touchY;
    return true;
}

void TouchScreen::flip() {
    #if TS_MODEL==TS_MODEL_GT911
        ts.setRotation(yoradio_touch_rotation(kYoradioTouchGeometryCurrent, config.store.fliptouch));
    #endif
}

#endif  // TS_MODEL!=TS_MODEL_UNDEFINED
