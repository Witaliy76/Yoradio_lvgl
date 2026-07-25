#ifndef AI_SUBSYSTEM_H
#define AI_SUBSYSTEM_H

/**
 * ai_subsystem.h - AI subsystem facade for yoRadio (Stage 6.0)
 * Description: Explicit AI ownership; event adapter and display coordination (not a Plugin)
 * Author: Witaliy76 - https://github.com/Witaliy76
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "../core/config.h"
#include "ai_types.h"
#include "ai_coordinator.h"
#include "ai_layer.h"
#include "layers/interpretation_layer.h"
#include "layers/moment_layer.h"
#include "providers/openai_compat_provider.h"
#include "ai_task.h"

/**
 * Причины валидации track_title / Track title validation reasons
 */
enum class TrackTitleValidationReason {
    TT_VALID,                    // Валидный трек / Valid track
    TT_EMPTY,                    // Пустой track_title / Empty track_title
    TT_HARD_DENY_ERROR,          // Системная ошибка (Error connecting to...) / System error
    TT_HARD_DENY_REQUEST_FAILED, // Ошибка запроса (Request ... failed) / Request error
    TT_HARD_DENY_HASH_ERROR,     // Ошибка с ##ERROR# / Hash error
    TT_HARD_DENY_URL,            // URL в строке / URL in string
    TT_SCORE_TOO_LOW,            // Score слишком низкий (< 3) / Score too low
    TT_STATION_LIKE              // Станционная строка (score <= -2) / Station-like string
};

/**
 * AISubsystem - явная AI-подсистема yoRadio (не Plugin)
 * AISubsystem - explicit AI subsystem for yoRadio (not a Plugin)
 *
 * Orchestrates src/ai/* core; core events call aiSubsystem directly.
 */
class AISubsystem {
public:
    AISubsystem();
    ~AISubsystem();

    // Explicit lifecycle — direct hooks, no plugin manager / Явный lifecycle без pm
    void init();

    void onSetup();
    void onTrackChange();
    void onTicker();  // 1 Hz from network ticks() / 1 Гц из network ticks()
    void onEnabledChanged(bool enabled);

private:
    bool _initialized;
    uint32_t _current_track_id;
    uint32_t _last_pump_time;
    bool _last_ai_activated_state;
    bool _ai_decided_for_track;
    uint32_t _enqueue_at_ms;
    uint32_t _enqueued_for_track_id;
    bool _ai_output_shown;
    uint32_t _ai_output_track_id;
    bool _moment_decided;
    uint32_t _moment_decided_track_id;
    uint32_t _ai_context_logged_track_id;
    uint32_t _tt_validation_logged_track_id;
    TrackTitleValidationReason _last_tt_reason;
    int8_t _last_tt_score;

    AIDisplayCoordinator _coordinator;
    InterpretationLayer _interpretationLayer;
    MomentLayer _momentLayer;
    OpenAICompatProvider _provider;
    AITaskManager _aiTaskManager;

    void _pumpResults();
    void _buildContext(AIContext& context);
    void _parseTrackTitle(const String& track_title, String& artist, String& song);
    bool _processLayers(const AIContext& context);
    bool _isAIActivated(const AIContext& context, bool log_state_change = true);
    bool _isLLMReady() const;
    bool _isValidTrackTitleForAI(const String& t);
    bool _logOncePerTrack(uint32_t track_id, const char* message);
    void _logTrackTitleValidation(uint32_t track_id, const String& title, bool is_valid);
};

extern AISubsystem aiSubsystem;

#endif // AI_SUBSYSTEM_H
