// Block 8-E18D: runtime disabled at call sites (Display/main/player); TU kept for future LVGL widget.
// Block 8-E18D: runtime отключён в Display/main/player; TU — для будущего LVGL-виджета.

#include "spectrum_analyzer.h"
#include "../../core/options.h"
#include "esp_heap_caps.h"
#include <math.h>

// Глобальный экземпляр (no active callers on LVGL product path after 8-E18D)
SpectrumAnalyzer spectrumAnalyzer;

// Улучшенная реализация спектр-анализатора с имитацией FFT
// Оптимизирована для RGB Panel ESP32-4848S040
// Использует частотное разделение и адаптивное масштабирование

SpectrumAnalyzer::SpectrumAnalyzer() {
    spectrum = nullptr;
    peakHold = nullptr;
    peakTime = nullptr;
    prevSpectrum = nullptr;
    initialized = false;
    dataMutex = nullptr;
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
    if (spectrum) free(spectrum);
    if (peakHold) free(peakHold);
    if (peakTime) free(peakTime);
    if (prevSpectrum) free(prevSpectrum);
    if (dataMutex) vSemaphoreDelete(dataMutex);
}

bool SpectrumAnalyzer::init() {
    // Инициализация для новой библиотеки audioI2S (48кГц, 16-bit, stereo)
    config.numBands = SPECTRUM_BANDS;
    config.fftSize = SPECTRUM_FFT_SIZE;
    config.sampleRate = 48000; // Новая библиотека ресемплирует до 48кГц
    config.smoothing = SPECTRUM_SMOOTHING;
    config.peakHoldTime = SPECTRUM_PEAK_HOLD_TIME;
    config.logarithmic = SPECTRUM_LOGARITHMIC;
    config.stereo = SPECTRUM_STEREO;
    config.gain = SPECTRUM_GAIN; // Общее усиление из макроса
    
    return begin();
}

bool SpectrumAnalyzer::begin() {
    // Проверяем наличие PSRAM
    if (!psramFound()) {
        Serial.println("[Spectrum] ERROR: PSRAM not found! Spectrum analyzer requires PSRAM.");
        return false;
    }
    
    Serial.printf("[Spectrum] Initializing simple mono analyzer with %d bands...\n", config.numBands);
    
    // Создаем мьютекс для защиты данных
    dataMutex = xSemaphoreCreateMutex();
    if (!dataMutex) {
        Serial.println("[Spectrum] ERROR: Failed to create data mutex!");
        return false;
    }
    
    // Выделяем память в PSRAM только для спектра
    spectrum = (float*)ps_malloc(config.numBands * sizeof(float));
    peakHold = (float*)ps_malloc(config.numBands * sizeof(float));
    peakTime = (uint32_t*)ps_malloc(config.numBands * sizeof(uint32_t));
    prevSpectrum = (float*)ps_malloc(config.numBands * sizeof(float));
    
    // Выделение памяти для динамического масштабирования
    adaptiveScale = (float*)ps_malloc(config.numBands * sizeof(float));
    maxValues = (float*)ps_malloc(config.numBands * sizeof(float));
    minValues = (float*)ps_malloc(config.numBands * sizeof(float));
    
    // Проверяем выделение памяти
    if (!spectrum || !peakHold || !peakTime || !prevSpectrum || !adaptiveScale || !maxValues || !minValues) {
        Serial.println("[Spectrum] ERROR: Failed to allocate memory in PSRAM!");
        if (spectrum) free(spectrum);
        if (peakHold) free(peakHold);
        if (peakTime) free(peakTime);
        if (prevSpectrum) free(prevSpectrum);
        if (adaptiveScale) free(adaptiveScale);
        if (maxValues) free(maxValues);
        if (minValues) free(minValues);
        if (dataMutex) vSemaphoreDelete(dataMutex);
        spectrum = nullptr;
        peakHold = nullptr;
        peakTime = nullptr;
        prevSpectrum = nullptr;
        adaptiveScale = nullptr;
        maxValues = nullptr;
        minValues = nullptr;
        dataMutex = nullptr;
        return false;
    }
    
    // Инициализация состояния
    sampleCount = 0;
    lastFFTTime = 0;
    debugTime = 0;
    
    // Инициализация массивов
    for (uint8_t i = 0; i < config.numBands; i++) {
        spectrum[i] = 0.0f;
        peakHold[i] = 0.0f;
        peakTime[i] = 0;
        prevSpectrum[i] = 0.0f;
        adaptiveScale[i] = 1.0f;
        maxValues[i] = 0.1f; // Начинаем с небольшого значения
        minValues[i] = 0.0f; // Начинаем с нуля
    }
    
    // Веса для разных частотных полос (улучшены для независимости)
    float weights[15] = {
        1.0f, 1.3f, 1.6f, 1.9f, 2.1f,  // Низкие частоты (бас) - более плавный переход
        2.3f, 2.6f, 2.8f, 2.6f, 2.3f,  // Средние частоты (вокал) - более плавный переход
        2.0f, 1.7f, 1.4f, 1.1f, 0.9f   // Высокие частоты (писк) - более плавный переход
    };
    
    Serial.printf("[Spectrum] Enhanced RGB Panel analyzer initialized with %d bands\n", config.numBands);
    
    initialized = true;
    return true;
}

