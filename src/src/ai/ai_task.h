#ifndef AI_TASK_H
#define AI_TASK_H

/**
 * ai_task.h - AI task manager header
 * Description: Data structures and interface for asynchronous AI request execution via FreeRTOS
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "ai_types.h"
#include "providers/llm_provider.h"
#include <Arduino.h>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#endif

/**
 * AI_TASK - FreeRTOS task для выполнения AI запросов
 * AI_TASK - FreeRTOS task for executing AI requests
 * 
 * Решает проблему stack overflow при HTTPS/TLS запросах
 * Solves stack overflow problem with HTTPS/TLS requests
 */

// Задача для AI запроса / AI request job
// Используем фиксированные буферы для FreeRTOS queue (побитовое копирование)
// Use fixed buffers for FreeRTOS queue (bitwise copy)
struct AIRequestJob {
    char station_name[64];
    char artist[64];
    char song[64];
    char track_title[128];
    char api_key[65];
    char model[32];
    uint32_t timestamp_ms;  // Время создания задачи / Job creation time
    uint32_t track_id;  // ID трека для защиты от stale results / Track ID to prevent stale results
};

// Результат AI запроса / AI request result
// Используем фиксированные буферы для FreeRTOS queue
// Use fixed buffers for FreeRTOS queue
struct AIRequestResult {
    char text[256];  // Переименовано из interpretation в text согласно манифесту / Renamed from interpretation to text per manifest
    char mode[16];
    float confidence;
    bool ok;
    uint32_t timestamp_ms;  // Время создания исходного запроса / Original request time
    uint32_t track_id;  // ID трека для защиты от stale results / Track ID to prevent stale results
};

/**
 * AITaskManager - менеджер AI задачи и очереди
 * AITaskManager - AI task and queue manager
 */
class AITaskManager {
private:
    #ifdef ESP_PLATFORM
    TaskHandle_t _taskHandle;
    QueueHandle_t _requestQueue;
    QueueHandle_t _resultQueue;
    LLMProvider* _provider;
    bool _taskRunning;
    #ifdef ESP_PLATFORM
    bool _requestInProgress;  // Флаг занятости / Busy flag
    uint32_t _lastRequestTime;  // Время последнего запроса для rate limiting / Last request time for rate limiting
    #endif
    
    static void _taskWrapper(void* param);
    void _taskLoop();
    #endif

public:
    AITaskManager();
    ~AITaskManager();
    
    // Инициализация (создание таска и очередей) / Initialization (create task and queues)
    bool begin(LLMProvider* provider);
    
    // Остановка таска / Stop task
    void stop();
    
    // Поставить задачу в очередь / Enqueue job
    // Возвращает true если задача поставлена, false если очередь полна
    // Returns true if job enqueued, false if queue full
    bool enqueueRequest(const AIRequestJob& job);
    
    // Получить результат (неблокирующий) / Get result (non-blocking)
    // Возвращает true если результат получен, false если очередь пуста
    // Returns true if result received, false if queue empty
    bool getResult(AIRequestResult& result);
    
    // Проверка работы таска / Task status check
    bool isRunning() const { 
        #ifdef ESP_PLATFORM
        return _taskRunning && _taskHandle != nullptr;
        #else
        return false;
        #endif
    }
    
    // Проверка, можно ли отправить запрос (rate limiting + проверка занятости)
    // Check if request can be sent (rate limiting + busy check)
    bool canSendRequest() const;
    
    // Очистка всех очередей и сброс флагов (thread-safe)
    // Clear all queues and reset flags (thread-safe)
    void cancelAll();
};

#endif // AI_TASK_H

