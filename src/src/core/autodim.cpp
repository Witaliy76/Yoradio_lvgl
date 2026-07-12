#include "autodim.h"

#include "config.h"
#include "display.h"
#include "options.h"

#include <Arduino.h>

static bool     s_dimmed                = false;
static bool     s_timer_inited          = false;
static uint32_t s_last_user_activity_ms = 0;

static constexpr uint16_t kKnownTimeoutsSec[] = {30, 60, 120, 300, 600};

static void ensure_timer_started() {
    if (!s_timer_inited) {
        s_last_user_activity_ms = millis();
        s_timer_inited = true;
    }
}

static uint32_t user_inactive_ms() {
    ensure_timer_started();
    return static_cast<uint32_t>(millis() - s_last_user_activity_ms);
}

uint8_t autodim_level_max_for_brightness(uint8_t brightness) {
    if (brightness <= 1) {
        return 1;
    }
    return static_cast<uint8_t>(brightness - 1);
}

uint8_t autodim_effective_level() {
    const uint8_t normal = config.store.brightness;
    if (normal <= 1) {
        return 1;
    }
    uint8_t effective = config.store.autodim_level;
    const uint8_t cap = static_cast<uint8_t>(normal - 1);
    if (effective > cap) {
        effective = cap;
    }
    if (effective < 1) {
        effective = 1;
    }
    return effective;
}

static bool is_known_timeout_sec(uint16_t sec) {
    for (uint16_t known : kKnownTimeoutsSec) {
        if (sec == known) {
            return true;
        }
    }
    return false;
}

bool autodim_sanitize_store() {
    bool changed = false;
    if (!is_known_timeout_sec(config.store.autodim_timeout_sec)) {
        config.store.autodim_timeout_sec = 60;
        changed = true;
    }
    const uint8_t max_lv = autodim_level_max_for_brightness(config.store.brightness);
    if (config.store.autodim_level < 1) {
        config.store.autodim_level = 20;
        changed = true;
    }
    if (config.store.autodim_level > max_lv) {
        config.store.autodim_level = max_lv;
        changed = true;
    }
    return changed;
}

static void apply_hardware_brightness(uint8_t pct) {
#ifdef ENABLE_BRIGHTNESS_CONTROL
    dsp.setBrightness(pct);
#else
    (void)pct;
#endif
}

static void restore_awake(const char* source) {
    (void)source;
    if (!s_dimmed) {
        return;
    }
    s_dimmed = false;
    apply_hardware_brightness(config.store.brightness);
}

void autodim_notify_activity(const char* source) {
    (void)source;
    ensure_timer_started();
    s_last_user_activity_ms = millis();

    if (!config.store.autodim_enabled) {
        return;
    }
    if (s_dimmed) {
        restore_awake(source);
    }
}

void autodim_on_disabled() {
    restore_awake("disabled");
}

void autodim_on_dim_level_changed() {
    if (s_dimmed && config.store.autodim_enabled) {
        const uint8_t effective = autodim_effective_level();
        if (effective >= config.store.brightness) {
            restore_awake("dim-level-no-op");
            return;
        }
        apply_hardware_brightness(effective);
    }
}

bool autodim_is_dimmed() {
    return s_dimmed;
}

void autodim_loop() {
    ensure_timer_started();

    if (!config.store.autodim_enabled) {
        if (s_dimmed) {
            restore_awake("disabled-loop");
        }
        return;
    }

    if (config.store.autodim_timeout_sec == 0) {
        return;
    }

    const uint32_t inactive_ms = user_inactive_ms();
    const uint32_t timeout_ms =
        static_cast<uint32_t>(config.store.autodim_timeout_sec) * 1000u;

    if (s_dimmed) {
        return;
    }

    if (inactive_ms < timeout_ms) {
        return;
    }

    const uint8_t normal = config.store.brightness;
    const uint8_t effective = autodim_effective_level();
    // Settled no-op when dim would not darken (e.g. both 1%) / settled без PWM.
    if (effective >= normal) {
        s_dimmed = true;
        return;
    }

    s_dimmed = true;
    apply_hardware_brightness(effective);
}