void SpectrumAnalyzer::reset() {
    if (dataMutex) {
        vSemaphoreDelete(dataMutex);
        dataMutex = nullptr;
    }
    
    if (spectrum) free(spectrum);
    if (peakHold) free(peakHold);
    if (peakTime) free(peakTime);
    if (prevSpectrum) free(prevSpectrum);
    if (adaptiveScale) free(adaptiveScale);
    if (maxValues) free(maxValues);
    if (minValues) free(minValues);
    
    spectrum = nullptr;
    peakHold = nullptr;
    peakTime = nullptr;
    prevSpectrum = nullptr;
    adaptiveScale = nullptr;
    maxValues = nullptr;
    minValues = nullptr;
    
    initialized = false;
}

void SpectrumAnalyzer::clearData() {
    if (!initialized || !spectrum || !dataMutex) {
        return;
    }
    
    // Захватываем мьютекс для записи данных
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // Не можем получить мьютекс
    }
    
    // Сбрасываем все данные спектра
    for (uint8_t i = 0; i < config.numBands; i++) {
        spectrum[i] = 0.0f;
        peakHold[i] = 0.0f;
        peakTime[i] = 0;
        prevSpectrum[i] = 0.0f;
        adaptiveScale[i] = 1.0f;
        maxValues[i] = 0.1f; // Начинаем с небольшого значения
        minValues[i] = 0.0f; // Начинаем с нуля
    }
    
    // Сбрасываем счетчики
    sampleCount = 0;
    lastFFTTime = 0;
    
    // Освобождаем мьютекс
    xSemaphoreGive(dataMutex);
    
    Serial.println("[Spectrum] Data cleared");
}

