#ifndef SPECTRUM_ANALYZER_H
#define SPECTRUM_ANALYZER_H

// Block 8-E18D: class/TU kept for future LVGL spectrum widget; product does not call init/processAudio.
// Block 8-E18D: класс сохранён для будущего LVGL-виджета; product path не вызывает init/processAudio.

// Спектр-анализатор оптимизирован для RGB Panel ESP32-4848S040
// Поддерживает 480x480 разрешение с плавной анимацией

#include "Arduino.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Универсальная структура конфигурации для RGB Panel
struct SpectrumConfig {
    uint8_t numBands;           // SPECTRUM_BANDS - количество полос спектра
    uint16_t fftSize;           // SPECTRUM_FFT_SIZE - размер FFT (оптимизирован для RGB Panel)
    uint32_t sampleRate;        // Частота дискретизации (44100 Hz для RGB Panel)
    float smoothing;            // SPECTRUM_SMOOTHING - коэффициент сглаживания
    float peakHoldTime;         // SPECTRUM_PEAK_HOLD_TIME - время удержания пиков
    bool logarithmic;           // SPECTRUM_LOGARITHMIC - логарифмическая шкала
    bool stereo;                // SPECTRUM_STEREO - стерео режим
    float gain;                 // Общее усиление спектра (оптимизировано для RGB Panel)
};

class SpectrumAnalyzer {
private:
    // Конфигурация для RGB Panel
    struct {
        uint8_t numBands;
        uint16_t fftSize;
        uint32_t sampleRate;
        float smoothing;
        float peakHoldTime;
        bool logarithmic;
        bool stereo;
        float gain;                 // Общее усиление спектра (оптимизировано для RGB Panel)
    } config;
    
    // Данные спектра
    float* spectrum;
    float* peakHold;
    uint32_t* peakTime;
    float* prevSpectrum;
    
    // Динамическое масштабирование для RGB Panel
    float* adaptiveScale;      // Адаптивный масштаб для каждой полосы
    float* maxValues;          // Максимальные значения для каждой полосы
    float* minValues;          // Минимальные значения для каждой полосы
    
    // Состояние
    uint16_t sampleCount;
    uint32_t lastFFTTime;
    uint32_t debugTime;
    bool initialized;
    
    // Защита от гонки данных для RGB Panel
    SemaphoreHandle_t dataMutex;
    
    // Внутренние функции
    void reset();
    
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer();
    
    // Инициализация
    bool init();
    bool begin();
    
    // Обработка аудио
    void processAudio(const int16_t* samples, int32_t count);
    
    // Сброс данных спектра
    void clearData();
    
    // Получение данных (с защитой)
    float* getSpectrum();
    float* getPeakHold();
    uint8_t getNumBands();
    bool isInitialized();
    
    // Безопасное копирование данных для RGB Panel
    bool copySpectrum(float* dest, uint8_t maxBands);
    bool copyPeakHold(float* dest, uint8_t maxBands);
    
    // Управление усилением для RGB Panel
    void setGain(float gain);
    float getGain();
};

// Глобальный экземпляр для RGB Panel
extern SpectrumAnalyzer spectrumAnalyzer;

#endif 