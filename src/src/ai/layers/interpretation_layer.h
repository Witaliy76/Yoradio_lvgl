#ifndef INTERPRETATION_LAYER_H
#define INTERPRETATION_LAYER_H

/**
 * interpretation_layer.h - Music interpretation layer header
 * Description: Music interpretation layer via LLM (facts and neutral phrases)
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "../ai_layer.h"
#include "../providers/llm_provider.h"
#include "../ai_task.h"

/**
 * InterpretationLayer - слой интерпретации музыки
 * InterpretationLayer - music interpretation layer
 * 
 * Runtime Manifest Section 4.2:
 * - ВСЕГДА одна строка, если AI активен
 * - Если есть общеизвестный факт -> mode="fact"
 * - Иначе -> mode="listen" (как слушать, нейтрально)
 * 
 * MVP-2: Интеграция с LLM провайдером (OpenAI-compatible)
 * MVP-2: Integration with LLM provider (OpenAI-compatible)
 */
class InterpretationLayer : public AILayer {
private:
    bool _enabled;
    LLMProvider* _provider;  // Провайдер LLM / LLM provider
    AITaskManager* _taskManager;  // Task Manager для асинхронных запросов / Task Manager for async requests
    uint32_t _track_id;  // ID текущего трека / Current track ID
    uint32_t _track_start_ms = 0;  // Время начала текущего трека / Current track start time
    uint32_t _last_enqueued_track_id = 0;  // ID последнего трека, для которого был отправлен запрос / Last track ID that had request enqueued
    AIContext _last_valid_context;  // Последний валидный контекст трека (для устойчивости к "шатающейся" meta) / Last valid track context (resilience to "shaky" meta)
    
public:
    InterpretationLayer();
    virtual ~InterpretationLayer() {}
    
    // Установка провайдера / Set provider
    void setProvider(LLMProvider* provider) { _provider = provider; }
    
    // Установка Task Manager / Set Task Manager
    void setTaskManager(AITaskManager* taskManager) { _taskManager = taskManager; }
    
    // Установка track_id для защиты от stale results / Set track_id to prevent stale results
    void setTrackId(uint32_t track_id) {
        _track_id = track_id;
        _track_start_ms = millis();
        _last_enqueued_track_id = 0;  // новый трек -> разрешаем один запрос / new track -> allow one request
    }
    
    // Попытка отправить отложенный запрос после debounce (вызывается из on_ticker)
    // Try to send pending request after debounce (called from on_ticker)
    // Возвращает true если запрос был отправлен, false если еще рано или уже отправлен
    // Returns true if request was sent, false if too early or already sent
    bool tryEnqueuePending(const AIContext& context);
    
    virtual bool process(const AIContext& context, AICandidate& out) override;
    virtual bool isEnabled() const override { return _enabled; }
    virtual void setEnabled(bool enabled) override { _enabled = enabled; }
    virtual AILayerType getType() const override { return LAYER_INTERPRETATION; }
};

#endif // INTERPRETATION_LAYER_H
