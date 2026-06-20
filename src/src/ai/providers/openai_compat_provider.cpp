/**
 * openai_compat_provider.cpp - OpenAI-compatible provider implementation
 * Description: Universal provider for OpenAI-compatible API, uses settings from runtime cache
 * Author: W76W, 4pda.to
 * Date: 02.01.2026
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "openai_compat_provider.h"
#include "../ai_log.h"  // AI Layer logging macros
#include "../../core/config.h"  // Для aiGetRuntimeConfig / For aiGetRuntimeConfig
#include "../ai_prompt.h"  // Для загрузки промптов из SPIFFS / For loading prompts from SPIFFS (aiPromptGet, aiPromptIsAvailable)
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#endif

// Per-request TLS client setup / Настройка TLS-клиента на один запрос
static void configureHttpsClient(WiFiClientSecure& client, uint32_t timeout_ms) {
    client.setInsecure();
    client.setTimeout(30);
    unsigned long hs_sec = timeout_ms / 1000;
    if (hs_sec < 10) {
        hs_sec = 10;
    }
    if (hs_sec > 30) {
        hs_sec = 30;
    }
    client.setHandshakeTimeout(hs_sec);
}

#ifdef ESP_PLATFORM
// TLS heap guard: skip HTTPS when internal largest block too small for mbedTLS
// TLS heap guard: не вызывать HTTPS при нехватке internal heap
static constexpr size_t AI_TLS_MIN_INTERNAL_LARGEST = 14000;  // E21C4 observe probe / наблюдение guard 18k
static constexpr int HTTPC_ERROR_TLS_HEAP_GUARD = -9001;

static void aiHttpsTlsHeapGuardLogSkip(size_t largest, size_t free_sz, size_t min_heap) {
    Serial.printf("[AI HTTPS] guard skip: largest=%u threshold=%u free=%u min=%u\n",
                  (unsigned)largest, (unsigned)AI_TLS_MIN_INTERNAL_LARGEST,
                  (unsigned)free_sz, (unsigned)min_heap);
}

#if AI_LAYER_DEBUG
static void logTlsLastErrorDebug(WiFiClientSecure& client) {
    char buf[96];
    buf[0] = '\0';
    const int err = client.lastError(buf, sizeof(buf));
    AI_DLOG("[OpenAICompatProvider] tls lastError=%d (%s)", err, buf[0] ? buf : "n/a");
}
#endif
#endif  // ESP_PLATFORM

OpenAICompatProvider::OpenAICompatProvider() {
    // Per-request HTTPClient/TLS in _makeHTTPRequest() / HTTPClient+TLS создаются на запрос
}

// Нормализация пути к /chat/completions / Normalize path to /chat/completions
String OpenAICompatProvider::normalizeChatCompletionsPath(const char* basePath) {
    if (!basePath || strlen(basePath) == 0) {
        return "/v1/chat/completions";
    }
    
    String path = String(basePath);
    path.trim();
    
    // Если уже содержит "chat/completions" → вернуть как есть
    // If already contains "chat/completions" → return as is
    if (path.indexOf("chat/completions") >= 0) {
        return path;
    }
    
    // Если заканчивается на "/v1" или "/v1/" → добавить "chat/completions"
    // If ends with "/v1" or "/v1/" → add "chat/completions"
    if (path.endsWith("/v1") || path.endsWith("/v1/")) {
        if (path.endsWith("/")) {
            return path + "chat/completions";
        } else {
            return path + "/chat/completions";
        }
    }
    
    // Иначе добавить "/chat/completions" с правильным слэшем
    // Otherwise add "/chat/completions" with correct slash
    if (path.endsWith("/")) {
        return path + "chat/completions";
    } else {
        return path + "/chat/completions";
    }
}

String OpenAICompatProvider::_buildPrompt(
    const String& station_name,
    const String& artist,
    const String& song,
    const String& track_title
) {
    // СТРОГАЯ ПРОВЕРКА: промпт должен быть доступен (строгий режим, без fallback) / STRICT CHECK: prompt must be available (strict mode, no fallback)
    extern bool aiPromptIsAvailable();
    if (!aiPromptIsAvailable()) {
        AI_LOG("[OpenAICompatProvider] Prompt not available, aborting request");
        return "";  // Пустой промпт → запрос не должен отправляться / Empty prompt → request should not be sent
    }
    
    // Загружаем system prompt из SPIFFS (строгий режим: только из файла) / Load system prompt from SPIFFS (strict mode: only from file)
    String system_prompt;
    bool prompt_loaded = aiPromptGet(system_prompt);  // Автоматически определяет язык из L10N_LANGUAGE / Automatically determines language from L10N_LANGUAGE
    
    // Проверка: промпт должен быть загружен из файла / Check: prompt must be loaded from file
    if (!prompt_loaded || system_prompt.isEmpty()) {
        AI_LOG("[OpenAICompatProvider] Prompt not available, aborting request");
        return "";  // Пустой промпт → запрос не должен отправляться / Empty prompt → request should not be sent
    }
    
    // Формируем user prompt с данными о треке / Build user prompt with track data
    String user_prompt = "";
    if (!artist.isEmpty() && !song.isEmpty()) {
        #if L10N_LANGUAGE==RU
        user_prompt = "Исполнитель: " + artist + "\nТрек: " + song;
        #else
        user_prompt = "Artist: " + artist + "\nTrack: " + song;
        #endif
    } else if (!track_title.isEmpty()) {
        #if L10N_LANGUAGE==RU
        user_prompt = "Трек: " + track_title;
        #else
        user_prompt = "Track: " + track_title;
        #endif
    } else {
        #if L10N_LANGUAGE==RU
        user_prompt = "Трек: неизвестен";
        #else
        user_prompt = "Track: unknown";
        #endif
    }
    
    // Возвращаем system и user prompt, разделённые \n\n для последующего парсинга в _buildRequestJSON()
    // Return system and user prompt separated by \n\n for subsequent parsing in _buildRequestJSON()
    return system_prompt + "\n\n" + user_prompt;
}

String OpenAICompatProvider::_buildRequestJSON(const String& model, const String& prompt_full) {
    // Разделяем system и user prompt (prompt_full содержит оба через \n\n)
    // Split system and user prompt (prompt_full contains both separated by \n\n)
    int separator_pos = prompt_full.indexOf("\n\n");
    String system_prompt = (separator_pos > 0) ? prompt_full.substring(0, separator_pos) : "";
    String user_prompt = (separator_pos > 0) ? prompt_full.substring(separator_pos + 2) : prompt_full;
    
    #ifdef ESP_PLATFORM
    #define WORDS_TO_BYTES(w) ((uint32_t)(w) * sizeof(StackType_t))
    UBaseType_t stack_before = uxTaskGetStackHighWaterMark(nullptr);
    AI_DLOG("[OpenAICompatProvider] Stack before JSON: %u words (~%u bytes)", stack_before, WORDS_TO_BYTES(stack_before));
    #undef WORDS_TO_BYTES
    #endif
    
    // ArduinoJson v7: elastic heap pool (grows as needed). v6-style capacity args are gone;
    // DynamicJsonDocument(N) in v7 also ignores N — use overflowed() to detect OOM.
    // ArduinoJson v7: эластичный heap-пул; аргумент ёмкости не нужен — OOM через overflowed().
    JsonDocument doc;

    // Bail before touching nested arrays/objects on an exhausted doc.
    // Выход до работы с вложенными структурами при исчерпании памяти.
    auto abortIfJsonOverflow = [&]() -> bool {
        if (!doc.overflowed()) return false;
        AI_LOG("[OpenAICompatProvider] JSON request build overflow (heap exhausted)");
        return true;
    };
    
    doc["model"] = model;
    if (abortIfJsonOverflow()) return String();

    // DeepSeek V4: явно отключаем thinking (non-thinking, как legacy deepseek-chat)
    // DeepSeek V4: explicitly disable thinking (non-thinking, like legacy deepseek-chat)
    if (model.startsWith("deepseek-v4-")) {
#if AI_LAYER_DEBUG
        AI_DLOG("[OpenAICompatProvider] DeepSeek V4: thinking disabled");
#endif
        JsonObject thinking = doc["thinking"].to<JsonObject>();
        if (abortIfJsonOverflow()) return String();
        thinking["type"] = "disabled";
        if (abortIfJsonOverflow()) return String();
    }

    JsonArray messages = doc["messages"].to<JsonArray>();
    if (abortIfJsonOverflow()) return String();
    
    // System prompt с правилами согласно манифесту
    // System prompt with rules per manifest
    JsonObject system_msg = messages.add<JsonObject>();
    if (abortIfJsonOverflow()) return String();
    system_msg["role"] = "system";
    system_msg["content"] = system_prompt;
    if (abortIfJsonOverflow()) return String();
    
    // User prompt с данными о треке
    // User prompt with track data
    JsonObject user_msg = messages.add<JsonObject>();
    if (abortIfJsonOverflow()) return String();
    user_msg["role"] = "user";
    user_msg["content"] = user_prompt;
    if (abortIfJsonOverflow()) return String();
    doc["temperature"] = 0.3;
    doc["max_tokens"] = 90;
    doc["stream"] = false;
    
    // Response format - требуем JSON (поддерживается OpenAI-compatible API)
    // Response format - require JSON (supported by OpenAI-compatible API)
    JsonObject response_format = doc["response_format"].to<JsonObject>();
    if (abortIfJsonOverflow()) return String();
    response_format["type"] = "json_object";
    if (abortIfJsonOverflow()) return String();
    
    String json_request;
    serializeJson(doc, json_request);
    return json_request;
}

bool OpenAICompatProvider::_readHTTPResponse(String& response_body, HTTPClient& http) {
    // Используем HTTPClient для чтения ответа / Use HTTPClient to read response
    response_body = http.getString();
    
    if (response_body.length() == 0) {
        AI_DLOG("[OpenAICompatProvider] getString() returned empty, trying getStreamPtr()");
        WiFiClient* stream = http.getStreamPtr();
        if (stream && stream->available()) {
            response_body = stream->readString();
            AI_DLOG("[OpenAICompatProvider] Read from stream, length: %u", response_body.length());
        } else {
            AI_LOG("[OpenAICompatProvider] Stream not available or empty");
            return false;
        }
    }
    
    if (response_body.length() == 0) {
        AI_LOG("[OpenAICompatProvider] Empty response body after all attempts");
        return false;
    }
    
    return true;
}

bool OpenAICompatProvider::_makeHTTPRequest(
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
) {
    // Получаем конфигурацию из runtime cache (без чтения SPIFFS)
    // Get configuration from runtime cache (without reading SPIFFS)
    AIConfig cfg;
    aiGetRuntimeConfig(cfg);
    
    // Валидация минимальных параметров / Validate minimum parameters
    if (strlen(cfg.host) == 0 || cfg.port == 0) {
        AI_LOG("[OpenAICompatProvider] Invalid config: host empty or port=0");
        return false;
    }
    
    // Ограничение timeout: min 1000ms, max 30000ms
    // Timeout limit: min 1000ms, max 30000ms
    uint32_t timeout_ms = cfg.timeout_ms;
    if (timeout_ms < 1000) {
        timeout_ms = 1000;
        AI_DLOG("[OpenAICompatProvider] Timeout clamped to 1000ms (was too low)");
    }
    if (timeout_ms > 30000) {
        timeout_ms = 30000;
        AI_DLOG("[OpenAICompatProvider] Timeout clamped to 30000ms (was too high)");
    }
    
    String prompt = _buildPrompt(station_name, artist, song, track_title);
    
    // СТРОГАЯ ПРОВЕРКА: если промпт пустой (не загружен), abort / STRICT CHECK: if prompt empty (not loaded), abort
    if (prompt.isEmpty()) {
        AI_LOG("[OpenAICompatProvider] Prompt is empty, aborting request");
        return false;
    }
    
    String json_request = _buildRequestJSON(model, prompt);
    prompt = String();  // Release prompt RAM before TLS / Освободить prompt до TLS
    if (json_request.isEmpty()) {
        AI_LOG("[OpenAICompatProvider] JSON request empty, aborting request");
        return false;
    }
    
    // Нормализация пути / Normalize path
    String normalized_path = normalizeChatCompletionsPath(cfg.path);
    
    // Формирование URL / Form URL
    // Для стандартных портов (80/443) порт не указывается в URL
    // For standard ports (80/443) port is not specified in URL
    String protocol = (cfg.port == 80) ? "http://" : "https://";
    String url;
    if (cfg.port == 80 || cfg.port == 443) {
        // Стандартные порты - не указываем порт в URL / Standard ports - don't specify port in URL
        url = protocol + String(cfg.host) + normalized_path;
    } else {
        // Нестандартные порты - указываем порт / Non-standard ports - specify port
        url = protocol + String(cfg.host) + ":" + String(cfg.port) + normalized_path;
    }
    
    const bool use_https = (cfg.port != 80);

    AI_DLOG("[OpenAICompatProvider] URL: %s", url.c_str());
    AI_DLOG("[OpenAICompatProvider] Timeout: %u ms", timeout_ms);

#ifdef ESP_PLATFORM
    if (use_https) {
        const size_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (int_largest < AI_TLS_MIN_INTERNAL_LARGEST) {
            aiHttpsTlsHeapGuardLogSkip(int_largest,
                                       heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                                       ESP.getMinFreeHeap());
            httpCode_out = HTTPC_ERROR_TLS_HEAP_GUARD;
            AI_LOG("[OpenAICompatProvider] HTTPS skipped: tls_internal_heap_too_low");
#if AI_LAYER_DEBUG
            AI_DLOG("[OpenAICompatProvider] stage=memory_guard err=\"tls_internal_heap_too_low\" code=%d",
                    HTTPC_ERROR_TLS_HEAP_GUARD);
#endif
            return false;
        }
    }
#endif

    int httpCode = 0;

    // Per-request HTTPClient + transport lifetime (same scope) / Локальный HTTPClient+transport в одном scope
    WiFiClient plainClient;
    WiFiClientSecure tlsClient;
    HTTPClient http;

    if (!use_https) {
        plainClient.stop();
        http.begin(plainClient, url);
    } else {
        configureHttpsClient(tlsClient, timeout_ms);
        http.begin(tlsClient, url);
    }

    http.setTimeout(timeout_ms);
    http.addHeader("Authorization", "Bearer " + api_key);
    http.addHeader("Content-Type", "application/json");

    httpCode = http.POST(json_request);
    json_request = String();  // Release JSON before read/TLS teardown / Освободить JSON после POST

    AI_LOG("[OpenAICompatProvider] HTTP POST completed, code: %d", httpCode);
#if AI_LAYER_DEBUG
    if (httpCode <= 0 && use_https) {
        AI_DLOG("[OpenAICompatProvider] HTTPClient::errorToString(%d)=\"%s\"",
                httpCode, http.errorToString(httpCode).c_str());
        logTlsLastErrorDebug(tlsClient);
    }
#endif

    auto endHttpAndTransport = [&]() {
        http.end();
        if (use_https) {
            tlsClient.stop();
        } else {
            plainClient.stop();
        }
    };
    
    // Проверяем код ответа / Check response code
    if (httpCode <= 0) {
        AI_LOG("[OpenAICompatProvider] HTTP POST failed, code: %d", httpCode);
        
        // Debug summary для диагностики ошибок HTTP (только при debug=1) / Debug summary for HTTP error diagnostics (only at debug=1)
        {
            // Определяем stage и текст ошибки / Determine stage and error text
            const char* stage = "unknown";
            const char* err_text = "";
            
            // Mapping для определения stage по коду ошибки / Mapping to determine stage by error code
            // HTTPClient error codes (ESP32 Arduino 3.3.x, see HTTPClient.h):
            // -1 = HTTPC_ERROR_CONNECTION_REFUSED (connect() failed in sendRequest)
            // -2 = HTTPC_ERROR_SEND_HEADER_FAILED
            // -3 = HTTPC_ERROR_SEND_PAYLOAD_FAILED
            // -4 = HTTPC_ERROR_NOT_CONNECTED
            // -5 = HTTPC_ERROR_CONNECTION_LOST
            // -11 = HTTPC_ERROR_READ_TIMEOUT
            switch (httpCode) {
                case -1:  // HTTPC_ERROR_CONNECTION_REFUSED (upstream name)
                    stage = "connect";
                    err_text = "connection_refused_or_failed";
                    break;
                case -2:  // HTTPC_ERROR_SEND_HEADER_FAILED
                    stage = "send_header";
                    err_text = "send_header_failed";
                    break;
                case -3:  // HTTPC_ERROR_SEND_PAYLOAD_FAILED
                    stage = "send_payload";
                    err_text = "send_payload_failed";
                    break;
                case -4:  // HTTPC_ERROR_NOT_CONNECTED
                    stage = "connect";
                    err_text = "not_connected";
                    break;
                case -5:  // HTTPC_ERROR_CONNECTION_LOST
                    stage = "connect";  // или send_header, но чаще connect
                    err_text = "connection_lost";
                    break;
                case -6:  // HTTPC_ERROR_NO_STREAM
                    stage = "read_body";
                    err_text = "no_stream";
                    break;
                case -7:  // HTTPC_ERROR_NO_HTTP_SERVER
                    stage = "connect";
                    err_text = "no_http_server";
                    break;
                case -11:  // HTTPC_ERROR_READ_TIMEOUT
                    stage = "read_body";
                    err_text = "read_timeout";
                    break;
                default:
                    stage = "unknown";
                    err_text = "unknown_error";
                    break;
            }
            
            AI_DLOG("[OpenAICompatProvider] HTTP %d debug: https=%d timeout=%ums host=%s:%d path=%s stage=%s err=\"%s\"",
                     httpCode, use_https ? 1 : 0, timeout_ms, cfg.host, cfg.port, normalized_path.c_str(), stage, err_text);
        }
        
        endHttpAndTransport();
        return false;
    }
    
    if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_CREATED) {
        AI_LOG("[OpenAICompatProvider] HTTP error, code: %d", httpCode);
        response_body = http.getString();
        endHttpAndTransport();
        return false;
    }
    
    // Читаем ответ / Read response
    bool success = _readHTTPResponse(response_body, http);
    
    if (!success) {
        endHttpAndTransport();
        return false;
    }
    
    // Сохраняем заголовки перед закрытием / Save headers before closing
    httpCode_out = httpCode;
    content_type_out = http.hasHeader("Content-Type") ? http.header("Content-Type") : "";
    content_length_out = http.hasHeader("Content-Length") ? http.header("Content-Length") : "";
    transfer_encoding_out = http.hasHeader("Transfer-Encoding") ? http.header("Transfer-Encoding") : "";
    content_encoding_out = http.hasHeader("Content-Encoding") ? http.header("Content-Encoding") : "";
    
    endHttpAndTransport();
    return true;
}

String OpenAICompatProvider::_removeBOM(const String& str) {
    // Удаляем UTF-8 BOM (EF BB BF) если присутствует
    // Remove UTF-8 BOM (EF BB BF) if present
    if (str.length() >= 3 && 
        (unsigned char)str[0] == 0xEF && 
        (unsigned char)str[1] == 0xBB && 
        (unsigned char)str[2] == 0xBF) {
        return str.substring(3);
    }
    return str;
}

bool OpenAICompatProvider::_parseJSONResponse(const String& json_raw, LLMResponse& response, int httpCode,
                                              const String& content_type, const String& content_length,
                                              const String& transfer_encoding, const String& content_encoding) {
    // Парсинг JSON ответа от OpenAI-compatible API
    // Parse JSON response from OpenAI-compatible API
    
    // Удаляем BOM и пробелы / Remove BOM and whitespace
    String json = _removeBOM(json_raw);
    json.trim();
    
    AI_DLOG("[OpenAICompatProvider] JSON length: %u, starts with '{': %d", json.length(), json.startsWith("{"));
    
    #ifdef ESP_PLATFORM
    #define WORDS_TO_BYTES(w) ((uint32_t)(w) * sizeof(StackType_t))
    UBaseType_t stack_before = uxTaskGetStackHighWaterMark(nullptr);
    AI_DLOG("[OpenAICompatProvider] Stack before parse: %u words (~%u bytes)", stack_before, WORDS_TO_BYTES(stack_before));
    #undef WORDS_TO_BYTES
    #endif
    
    // Используем ArduinoJson для парсинга / Use ArduinoJson for parsing
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error || doc.overflowed()) {
        if (error) {
            AI_LOG("[OpenAICompatProvider] JSON deserialize error: %s", error.c_str());
        } else {
            AI_LOG("[OpenAICompatProvider] JSON deserialize overflow (heap exhausted)");
        }
        AI_LOG("[OpenAICompatProvider] HTTP code: %d", httpCode);
        
        if (!content_type.isEmpty()) {
            AI_DLOG("[OpenAICompatProvider] Content-Type: %s", content_type.c_str());
        }
        if (!content_length.isEmpty()) {
            AI_DLOG("[OpenAICompatProvider] Content-Length: %s", content_length.c_str());
        }
        if (!transfer_encoding.isEmpty()) {
            AI_DLOG("[OpenAICompatProvider] Transfer-Encoding: %s", transfer_encoding.c_str());
        }
        if (!content_encoding.isEmpty()) {
            AI_DLOG("[OpenAICompatProvider] Content-Encoding: %s", content_encoding.c_str());
        }
        
        AI_DLOG("[OpenAICompatProvider] Body preview (first 120 chars): %s", json.length() > 120 ? json.substring(0, 120).c_str() : json.c_str());
        
        char hex_buf[64] = {0};
        for (int i = 0, j = 0; i < 16 && i < json.length() && j < 60; i++, j += 3) {
            snprintf(hex_buf + j, 4, "%02X ", (unsigned char)json[i]);
        }
        AI_DLOG("[OpenAICompatProvider] Body hex (first 16 bytes): %s", hex_buf);
        
        return false;
    }
    
    AI_DLOG("[OpenAICompatProvider] JSON deserialized successfully");
    
    // Извлекаем content из choices[0].message.content
    if (doc["choices"].isNull()) {
        AI_LOG("[OpenAICompatProvider] No 'choices' key in JSON");
        return false;
    }
    if (!doc["choices"].is<JsonArray>()) {
        AI_LOG("[OpenAICompatProvider] 'choices' is not an array");
        return false;
    }
    if (doc["choices"].size() == 0) {
        AI_LOG("[OpenAICompatProvider] 'choices' array is empty");
        return false;
    }
    
    AI_DLOG("[OpenAICompatProvider] Found choices array");
    
    JsonObject choice = doc["choices"][0];
    if (choice["message"].isNull()) {
        AI_LOG("[OpenAICompatProvider] No 'message' key in choice");
        return false;
    }
    if (choice["message"]["content"].isNull()) {
        AI_LOG("[OpenAICompatProvider] No 'content' key in message");
        return false;
    }
    
    String content = choice["message"]["content"].as<String>();
    content.trim();
    
    AI_DLOG("[OpenAICompatProvider] Content length: %u", content.length());
    AI_DLOG("[OpenAICompatProvider] Content preview (first 200 chars): %s", content.substring(0, 200).c_str());
    
    // Парсим внутренний JSON из content
    JsonDocument content_doc;
    DeserializationError content_error = deserializeJson(content_doc, content);
    
    if (content_error || content_doc.overflowed()) {
        if (content_error) {
            AI_LOG("[OpenAICompatProvider] Content JSON deserialize error: %s", content_error.c_str());
        } else {
            AI_LOG("[OpenAICompatProvider] Content JSON deserialize overflow (heap exhausted)");
        }
        return false;
    }
    
    AI_DLOG("[OpenAICompatProvider] Content JSON deserialized successfully");
    
    // Проверяем обязательное поле "ok"
    if (content_doc["ok"].isNull()) {
        AI_LOG("[OpenAICompatProvider] No 'ok' key in content JSON");
        return false;
    }
    
    response.ok = content_doc["ok"].as<bool>();
    if (!response.ok) {
        // ok=false - валидный ответ, но без интерпретации (модель решила молчать)
        // ok=false - valid response but no interpretation (model decided to stay silent)
        AI_DLOG("[OpenAICompatProvider] HTTP 200 -> not_ok: model returned ok=false");
        return true;
    }
    
    // Проверяем обязательные поля для успешного ответа
    if (content_doc["text"].isNull() || content_doc["mode"].isNull()) {
        response.ok = false;
        // Debug summary для диагностики контракта / Debug summary for contract diagnostics
        AI_DLOG("[OpenAICompatProvider] HTTP 200 -> not_ok: contract missing text=%d mode=%d",
                 !content_doc["text"].isNull() ? 1 : 0, !content_doc["mode"].isNull() ? 1 : 0);
        return true;
    }
    
    response.text = content_doc["text"].as<String>();
    response.mode = content_doc["mode"].as<String>();
    
    // confidence опционален, по умолчанию 0.5
    if (!content_doc["confidence"].isNull()) {
        response.confidence = content_doc["confidence"].as<float>();
    } else {
        response.confidence = 0.5f;
    }
    
    // Валидация значений / Validate values
    if (response.text.isEmpty()) {
        response.ok = false;
        // Debug summary для диагностики пустого текста / Debug summary for empty text diagnostics
        AI_DLOG("[OpenAICompatProvider] HTTP 200 -> not_ok: text_empty_after_parse mode=%s",
                 response.mode.c_str());
        return true;
    }
    
    if (response.mode != "fact" && response.mode != "listen") {
        response.ok = false;
        // Debug summary для диагностики невалидного mode / Debug summary for invalid mode diagnostics
        AI_DLOG("[OpenAICompatProvider] HTTP 200 -> not_ok: mode_invalid got=%s expected=fact|listen",
                 response.mode.c_str());
        return true;
    }
    
    // Удаляем переводы строк из text / Remove newlines from text
    response.text.replace("\n", " ");
    response.text.replace("\r", " ");
    response.text.trim();
    
    return true;
}

bool OpenAICompatProvider::isAvailable(const String& api_key) {
    // Ленивая проверка: фактический запрос при первой необходимости
    // Lazy check: actual request on first need
    return !api_key.isEmpty();
}

bool OpenAICompatProvider::requestInterpretation(
    const String& api_key,
    const String& model,
    const String& station_name,
    const String& artist,
    const String& song,
    const String& track_title,
    LLMResponse& response
) {
    // Проверка входных параметров / Validate input parameters
    if (api_key.isEmpty() || model.isEmpty()) {
        AI_LOG("[OpenAICompatProvider] api_key or model empty");
        return false;
    }
    
    // Инициализируем ответ как неуспешный / Initialize response as unsuccessful
    response = LLMResponse();
    
    // Выполняем HTTP/HTTPS запрос / Perform HTTP/HTTPS request
    String response_body;
    int httpCode;
    String content_type, content_length, transfer_encoding, content_encoding;
    
    if (!_makeHTTPRequest(api_key, model, station_name, artist, song, track_title, 
                         response_body, httpCode, content_type, content_length, 
                         transfer_encoding, content_encoding)) {
        AI_LOG("[OpenAICompatProvider] _makeHTTPRequest() failed");
        return false;
    }
    
    AI_DLOG("[OpenAICompatProvider] Response body length: %u", response_body.length());
    
    // Парсим JSON ответ / Parse JSON response
    if (!_parseJSONResponse(response_body, response, httpCode, content_type, content_length, 
                            transfer_encoding, content_encoding)) {
        AI_LOG("[OpenAICompatProvider] _parseJSONResponse() failed");
        return false;
    }
    
    // Успешный парсинг логируется внутри _parseJSONResponse, не дублируем здесь / Successful parsing is logged inside _parseJSONResponse, don't duplicate here
    AI_DLOG("[OpenAICompatProvider] requestInterpretation() succeeded");
    return true;
}

