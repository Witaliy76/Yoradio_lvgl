#ifndef AI_COORDINATOR_H
#define AI_COORDINATOR_H

/**
 * ai_coordinator.h - AI text display coordinator header
 * Description: AI text display manager interface (anti-spam, deduplication, rate limiting)
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "ai_types.h"
#include <Arduino.h>

/**
 * AIDisplayCoordinator - менеджер показа AI-текста
 * AIDisplayCoordinator - AI text display manager
 * 
 * Роль: анти-спам, дедупликация, контроль частоты, fallback в тишину
 * Role: anti-spam, deduplication, frequency control, fallback to silence
 * 
 * НЕ генерирует контент, НЕ выбирает "лучший слой"
 * Does NOT generate content, does NOT choose "best layer"
 */
class AIDisplayCoordinator {
private:
    // Запись о последнем показанном тексте для каждого слоя
    // Record of last shown text for each layer
    struct LastShown {
        String text;
        uint32_t timestamp_ms;
        bool valid;  // Есть ли валидная запись / Is there valid record
    };
    
    LastShown _last_facts;
    LastShown _last_interpretation;
    LastShown _last_moment;
    
    LastShown* _getLastShown(AILayerType layer);
    
public:
    AIDisplayCoordinator();
    
    /**
     * Решает: показывать ли кандидата от слоя сейчас
     * Decides: should show candidate from layer now
     * 
     * @param candidate - кандидат от слоя
     * @param current_time_ms - текущее время (millis())
     * @return true если можно показывать, false если молчим
     */
    bool shouldShow(const AICandidate* candidate, uint32_t current_time_ms);
    
    /**
     * Обновляет запись о показанном тексте
     * Updates record of shown text
     */
    void markAsShown(const AICandidate* candidate, uint32_t current_time_ms);
    
    /**
     * Фильтрует служебные строки из track_title
     * Filters service strings from track_title
     * 
     * Игнорирует: [соединение], [готов], [остановлено] и т.п.
     * Ignores: [connection], [ready], [stopped], etc.
     */
    static String filterTrackTitle(const String& raw_title);
};

#endif // AI_COORDINATOR_H
