/**
 * moment_layer.cpp - Moment layer implementation
 * Description: Autonomous moment layer with predefined phrases (no LLM)
 * Author: Witaliy76 - https://github.com/Witaliy76
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "moment_layer.h"
#include "../../core/options.h"  // Для L10N_LANGUAGE / For L10N_LANGUAGE

// Предустановленные фразы для MomentLayer (без LLM) / Predefined phrases for MomentLayer (no LLM)
// Русские фразы / Russian phrases
#if L10N_LANGUAGE == RU
static const char* kMomentPhrases[] = {
    "Всё идёт своим темпом.",
    "Сейчас можно не торопиться.",
    "Звук держит пространство ровно.",
    "Ничего не требует ответа.",
    "Тишина между событиями тоже часть дня."
};
// Английские фразы / English phrases
#else
static const char* kMomentPhrases[] = {
    "Everything moves at its own pace.",
    "There's no need to rush right now.",
    "Sound holds the space steady.",
    "Nothing requires an answer.",
    "The silence between events is also part of the day."
};
#endif
static const size_t kMomentPhrasesCount = sizeof(kMomentPhrases) / sizeof(kMomentPhrases[0]);

MomentLayer::MomentLayer() : _enabled(true) {
    // Fallback-слой: срабатывает только после неуспешного LLM результата на валидном треке
    // Fallback layer: triggers only after unsuccessful LLM result on valid track
    // Интервал контролируется в AISubsystem через _moment_decided (one-shot на track_id)
    // Interval is controlled in AISubsystem via _moment_decided (one-shot per track_id)
}

bool MomentLayer::process(const AIContext& context, AICandidate& out) {
    // Диагностика: вход в process() / Diagnostics: entry to process()
    //Serial.print("[MomentLayer] process() entered, track_title=\"");
    //Serial.print(context.track_title);
    //Serial.println("\"");
    
    // MomentLayer - fallback-слой: показывается ТОЛЬКО когда есть валидный трек
    // MomentLayer - fallback layer: shows ONLY when there's a valid track
    // НЕ должен срабатывать при пустом track_title (boot/соединение)
    // Should NOT trigger on empty track_title (boot/connection)
    if (context.track_title.isEmpty()) {
        return false;  // Нет валидного трека - молчим / No valid track - silent
    }
    
    // ПРИМЕЧАНИЕ: Интервал контролируется в AISubsystem через _moment_decided (one-shot на track_id)
    // NOTE: Interval is controlled in AISubsystem via _moment_decided (one-shot per track_id)
    // Для fallback-слоя интервал не нужен - он должен срабатывать для каждого нового трека когда LLM молчит
    // For fallback layer interval is not needed - it should trigger for each new track when LLM is silent
    
    // Выбираем случайную фразу из предустановленных
    // Select random phrase from predefined
    static uint32_t phrase_index = 0;
    phrase_index = (phrase_index + 1) % kMomentPhrasesCount;
    
    // Формируем кандидата
    // Build candidate
    out.text = String(kMomentPhrases[phrase_index]);
    out.source_layer = LAYER_MOMENT;
    out.min_interval_ms = 0;  // Интервал контролируется в AISubsystem / Interval controlled in AISubsystem
    out.confidence = 1.0f;  // Всегда уверены в предустановленных фразах / Always confident in predefined phrases
    
    // Диагностика: длина текста перед возвратом / Diagnostics: text length before return
    // Serial.printf("[MomentLayer] text len=%d before emit\n", out.text.length());
    
    return true;  // Возвращаем кандидата / Return candidate
}
