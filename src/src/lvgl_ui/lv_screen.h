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

    // Stage 6.6R-B: default no-op — screens that support live theme reapply override this.
    // Boot / Wi-Fi Flow / Stub remain no-op (excluded from runtime theme contract).
    // Этап 6.6R-B: заглушка; экраны с live reapply переопределяют. Boot/Wi-Fi/Stub — без реализации.
    virtual void liveReapplyTheme() {}

    // W2F: unified carousel auto-delete lifecycle (replaces the W2E Weather-only non-resident hack).
    // On every carousel switch PageChain loads the next screen with lv_scr_load_anim(..., auto_del=true),
    // letting LVGL free the previous screen tree back into the 48 KB pool. Two paired hooks bracket it:
    //
    //   prepareForAutoDelete(): called while the old screen is STILL the active LVGL screen, right
    //     before the auto_del load. Stop page-owned resources that are NOT part of the LVGL tree and
    //     could touch lv_obj_t after deletion (e.g. lv_timer, async producers). Do NOT lv_obj_del here.
    //
    //   releaseAfterAutoDelete(): called right after the auto_del load (synchronous for ANIM_NONE/0/0),
    //     when LVGL has ALREADY deleted the old screen tree. Null every lv_obj_t* handle and free any
    //     non-LVGL resources. NEVER call lv_obj_del() here — the tree is already gone (double-free).
    //
    // Default no-ops: special screens (Boot / Wi-Fi Flow) are never auto-deleted by the carousel —
    // they keep their explicit destroy()-based teardown. Every carousel page overrides the release hook.
    //
    // W2F: единый lifecycle авто-удаления карусели (вместо W2E-хака isNonResident только для Weather).
    // prepareForAutoDelete(): пока старый экран ещё активен — погасить ресурсы вне дерева LVGL (таймеры).
    // releaseAfterAutoDelete(): дерево уже удалено LVGL — только обнулить указатели/освободить не-LVGL.
    // Никогда не звать lv_obj_del() в release-хуке (двойное удаление).
    virtual void prepareForAutoDelete() {}
    virtual void releaseAfterAutoDelete() {}
};

} // namespace lvgl_ui

#endif // __cplusplus

#endif // LV_SCREEN_H
