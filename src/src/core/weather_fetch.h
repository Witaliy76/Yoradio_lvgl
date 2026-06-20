#ifndef weather_fetch_h
#define weather_fetch_h

#include <cstdint>

/*
 * Weather W1 — OpenWeatherMap 5 day / 3 hour forecast fetch.
 * Weather W1 — загрузка прогноза OWM 5 дней / 3 часа.
 *
 * Transport: plain HTTP (api.openweathermap.org:80), NO TLS — deliberately, so the
 * forecast path does not spin up a second mbedTLS context competing with the AI HTTPS
 * provider for internal heap. Mirrors the existing current-weather request.
 * Транспорт: обычный HTTP (порт 80), без TLS — намеренно, чтобы не поднимать второй
 * mbedTLS-контекст рядом с AI HTTPS. Повторяет подход текущего запроса погоды.
 *
 * Ownership / threading:
 *   - MUST be called only from the weather-sync context (core, doSync on Core 0).
 *   - NEVER call from LVGL/DspTask or any UI path — no network access from UI.
 *   - On success publishes a fresh WeatherState; on any failure keeps last-known-good.
 *   - Только из weather-sync контекста (core, doSync, Core 0); никогда из UI/LVGL.
 *   - При успехе публикует новый WeatherState; при ошибке — keep last-known-good.
 *
 * `units` / `lang` are passed in by the caller (locale-owned PROGMEM strings) to avoid
 * coupling this core module to the display-layer l10n include chain / L10N_LANGUAGE.
 * `units`/`lang` передаёт вызывающий (PROGMEM из locale), чтобы не тащить сюда
 * include-цепочку l10n из display-слоя.
 *
 * Fetch outcome — lets doSync distinguish deferred low-memory from other failures.
 * Результат fetch — doSync отличает отложенный low-memory от прочих ошибок.
 */
enum class WeatherForecastFetchResult : uint8_t {
    Published,            // parsed + published / разобран и опубликован
    NotConfigured,        // weather off or no API key / погода выкл или нет ключа
    NotConnected,         // Wi-Fi not ready / Wi-Fi не готов
    DeferredInternalLow,  // internal heap guard — pending retry / гард heap — отложенный retry
    Failed,               // network/HTTP/parse/other guard / сеть/HTTP/parse/другой гард
};

// HF-W-DNS: forward declaration only — full include in weather_fetch.cpp.
// Using a reference to WeatherEdgeSession does not require complete type in the header.
struct WeatherEdgeSession;

// session carries cycle-level preferred edge (from current weather) and is used for
// request-level deduplication inside the forecast transport loop.
// session несёт cycle-level preferred (от current) и request-level дедупликацию в transport loop.
WeatherForecastFetchResult weatherFetchForecast(const char* units, const char* lang,
                                                WeatherEdgeSession& session);

/*
 * A2b: request an async weather refresh from UI or other non-network tasks.
 * Sets the core scheduler flag only — HTTP runs later in doSync (Core 0), never here.
 * A2b: асинхронный запрос обновления из UI; только флаг — HTTP позже в doSync, не здесь.
 */
void weatherRequestManualRefresh();

// W-R1C.1: coalesced deferred forecast — one pending flag, forecast-only retry in doSync.
// W-R1C.1: отложенный прогноз — один pending, retry только forecast в doSync.
void weatherForecastMarkPending();
bool weatherForecastIsPending();
bool weatherForecastPollPending();       // ticks: admission + cooldown; may arm pending-only doSync
bool weatherForecastTakePendingOnlyRun(); // doSync: consume one forecast-only pass
void weatherForecastDiscardPendingOnlyArm(); // doSync: coalesce when full forceWeather runs

#endif // weather_fetch_h