void SpectrumAnalyzer::processAudio(const int16_t* samples, int32_t count) {
    if (!initialized || !spectrum || !dataMutex) {
        return;
    }

    // Улучшенная реализация с имитацией FFT для RGB Panel
    // Используем RMS (Root Mean Square) с частотным разделением
    // Оптимизировано для 480x480 разрешения и плавной анимации
    // Новая библиотека audioI2S обеспечивает 48кГц, 16-bit, stereo данные
    
    uint32_t currentTime = millis();
    
    // Обновляем спектр каждые 40мс для более плавной анимации на RGB Panel
    if (currentTime - lastFFTTime < 40) {
        return;
    }
    
    // Захватываем мьютекс для записи данных
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // Не можем получить мьютекс, пропускаем обновление
    }
    
    // Явная инициализация массива spectrum
    for (uint8_t i = 0; i < config.numBands; i++) {
        spectrum[i] = 0.0f;
    }
    
    // Улучшенный анализ моно сигнала с частотным разделением
    float rms = 0.0f;
    float maxSample = 0.0f;
    float lowFreq = 0.0f;    // Низкие частоты (0-200 Hz)
    float midFreq = 0.0f;    // Средние частоты (200-2000 Hz)
    float highFreq = 0.0f;   // Высокие частоты (2000+ Hz)
    
    // Вычисляем RMS и максимальное значение с частотным разделением
    for (int32_t i = 0; i < count; i++) {
        float sample = samples[i] / 32768.0f; // Нормализация к [-1, 1]
        rms += sample * sample;
        
        // Умное частотное разделение на основе индекса сэмпла с дополнительной случайностью
        // Оптимизировано для RGB Panel и лучшего визуального эффекта
        float freqRandom = (float)((i * 12345) % 100) / 100.0f; // Случайность для каждой полосы
        
        if (i < count / 3) {
            lowFreq += sample * sample * (0.8f + freqRandom * 0.4f);  // Первая треть - низкие частоты (бас)
        } else if (i < 2 * count / 3) {
            midFreq += sample * sample * (0.9f + freqRandom * 0.2f);  // Вторая треть - средние частоты (вокал)
        } else {
            highFreq += sample * sample * (0.7f + freqRandom * 0.6f); // Последняя треть - высокие частоты (писк)
        }
        
        if (abs(sample) > maxSample) {
            maxSample = abs(sample);
        }
    }
    
    rms = sqrt(rms / count);
    lowFreq = sqrt(lowFreq / (count / 3));
    midFreq = sqrt(midFreq / (count / 3));
    highFreq = sqrt(highFreq / (count / 3));
    
    // Динамическое масштабирование в зависимости от уровня сигнала
    float dynamicGain = 1.5f; // Уменьшаем базовое усиление еще больше
    if (rms > 0.1f) {
        dynamicGain = 0.8f; // Уменьшаем усиление для громких сигналов
    } else if (rms > 0.05f) {
        dynamicGain = 1.0f; // Уменьшаем усиление для средних сигналов
    } else if (rms > 0.01f) {
        dynamicGain = 1.3f; // Уменьшаем усиление для тихих сигналов
    } else {
        dynamicGain = 1.6f; // Уменьшаем усиление для очень тихих сигналов
    }
    
    // Гарантируем, что не выйдем за пределы массива weights
    if (config.numBands > 15) config.numBands = 15;
    float weights[15] = {
        1.0f, 1.3f, 1.6f, 1.9f, 2.1f,  // Низкие частоты (бас) - более плавный переход
        2.3f, 2.6f, 2.8f, 2.6f, 2.3f,  // Средние частоты (вокал) - более плавный переход
        2.0f, 1.7f, 1.4f, 1.1f, 0.9f   // Высокие частоты (писк) - более плавный переход
    };
    
    // Улучшенное распределение значений по полосам с использованием частотного анализа
    static uint32_t seed = 12345; // Начинаем с ненулевого значения
    for (uint8_t i = 0; i < config.numBands; i++) {
        float weight = (i < 15) ? weights[i] : 1.0f;
        float baseValue;
        
        // Умное распределение значений на основе частотного анализа
        // Оптимизировано для RGB Panel 480x480 и лучшего визуального восприятия
        if (i < 5) {
            // Низкие частоты (бас) - используем lowFreq с независимым усилением
            baseValue = lowFreq * weight * (0.8f + (i * 0.1f)); // Разное усиление для каждой полосы
        } else if (i < 10) {
            // Средние частоты (вокал) - используем midFreq с независимым усилением
            baseValue = midFreq * weight * (1.0f + ((i - 5) * 0.08f)); // Разное усиление для каждой полосы
        } else {
            // Высокие частоты - используем highFreq с независимым усилением
            baseValue = highFreq * weight * (0.7f + ((i - 10) * 0.06f)); // Разное усиление для каждой полосы
        }
        
        // Добавляем реалистичные вариации для каждого столбика
        // Оптимизировано для RGB Panel - больше независимости между полосами
        seed = seed * 1103515245 + 12345 + i; // Разный seed для каждого столбика
        float randomFactor = (float)(seed % 100) / 100.0f * 0.5f + 0.5f; // Увеличиваем случайность для независимости
        float newValue = baseValue * randomFactor;
        
        // Добавляем дополнительную независимость для каждой полосы
        float independenceFactor = 0.8f + (float)(seed % 40) / 100.0f; // 0.8 - 1.2
        newValue *= independenceFactor;
        
        // МУЗЫКАЛЬНЫЕ ЭФФЕКТЫ для RGB Panel - добавляем волнообразные движения
        float time = (float)currentTime / 1000.0f; // Время в секундах
        float frequency = 2.0f + (i * 0.3f); // Разная частота для каждой полосы
        float wave = sin(time * frequency + i * 0.5f) * 0.1f; // Волна с амплитудой 10%
        newValue *= (1.0f + wave); // Применяем волну
        
        // Гармонические связи между соседними полосами для музыкальности
        if (i > 0 && i < config.numBands - 1) {
            float neighborInfluence = (spectrum[i-1] + spectrum[i+1]) * 0.05f; // 5% влияние соседей
            newValue += neighborInfluence;
        }
        
        // Применяем динамическое усиление
        newValue *= dynamicGain;
        
        // Применяем общее усиление
        newValue *= config.gain;
        
        // Защита от NaN и Inf
        if (isnan(newValue) || isinf(newValue)) {
            newValue = 0.0f;
        }
        
        spectrum[i] = newValue;
        
        // ОПТИМИЗИРОВАННОЕ ДИНАМИЧЕСКОЕ МАСШТАБИРОВАНИЕ для RGB Panel 480x480
        if (spectrum[i] > 0.0f) {
            // Обновляем максимальные и минимальные значения для этой полосы
            // Критично для стабильности отображения на RGB Panel
            if (spectrum[i] > maxValues[i]) {
                maxValues[i] = spectrum[i];
            }
            if (spectrum[i] < minValues[i] || minValues[i] == 1.0f) {
                minValues[i] = spectrum[i];
            }
            
            // Вычисляем адаптивный масштаб для этой полосы
            float range = maxValues[i] - minValues[i];
            if (range > 0.001f) { // Минимальный диапазон для избежания деления на ноль
                // Нормализуем значение к диапазону [0, 1] для этой полосы
                float normalized = (spectrum[i] - minValues[i]) / range;
                
                // Применяем оптимизированную кривую для RGB Panel 480x480
                normalized = pow(normalized, 0.6f); // Более чувствительная кривая для лучшей видимости
                
                // Оптимизируем диапазон для 480x480 разрешения RGB Panel
                normalized *= 0.9f; // Уменьшаем диапазон для стабильности АРУ
                
                // Ограничиваем значения
                if (normalized < 0.0f) normalized = 0.0f;
                if (normalized > 1.0f) normalized = 1.0f;
                
                spectrum[i] = normalized;
            } else {
                // Если диапазон слишком мал, используем оптимизированное масштабирование
                // Специально настроено для RGB Panel 480x480
                spectrum[i] = spectrum[i] * 1.1f; // Минимальное усиление для стабильности АРУ
                if (spectrum[i] > 1.0f) spectrum[i] = 1.0f;
            }
        }
        
        // ОПТИМИЗИРОВАННЫЕ ПАРАМЕТРЫ ЗАТУХАНИЯ для RGB Panel 480x480
        // Более мягкое затухание максимальных значений для стабильности АРУ
        maxValues[i] *= 0.995f; // Более мягкое затухание для стабильности
        if (maxValues[i] < 0.005f) maxValues[i] = 0.005f; // Более низкий порог для стабильности
        
        // Более мягкое увеличение минимальных значений
        minValues[i] *= 1.004f; // Более мягкое увеличение для стабильности
        if (minValues[i] > 0.10f) minValues[i] = 0.10f; // Более низкий порог для RGB Panel
        
        // УМНЫЕ АЛГОРИТМЫ СЖАТИЯ для разных уровней громкости
        // Более мягкое сжатие для стабильности АРУ
        if (rms < 0.02f) { // Очень тихие сигналы
            maxValues[i] *= 0.96f; // Более мягкое затухание для стабильности
            minValues[i] *= 1.015f; // Более мягкое увеличение для стабильности
        } else if (rms < 0.05f) { // Тихие сигналы
            maxValues[i] *= 0.98f; // Более мягкое затухание
            minValues[i] *= 1.008f; // Более мягкое увеличение
        } else if (rms > 0.15f) { // Громкие сигналы
            maxValues[i] *= 0.999f; // Очень медленное затухание для стабильности
            minValues[i] *= 1.001f; // Очень медленное увеличение
        }
        
        // АДАПТИВНОЕ СЖАТИЕ для RGB Panel
        // Более мягкое сжатие для стабильности АРУ
        static uint32_t lastCompressionTime = 0;
        if (currentTime - lastCompressionTime > 2000) { // Каждые 2 секунды для стабильности
            lastCompressionTime = currentTime;
            // Более мягкое сжатие в зависимости от активности
            float compressionFactor = (rms > 0.1f) ? 0.95f : 0.92f; // Более мягкое сжатие для стабильности
            maxValues[i] *= compressionFactor;
            minValues[i] *= (2.0f - compressionFactor); // Компенсируем сжатие
        }
        
        // ОПТИМИЗИРОВАННОЕ СГЛАЖИВАНИЕ для RGB Panel 480x480
        spectrum[i] = spectrum[i] * 0.70f + prevSpectrum[i] * 0.30f; // Увеличенное сглаживание для стабильности
        
        // Защита от NaN и Inf
        if (isnan(spectrum[i]) || isinf(spectrum[i])) {
            spectrum[i] = 0.0f;
        }
        
        // Ограничиваем значения
        if (spectrum[i] < 0.0f) spectrum[i] = 0.0f;
        if (spectrum[i] > 1.0f) spectrum[i] = 1.0f;
        
        if (spectrum[i] > peakHold[i]) {
            peakHold[i] = spectrum[i];
            peakTime[i] = currentTime;
        } else if (currentTime - peakTime[i] > config.peakHoldTime) {
            peakHold[i] *= 0.90f;
            if (peakHold[i] < spectrum[i]) {
                peakHold[i] = spectrum[i];
            }
        }
        
        // Защита пиковых значений от NaN
        if (isnan(peakHold[i]) || isinf(peakHold[i])) {
            peakHold[i] = 0.0f;
        }
    }
    lastFFTTime = currentTime;
    
    // Освобождаем мьютекс
    xSemaphoreGive(dataMutex);
    
    // Отладочная информация удалена для чистоты вывода
    // Спектр-анализатор оптимизирован для RGB Panel ESP32-4848S040
}

