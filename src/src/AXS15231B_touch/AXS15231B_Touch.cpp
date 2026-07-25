/**
 * @file AXS15231B_Touch.cpp
 * @brief Touch driver for AXS15231B
 * @brief Драйвер для сенсорного экрана AXS15231B
 * Adaptation: Witaliy76 - https://github.com/Witaliy76
 * @version 1.0
 * @date 2024
 * 
 * Реализация драйвера для работы с сенсорным экраном AXS15231B
 * Поддерживает:
 * - Чтение координат касания
 * - Настройку разрешения экрана
 * - Поворот координат
 * - Мультитач (до 2 одновременных касаний)
 * - Определение жестов двумя пальцами
 */

#include "AXS15231B_Touch.h"

// I2C адрес сенсорного экрана
#define AXS15231B_I2C_ADDR 0x3B
// Максимальное количество одновременных касаний
#define AXS_MAX_TOUCH_NUMBER 2  // Поддержка до 2 одновременных касаний для жестов

// Константы для валидации координат / Constants for coordinate validation
#define AXS_INVALID_COORD       273   // Невалидное значение координаты от сенсора / Invalid coordinate value from sensor
#define AXS_MAX_RAW_COORD      4095   // Максимальное 12-bit значение / Maximum 12-bit value
#define AXS_VALID_THRESHOLD    4000   // Порог валидности (отсекаем шум > 4000) / Validity threshold (filter noise > 4000)

/**
 * @brief Конструктор класса AXS15231B_Touch
 * @param _sda Пин для линии SDA I2C
 * @param _scl Пин для линии SCL I2C
 * @param _int Пин прерывания
 * @param _rst Пин сброса (опционально)
 * @param _width Ширина экрана
 * @param _height Высота экрана
 */
AXS15231B_Touch::AXS15231B_Touch(uint8_t _sda, uint8_t _scl, uint8_t _int, int8_t _rst, uint16_t _width, uint16_t _height)
  : pinSda(_sda), pinScl(_scl), pinInt(_int), pinRst(_rst), width(_width), height(_height), rotation(0) {}

/**
 * @brief Инициализация сенсорного экрана
 * Настраивает I2C интерфейс и пины управления
 */
void AXS15231B_Touch::begin() {
  Wire.begin(pinSda, pinScl);
  // TODO: инициализация INT/RST если нужно
}

/**
 * @brief Установка поворота координат
 * @param rot Угол поворота (0, 90, 180, 270 градусов)
 */
void AXS15231B_Touch::setRotation(uint8_t rot) {
  rotation = rot;
  // TODO: обработка поворота координат
}

/**
 * @brief Установка разрешения экрана
 * @param _width Ширина экрана
 * @param _height Высота экрана
 */
void AXS15231B_Touch::setResolution(uint16_t _width, uint16_t _height) {
  width = _width;
  height = _height;
}

/**
 * @brief Проверка валидности сырых координат
 * @param x Сырая координата X
 * @param y Сырая координата Y
 * @return true если координаты валидны, false если нет
 * 
 * ЛОГИКА:
 * - Отклоняем если ОБЕ координаты == 273 (невалидное значение сенсора)
 * - Отклоняем если ХОТЯ БЫ ОДНА > 4000 (шум/выход за пределы)
 */
inline bool isValidRawCoord(uint16_t x, uint16_t y) {
    // Проверка на невалидное значение сенсора (обе координаты 273)
    if (x == AXS_INVALID_COORD && y == AXS_INVALID_COORD) {
        return false;
    }
    // Проверка на шум (хотя бы одна координата > 4000)
    if (x > AXS_VALID_THRESHOLD || y > AXS_VALID_THRESHOLD) {
        return false;
    }
    return true;
}

/**
 * @brief Обработка одного касания (swap осей + обрезка)
 * @param id Идентификатор касания (0 или 1)
 * @param rawX Сырая координата X (0-4095)
 * @param rawY Сырая координата Y (0-4095)
 * @return Обработанная точка касания
 */
inline TP_Point processTouch(uint8_t id, uint16_t rawX, uint16_t rawY) {
    // Swap X and Y axes / Меняем оси X и Y местами
    uint16_t x = rawY;
    uint16_t y = rawX;
    
    // Boundary check / Проверка границ
    if (x > AXS_MAX_RAW_COORD) x = AXS_MAX_RAW_COORD;
    if (y > AXS_MAX_RAW_COORD) y = AXS_MAX_RAW_COORD;
    
    return TP_Point(id, x, y, (uint8_t)0);
}

/**
 * @brief Чтение данных с сенсорного экрана
 * Считывает координаты касания через I2C интерфейс
 * Обрабатывает сырые данные и преобразует их в координаты экрана
 * Поддерживает определение до 2 одновременных касаний
 * Используется для реализации жестов двумя пальцами
 */
void AXS15231B_Touch::read() {
  // Буфер для чтения данных с сенсора
  uint8_t data[AXS_MAX_TOUCH_NUMBER * 6 + 2] = {0};
  
  // Команда чтения данных с сенсора
  const uint8_t read_cmd[11] = {
    0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00,
    (uint8_t)((AXS_MAX_TOUCH_NUMBER * 6 + 2) >> 8),
    (uint8_t)((AXS_MAX_TOUCH_NUMBER * 6 + 2) & 0xff),
    0x00, 0x00, 0x00
  };

  // Отправка команды чтения
  Wire.beginTransmission(AXS15231B_I2C_ADDR);
  Wire.write(read_cmd, 11);
  if (Wire.endTransmission() != 0) {
    isTouched = false;
    touches = 0;
    return;
  }

  // Запрос данных с сенсора
  if (Wire.requestFrom(AXS15231B_I2C_ADDR, (uint8_t)sizeof(data)) != sizeof(data)) {
    isTouched = false;
    touches = 0;
    return;
  }

  // Чтение данных
  for (int i = 0; i < (int)sizeof(data); i++) {
    data[i] = Wire.read();
  }

  // Обработка данных касания
  touches = data[1];
  if (touches > 0 && touches <= AXS_MAX_TOUCH_NUMBER) {
    isTouched = true;
    
    // Обработка первого касания / Processing first touch
    uint16_t rawX = ((data[2] & 0x0F) << 8) | data[3];
    uint16_t rawY = ((data[4] & 0x0F) << 8) | data[5];
    
    // Проверка валидности координат / Validate coordinates
    if (!isValidRawCoord(rawX, rawY)) {
      isTouched = false;
      touches = 0;
      return;
    }

    // Обработка касания (swap осей + обрезка) / Process touch (swap axes + clipping)
    points[0] = processTouch(0, rawX, rawY);

    // Обработка второго касания, если есть / Processing second touch if exists
    if (touches > 1) {
      uint16_t rawX2 = ((data[8] & 0x0F) << 8) | data[9];
      uint16_t rawY2 = ((data[10] & 0x0F) << 8) | data[11];
      
      // Проверка валидности второго касания / Validate second touch
      if (isValidRawCoord(rawX2, rawY2)) {
        points[1] = processTouch(1, rawX2, rawY2);
      } else {
        // Если второе касание некорректное, считаем что касание одно
        // If second touch is invalid, count as single touch
        touches = 1;
      }
    }
  } else {
    isTouched = false;
    touches = 0;
  }
} 