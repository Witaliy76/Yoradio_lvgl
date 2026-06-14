#ifndef weather_fetch_h
#define weather_fetch_h

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
 * Returns true if a new forecast was parsed AND published; false otherwise.
 * Возвращает true, если новый прогноз разобран И опубликован; иначе false.
 */
bool weatherFetchForecast(const char* units, const char* lang);

#endif // weather_fetch_h
