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
 * - XPT2046 - резистивный тачскрин (SPI)
 * - GT911 - емкостный тачскрин (I2C) - оптимизирован для 480x480
 * - AXS15231B - емкостный тачскрин (I2C)
 * - CST826 - емкостный тачскрин (I2C) - высокое разрешение 4095
 * 
 * Функциональность:
 * - Одиночные касания (короткое/долгое)
 * - Свайпы влево/вправо для управления громкостью
 * - Свайпы вверх/вниз для управления списком станций
 * - Мультитач (два пальца) для переключения режимов
 * - Защиту от случайных касаний
 * - Улучшенную обработку последовательных свайпов
 * - Адаптивные пороги для разных типов тачскринов
 * - Условную компиляцию для каждой модели
 * 
 * Обновлено: октябрь 2025 - добавлена поддержка CST826
 */

#include "options.h"
#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)

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

#if TS_MODEL==TS_MODEL_XPT2046
  #ifdef TS_SPIPINS
    SPIClass  TSSPI(HSPI);
  #endif
  #include <XPT2046_Touchscreen.h>
  XPT2046_Touchscreen ts(TS_CS);
  typedef TS_Point TSPoint;
#elif TS_MODEL==TS_MODEL_GT911
  #include "../GT911_Touchscreen/TAMC_GT911.h"
  TAMC_GT911 ts = TAMC_GT911(TS_SDA, TS_SCL, TS_INT, TS_RST, 480, 480);
  typedef TP_Point TSPoint;
#elif TS_MODEL==TS_MODEL_AXS15231B
  #include "../AXS15231B_touch/AXS15231B_Touch.h"
  AXS15231B_Touch ts = AXS15231B_Touch(TS_SDA, TS_SCL, TS_INT, TS_RST, 0, 0);
  typedef TP_Point TSPoint;
#elif TS_MODEL==TS_MODEL_CST826
  #include "../Adafruit_CST8XX_Library/Adafruit_CST8XX.h"
  Adafruit_CST8XX ts;
  typedef CST_TS_Point TSPoint;
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

// Специальные настройки для GT911 на квадратном экране 480x480
#if TS_MODEL==TS_MODEL_GT911
  // Уменьшенные пороги для высокого разрешения
  #define GT911_SWIPE_THRESHOLD      20    // Было 30, уменьшаем для 480x480
  #define GT911_SWIPE_MIN_DISTANCE   25    // Было 35, оптимизируем для квадратного экрана
  #define GT911_MOVEMENT_THRESHOLD   6     // Было 8, более чувствительно для 480x480
  #define GT911_SWIPE_DEADZONE       2     // Было 3, уменьшаем для точности
  #define GT911_SWIPE_ANGLE_THRESHOLD 25.0 // Было 22.5, увеличиваем для стабильности
#elif TS_MODEL==TS_MODEL_CST826
  // Специальные настройки для CST826 (высокое разрешение 4095)
  #define GT911_SWIPE_THRESHOLD      15    // Еще меньше для высокого разрешения CST826
  #define GT911_SWIPE_MIN_DISTANCE   20    // Уменьшаем минимальное расстояние
  #define GT911_MOVEMENT_THRESHOLD   5     // Более чувствительный порог
  #define GT911_SWIPE_DEADZONE       2     // Маленькая мертвая зона
  #define GT911_SWIPE_ANGLE_THRESHOLD 25.0 // Увеличенный угол для стабильности
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

