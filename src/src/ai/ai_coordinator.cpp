/**
 * ai_coordinator.cpp - AI text display coordinator implementation
 * Description: AI text display manager (anti-spam, deduplication, rate limiting)
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "ai_coordinator.h"

AIDisplayCoordinator::AIDisplayCoordinator() {
    _last_facts.valid = false;
    _last_interpretation.valid = false;
    _last_moment.valid = false;
}

AIDisplayCoordinator::LastShown* AIDisplayCoordinator::_getLastShown(AILayerType layer) {
    switch (layer) {
        case LAYER_FACTS: return &_last_facts;
        case LAYER_INTERPRETATION: return &_last_interpretation;
        case LAYER_MOMENT: return &_last_moment;
        default: return nullptr;
    }
}

bool AIDisplayCoordinator::shouldShow(const AICandidate* candidate, uint32_t current_time_ms) {
    if (!candidate || candidate->text.isEmpty()) {
        return false;  // Тишина / Silence
    }
    
    // Выбираем правильную запись о последнем показанном
    // Select correct record of last shown
    LastShown* last = _getLastShown(candidate->source_layer);
    if (!last) {
        return false;
    }
    
    // Проверка 1: Дедупликация (тот же текст?)
    // Check 1: Deduplication (same text?)
    if (last->valid && last->text == candidate->text) {
        return false;  // Повтор - молчим / Repeat - silence
    }
    
    // Проверка 2: Анти-спам (прошло ли min_interval?)
    // Check 2: Anti-spam (has min_interval passed?)
    if (last->valid) {
        uint32_t time_since_last = current_time_ms - last->timestamp_ms;
        if (time_since_last < candidate->min_interval_ms) {
            return false;  // Слишком часто - молчим / Too frequent - silence
        }
    }
    
    // Проверка 3: Confidence для Facts
    // Check 3: Confidence for Facts
    if (candidate->source_layer == LAYER_FACTS && candidate->confidence < 0.7f) {
        return false;  // Низкая уверенность - молчим / Low confidence - silence
    }
    
    return true;  // Можно показывать / Can show
}

void AIDisplayCoordinator::markAsShown(const AICandidate* candidate, uint32_t current_time_ms) {
    if (!candidate) {
        return;
    }
    
    LastShown* last = _getLastShown(candidate->source_layer);
    if (last) {
        last->text = candidate->text;
        last->timestamp_ms = current_time_ms;
        last->valid = true;
    }
}

String AIDisplayCoordinator::filterTrackTitle(const String& raw_title) {
    if (raw_title.isEmpty()) {
        return "";
    }
    
    // Удаляем строки, начинающиеся с ## (служебные сообщения)
    // Remove lines starting with ## (service messages)
    String result = raw_title;
    int newline_pos = result.indexOf('\n');
    while (newline_pos >= 0) {
        String line = result.substring(0, newline_pos);
        line.trim();
        if (line.startsWith("##")) {
            // Удаляем строку, начинающуюся с ##
            // Remove line starting with ##
            if (newline_pos + 1 < result.length()) {
                result = result.substring(newline_pos + 1);
                result.trim();
                newline_pos = result.indexOf('\n');
            } else {
                result = "";
                break;
            }
        } else {
            // Первая строка не служебная - останавливаемся
            // First line is not service - stop
            break;
        }
    }
    
    // Список служебных строк для игнорирования
    // List of service strings to ignore
    const char* ignore_patterns[] = {
        "[соединение]",
        "[готов]",
        "[остановлено]",
        "[connection]",
        "[ready]",
        "[stopped]"
    };
    
    String filtered = raw_title;
    filtered.trim();
    
    // Проверяем каждый паттерн
    // Check each pattern
    for (size_t i = 0; i < sizeof(ignore_patterns)/sizeof(ignore_patterns[0]); i++) {
        if (filtered.equalsIgnoreCase(ignore_patterns[i])) {
            return "";  // Служебная строка - игнорируем / Service string - ignore
        }
    }
    
    return filtered;
}
