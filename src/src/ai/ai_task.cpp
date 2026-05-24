/**
 * ai_task.cpp - AI task manager implementation
 * Description: Asynchronous AI request execution via FreeRTOS task, queue management
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "ai_task.h"
#include "ai_log.h"  // AI Layer logging macros
#include "utils/utf8_truncate.h"
#include <string.h> // For strnlen, memcpy

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Макрос для корректного преобразования words в bytes для stack HWM
// Macro for correct conversion of words to bytes for stack HWM
#define WORDS_TO_BYTES(w) ((uint32_t)(w) * sizeof(StackType_t))
#endif

AITaskManager::AITaskManager() 
    #ifdef ESP_PLATFORM
    : _taskHandle(nullptr), _requestQueue(nullptr), _resultQueue(nullptr), 
      _provider(nullptr), _taskRunning(false)
    #endif
{
    #ifdef ESP_PLATFORM
    _requestInProgress = false;
    _lastRequestTime = 0;
    #endif
}

AITaskManager::~AITaskManager() {
    stop();
}

bool AITaskManager::begin(LLMProvider* provider) {
    if (!provider) {
        AI_LOG("[AITaskManager] begin() failed: provider is null");
        return false;
    }
    
    #ifdef ESP_PLATFORM
    _provider = provider;
    
    // Создаём очереди / Create queues
    // Request queue: глубина 2 (для отсечения устаревших запросов) / depth 2 (to discard stale requests)
    // Result queue: глубина 1 (обрабатываем по одному результату) / depth 1 (process one result at a time)
    _requestQueue = xQueueCreate(2, sizeof(AIRequestJob));
    _resultQueue = xQueueCreate(1, sizeof(AIRequestResult));
    
    if (!_requestQueue || !_resultQueue) {
        AI_LOG("[AITaskManager] begin() failed: queue creation failed");
        return false;
    }
    
    // Создаём таск с большим стеком для HTTPS/TLS / Create task with large stack for HTTPS/TLS
    // Stack: 16KB (16384 bytes) - достаточно для mbedTLS / Enough for mbedTLS
    // Priority: 1 (ниже audio task для меньшего влияния на аудио) / Lower than audio task to reduce audio impact
    // Core: 1 (там же где audio обычно) / Same core as audio typically
        BaseType_t result = xTaskCreatePinnedToCore(
                    _taskWrapper,
                    "AI_HTTP_Task",
                    16384 / sizeof(StackType_t),  // Stack size in words
                    this,
                    1,  // Priority (уменьшено для меньшего влияния на аудио / reduced to reduce audio impact)
                    &_taskHandle,
                    0   // Core 0 (чтобы не мешать аудио на core1 / to not interfere with audio on core1)
                );
    
    if (result != pdPASS) {
        AI_LOG("[AITaskManager] begin() failed: task creation failed");
        if (_requestQueue) {
            vQueueDelete(_requestQueue);
            _requestQueue = nullptr;
        }
        if (_resultQueue) {
            vQueueDelete(_resultQueue);
            _resultQueue = nullptr;
        }
        return false;
    }
    
    _taskRunning = true;
    AI_LOG("[AITaskManager] begin() success: task created, queues ready");
    
    #ifdef ESP_PLATFORM
    UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(_taskHandle);
    AI_DLOG("[AITaskManager] AI task stack HWM: %u words (~%u bytes)", 
                  stack_remaining, stack_remaining * sizeof(StackType_t));
    #endif
    
    return true;
    #else
    return false;
    #endif
}

void AITaskManager::stop() {
    #ifdef ESP_PLATFORM
    _taskRunning = false;
    
    if (_taskHandle) {
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
    
    if (_requestQueue) {
        vQueueDelete(_requestQueue);
        _requestQueue = nullptr;
    }
    
    if (_resultQueue) {
        vQueueDelete(_resultQueue);
        _resultQueue = nullptr;
    }
    #endif
}

bool AITaskManager::canSendRequest() const {
    #ifdef ESP_PLATFORM
    if (!_taskRunning) {
        return false;
    }
    
    if (_requestInProgress) {
        return false;  // Запрос уже выполняется / Request already in progress
    }
    
    // Rate limiting: минимум 12 секунд между запросами
    // Rate limiting: minimum 12 seconds between requests
    uint32_t now = millis();
    if (_lastRequestTime > 0 && (now - _lastRequestTime) < 12000) {
        return false;  // Слишком рано / Too soon
    }
    
    return true;
    #else
    return false;
    #endif
}

bool AITaskManager::enqueueRequest(const AIRequestJob& job) {
    #ifdef ESP_PLATFORM
    if (!_requestQueue || !_taskRunning) {
        return false;
    }
    
    // Проверяем rate limiting и занятость
    // Check rate limiting and busy state
    if (!canSendRequest()) {
        AI_DLOG("[AITaskManager] Rate limit or busy -> skipping (silence is valid)");
        return false;
    }
    
    // Неблокирующая отправка / Non-blocking send
    // Если очередь полна - просто возвращаем false (silence is valid)
    // If queue is full - just return false (silence is valid)
    BaseType_t result = xQueueSend(_requestQueue, &job, 0);
    
    if (result == pdTRUE) {
        #ifdef ESP_PLATFORM
        TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
        const char* task_name = pcTaskGetName(current_task);
        int core_id = xPortGetCoreID();
        UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(nullptr);
        AI_DLOG("[AITaskManager] Enqueued request from task=%s core=%d stack_HWM=%u words (~%u bytes)",
                      task_name ? task_name : "unknown", core_id, stack_remaining, (unsigned int)(stack_remaining * sizeof(StackType_t)));
        #endif
        return true;
    } else {
        AI_LOG("[AITaskManager] Queue full -> skipping (silence is valid)");
        return false;
    }
    #else
    return false;
    #endif
}

bool AITaskManager::getResult(AIRequestResult& result) {
    #ifdef ESP_PLATFORM
    if (!_resultQueue) {
        return false;
    }
    
    // Неблокирующее получение / Non-blocking receive
    return xQueueReceive(_resultQueue, &result, 0) == pdTRUE;
    #else
    return false;
    #endif
}

void AITaskManager::cancelAll() {
    #ifdef ESP_PLATFORM
    // Очистка очереди запросов / Clear request queue
    if (_requestQueue) {
        AIRequestJob dummy;
        while (xQueueReceive(_requestQueue, &dummy, 0) == pdTRUE) {
            // Удаляем все элементы из очереди / Remove all items from queue
        }
    }
    
    // Очистка очереди результатов / Clear result queue
    if (_resultQueue) {
        AIRequestResult dummy;
        while (xQueueReceive(_resultQueue, &dummy, 0) == pdTRUE) {
            // Удаляем все элементы из очереди / Remove all items from queue
        }
    }
    
    // Сброс флагов / Reset flags
    _requestInProgress = false;
    _lastRequestTime = 0;
    #endif
}

#ifdef ESP_PLATFORM
void AITaskManager::_taskWrapper(void* param) {
    AITaskManager* manager = static_cast<AITaskManager*>(param);
    manager->_taskLoop();
}

void AITaskManager::_taskLoop() {
    AI_DLOG("[AITaskManager] AI task started");
    
    #ifdef ESP_PLATFORM
    UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(nullptr);
    const char* task_name = pcTaskGetName(nullptr);
    int core_id = xPortGetCoreID();
    AI_DLOG("[AITaskManager] Task running: name=%s core=%d initial_stack_HWM=%u words (~%u bytes)",
                  task_name ? task_name : "unknown", core_id, stack_remaining, WORDS_TO_BYTES(stack_remaining));
    #endif
    
    AIRequestJob job;
    
    while (_taskRunning) {
        // Ждём задачу с таймаутом / Wait for job with timeout
        if (xQueueReceive(_requestQueue, &job, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Устанавливаем флаг занятости / Set busy flag
            _requestInProgress = true;
            _lastRequestTime = millis();
            
            AI_DLOG("[AITaskManager] Processing request");
            
            // Небольшая задержка перед HTTPS для меньшего влияния на аудио
            // Small delay before HTTPS to reduce audio impact
            vTaskDelay(pdMS_TO_TICKS(50));
            
            #ifdef ESP_PLATFORM
            stack_remaining = uxTaskGetStackHighWaterMark(nullptr);
            AI_DLOG("[AITaskManager] Stack HWM before HTTPS: %u words (~%u bytes)",
                          stack_remaining, (unsigned int)(stack_remaining * sizeof(StackType_t)));
            #endif
            
            // Выполняем HTTPS запрос / Perform HTTPS request
            // Преобразуем буферы в String для провайдера / Convert buffers to String for provider
            LLMResponse response;
            bool success = _provider->requestInterpretation(
                String(job.api_key),
                String(job.model),
                String(job.station_name),
                String(job.artist),
                String(job.song),
                String(job.track_title),
                response
            );
            
            // Формируем результат / Build result
            AIRequestResult result;
            result.ok = success && response.ok;
            
            // Используем безопасное копирование UTF-8 с корректной обрезкой
            // Use safe UTF-8 copy with correct truncation
            utf8_copy_trunc(result.text, sizeof(result.text), response.text.c_str());
            result.text[sizeof(result.text) - 1] = '\0';  // Принудительный нуль-терминатор / Force null terminator
            
            // Используем безопасное копирование UTF-8 для mode (хотя обычно mode короткий)
            // Use safe UTF-8 copy for mode (though mode is usually short)
            utf8_copy_trunc(result.mode, sizeof(result.mode), response.mode.c_str());
            result.mode[sizeof(result.mode) - 1] = '\0';  // Принудительный нуль-терминатор / Force null terminator
            
            result.confidence = response.confidence;
            result.timestamp_ms = job.timestamp_ms;
            result.track_id = job.track_id;  // Сохраняем track_id для проверки stale results / Save track_id to check for stale results
            
            // Диагностическое логирование для проверки корректности UTF-8 обрезки (временно)
            // Diagnostic logging to verify UTF-8 truncation correctness (temporary)
            {
                size_t text_len = strlen(result.text);
                if (text_len > 0) {
                    char hex_buf[32] = {0};
                    size_t tail_start = (text_len > 8) ? text_len - 8 : 0;
                    for (size_t i = tail_start, j = 0; i < text_len && j < 30; i++, j += 3) {
                        snprintf(hex_buf + j, 4, "%02X ", (uint8_t)result.text[i]);
                    }
                    AI_DLOG("[AITaskManager] UTF-8 truncate check: len=%u, tail hex: %s", text_len, hex_buf);
                }
            }
            
            // Отправляем результат (неблокирующе, если очередь полна - результат отбрасывается)
            // Send result (non-blocking, if queue full - result discarded)
            if (xQueueSend(_resultQueue, &result, 0) != pdTRUE) {
                AI_LOG("[AITaskManager] Result queue full, discarding result");
            } else {
                AI_DLOG("[AITaskManager] Result enqueued: ok=%d, text=\"%s\", track_id=%u",
                        result.ok, result.text, result.track_id);
            }
            
            // Сбрасываем флаг занятости / Clear busy flag
            _requestInProgress = false;
        }
    }
    
    AI_DLOG("[AITaskManager] AI task stopped");
    vTaskDelete(nullptr);
}
#endif