// Функция фильтрации координат GT911 (как в AXS15231B)
#if TS_MODEL==TS_MODEL_GT911
bool TouchScreen::_filterGT911Coordinates(uint16_t rawX, uint16_t rawY) {
    // Проверяем валидность координат GT911 для экрана 480x480
    // GT911 выдает координаты в диапазоне 0-480, а не 100-3800
    if (rawX > 480 || rawY > 480) {
        // Координаты выходят за пределы экрана - игнорируем
        return false;
    }
    
    // Проверяем на "мертвые" координаты GT911
    if (rawX == 0 && rawY == 0) {
        // Нулевые координаты - игнорируем
        return false;
    }
    
    // Проверяем на подозрительно одинаковые координаты (только если они не в углах)
    if (rawX == rawY && rawX > 10 && rawX < 470) {
        // Возможные некорректные значения в центре экрана - игнорируем
        return false;
    }
    
    return true;
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
#if TS_MODEL==TS_MODEL_AXS15231B
  ts.read();
#endif
#if TS_MODEL==TS_MODEL_CST826
  // CST826 не требует явного вызова read(), данные читаются автоматически
#endif
    
  bool istouched = _istouched();
  if(istouched){
    #if TS_MODEL==TS_MODEL_XPT2046
      TSPoint p = ts.getPoint();
      touchX = map(p.x, TS_X_MIN, TS_X_MAX, 0, _width);
      touchY = map(p.y, TS_Y_MIN, TS_Y_MAX, 0, _height);
    #elif TS_MODEL==TS_MODEL_GT911
      TSPoint p = ts.points[0];
      
      // Применяем фильтрацию координат GT911
      if (!_filterGT911Coordinates(p.x, p.y)) {
          // Координаты не прошли фильтр - игнорируем
          return;
      }
      
      // Маппирование координат GT911 (координаты уже в диапазоне 0-480)
      // GT911 выдает координаты напрямую, маппирование не требуется
      touchX = p.y;  // Оси перепутаны, но координаты уже правильные
      touchY = p.x;
      
      // Дополнительная проверка границ экрана
      if (touchX >= _width) touchX = _width - 1;
      if (touchY >= _height) touchY = _height - 1;
      if (touchX < 0) touchX = 0;
      if (touchY < 0) touchY = 0;
    #elif TS_MODEL==TS_MODEL_CST826
      TSPoint p = ts.getPoint(0);
      
      if (config.store.dbgtouch) {
        Serial.printf("[CST826] Raw touch: x=%d, y=%d, event=%d\n", p.x, p.y, p.event);
      }
      
      // Проверяем событие касания (PRESS или TOUCHING)
      if (p.event != PRESS && p.event != TOUCHING) {
          return;
      }
      
      // Маппирование координат CST826 (координаты в диапазоне 0-4095)
      // Прямое маппирование осей для правильного управления
      touchX = map(p.x, 0, 4095, 0, _width);    // X сенсора -> X экрана (громкость, слева-направо)
      touchY = map(p.y, 0, 4095, 0, _height);   // Y сенсора -> Y экрана (станции, сверху-вниз)
      
      // Дополнительная проверка границ экрана
      if (touchX >= _width) touchX = _width - 1;
      if (touchY >= _height) touchY = _height - 1;
      if (touchX < 0) touchX = 0;
      if (touchY < 0) touchY = 0;
      
      if (config.store.dbgtouch) {
        Serial.printf("[CST826] Mapped touch: x=%d, y=%d\n", touchX, touchY);
      }
      
      // Обработка мультитача для CST826 (поддержка до 5 касаний)
      // Мультитач используется для переключения между WEB и SD режимами
      // Multitouch is used to switch between WEB and SD modes
      #if SDC_CS != 255
      uint8_t touches = ts.touched();
      if (touches > 1) {
                uint32_t currentTime = millis();
                if (currentTime - lastMultiTouchTime > TOUCH_MULTI_DELAY) {
                    multiTouchCount++;
                    
                    if (multiTouchCount >= 2) {
        wasTwoFingerTouch = true;
                        lastMultiTouchTime = currentTime;
                        multiTouchCount = 0;
                        
                        bool pir = player.isRunning();
                        
                        if(config.getMode()==PM_SDCARD) {
                            config.sdResumePos = player.getFilePos();
                        }
                        
                        if (display.mode() == PLAYER && !modeChangeInProgress) {
                            modeChangeInProgress = true;
                            // Block 8-E8: WEB/SD mode switch without SDCHANGE legacy UI.
                            config.changeMode();
                            delay(TOUCH_MODE_DELAY);
                            if (pir) {
                                player.sendCommand({PR_PLAY, config.getMode()==PM_WEB?config.store.lastStation:config.store.lastSdStation});
                            }
                            modeChangeInProgress = false;
                        }
                        
        return;
                    }
                } else {
                    if (millis() - lastMultiTouchTime > TOUCH_MULTI_DELAY) {
                        wasTwoFingerTouch = false;
                        multiTouchCount = 0;
                    }
                }
      }
      #endif
    #elif TS_MODEL==TS_MODEL_AXS15231B
      TSPoint p = ts.points[0];
      touchX = p.x;
      touchY = p.y;
      
      if (config.store.dbgtouch) {
        Serial.printf("[AXS15231B] Raw: x=%d y=%d touches=%d\n", touchX, touchY, ts.touches);
      }
      
            // Обработка мультитача для AXS15231B (поддержка до 2 касаний)
            // Мультитач используется для переключения между WEB и SD режимами
            // Multitouch is used to switch between WEB and SD modes
      #if SDC_CS != 255
      if (ts.touches > 1) {
                uint32_t currentTime = millis();
                if (currentTime - lastMultiTouchTime > TOUCH_MULTI_DELAY) {
                    multiTouchCount++;
                    
                    if (multiTouchCount >= 2) {
        wasTwoFingerTouch = true;
                        lastMultiTouchTime = currentTime;
                        multiTouchCount = 0;
                        
                        bool pir = player.isRunning();
                        
                        if(config.getMode()==PM_SDCARD) {
                            config.sdResumePos = player.getFilePos();
                        }
                        
                        if (display.mode() == PLAYER && !modeChangeInProgress) {
                            modeChangeInProgress = true;
                            // Block 8-E8: WEB/SD mode switch without SDCHANGE legacy UI.
                            config.changeMode();
                            delay(TOUCH_MODE_DELAY);
                            if (pir) {
                                player.sendCommand({PR_PLAY, config.getMode()==PM_WEB?config.store.lastStation:config.store.lastSdStation});
                            }
                            modeChangeInProgress = false;
                        }
                        
        return;
                    }
                } else {
                    if (millis() - lastMultiTouchTime > TOUCH_MULTI_DELAY) {
                        wasTwoFingerTouch = false;
                        multiTouchCount = 0;
                    }
                }
      }
      #endif
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
              #if TS_MODEL==TS_MODEL_CST826
              bool volumeUp = totalX > 0;  // CST826: totalX > 0 = вправо (громкость ВВЕРХ), totalX < 0 = влево (громкость ВНИЗ)
              #elif TS_MODEL==TS_MODEL_AXS15231B
              bool volumeUp = totalY > 0;  // AXS15231B: используем totalY (горизонталь после swap) для громкости
              #else
              bool volumeUp = totalX < 0;  // Другие модели: totalX < 0 = влево (громкость ВВЕРХ), totalX > 0 = вправо (громкость ВНИЗ)
              #endif
              
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
                  #if TS_MODEL==TS_MODEL_AXS15231B
                  bool nextStation = totalX < 0;  // AXS15231B: после swap используем totalX (вертикаль)
                  #else
                  bool nextStation = totalY < 0;  // totalY < 0 = вверх (следующая станция), totalY > 0 = вниз (предыдущая станция)
                  #endif
                  
                  if (config.store.dbgtouch) {
                    Serial.printf("[TS] Station: %s (totalX=%d totalY=%d)\n", nextStation ? "NEXT" : "PREV", totalX, totalY);
                  }
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
                  // Block 8-E12A: Main vertical swipe must NOT switch stations (no next/prev, no Station Page).
                  // Stage 6.4 Preset / fixed-stations overlay is not in this tree — follow-up; safe no-op here.
                  // Block 8-E12A: вертикальный свайп на Main не меняет станцию; Preset overlay — позже.
                  (void)nextStation;
                  if (config.store.dbgtouch) {
                    Serial.println("[TS] Station swipe ignored on LVGL Main (8-E12A no-op)");
                  }
#else
                  display.putRequest(NEWMODE, STATIONS);
                  controlsEvent(nextStation);
#endif
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
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
          lvgl_ui::toggleStationListUiFromProductInput();
#else
          display.putRequest(NEWMODE, display.mode() == PLAYER ? STATIONS : PLAYER);
#endif
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
  #if TS_MODEL==TS_MODEL_XPT2046
    return ts.touched();
  #elif TS_MODEL==TS_MODEL_GT911
    return ts.isTouched;
  #elif TS_MODEL==TS_MODEL_AXS15231B
    return ts.isTouched;
  #elif TS_MODEL==TS_MODEL_CST826
    return ts.touched() > 0;
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
    #elif TS_MODEL==TS_MODEL_CST826
    // Специальная логика для CST826 на квадратном экране (использует ту же логику что и GT911)
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
    #if TS_MODEL==TS_MODEL_XPT2046
        #ifdef TS_SPIPINS
            TSSPI.begin(TS_SPIPINS);
            ts.begin(TSSPI);
        #else
            #if TS_HSPI
                ts.begin(SPI2);
            #else
                ts.begin();
            #endif
        #endif
        ts.setRotation(config.store.fliptouch?3:1);
    #endif
    #if TS_MODEL==TS_MODEL_GT911
        ts.begin();
        ts.setRotation(config.store.fliptouch?0:2);
        ts.setResolution(_width, _height);
        
        // Автоматическая настройка порогов для квадратного экрана
        if (_width == _height && _width >= 480) {
            // Для квадратного экрана 480x480 и больше используем оптимизированные настройки
            // Эти настройки уже определены в константах выше
        }
    #endif
    #if TS_MODEL==TS_MODEL_AXS15231B
        ts.begin();
        ts.setRotation(config.store.fliptouch?0:2);
    #endif
    #if TS_MODEL==TS_MODEL_CST826
        Wire.begin(TS_SDA, TS_SCL);
        if (!ts.begin(&Wire, 0x15)) {
            if (config.store.dbgtouch) {
              Serial.println("[CST826] Touchscreen not found!");
            } else {
              Serial.println("Touchscreen not found!");
            }
        } else {
            if (config.store.dbgtouch) {
              Serial.println("[CST826] Touchscreen initialized successfully");
            }
        }
    #endif
    _width  = dsp.width();
    _height = dsp.height();
    #if TS_MODEL==TS_MODEL_GT911
        ts.setResolution(_width, _height);
    #endif
    #if TS_MODEL==TS_MODEL_AXS15231B
        ts.setResolution(_width, _height);
    #endif
    _hwReady = true;
}

bool TouchScreen::readPointerForLvgl(uint16_t* outX, uint16_t* outY) {
    if (!outX || !outY || !_hwReady) {
        return false;
    }

#if TS_MODEL==TS_MODEL_GT911
    ts.read();
#elif TS_MODEL==TS_MODEL_AXS15231B
    ts.read();
#endif

    if (!_istouched()) {
        return false;
    }

    uint16_t touchX = 0;
    uint16_t touchY = 0;

#if TS_MODEL==TS_MODEL_XPT2046
    {
        TSPoint p = ts.getPoint();
        touchX = static_cast<uint16_t>(map(p.x, TS_X_MIN, TS_X_MAX, 0, _width));
        touchY = static_cast<uint16_t>(map(p.y, TS_Y_MIN, TS_Y_MAX, 0, _height));
    }
#elif TS_MODEL==TS_MODEL_GT911
    {
        TSPoint p = ts.points[0];
        if (!_filterGT911Coordinates(p.x, p.y)) {
            return false;
        }
        touchX = p.y;
        touchY = p.x;
        if (touchX >= _width) {
            touchX = _width - 1;
        }
        if (touchY >= _height) {
            touchY = _height - 1;
        }
    }
#elif TS_MODEL==TS_MODEL_CST826
    {
        TSPoint p = ts.getPoint(0);
        if (p.event != PRESS && p.event != TOUCHING) {
            return false;
        }
        touchX = static_cast<uint16_t>(map(p.x, 0, 4095, 0, _width));
        touchY = static_cast<uint16_t>(map(p.y, 0, 4095, 0, _height));
        if (touchX >= _width) {
            touchX = _width - 1;
        }
        if (touchY >= _height) {
            touchY = _height - 1;
        }
    }
#elif TS_MODEL==TS_MODEL_AXS15231B
    {
        TSPoint p = ts.points[0];
        touchX = p.x;
        touchY = p.y;
    }
#endif

    *outX = touchX;
    *outY = touchY;
    return true;
}

void TouchScreen::flip() {
    #if TS_MODEL==TS_MODEL_XPT2046
        ts.setRotation(config.store.fliptouch?3:1);
    #endif
    #if TS_MODEL==TS_MODEL_GT911
        ts.setRotation(config.store.fliptouch?0:2);
    #endif
    #if TS_MODEL==TS_MODEL_AXS15231B
        ts.setRotation(config.store.fliptouch?0:2);
    #endif
    #if TS_MODEL==TS_MODEL_CST826
        // CST826 не поддерживает программный поворот через API
        // Поворот обрабатывается на уровне координат в loop()
        if (config.store.dbgtouch) {
          Serial.println("[CST826] Flip requested - handled in coordinate processing");
        }
    #endif
}

#endif  // TS_MODEL!=TS_MODEL_UNDEFINED
