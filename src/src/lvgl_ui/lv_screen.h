#ifndef LV_SCREEN_H
#define LV_SCREEN_H

#ifdef __cplusplus

// Forward declare LVGL root object (avoid including lvgl.h in headers).
// Предварительное объявление корневого объекта LVGL (без включения lvgl.h в заголовках).
extern "C" {
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
}

namespace lvgl_ui {

// Screen taxonomy — Stage 4.6 frozen (see docs/stage_4_6_spec_freeze.md).
// Full taxonomy also includes Overlay + Screensaver; those are NOT ScreenType values
// (overlays use lv_layer_top(); screensaver = inactivity mechanism — Stage 5+).
// Таксономия — Stage 4.6 freeze. Overlay и Screensaver не в enum (отдельные механизмы).
enum class ScreenType {
    Boot,            // startup only / только при старте
    Page,            // horizontal carousel / горизонтальная карусель
    Temporary,       // only Preset in product scope / в продукте только Preset
    RebootRequired   // WiFi setup, exit = reboot only / WiFi, выход только через reboot
};

// Lifecycle for LVGL-owned full screens (not overlays on lv_layer_top).
// Жизненный цикл полноэкранных экранов LVGL (не оверлеи на lv_layer_top).
class ILvglScreen {
public:
    virtual ~ILvglScreen() = default;
    virtual ScreenType screenType() const = 0;
    virtual void create() = 0;
    virtual void enter() = 0;
    virtual void update() = 0;
    virtual void exit() = 0;
    virtual void destroy() = 0;
    virtual lv_obj_t* screen() = 0;
};

} // namespace lvgl_ui

#endif // __cplusplus

#endif // LV_SCREEN_H
