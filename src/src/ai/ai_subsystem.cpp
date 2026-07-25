/**
 * ai_subsystem.cpp - AI subsystem implementation for yoRadio
 * Description: Core AI Layer logic, event handling, layer coordination
 * Author: Witaliy76 - https://github.com/Witaliy76
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "ai_subsystem.h"
#include "utils/utf8_casefold_search.h"
#include "ai_log.h"  // AI Layer logging macros
#include "../core/network.h"
#include "../core/player.h"
#include "../core/display.h"
#include <WiFi.h>  // Для проверки WiFi статуса / For WiFi status check

extern Config config;
extern MyNetwork network;
extern Player player;

AISubsystem::AISubsystem() : _initialized(false) {
}

AISubsystem::~AISubsystem() {
    // Деструктор для очистки ресурсов (если понадобится)
    // Destructor for resource cleanup (if needed)
}

void AISubsystem::init() {
    // Stage 6.0A: explicit subsystem init only — no plugin manager / только явный init
    // Task/layer setup stays in onSetup() (same timing as former plugin-era setup path)
}

void AISubsystem::onSetup() {
    AI_DLOG("[AISubsystem] onSetup() called - AI subsystem initialized");
    AI_DLOG("[AISubsystem] MVP-2: Architecture ready with LLM provider integration");
    AI_DLOG("[AISubsystem] Runtime Manifest: AI is optional, silence is valid");
    
    // Инициализация AI Task Manager для асинхронного выполнения HTTPS запросов
    // Initialize AI Task Manager for asynchronous HTTPS request execution
    if (!_aiTaskManager.begin(&_provider)) {
        AI_LOG("[AISubsystem] WARNING: AI Task Manager initialization failed");
    }
    
    // Передаём Task Manager в InterpretationLayer
    // Pass Task Manager to InterpretationLayer
    _interpretationLayer.setTaskManager(&_aiTaskManager);
    
    _last_pump_time = 0;  // Инициализация времени последнего pump / Initialize last pump time
    _last_ai_activated_state = false;  // Инициализация состояния активации AI / Initialize AI activation state
    _ai_decided_for_track = false;  // Инициализация флага принятия решения / Initialize decision flag
    _enqueue_at_ms = 0;  // Инициализация времени debounce / Initialize debounce time
    _enqueued_for_track_id = 0;  // Инициализация ID трека для enqueue / Initialize track ID for enqueue
    _ai_output_shown = false;  // Инициализация флага показа AI вывода / Initialize AI output shown flag
    _ai_output_track_id = 0;  // Инициализация ID трека для AI вывода / Initialize track ID for AI output
    _moment_decided = false;  // Инициализация флага решения по Moment / Initialize Moment decision flag
    _moment_decided_track_id = 0;  // Инициализация ID трека для решения по Moment / Initialize track ID for Moment decision
    _ai_context_logged_track_id = 0;  // Инициализация ID трека для лога контекста / Initialize track ID for context log
    _tt_validation_logged_track_id = 0;  // Инициализация ID трека для диагностического лога валидации / Initialize track ID for validation diagnostic log
    _last_tt_reason = TrackTitleValidationReason::TT_EMPTY;  // Инициализация причины валидации / Initialize validation reason
    _last_tt_score = 0;  // Инициализация score валидации / Initialize validation score
    _initialized = true;
}

void AISubsystem::_parseTrackTitle(const String& track_title, String& artist, String& song) {
    artist = "";
    song = "";
    
    if (track_title.isEmpty()) {
        return;
    }
    
    // Ищем разделитель " - " / Look for separator " - "
    int sep_pos = track_title.indexOf(" - ");
    if (sep_pos > 0) {
        artist = track_title.substring(0, sep_pos);
        artist.trim();
        song = track_title.substring(sep_pos + 3);
        song.trim();
    } else {
        // Нет разделителя - весь текст в song
        // No separator - all text goes to song
        song = track_title;
        song.trim();
    }
}

void AISubsystem::_buildContext(AIContext& context) {
    // Музыка/радио / Music/radio
    context.station_name = String(config.station.name);
    context.is_playing = player.isRunning();
    
    // Фильтруем track_title от служебных строк
    // Filter track_title from service strings
    String raw_title = String(config.station.title);
    context.track_title = AIDisplayCoordinator::filterTrackTitle(raw_title);
    
    // Парсим artist/song из track_title
    // Parse artist/song from track_title
    _parseTrackTitle(context.track_title, context.artist, context.song);
    
    // Время / Time
    // Проверяем валидность времени / Check time validity
    if (network.timeinfo.tm_year > 100) {
        context.current_hour = network.timeinfo.tm_hour;  // 0-23
    } else {
        context.current_hour = 255;  // Время невалидно / Time invalid
    }
    
    context.uptime_ms = millis();
}

void AISubsystem::_pumpResults() {
    // Периодическая обработка результатов AI Task (независимо от смены трека)
    // Periodic processing of AI Task results (independent of track change)
    uint32_t current_time = millis();
    
    // Rate limiting: вызываем не чаще чем каждые 300ms для снижения нагрузки
    // Rate limiting: call no more than every 300ms to reduce load
    if (current_time - _last_pump_time < 300) {
        return;  // Слишком рано / Too soon
    }
    _last_pump_time = current_time;
    
    // Latch: если решение уже принято для текущего трека - игнорируем новые результаты
    // Latch: if decision already made for current track - ignore new results
    if (_ai_decided_for_track) {
        return;  // Решение принято, больше ничего не меняем / Decision made, don't change anything
    }
    
    AIRequestResult result;
    while (_aiTaskManager.getResult(result)) {
        // LAST GUARD: проверяем, не был ли AI выключен после dequeue
        // LAST GUARD: check if AI was disabled after dequeue
        if (!config.store.ai_enabled) {
            AI_LOG("[AISubsystem] Dropping result because AI disabled");
            continue;  // Не обрабатываем результат если AI выключен / Don't process result if AI disabled
        }
        
        // Диагностический лог: результат получен из очереди (показываем raw данные)
        // Diagnostic log: result dequeued (show raw data)
        AI_DLOG("[AISubsystem] Dequeued result: ok=%d mode=%s track_id=%u current=%u conf=%.2f",
                      result.ok, result.mode, result.track_id, _current_track_id, result.confidence);
        
        // Проверяем, не устарел ли результат / Check if result is stale
        if (result.track_id != _current_track_id) {
            AI_DLOG("[AISubsystem] Stale result dropped: result_id=%u current_id=%u", 
                          result.track_id, _current_track_id);
            continue;  // Пропускаем устаревший результат / Skip stale result
        }
        
        // Latch: если решение уже принято для этого трека - игнорируем результат
        // Latch: if decision already made for this track - ignore result
        if (_ai_decided_for_track) {
            AI_DLOG("[AISubsystem] Decision already made for this track, ignoring result");
            continue;
        }
        
        if (!result.ok) {
            AI_LOG("[AISubsystem] Coordinator reject reason: not_ok");
            // ok=false - молчим, решение принято (silence is valid)
            // ok=false - silence, decision made (silence is valid)
            _ai_decided_for_track = true;
            continue;
        }
        
        if (strlen(result.text) == 0) {
            AI_LOG("[AISubsystem] Coordinator reject reason: empty");
            // Пустой текст - молчим, решение принято (silence is valid)
            // Empty text - silence, decision made (silence is valid)
            _ai_decided_for_track = true;
            continue;
        }
        
        // Предохранитель "строгих фактов" по манифесту с risk scoring
        // "Strict facts" safety gate per manifest with risk scoring
        String mode = String(result.mode);
        float confidence = result.confidence;
        bool was_downgraded = false;  // Флаг: был ли downgrade fact → listen / Flag: was there a downgrade fact → listen
        
        if (mode == "fact") {
            // Risk-scoring для безопасности фактов (без String, без heap-аллокаций)
            // We do NOT hard-ban phrases; we raise required confidence gradually.
            // Goal: keep interesting facts, but suppress high-risk narrative claims unless confidence is high.
            
            // Helper lambda для clamp
            auto clampf = [](float v, float lo, float hi) -> float {
                if (v < lo) return lo;
                if (v > hi) return hi;
                return v;
            };
            
            // Используем const char* напрямую, без String
            const char* txt = result.text;
            
            // Class A: credits / production / collaborations (very specific, high hallucination cost)
            static const char* kRiskA[] = {
                "соавтор", "соавторстве", "продюсер", "продюсировал", "продюсирован",
                "работал с", "в соавторстве",
                "collaborat", "producer", "produced", "co-wrote", "cowrote", "co-writer", "co writer"
            };
            
            // Class B: narrative / origin / dedication / charity / soundtrack (risky but common in true facts)
            static const char* kRiskB1[] = { // +1
                "впервые", "originally", "изначально", "первоначально"
            };
            static const char* kRiskB2[] = { // +2
                "благотвор", "благотворительн", "charity", "benefit",
                "посвящен", "посвящена", "в честь", "в память", "dedicated", "in honor", "in honour",
                "написана для", "записана для", "создана для", "written for",
                "soundtrack", "саундтрек", "по заказу", "commissioned"
            };
            static const char* kRiskB3[] = { // +2 (abbrev expansions are often hallucinated)
                "расшифровывается как"
            };
            
            int riskScore = 0;
            bool hitA = utf8_contains_any_ci(txt, kRiskA, sizeof(kRiskA)/sizeof(kRiskA[0]));
            if (hitA) riskScore += 3;
            
            bool hitB1 = utf8_contains_any_ci(txt, kRiskB1, sizeof(kRiskB1)/sizeof(kRiskB1[0]));
            if (hitB1) riskScore += 1;
            
            bool hitB2 = utf8_contains_any_ci(txt, kRiskB2, sizeof(kRiskB2)/sizeof(kRiskB2[0]));
            if (hitB2) riskScore += 2;
            
            bool hitB3 = utf8_contains_any_ci(txt, kRiskB3, sizeof(kRiskB3)/sizeof(kRiskB3[0]));
            if (hitB3) riskScore += 2;
            
            // Dynamic confidence threshold for facts
            // Base 0.85, then increases with riskScore
            float required_conf = 0.85f;
            if (riskScore == 0) {
                required_conf = 0.85f;
            } else if (riskScore <= 2) {
                required_conf = 0.90f;
            } else if (riskScore <= 4) {
                required_conf = 0.93f;
            } else {
                required_conf = 0.95f;
            }
            
            // If Class A is hit, never go below 0.95 (keep old behavior / improve clarity)
            if (hitA && required_conf < 0.95f) {
                required_conf = 0.95f;
            }
            
            // Cap at 0.97 if you want ultra strict at very high risk
            required_conf = clampf(required_conf, 0.85f, 0.97f);
            
            // Apply downgrade only for "fact"
            if (confidence < required_conf) {
                // Логируем с куском текста для диагностики
                AI_LOG("[AISubsystem] RiskScore=%d (A=%d B1=%d B2=%d B3=%d) required_conf=%.2f, got=%.2f -> downgrade fact->listen text=\"%.100s\"",
                              riskScore, hitA?1:0, hitB1?1:0, hitB2?1:0, hitB3?1:0, required_conf, confidence, result.text);
                mode = "listen";
                confidence = 0.5f;
                was_downgraded = true;
            } else {
                AI_DLOG("[AISubsystem] RiskScore=%d required_conf=%.2f, got=%.2f -> fact allowed",
                              riskScore, required_conf, confidence);
            }
        }
        
        // Расширенный диагностический лог с effective_mode и was_downgraded
        // Extended diagnostic log with effective_mode and was_downgraded
        AI_DLOG("[AISubsystem] Processed result: original_mode=%s effective_mode=%s was_downgraded=%d",
                      result.mode, mode.c_str(), was_downgraded ? 1 : 0);
        
        // Если был downgrade fact → listen: не показываем (silence is valid по манифесту)
        // If there was downgrade fact → listen: don't show (silence is valid per manifest)
        if (was_downgraded) {
            // Не показываем downgraded listen, чтобы не спамить одинаковой строкой
            // Don't show downgraded listen to avoid spamming the same line
            // MVP-1: Очищаем виджет при downgrade / MVP-1: Clear widget on downgrade
            display.setAIInterpretation("");
            AI_LOG("[AISubsystem] Coordinator reject reason: downgraded_fact_to_listen (silence is valid)");
            // Решение принято: молчим / Decision made: silence
            _ai_decided_for_track = true;
            continue;  // Пропускаем этот результат / Skip this result
        }
        
        // Если mode="listen" пришел напрямую от LLM — используем оригинальный текст
        // If mode="listen" came directly from LLM — use original text
        // Формируем кандидата из результата / Build candidate from result
        AICandidate candidate;
        candidate.text = String(result.text);
        // Разводим source_layer по mode / Set source_layer based on mode
        if (mode == "fact") {
            candidate.source_layer = LAYER_FACTS;
        } else {
            candidate.source_layer = LAYER_INTERPRETATION;  // mode == "listen"
        }
        candidate.min_interval_ms = 10000;
        candidate.confidence = confidence;  // Используем скорректированную уверенность / Use adjusted confidence
        
        // Coordinator решает: показывать ли / Coordinator decides: show or not
        bool should_show = _coordinator.shouldShow(&candidate, current_time);
        
        if (should_show) {
            // LAST GUARD: проверяем, не был ли AI выключен перед показом
            // LAST GUARD: check if AI was disabled before showing
            if (!config.store.ai_enabled) {
                AI_LOG("[AISubsystem] Dropping result because AI disabled (before show)");
                continue;  // Не показываем результат если AI выключен / Don't show result if AI disabled
            }
            
            _coordinator.markAsShown(&candidate, current_time);
            
            // MVP-1: Вывод интерпретации на экран / MVP-1: Display interpretation on screen
            display.setAIInterpretation(candidate.text);
            
            // Логируем с корректным префиксом в зависимости от mode / Log with correct prefix based on mode
            if (mode == "fact") {
                AI_LOG("##AI.FACT#: %s", candidate.text.c_str());
            } else {
                AI_LOG("##AI.LISTEN#: %s", candidate.text.c_str());  // mode == "listen"
            }
            AI_DLOG("[AISubsystem] Coordinator: show");
            // Решение принято: текст показан / Decision made: text shown
            _ai_decided_for_track = true;
            // Отмечаем что для этого трека показан AI.FACT или AI.LISTEN / Mark that AI.FACT or AI.LISTEN was shown for this track
            _ai_output_shown = true;
            _ai_output_track_id = _current_track_id;
        } else {
            // Coordinator отклонил - логируем причину (будет видно в shouldShow если добавим детализацию)
            // Coordinator rejected - log reason (will be visible in shouldShow if we add details)
            AI_DLOG("[AISubsystem] Coordinator reject reason: rate_limit_or_duplicate");
            // НЕ выставляем latch здесь - это может быть промежуточный результат
            // Если это финальный результат для трека, latch выставится при следующем невалидном результате
            // DON'T set latch here - this may be intermediate result
            // If this is final result for track, latch will be set on next invalid result
        }
    }
}

bool AISubsystem::_processLayers(const AIContext& context) {
    // СТРОГАЯ ПРОВЕРКА: не обрабатываем слои если AI не активирован
    // STRICT CHECK: don't process layers if AI not activated
    bool ai_activated = _isAIActivated(context, false);
    if (!ai_activated) {
        AI_LOG("[AISubsystem] Skip AI: ai_activated=false");
        return false;
    }
    
    // СТРОГАЯ ПРОВЕРКА: не обрабатываем слои если track_title невалиден (пустой или системный)
    // STRICT CHECK: don't process layers if track_title invalid (empty or system)
    // Примечание: диагностическое логирование уже выполнено в _isAIActivated() или onTrackChange()
    // Note: diagnostic logging already done in _isAIActivated() or onTrackChange()
    if (!_isValidTrackTitleForAI(context.track_title)) {
        // Логируем только если еще не залогировано / Log only if not already logged
        _logTrackTitleValidation(_current_track_id, context.track_title, false);
        return false;
    }
    
    // Логируем валидный track_title только если еще не залогировано / Log valid track_title only if not already logged
    _logTrackTitleValidation(_current_track_id, context.track_title, true);
    
    uint32_t current_time = millis();
    
    // Обрабатываем результаты (также вызывается периодически через _pumpResults)
    // Process results (also called periodically via _pumpResults)
    _pumpResults();
    
    // Latch: если решение уже принято для текущего трека - не обрабатываем слои
    // Latch: if decision already made for current track - don't process layers
    if (_ai_decided_for_track) {
        AI_DLOG("[AISubsystem] Decision already made for track, skipping layer processing");
        return false;
    }
    
    // Обновляем track_id в InterpretationLayer перед обработкой
    // Update track_id in InterpretationLayer before processing
    _interpretationLayer.setTrackId(_current_track_id);
    
    AICandidate candidate;
    bool enqueued_any = false;  // Флаг успешного enqueue / Flag for successful enqueue
    
    // Обрабатываем слой Interpretation (MomentLayer обрабатывается только в тикере)
    // Process Interpretation layer (MomentLayer processed only in ticker)
    AILayer* layers[] = { &_interpretationLayer };
    const char* layer_names[] = { "Interpretation" };
    
    for (size_t i = 0; i < 1; i++) {
        if (!layers[i]->isEnabled()) {
            continue;  // Слой выключен / Layer disabled
        }
        
        // Слой обрабатывает контекст / Layer processes context
        if (layers[i]->process(context, candidate)) {
            // InterpretationLayer возвращает true только при успешном enqueueRequest()
            // InterpretationLayer returns true only on successful enqueueRequest()
            enqueued_any = true;
            
            // Coordinator решает: показывать ли / Coordinator decides: show or not
            if (_coordinator.shouldShow(&candidate, current_time)) {
                // Отмечаем как показанное / Mark as shown
                _coordinator.markAsShown(&candidate, current_time);
                
                // Логирование происходит в слоях (например, ##AI.INTERP# в InterpretationLayer)
                // Logging happens in layers (e.g., ##AI.INTERP# in InterpretationLayer)
                
                // TODO: В будущем здесь будет вывод на экран
                // TODO: In the future, screen output will be here
            }
            // Не логируем фильтрацию или молчание для уменьшения шума
            // Don't log filtering or silence to reduce noise
        }
        // Слой молчит / Layer silent - не логируем (Runtime Manifest: минимизация логов)
        // Layer silent - don't log (Runtime Manifest: minimize logs)
    }
    
    return enqueued_any;
}

bool AISubsystem::_isAIActivated(const AIContext& context, bool log_state_change) {
    // Runtime Manifest Section 1: AI activation conditions
    // Все условия должны выполняться одновременно / All conditions must be met simultaneously
    
    // 0. AI включён в настройках / AI enabled in settings
    if (!config.store.ai_enabled) {
        if (log_state_change) {
            AI_LOG("[AISubsystem] _isAIActivated: ai_enabled=false");
        }
        return false;
    }
    
    // 1. Wi‑Fi подключён / Wi‑Fi connected
    if (network.status != CONNECTED || WiFi.status() != WL_CONNECTED) {
        if (log_state_change) {
            AI_LOG("[AISubsystem] _isAIActivated: WiFi not connected");
        }
        return false;
    }
    
    // 2. Интернет доступен (проверяем наличие IP адреса) / Internet available (check IP)
    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) {
        if (log_state_change) {
            AI_LOG("[AISubsystem] _isAIActivated: No IP address");
        }
        return false;
    }
    
    // 3. Провайдер LLM настроен / LLM provider configured
    if (config.store.llm_provider == LLM_NONE) {
        if (log_state_change) {
            AI_LOG("[AISubsystem] _isAIActivated: llm_provider=LLM_NONE");
        }
        return false;
    }
    
    // 4. API ключ и модель настроены / API key and model configured
    // Унифицированная проверка через strlen() / Unified check via strlen()
    bool has_api_key = (strlen(config.store.ai_api_key) > 0);
    bool has_model = (strlen(config.store.ai_model) > 0);
    if (!has_api_key || !has_model) {
        if (log_state_change) {
            AI_LOG("[AISubsystem] _isAIActivated: API key or model empty");
        }
        return false;
    }
    
    // 5. Валидный музыкальный контекст (реальный трек, не системный статус)
    // Valid music context (real track, not system status)
    // СТРОГАЯ ПРОВЕРКА: track_title должен быть валидным (не пустой, не системный/ошибочный)
    // STRICT CHECK: track_title must be valid (not empty, not system/error)
    if (!_isValidTrackTitleForAI(context.track_title)) {
        if (log_state_change) {
            // Диагностическое логирование невалидного track_title / Diagnostic logging of invalid track_title
            _logTrackTitleValidation(_current_track_id, context.track_title, false);
            
            // Конкретная причина валидации / Specific validation reason
            String msg = "[AISubsystem] _isAIActivated: Invalid context - ";
            if (_last_tt_reason == TrackTitleValidationReason::TT_EMPTY) {
                msg += "empty track_title";
            } else if (_last_tt_reason == TrackTitleValidationReason::TT_HARD_DENY_URL) {
                msg += "hard_deny (url)";
            } else if (_last_tt_reason == TrackTitleValidationReason::TT_HARD_DENY_ERROR ||
                       _last_tt_reason == TrackTitleValidationReason::TT_HARD_DENY_REQUEST_FAILED ||
                       _last_tt_reason == TrackTitleValidationReason::TT_HARD_DENY_HASH_ERROR) {
                msg += "hard_deny (error/request)";
            } else if (_last_tt_reason == TrackTitleValidationReason::TT_STATION_LIKE) {
                msg += "station_like (score=";
                msg += _last_tt_score;
                msg += ")";
            } else {
                msg += "non_track (score=";
                msg += _last_tt_score;
                msg += ")";
            }
            _logOncePerTrack(_current_track_id, msg.c_str());
        }
        return false;
    }
    
    // Логируем валидный track_title / Log valid track_title
    if (log_state_change) {
        _logTrackTitleValidation(_current_track_id, context.track_title, true);
    }
    
    // Все условия выполнены / All conditions met
    if (log_state_change) {
        AI_DLOG("[AISubsystem] _isAIActivated: All conditions met");
    }
    return true;
}

bool AISubsystem::_isLLMReady() const {
    // Проверка готовности LLM без требования track_title
    // Check LLM readiness without track_title requirement
    // Используется для enqueue после debounce в тикере
    // Used for enqueue after debounce in ticker
    
    // 0. AI включён в настройках / AI enabled in settings
    if (!config.store.ai_enabled) {
        return false;
    }
    
    // 1. Wi‑Fi подключён / Wi‑Fi connected
    if (network.status != CONNECTED || WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    // 2. Интернет доступен (проверяем наличие IP адреса) / Internet available (check IP)
    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) {
        return false;
    }
    
    // 3. Провайдер LLM настроен / LLM provider configured
    if (config.store.llm_provider == LLM_NONE) {
        return false;
    }
    
    // 4. API ключ и модель настроены / API key and model configured
    // Унифицированная проверка через strlen() / Unified check via strlen()
    bool has_api_key = (strlen(config.store.ai_api_key) > 0);
    bool has_model = (strlen(config.store.ai_model) > 0);
    if (!has_api_key || !has_model) {
        return false;
    }
    
    // Все условия выполнены (без требования track_title) / All conditions met (without track_title requirement)
    return true;
}

// Helper: подсчёт количества слов в строке / Helper: count words in string
static int _countWords(const String& s) {
    int count = 0;
    bool in_word = false;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        bool is_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (!is_space && !in_word) {
            in_word = true;
            count++;
        } else if (is_space) {
            in_word = false;
        }
    }
    return count;
}

// Helper: проверка наличия букв (латиница или кириллица) / Helper: check for letters (latin or cyrillic)
static bool _hasLetters(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        // Латиница / Latin
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            return true;
        }
        // Кириллица (UTF-8: 0xD0 0x80-0xBF или 0xD1 0x80-0x8F) / Cyrillic
        if ((unsigned char)c == 0xD0 || (unsigned char)c == 0xD1) {
            return true;
        }
    }
    return false;
}

// Helper: проверка на X - X (одинаковые части) / Helper: check for X - X (same parts)
static bool _isSameParts(const String& s) {
    int sep_pos = s.indexOf(" - ");
    if (sep_pos < 0) {
        return false;
    }
    String left = s.substring(0, sep_pos);
    String right = s.substring(sep_pos + 3);
    left.trim();
    right.trim();
    return left.equalsIgnoreCase(right);
}

// Helper: проверка является ли символ буквой или цифрой / Helper: check if character is letter or digit
static bool _isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
           (c >= '0' && c <= '9') || (unsigned char)c == 0xD0 || (unsigned char)c == 0xD1;
}

// Helper: проверка слова с границами слов (word-boundary matching) / Helper: word-boundary matching
// Проверяет что word найдено как отдельное слово, а не как подстрока внутри другого слова
// Checks that word is found as a standalone word, not as substring inside another word
static bool _hasWordBoundary(const String& text_lower, const char* word) {
    int word_len = strlen(word);
    int pos = text_lower.indexOf(word);
    
    while (pos >= 0) {
        // Проверяем границу перед словом / Check boundary before word
        bool boundary_before = (pos == 0) || !_isWordChar(text_lower.charAt(pos - 1));
        // Проверяем границу после слова / Check boundary after word
        bool boundary_after = (pos + word_len >= text_lower.length()) || 
                             !_isWordChar(text_lower.charAt(pos + word_len));
        
        if (boundary_before && boundary_after) {
            return true;  // Найдено как отдельное слово / Found as standalone word
        }
        
        // Ищем следующее вхождение / Search for next occurrence
        pos = text_lower.indexOf(word, pos + 1);
    }
    
    return false;  // Не найдено как отдельное слово / Not found as standalone word
}

bool AISubsystem::_isValidTrackTitleForAI(const String& t) {
    // Score-based валидация track_title для AI / Score-based validation of track_title for AI
    // Проверка валидности track_title (фильтрация системных/станционных/мусорных строк)
    // Check if track_title is valid (filter system/station/garbage strings)
    // Устанавливает _last_tt_reason и _last_tt_score / Sets _last_tt_reason and _last_tt_score
    
    // ЖЁСТКИЕ DENY-ПРАВИЛА (возвращаем false сразу) / HARD DENY RULES (return false immediately)
    // Пустой track_title - невалиден / Empty track_title - invalid
    if (t.isEmpty()) {
        _last_tt_reason = TrackTitleValidationReason::TT_EMPTY;
        _last_tt_score = 0;
        return false;
    }
    
    // Системные/ошибочные строки - невалидны / System/error strings - invalid
    if (t.startsWith("Error connecting to ")) {
        _last_tt_reason = TrackTitleValidationReason::TT_HARD_DENY_ERROR;
        _last_tt_score = 0;
        return false;
    }
    
    if (t.indexOf("Request ") >= 0 && t.indexOf(" failed") >= 0) {
        _last_tt_reason = TrackTitleValidationReason::TT_HARD_DENY_REQUEST_FAILED;
        _last_tt_score = 0;
        return false;
    }
    
    if (t.startsWith("##ERROR#")) {
        _last_tt_reason = TrackTitleValidationReason::TT_HARD_DENY_HASH_ERROR;
        _last_tt_score = 0;
        return false;
    }
    
    // URL всегда невалиден (больше НЕ спасается дефисом) / URL always invalid (no longer saved by dash)
    if (t.indexOf("http://") >= 0 || t.indexOf("https://") >= 0) {
        _last_tt_reason = TrackTitleValidationReason::TT_HARD_DENY_URL;
        _last_tt_score = 0;
        return false;
    }
    
    // SCORE-BASED ВАЛИДАЦИЯ / SCORE-BASED VALIDATION
    int score = 0;
    String t_lower = t;
    t_lower.toLowerCase();
    String breakdown;  // Для логирования breakdown / For breakdown logging
    
    // ПОЗИТИВНЫЕ ПРИЗНАКИ (увеличивают score) / POSITIVE SIGNS (increase score)
    
    // Разделители / Separators
    int sep_count = 0;
    if (t.indexOf(" - ") >= 0) { score++; sep_count++; }
    if (t.indexOf(" — ") >= 0) { score++; sep_count++; }
    if (t.indexOf(" – ") >= 0) { score++; sep_count++; }
    if (t.indexOf(": ") >= 0) { score++; sep_count++; }
    if (t.indexOf(" | ") >= 0) { score++; sep_count++; }
    if (t.indexOf(" / ") >= 0) { score++; sep_count++; }
    if (t.indexOf(" • ") >= 0) { score++; sep_count++; }
    if (sep_count > 0) {
        breakdown += "+sep(";
        breakdown += sep_count;
        breakdown += ") ";
    }
    
    // feat / ft / featuring (любой регистр) / feat / ft / featuring (any case)
    if (t_lower.indexOf("feat") >= 0 || t_lower.indexOf(" ft ") >= 0 || t_lower.indexOf("featuring") >= 0) {
        score++;
        breakdown += "+feat ";
    }
    
    // Скобки с ключевыми словами / Brackets with keywords
    int open_brace = t.indexOf('(');
    int close_brace = t.indexOf(')');
    if (open_brace >= 0 && close_brace > open_brace) {
        String in_braces = t.substring(open_brace + 1, close_brace);
        in_braces.toLowerCase();
        if (in_braces.indexOf("remix") >= 0 || in_braces.indexOf("live") >= 0 ||
            in_braces.indexOf("edit") >= 0 || in_braces.indexOf("acoustic") >= 0) {
            score++;
            breakdown += "+brackets ";
        }
    }
    
    // Количество слов >= 3 / Word count >= 3
    int word_count = _countWords(t);
    if (word_count >= 3) {
        score++;
        breakdown += "+words ";
    }
    
    // Наличие букв (латиница или кириллица) / Presence of letters (latin or cyrillic)
    if (_hasLetters(t)) {
        score++;
        breakdown += "+letters ";
    }
    
    // НЕГАТИВНЫЕ ПРИЗНАКИ (уменьшают score) / NEGATIVE SIGNS (decrease score)
    
    // X - X (левая и правая часть одинаковы) / X - X (left and right parts are same)
    if (_isSameParts(t)) {
        score -= 2;
        breakdown += "-same_parts(-2) ";
    }
    
    // Station-слова (word-boundary matching) / Station words (word-boundary matching)
    // Используем word-boundary matching чтобы не находить слова внутри других слов
    // Use word-boundary matching to avoid finding words inside other words
    if (_hasWordBoundary(t_lower, "radio")) {
        score--;
        breakdown += "-station(radio) ";
    }
    if (_hasWordBoundary(t_lower, "fm")) {
        score--;
        breakdown += "-station(fm) ";
    }
    if (_hasWordBoundary(t_lower, "am")) {
        score--;
        breakdown += "-station(am) ";
    }
    if (_hasWordBoundary(t_lower, "mix")) {
        score--;
        breakdown += "-station(mix) ";
    }
    if (_hasWordBoundary(t_lower, "hits")) {
        score--;
        breakdown += "-station(hits) ";
    }
    if (_hasWordBoundary(t_lower, "top")) {
        score--;
        breakdown += "-station(top) ";
    }
    if (_hasWordBoundary(t_lower, "playlist")) {
        score--;
        breakdown += "-station(playlist) ";
    }
    // "live" как station-слово (не в скобках, с word-boundary) / "live" as station word (not in brackets, with word-boundary)
    if (_hasWordBoundary(t_lower, "live")) {
        // Проверяем, не в скобках ли "live" / Check if "live" not in brackets
        int live_pos = t_lower.indexOf("live");
        if (live_pos >= 0) {
            int brace_level = 0;
            for (int i = 0; i < live_pos; i++) {
                if (t.charAt(i) == '(') brace_level++;
                else if (t.charAt(i) == ')') brace_level--;
            }
            // Если "live" не в скобках (brace_level == 0) - это station-слово / If "live" not in brackets - station word
            if (brace_level == 0) {
                score--;
                breakdown += "-station(live) ";
            }
        }
    }
    if (_hasWordBoundary(t_lower, "stream")) {
        score--;
        breakdown += "-station(stream) ";
    }
    if (_hasWordBoundary(t_lower, "channel")) {
        score--;
        breakdown += "-station(channel) ";
    }
    
    // Длина строки < 6 / String length < 6
    if (t.length() < 6) {
        score--;
        breakdown += "-short ";
    }
    
    // Логирование breakdown score (один раз на track_id) / Breakdown score logging (once per track_id)
    // Используем статическую переменную для отслеживания последнего залогированного track_id
    // Use static variable to track last logged track_id
    static uint32_t last_breakdown_track_id = 0;
    if (last_breakdown_track_id != _current_track_id) {
        if (breakdown.length() > 0) {
            breakdown.trim();
            breakdown += " => ";
            breakdown += score;
            String breakdown_msg = "[AISubsystem] TT score details: ";
            breakdown_msg += breakdown;
            // Используем _logOncePerTrack вместо AI_DLOG чтобы избежать переполнения WebSocket очереди
            // Use _logOncePerTrack instead of AI_DLOG to avoid WebSocket queue overflow
            _logOncePerTrack(_current_track_id, breakdown_msg.c_str());
        }
        last_breakdown_track_id = _current_track_id;
    }
    
    // ПОРОГИ ПРИНЯТИЯ РЕШЕНИЯ / DECISION THRESHOLDS
    // score >= 3 → VALID track / score >= 3 → VALID track
    if (score >= 3) {
        _last_tt_reason = TrackTitleValidationReason::TT_VALID;
        _last_tt_score = score;
        return true;
    }
    
    // score <= -2 → INVALID track (station-like) / score <= -2 → INVALID track (station-like)
    if (score <= -2) {
        _last_tt_reason = TrackTitleValidationReason::TT_STATION_LIKE;
        _last_tt_score = score;
        return false;
    }
    
    // иначе → INVALID (score слишком низкий) / otherwise → INVALID (score too low)
    _last_tt_reason = TrackTitleValidationReason::TT_SCORE_TOO_LOW;
    _last_tt_score = score;
    return false;
}

// Helper: преобразование enum в строку для логов / Helper: convert enum to string for logs
static const char* _ttReasonToString(TrackTitleValidationReason reason) {
    switch (reason) {
        case TrackTitleValidationReason::TT_VALID: return "TT_VALID";
        case TrackTitleValidationReason::TT_EMPTY: return "TT_EMPTY";
        case TrackTitleValidationReason::TT_HARD_DENY_ERROR: return "TT_HARD_DENY_ERROR";
        case TrackTitleValidationReason::TT_HARD_DENY_REQUEST_FAILED: return "TT_HARD_DENY_REQUEST_FAILED";
        case TrackTitleValidationReason::TT_HARD_DENY_HASH_ERROR: return "TT_HARD_DENY_HASH_ERROR";
        case TrackTitleValidationReason::TT_HARD_DENY_URL: return "TT_HARD_DENY_URL";
        case TrackTitleValidationReason::TT_SCORE_TOO_LOW: return "TT_SCORE_TOO_LOW";
        case TrackTitleValidationReason::TT_STATION_LIKE: return "TT_STATION_LIKE";
        default: return "TT_UNKNOWN";
    }
}

bool AISubsystem::_logOncePerTrack(uint32_t track_id, const char* message) {
    // Helper для логов один раз на track_id / Helper for logs once per track_id
    if (_ai_context_logged_track_id != track_id) {
        AI_DLOG("%s", message);
        _ai_context_logged_track_id = track_id;
        return true;  // Лог выведен / Log printed
    }
    return false;  // Лог уже был выведен для этого track_id / Log already printed for this track_id
}

// Helper: диагностическое логирование валидации track_title / Helper: diagnostic logging of track_title validation
void AISubsystem::_logTrackTitleValidation(uint32_t track_id, const String& title, bool is_valid) {
    // Логируем один раз на track_id / Log once per track_id
    if (_tt_validation_logged_track_id == track_id) {
        return;  // Уже залогировано / Already logged
    }
    
    String title_short = title;
    if (title_short.length() > 64) {
        title_short = title_short.substring(0, 64);
    }
    
    if (is_valid) {
        // Валидный track_title / Valid track_title
        AI_LOG("[AISubsystem] TrackTitle valid: score=%d title=\"%s\"", 
                 _last_tt_score, title_short.c_str());
        _tt_validation_logged_track_id = track_id;
    } else {
        // Невалидный track_title / Invalid track_title
        const char* reason_str = _ttReasonToString(_last_tt_reason);
        AI_LOG("[AISubsystem] TrackTitle invalid: reason=%s score=%d title=\"%s\"", 
                 reason_str, _last_tt_score, title_short.c_str());
        _tt_validation_logged_track_id = track_id;
    }
}

void AISubsystem::onTrackChange() {
    // Инкрементируем ID трека при валидной смене трека
    // Increment track ID on valid track change
    _current_track_id++;
    AI_LOG("[AISubsystem] Track changed, new track_id: %u", _current_track_id);
    
    // Сбрасываем флаг принятия решения для нового трека
    // Reset decision flag for new track
    _ai_decided_for_track = false;
    
    // Сбрасываем флаг показа AI вывода при смене трека
    // Reset AI output shown flag on track change
    _ai_output_shown = false;
    _ai_output_track_id = _current_track_id;
    
    // Сбрасываем флаг решения по Moment при смене трека (one-shot логика)
    // Reset Moment decision flag on track change (one-shot logic)
    _moment_decided = false;
    _moment_decided_track_id = _current_track_id;
    
    // Сбрасываем флаг лога контекста при смене трека (антиспам логов)
    // Reset context log flag on track change (log anti-spam)
    _ai_context_logged_track_id = 0;
    _tt_validation_logged_track_id = 0;  // Сбрасываем флаг диагностического лога валидации / Reset validation diagnostic log flag
    
    // Устанавливаем время debounce: запрос можно отправить через 4 секунды
    // Set debounce time: request can be sent after 4 seconds
    uint32_t now = millis();
    _enqueue_at_ms = now + 4000;  // Debounce 4 секунды / Debounce 4 seconds
    _enqueued_for_track_id = 0;  // Сбрасываем флаг отправки запроса / Reset enqueue flag
    
    // MVP-1: Очищаем виджет интерпретации при смене трека / MVP-1: Clear interpretation widget on track change
    display.setAIInterpretation("");
    
    if (!_initialized) {
        AI_DLOG("[AISubsystem] onTrackChange() called but not initialized");
        return;
    }

    // MVP-2: Формируем контекст и проверяем условия активации
    // MVP-2: Build context and check activation conditions
    AIContext context;
    _buildContext(context);
    
    // Временное логирование для отладки / Temporary logging for debugging
    // Унифицированная проверка через strlen() / Unified check via strlen()
    bool has_api_key = (strlen(config.store.ai_api_key) > 0);
    bool has_model = (strlen(config.store.ai_model) > 0);
    AI_DLOG("[AISubsystem] onTrackChange() - ai_enabled=%d, llm_provider=%d, has_api_key=%d, has_model=%d, track_title=\"%s\"",
            config.store.ai_enabled, config.store.llm_provider, has_api_key ? 1 : 0, has_model ? 1 : 0, context.track_title.c_str());
    
    // РАННИЙ ABORT: проверяем валидность track_title до активации AI
    // EARLY ABORT: check track_title validity before AI activation
    if (!_isValidTrackTitleForAI(context.track_title)) {
        // Диагностическое логирование невалидного track_title / Diagnostic logging of invalid track_title
        _logTrackTitleValidation(_current_track_id, context.track_title, false);
        
        // Отменяем debounce таймер / Cancel debounce timer
        _enqueue_at_ms = 0;
        _enqueued_for_track_id = _current_track_id;  // Помечаем что попытка была / Mark attempt as made
        
        // AI молчит для невалидного track_title / AI silent for invalid track_title
        AI_LOG("[AISubsystem] TrackTitle invalid - aborting, AI silent");
        return;
    }
    
    // Логируем валидный track_title / Log valid track_title
    _logTrackTitleValidation(_current_track_id, context.track_title, true);
    
    // Runtime Manifest: AI активируется только при выполнении всех условий
    // Runtime Manifest: AI activates only when all conditions are met
    bool ai_activated = _isAIActivated(context, true);  // Логируем состояние при track_change / Log state on track_change
    if (!ai_activated) {
        AI_LOG("[AISubsystem] AI not activated - skipping");
        _last_ai_activated_state = false;  // Обновляем кеш состояния / Update state cache
        return;
    }
    
    // Обновляем кеш состояния активации чтобы избежать дубля в on_ticker / Update activation state cache to avoid duplicate in on_ticker
    _last_ai_activated_state = true;
    
    AI_LOG("[AISubsystem] AI activated - debounce scheduled, will process layers after 4s");
    // НЕ вызываем _processLayers() сразу - запрос уйдет через тикер после debounce
    // DON'T call _processLayers() immediately - request will be sent via ticker after debounce
}

void AISubsystem::onTicker() {
    // Вызывается из ticks() каждую секунду / Called from ticks() every second
    // Вызываем _pumpResults() для периодической обработки результатов AI Task
    // Call _pumpResults() for periodic processing of AI Task results
    _pumpResults();
    
    uint32_t now = millis();
    AIContext context;
    _buildContext(context);
    
    // Проверяем, активирован ли AI (логируем только при смене состояния)
    // Check if AI is activated (log only on state change)
    bool ai_activated = _isAIActivated(context, false);  // Не логируем каждый тик / Don't log every tick
    if (ai_activated != _last_ai_activated_state) {
        _last_ai_activated_state = ai_activated;
        _isAIActivated(context, true);  // Логируем смену состояния / Log state change
    }
    
    // ИСКЛЮЧЕНИЕ: отправка LLM запроса после debounce (единственный случай enqueue из тикера)
    // EXCEPTION: send LLM request after debounce (only case of enqueue from ticker)
    // STRICT MODE: проверяем все условия перед обработкой / STRICT MODE: check all conditions before processing
    bool llm_ready = _isLLMReady();
    if (llm_ready && _enqueue_at_ms > 0 && now >= _enqueue_at_ms && 
        _enqueued_for_track_id != _current_track_id && !_ai_decided_for_track) {
        
        // СТРОГАЯ ПРОВЕРКА: debounce не должен проходить если ai_activated=false или track_title пустой
        // STRICT CHECK: debounce should not pass if ai_activated=false or track_title empty
        if (!ai_activated) {
            AI_LOG("[AISubsystem] Debounce aborted: ai_activated=false");
            _enqueued_for_track_id = _current_track_id;  // Помечаем что попытка была / Mark attempt as made
            _enqueue_at_ms = 0;  // Сбрасываем debounce таймер / Reset debounce timer
            return;  // Не обрабатываем слои / Don't process layers
        }
        
        // СТРОГАЯ ПРОВЕРКА: debounce не должен проходить если track_title невалиден (пустой или системный)
        // STRICT CHECK: debounce should not pass if track_title invalid (empty or system)
        if (!_isValidTrackTitleForAI(context.track_title)) {
            // Диагностическое логирование невалидного track_title / Diagnostic logging of invalid track_title
            _logTrackTitleValidation(_current_track_id, context.track_title, false);
            _logOncePerTrack(_current_track_id, "[AISubsystem] Debounce aborted: invalid track_title");
            _enqueued_for_track_id = _current_track_id;  // Помечаем что попытка была / Mark attempt as made
            _enqueue_at_ms = 0;  // Сбрасываем debounce таймер / Reset debounce timer
            return;  // Не обрабатываем слои / Don't process layers
        }
        
        // Диагностический лог перед enqueue (вместо повторного "All conditions met") / Diagnostic log before enqueue (instead of repeated "All conditions met")
        AI_DLOG("[AISubsystem] Debounce check: track_title_len=%d llm_ready=%d ai_activated=%d",
                 context.track_title.length(), llm_ready ? 1 : 0, ai_activated ? 1 : 0);
        
        // Debounce прошел, отправляем запрос один раз для текущего трека
        // Debounce passed, send request once for current track
        AI_LOG("[AISubsystem] Debounce passed, processing layers to enqueue LLM request");
        bool enqueued = _processLayers(context);
        
        // ВСЕГДА выставляем флаги после попытки (строго 1 attempt per track)
        // ALWAYS set flags after attempt (strictly 1 attempt per track)
        _enqueued_for_track_id = _current_track_id;  // Помечаем что попытка была / Mark attempt as made
        _enqueue_at_ms = 0;  // Сбрасываем debounce таймер / Reset debounce timer
        
        if (enqueued) {
            AI_LOG("[AISubsystem] LLM request enqueued successfully");
        } else {
            // Различаем причины неудачного enqueue / Distinguish reasons for failed enqueue
            // Если track_title пустой - это уже обработано выше, здесь только busy/rate limit
            // If track_title empty - already handled above, here only busy/rate limit
            AI_LOG("[AISubsystem] LLM enqueue attempt failed (rate limit/busy) -> silence for this track");
        }
    }
    
    // MomentLayer: fallback-слой, срабатывает только после неуспешного LLM результата
    // MomentLayer: fallback layer, triggers only after unsuccessful LLM result
    // ONE-SHOT ЛОГИКА: решение по Moment принимается ровно один раз на track_id
    // ONE-SHOT LOGIC: Moment decision made exactly once per track_id
    // УСЛОВИЯ: валидный track_title + AI активирован + решение принято (LLM молчит)
    // CONDITIONS: valid track_title + AI activated + decision made (LLM silent)
    if (_isValidTrackTitleForAI(context.track_title) && ai_activated && _ai_decided_for_track) {
        // ONE-SHOT: если решение по Moment уже принято для этого трека - пропускаем
        // ONE-SHOT: if Moment decision already made for this track - skip
        if (_moment_decided && _moment_decided_track_id == _current_track_id) {
            return;  // Решение уже принято / Decision already made
        }
        
        // Блокируем MomentLayer если для этого трека уже показан AI.FACT или AI.LISTEN
        // Block MomentLayer if AI.FACT or AI.LISTEN already shown for this track
        if (_ai_output_shown && _ai_output_track_id == _current_track_id) {
            AI_DLOG("[AISubsystem] Moment blocked: AI output already shown for track");
            _moment_decided = true;  // Решение принято: блокировка / Decision made: blocked
            _moment_decided_track_id = _current_track_id;
            return;  // Не показываем Moment / Don't show Moment
        }
        
        // Проверяем минимальные условия для MomentLayer
        // Check minimal conditions for MomentLayer
        // СТРОГАЯ ПРОВЕРКА: MomentLayer тоже требует промпт (AI молчит полностью без промпта)
        // STRICT CHECK: MomentLayer also requires prompt (AI is completely silent without prompt)
        extern bool aiPromptIsAvailable();
        bool moment_ready = config.store.ai_enabled &&
                            (network.status == CONNECTED) &&
                            (WiFi.status() == WL_CONNECTED) &&
                            (WiFi.localIP() != IPAddress(0, 0, 0, 0)) &&
                            aiPromptIsAvailable();  // Промпт должен быть доступен / Prompt must be available
        
        if (moment_ready) {
            // MomentLayer автономен: только локальные шаблоны, никаких LLM запросов
            // MomentLayer is autonomous: only local templates, no LLM requests
            // Fallback: когда LLM решил молчать (ok=false, пустой текст, downgrade)
            // Fallback: when LLM decided to stay silent (ok=false, empty text, downgrade)
            // Логируем напрямую (не через _logOncePerTrack) чтобы избежать конфликтов с другими логами
            // Log directly (not via _logOncePerTrack) to avoid conflicts with other logs
            if (!_moment_decided || _moment_decided_track_id != _current_track_id) {
                AI_LOG("[AISubsystem] Moment fallback: LLM silent, showing moment");
            }
            AICandidate moment_candidate;
            if (_momentLayer.process(context, moment_candidate)) {
                // MomentLayer вернул кандидата - обрабатываем его
                // MomentLayer returned candidate - process it
                // ВАЖНО: MomentLayer - это fallback, обходим Coordinator (он должен показываться всегда при LLM silence)
                // IMPORTANT: MomentLayer is fallback, bypass Coordinator (should always show on LLM silence)
                if (!moment_candidate.text.isEmpty()) {
                    uint32_t current_time = millis();
                    // Обходим Coordinator для fallback-слоя / Bypass Coordinator for fallback layer
                    _coordinator.markAsShown(&moment_candidate, current_time);
                    display.setAIInterpretation(moment_candidate.text);
                    AI_LOG("##AI.MOMENT#: %s", moment_candidate.text.c_str());
                    // Убираем дополнительное диагностическое логирование чтобы избежать переполнения WebSocket очереди
                    // Remove additional diagnostic logging to avoid WebSocket queue overflow
                } else {
                    // Текст пустой - логируем и не показываем / Text empty - log and don't show
                    AI_DLOG("[MomentLayer] ERROR: empty moment text");
                }
            } else {
                // MomentLayer.process() вернул false - не готов / MomentLayer.process() returned false - not ready
                // Убираем диагностическое логирование track_title чтобы избежать переполнения
                // Remove track_title diagnostic logging to avoid overflow
                AI_DLOG("[MomentLayer] process() returned false");
            }
            // Решение принято: Moment обработан (показан или нет) / Decision made: Moment processed (shown or not)
            _moment_decided = true;
            _moment_decided_track_id = _current_track_id;
        } else {
            // Условия не выполнены - решение принято: Moment не нужен / Conditions not met - decision made: Moment not needed
            _moment_decided = true;
            _moment_decided_track_id = _current_track_id;
        }
    }
}

// Публичный метод для периодического вызова (можно вызывать из main loop)
// Public method for periodic calls (can be called from main loop)
// ВАЖНО: Для интеграции в main loop нужно добавить вызов в network.ticks() или main loop()
// IMPORTANT: To integrate into main loop, add call to network.ticks() or main loop()
// Пока вызывается только из _processLayers() при смене трека
// Currently called only from _processLayers() on track change

void AISubsystem::onEnabledChanged(bool enabled) {
    // Проверяем, не изменилось ли состояние на самом деле / Check if state actually changed
    // Избегаем повторных вызовов для одного и того же состояния / Avoid repeated calls for the same state
    if (enabled == config.store.ai_enabled) {
        // Состояние уже соответствует - пропускаем / State already matches - skip
        return;
    }
    
    if (!enabled) {
        // AI выключен - отменяем все операции / AI disabled - cancel all operations
        AI_LOG("[AISubsystem] AI disabled: canceling timers, clearing queues, clearing display");
        
        // Сбрасываем внутренние флаги / Reset internal flags
        _enqueue_at_ms = 0;  // Отменяем debounce timer / Cancel debounce timer
        _enqueued_for_track_id = 0;  // Сбрасываем флаг отправки запроса / Reset enqueue flag
        _ai_decided_for_track = false;  // Сбрасываем флаг принятия решения / Reset decision flag
        _last_ai_activated_state = false;  // Сбрасываем кеш состояния активации / Reset activation state cache
        _ai_output_shown = false;  // Сбрасываем флаг показа AI вывода / Reset AI output shown flag
        _ai_output_track_id = 0;  // Сбрасываем ID трека для AI вывода / Reset track ID for AI output
        _moment_decided = false;  // Сбрасываем флаг решения по Moment / Reset Moment decision flag
        _moment_decided_track_id = 0;  // Сбрасываем ID трека для решения по Moment / Reset track ID for Moment decision
        _ai_context_logged_track_id = 0;  // Сбрасываем флаг лога контекста / Reset context log flag
        _tt_validation_logged_track_id = 0;  // Сбрасываем флаг диагностического лога валидации / Reset validation diagnostic log flag
        
        // Очищаем очереди менеджера задач / Clear task manager queues
        _aiTaskManager.cancelAll();
        
        // Очищаем отображение / Clear display
        if (display.ready()) {
            display.setAIInterpretation("");
        }
    } else {
        // AI включён - логируем / AI enabled - log
        AI_LOG("[AISubsystem] AI enabled");
    }
}

AISubsystem aiSubsystem;
