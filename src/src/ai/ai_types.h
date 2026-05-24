#ifndef AI_TYPES_H
#define AI_TYPES_H

/**
 * ai_types.h - Base data types for AI Layer
 * Description: Data structures, enumerations and types definitions for AI Layer
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include <Arduino.h>

/**
 * AI Types - базовые типы данных для AI-слоя
 * AI Types - basic data types for AI layer
 */

// Тип слоя / Layer type
enum AILayerType {
    LAYER_FACTS = 0,
    LAYER_INTERPRETATION = 1,
    LAYER_MOMENT = 2
};

// Контекст для обработки слоями / Context for layer processing
struct AIContext {
    // Музыка/радио / Music/radio
    String station_name;
    String track_title;  // После фильтрации служебных строк / After filtering service strings
    String artist;
    String song;
    bool is_playing;
    
    // Время / Time
    uint8_t current_hour;  // 0-23, или 255 если невалидно / 0-23, or 255 if invalid
    uint32_t uptime_ms;
    
    // TODO: возможно добавить codec/bitrate если нужно
};

// Кандидат от слоя / Candidate from layer
struct AICandidate {
    String text;  // TODO: рассмотреть переход на char[96] для оптимизации памяти
    AILayerType source_layer;
    uint32_t min_interval_ms;  // Защита от спама / Spam protection
    float confidence;          // 0.0-1.0 (для Facts обязательно / required for Facts)
};

#endif // AI_TYPES_H