// Простые функции для моно индикатора
float* SpectrumAnalyzer::getSpectrum() {
    if (!initialized || !spectrum || !dataMutex) {
        return nullptr;
    }
    
    // Захватываем мьютекс для чтения данных
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return nullptr; // Не можем получить мьютекс
    }
    
    // Освобождаем мьютекс сразу после получения указателя
    xSemaphoreGive(dataMutex);
    
    return spectrum;
}

float* SpectrumAnalyzer::getPeakHold() {
    if (!initialized || !peakHold || !dataMutex) {
        return nullptr;
    }
    
    // Возвращаем указатель без захвата мьютекса
    // Виджет должен использовать copySpectrum для безопасного доступа
    return peakHold;
}

uint8_t SpectrumAnalyzer::getNumBands() {
    return config.numBands;
}

bool SpectrumAnalyzer::isInitialized() {
    return initialized;
}

// Безопасное копирование данных спектра для RGB Panel
bool SpectrumAnalyzer::copySpectrum(float* dest, uint8_t maxBands) {
    if (!initialized || !spectrum || !dataMutex || !dest) {
        return false;
    }
    
    // Захватываем мьютекс для чтения данных
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false; // Не можем получить мьютекс
    }
    
    // Копируем данные с защитой от NaN и оптимизацией для RGB Panel
    uint8_t bandsToCopy = (config.numBands < maxBands) ? config.numBands : maxBands;
    for (uint8_t i = 0; i < bandsToCopy; i++) {
        if (isnan(spectrum[i]) || isinf(spectrum[i])) {
            dest[i] = 0.0f;
        } else {
            dest[i] = spectrum[i];
        }
    }
    
    // Освобождаем мьютекс
    xSemaphoreGive(dataMutex);
    
    return true;
}

