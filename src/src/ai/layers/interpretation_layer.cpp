/**
 * interpretation_layer.cpp - Music interpretation layer implementation
 * Description: Music interpretation layer via LLM, request submission, result processing
 * Author: Witaliy76 - https://github.com/Witaliy76
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "interpretation_layer.h"
#include "../ai_log.h"  // AI Layer logging macros
#include "../utils/utf8_truncate.h"
#include "../../core/config.h"
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

extern Config config;

InterpretationLayer::InterpretationLayer() : _enabled(true), _provider(nullptr), _taskManager(nullptr), _track_id(0), _track_start_ms(0), _last_enqueued_track_id(0) {
    // MVP-2: Провайдер и Task Manager устанавливаются извне / Provider and Task Manager are set from outside
    // Инициализируем последний валидный контекст пустым / Initialize last valid context as empty
    _last_valid_context.track_title = "";
    _last_valid_context.artist = "";
    _last_valid_context.song = "";
    _last_valid_context.station_name = "";
}

bool InterpretationLayer::tryEnqueuePending(const AIContext& context) {
    // Проверяем, нужно ли отправить отложенный запрос после debounce
    // Check if we need to send pending request after debounce
    
    if (!_enabled || !_taskManager) {
        return false;  // Слой выключен или нет Task Manager / Layer disabled or no Task Manager
    }
    
    // Проверяем настройки AI из config
    if (!config.store.ai_enabled || config.store.llm_provider == LLM_NONE) {
        return false;  // AI отключен в настройках / AI disabled in settings
    }
    
    // Получаем API ключ и модель
    String api_key = String(config.store.ai_api_key);
    String model = String(config.store.ai_model);
    
    if (api_key.isEmpty() || model.isEmpty()) {
        return false;  // Нет API ключа или модели / No API key or model
    }
    
    // Используем Task Manager для асинхронного выполнения HTTPS запроса
    if (!_taskManager->isRunning()) {
        return false;
    }
    
    const uint32_t now = millis();
    
    // 1) Проверяем debounce: должно пройти >= 4 секунды после смены трека
    if (_track_start_ms == 0 || (now - _track_start_ms) < 4000) {
        return false;  // Еще рано / Too early
    }
    
    // 2) Проверяем, что еще не отправляли запрос для этого трека
    if (_last_enqueued_track_id == _track_id) {
        return false;  // Уже отправлен / Already sent
    }
    
    // Все условия выполнены - отправляем запрос
    // All conditions met - send request
    AIRequestJob job;
    utf8_copy_trunc(job.station_name, sizeof(job.station_name), context.station_name.c_str());
    utf8_copy_trunc(job.artist,       sizeof(job.artist),       context.artist.c_str());
    utf8_copy_trunc(job.song,         sizeof(job.song),         context.song.c_str());
    utf8_copy_trunc(job.track_title,  sizeof(job.track_title),  context.track_title.c_str());
    utf8_copy_trunc(job.api_key,      sizeof(job.api_key),      api_key.c_str());
    utf8_copy_trunc(job.model,        sizeof(job.model),        model.c_str());
    job.timestamp_ms = now;
    job.track_id = _track_id;  // защита от stale results / protection from stale results
    
    // Поставляем задачу в очередь (неблокирующе)
    if (!_taskManager->enqueueRequest(job)) {
        // Очередь полна или task manager не работает -> молчим (Runtime Manifest: silence is valid)
        return false;
    }
    
    _last_enqueued_track_id = _track_id;  // Фиксируем "уже отправили" / Mark as enqueued
    AI_LOG("[InterpretationLayer] Pending request enqueued after debounce");
    return true;  // Запрос отправлен / Request sent
}

bool InterpretationLayer::process(const AIContext& context, AICandidate& out) {
    if (!_enabled || !_taskManager) {
        AI_LOG("[InterpretationLayer] disabled or no task manager");
        return false;  // Слой выключен или нет Task Manager / Layer disabled or no Task Manager
    }
    
    // Runtime Manifest: Interpretation должен выводиться при каждой смене песни, если AI активен
    // Runtime Manifest: Interpretation should output on every track change if AI is active
    
    // Проверяем настройки AI из config
    if (!config.store.ai_enabled || config.store.llm_provider == LLM_NONE) {
        AI_LOG("[InterpretationLayer] AI disabled in config");
        return false;  // AI отключен в настройках / AI disabled in settings
    }
    
    // Получаем API ключ и модель
    String api_key = String(config.store.ai_api_key);
    String model = String(config.store.ai_model);
    
    if (api_key.isEmpty() || model.isEmpty()) {
        AI_LOG("[InterpretationLayer] API key or model empty");
        return false;  // Нет API ключа или модели / No API key or model
    }
    
    // Используем Task Manager для асинхронного выполнения HTTPS запроса
    // Use Task Manager for asynchronous HTTPS request execution
    if (!_taskManager->isRunning()) {
        AI_LOG("[InterpretationLayer] Task Manager not running, returning false");
        return false;
    }
    
    // Запрет LLM без валидного трека / Prohibit LLM without valid track
    if (context.track_title.isEmpty()) {
        AI_LOG("[InterpretationLayer] track_title empty, skipping LLM request");
        return false;  // На boot/служебных строках/станциях без метаданных LLM не дергать / Don't call LLM on boot/service strings/stations without metadata
    }
    
    const uint32_t now = millis();
    
    // Debounce обрабатывается в AISubsystem, здесь не нужен
    // Debounce is handled in AISubsystem, not needed here
    
    // Строго 1 запрос на трек / Strictly 1 request per track
    if (_last_enqueued_track_id == _track_id) {
        AI_DLOG("[InterpretationLayer] Already enqueued for this track_id, skipping");
        return false;
    }
    
    // Формируем задачу для AI Task / Build job for AI Task
    // Копируем String в фиксированные буферы для FreeRTOS queue
    // Copy String to fixed buffers for FreeRTOS queue
    AIRequestJob job;
    utf8_copy_trunc(job.station_name, sizeof(job.station_name), context.station_name.c_str());
    utf8_copy_trunc(job.artist,       sizeof(job.artist),       context.artist.c_str());
    utf8_copy_trunc(job.song,         sizeof(job.song),         context.song.c_str());
    utf8_copy_trunc(job.track_title,  sizeof(job.track_title),  context.track_title.c_str());
    utf8_copy_trunc(job.api_key,      sizeof(job.api_key),      api_key.c_str());
    utf8_copy_trunc(job.model,        sizeof(job.model),        model.c_str());
    job.timestamp_ms = now;
    job.track_id = _track_id;  // защита от stale results / protection from stale results
    
    // Поставляем задачу в очередь (неблокирующе)
    // Enqueue job (non-blocking)
    if (!_taskManager->enqueueRequest(job)) {
        // Очередь полна или task manager не работает -> молчим (Runtime Manifest: silence is valid)
        // Queue full or task manager not running -> silent (Runtime Manifest: silence is valid)
        return false;
    }
    
    _last_enqueued_track_id = _track_id;  // Фиксируем "уже отправили" / Mark as enqueued
    
    // Задача поставлена в очередь, результат будет обработан асинхронно в _processLayers()
    // Job enqueued, result will be processed asynchronously in _processLayers()
    AI_LOG("[InterpretationLayer] Request enqueued successfully");
    return true;  // Возвращаем true для индикации успешного enqueue / Return true to indicate successful enqueue
}
