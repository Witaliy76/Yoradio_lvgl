#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

/**
 * llm_provider.h - Abstract base class for LLM providers
 * Description: Interface for pluggable architecture supporting different LLM APIs
 * Author: W76W, 4pda.to
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include <Arduino.h>
#include <WiFiClient.h>

/**
 * LLMProvider - абстрактный базовый класс для LLM провайдеров
 * LLMProvider - abstract base class for LLM providers
 * 
 * Pluggable архитектура для поддержки разных LLM API
 * Pluggable architecture for supporting different LLM APIs
 */
struct LLMResponse {
    bool ok;                    // Успешный ответ / Success response
    String text;                // Текст (факт или "как слушать") / Text (fact or "how to listen") - переименовано из interpretation согласно манифесту
    String mode;                // "fact" или "listen" / "fact" or "listen"
    float confidence;           // Уверенность 0.0-1.0 / Confidence 0.0-1.0
    
    LLMResponse() : ok(false), mode(""), confidence(0.0f) {}
};

class LLMProvider {
public:
    virtual ~LLMProvider() {}
    
    /**
     * Проверка доступности провайдера (health check)
     * Check provider availability (health check)
     * 
     * Ленивая проверка: фактический запрос при первой необходимости
     * Lazy check: actual request on first need
     * 
     * @param api_key API ключ провайдера
     * @return true если провайдер доступен, false иначе
     */
    virtual bool isAvailable(const String& api_key) = 0;
    
    /**
     * Запрос интерпретации для трека
     * Request interpretation for track
     * 
     * @param api_key API ключ провайдера
     * @param model Модель для использования
     * @param station_name Название станции
     * @param artist Исполнитель
     * @param song Название песни
     * @param track_title Полное название трека (если artist/song не распарсены)
     * @param response Выходной ответ
     * @return true если запрос успешен, false при любой ошибке
     */
    virtual bool requestInterpretation(
        const String& api_key,
        const String& model,
        const String& station_name,
        const String& artist,
        const String& song,
        const String& track_title,
        LLMResponse& response
    ) = 0;
    
    /**
     * Имя провайдера для логирования
     * Provider name for logging
     */
    virtual const char* getName() const = 0;
};

#endif // LLM_PROVIDER_H