// Безопасное копирование пиковых значений для RGB Panel
bool SpectrumAnalyzer::copyPeakHold(float* dest, uint8_t maxBands) {
    if (!initialized || !peakHold || !dataMutex || !dest) {
        return false;
    }
    
    // Захватываем мьютекс для чтения данных
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false; // Не можем получить мьютекс
    }
    
    // Копируем данные с защитой от NaN и оптимизацией для RGB Panel
    uint8_t bandsToCopy = (config.numBands < maxBands) ? config.numBands : maxBands;
    for (uint8_t i = 0; i < bandsToCopy; i++) {
        if (isnan(peakHold[i]) || isinf(peakHold[i])) {
            dest[i] = 0.0f;
        } else {
            dest[i] = peakHold[i];
        }
    }
    
    // Освобождаем мьютекс
    xSemaphoreGive(dataMutex);
    
    return true;
}

// Управление общим усилением для RGB Panel
void SpectrumAnalyzer::setGain(float gain) {
    if (gain < 0.1f) gain = 0.1f; // Минимальное усиление для стабильности
    if (gain > 10.0f) gain = 10.0f; // Максимальное усиление для RGB Panel
    config.gain = gain;
}

float SpectrumAnalyzer::getGain() {
    return config.gain; // Возвращает текущее усиление для RGB Panel
} 