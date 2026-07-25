#ifndef MOMENT_LAYER_H
#define MOMENT_LAYER_H

/**
 * moment_layer.h - Moment layer header
 * Description: Autonomous moment layer (predefined phrases, no LLM)
 * Author: Witaliy76 - https://github.com/Witaliy76
 * Date: 21.12.2025
 * Version: Yoradio RGB Panel v0.9.434m-r2
 */

#include "../ai_layer.h"

/**
 * MomentLayer - слой фразы момента
 * MomentLayer - moment phrase layer
 * 
 * Контекстный слой присутствия, не привязан напрямую к смене трека
 * Contextual presence layer, not directly tied to track change
 * 
 * MVP-0: пустая реализация (всегда молчит)
 * MVP-0: empty implementation (always silent)
 */
class MomentLayer : public AILayer {
private:
    bool _enabled;
    // ПРИМЕЧАНИЕ: _last_shown_ms удален - интервал контролируется в AISubsystem через _moment_decided
    // NOTE: _last_shown_ms removed - interval is controlled in AISubsystem via _moment_decided
    
public:
    MomentLayer();
    virtual ~MomentLayer() {}
    
    virtual bool process(const AIContext& context, AICandidate& out) override;
    virtual bool isEnabled() const override { return _enabled; }
    virtual void setEnabled(bool enabled) override { _enabled = enabled; }
    virtual AILayerType getType() const override { return LAYER_MOMENT; }
};

#endif // MOMENT_LAYER_H
