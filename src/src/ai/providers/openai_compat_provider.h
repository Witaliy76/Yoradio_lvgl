#ifndef OPENAI_COMPAT_PROVIDER_H
#define OPENAI_COMPAT_PROVIDER_H

/**
 * openai_compat_provider.h - OpenAI-compatible provider header
 * Description: Universal provider for OpenAI-compatible API (DeepSeek, OpenAI, Perplexity, etc.)
 * Author: Witaliy76 - https://github.com/Witaliy76
 * Date: 02.01.2026
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "llm_provider.h"
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

/**
 * OpenAICompatProvider - универсальный провайдер для OpenAI-compatible API
 * OpenAICompatProvider - universal provider for OpenAI-compatible API
 * 
 * Использует настройки из runtime cache (aiGetRuntimeConfig)
 * Uses settings from runtime cache (aiGetRuntimeConfig)
 * Поддерживает HTTP (port=80) и HTTPS (port=443)
 * Supports HTTP (port=80) and HTTPS (port=443)
 */
class OpenAICompatProvider : public LLMProvider {
private:
    // Per-request local HTTPClient/TLS (no member transport pointer) / Локальный HTTPClient+TLS на запрос
    
    // Вспомогательные методы / Helper methods
    bool _makeHTTPRequest(
        const String& api_key,
        const String& model,
        const String& station_name,
        const String& artist,
        const String& song,
        const String& track_title,
        String& response_body,
        int& httpCode_out,
        String& content_type_out,
        String& content_length_out,
        String& transfer_encoding_out,
        String& content_encoding_out
    );
    
    String _buildRequestJSON(const String& model, const String& prompt);
    bool _readHTTPResponse(String& response_body, HTTPClient& http);
    
    // Удаление UTF-8 BOM из строки / Remove UTF-8 BOM from string
    String _removeBOM(const String& str);
    
    bool _parseJSONResponse(const String& json, LLMResponse& response, int httpCode, 
                           const String& content_type, const String& content_length,
                           const String& transfer_encoding, const String& content_encoding);
    String _buildPrompt(const String& station_name, const String& artist, const String& song, const String& track_title);
    
    // Нормализация пути к /chat/completions / Normalize path to /chat/completions
    static String normalizeChatCompletionsPath(const char* basePath);
    
public:
    OpenAICompatProvider();
    virtual ~OpenAICompatProvider() {}
    
    virtual bool isAvailable(const String& api_key) override;
    virtual bool requestInterpretation(
        const String& api_key,
        const String& model,
        const String& station_name,
        const String& artist,
        const String& song,
        const String& track_title,
        LLMResponse& response
    ) override;
    
    virtual const char* getName() const override { return "OpenAI-Compatible"; }
};

#endif // OPENAI_COMPAT_PROVIDER_H

