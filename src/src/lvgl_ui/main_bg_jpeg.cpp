/*
 * Main background JPEG pipeline (TJPGD + FIT ScaleOnce + RGB565 PSRAM cache).
 * Пайплайн фона Main: TJPGD + FIT ScaleOnce + кэш RGB565 в PSRAM.
 *
 * TJPGD JD_FORMAT=0 MCU output is BGR888 (ChaN jd_mcu_output) — convert to RGB before scale.
 * Выход TJPGD JD_FORMAT=0 — BGR888; перед скейлом переставляем в RGB.
 *
 * Do not call lv_timer_handler from the decode/scale loop: that desyncs RGB scanout on Boot.
 * Не вызывать lv_timer_handler из decode/scale: на Boot это сдвигает RGB scanout.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#include "main_bg_jpeg.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "profiles/lv_profile_select.h"
#include "src/libs/tjpgd/tjpgd.h"
#include "theme/lv_theme_yoradio.h"

namespace lvgl_ui {
namespace {

constexpr char kPathDark[]       = "/bg/main_dark.jpg";
constexpr char kPathLight[]      = "/bg/main_light.jpg";
constexpr char kPathCustom[]     = "/bg/main_custom.jpg";
constexpr char kUserPathDark[]   = "/bg/user_dark.jpg";
constexpr char kUserPathLight[]  = "/bg/user_light.jpg";
constexpr char kUserPathCustom[] = "/bg/user_custom.jpg";
constexpr uint32_t kTjpgdPoolBytes = 32u * 1024u;
constexpr uint16_t kMinSrcSide = 16u; // user images may be landscape <480 on one side / пользовательский JPEG может быть ниже 480
constexpr uint32_t kWorkerStack = 8192u;

struct MemJpeg {
    const uint8_t* data = nullptr;
    size_t         len  = 0;
    size_t         pos  = 0;
};

struct DecodeSink {
    uint8_t* rgb888 = nullptr;
    uint16_t src_w  = 0;
    uint16_t src_h  = 0;
    uint32_t mcu_n  = 0;
};

struct MainBgCache {
    char         path[64] = {};
    uint8_t*     data     = nullptr;
    lv_img_dsc_t dsc      = {};
    bool valid() const { return data != nullptr && path[0] != '\0'; }
};

struct JpegJob {
    char     path[64] = {};
    uint8_t  slot     = 0;
    uint16_t dst_w    = 0;
    uint16_t dst_h    = 0;
    uint16_t fill     = 0;
    uint32_t gen      = 0;
};

static MainBgCache s_cache;
static DecodeSink* s_sink = nullptr;
static volatile uint32_t s_live_gen = 0;

static TaskHandle_t s_worker = nullptr;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static JpegJob s_req;
static volatile uint32_t s_req_gen = 0;

static uint8_t* s_done_buf = nullptr;
static lv_img_dsc_t s_done_dsc = {};
static char s_done_path[64] = {};
static volatile uint32_t s_done_gen = 0;
static volatile bool s_done_ready = false;
static volatile bool s_runtime_busy = false;

static void s_yield_bus() {
    yield();
    vTaskDelay(1);
}

static bool s_job_stale(uint32_t gen) {
    return gen != s_req_gen;
}

static const char* s_slot_name(uint8_t slot) {
    if (slot == 1) return "Light";
    if (slot == 2) return "Custom";
    return "Dark";
}

constexpr char kUserPathPrefix[] = "/bg/user_";

static bool s_path_is_user(const char* path) {
    const size_t n = sizeof(kUserPathPrefix) - 1u; // exclude NUL / без терминатора
    return path && std::strncmp(path, kUserPathPrefix, n) == 0;
}

// Serialize LittleFS source open/read/close against upload replace/delete of the same dest.
// Сериализуем open/read/close источника против replace/delete того же dest.
static SemaphoreHandle_t s_src_fd_sem() {
    static StaticSemaphore_t buf;
    static SemaphoreHandle_t h = xSemaphoreCreateMutexStatic(&buf);
    return h;
}

struct SourceFdHold {
    SourceFdHold() { xSemaphoreTake(s_src_fd_sem(), portMAX_DELAY); }
    ~SourceFdHold() { xSemaphoreGive(s_src_fd_sem()); }
    SourceFdHold(const SourceFdHold&) = delete;
    SourceFdHold& operator=(const SourceFdHold&) = delete;
};

static size_t s_tjpgd_in(JDEC* jd, uint8_t* buf, size_t nbuf) {
    auto* src = static_cast<MemJpeg*>(jd->device);
    if (!src || src->pos >= src->len) return 0;
    const size_t n = (nbuf < (src->len - src->pos)) ? nbuf : (src->len - src->pos);
    if (buf) {
        memcpy(buf, src->data + src->pos, n);
    }
    src->pos += n;
    return n;
}

static int s_tjpgd_out(JDEC* /*jd*/, void* bitmap, JRECT* rect) {
    if (!s_sink || !s_sink->rgb888 || !bitmap || !rect) return 0;
    if (s_job_stale(s_live_gen)) return 0;
    const int src_w = static_cast<int>(s_sink->src_w);
    const int src_h = static_cast<int>(s_sink->src_h);
    int left = static_cast<int>(rect->left);
    int top  = static_cast<int>(rect->top);
    int right  = static_cast<int>(rect->right);
    int bottom = static_cast<int>(rect->bottom);
    const int bmp_w = right - left + 1;
    if (left >= src_w || top >= src_h) return 1;
    if (right >= src_w) right = src_w - 1;
    if (bottom >= src_h) bottom = src_h - 1;
    if (right < left || bottom < top) return 1;

    const uint8_t* src = static_cast<const uint8_t*>(bitmap);
    for (int y = top; y <= bottom; y++) {
        const uint8_t* row = src + (static_cast<size_t>(y - static_cast<int>(rect->top)) *
                                    static_cast<size_t>(bmp_w) +
                                    static_cast<size_t>(left - static_cast<int>(rect->left))) * 3u;
        uint8_t* dst = s_sink->rgb888 + (static_cast<size_t>(y) * static_cast<size_t>(src_w) +
                                         static_cast<size_t>(left)) * 3u;
        const int n = right - left + 1;
        for (int x = 0; x < n; x++) {
            dst[0] = row[2]; // R from BGR
            dst[1] = row[1];
            dst[2] = row[0];
            dst += 3;
            row += 3;
        }
    }
    s_sink->mcu_n++;
    if ((s_sink->mcu_n & 0x07u) == 0u) {
        s_yield_bus();
    }
    return 1;
}

static void s_fill_rgb565(uint16_t* dst, size_t count, uint16_t color) {
    for (size_t i = 0; i < count; i++) dst[i] = color;
}

static void s_fit_scale_area_average(const uint8_t* rgb888, uint16_t src_w, uint16_t src_h,
                                     uint16_t* dst, uint16_t dst_w, uint16_t dst_h,
                                     uint16_t fill_rgb565, uint32_t gen) {
    s_fill_rgb565(dst, static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h), fill_rgb565);

    uint32_t fit_w;
    uint32_t fit_h;
    if (static_cast<uint32_t>(dst_w) * src_h <= static_cast<uint32_t>(dst_h) * src_w) {
        fit_w = dst_w;
        fit_h = (static_cast<uint32_t>(src_h) * dst_w) / src_w;
        if (fit_h == 0) fit_h = 1;
        if (fit_h > dst_h) fit_h = dst_h;
    } else {
        fit_h = dst_h;
        fit_w = (static_cast<uint32_t>(src_w) * dst_h) / src_h;
        if (fit_w == 0) fit_w = 1;
        if (fit_w > dst_w) fit_w = dst_w;
    }
    const uint32_t off_x = (dst_w - fit_w) / 2u;
    const uint32_t off_y = (dst_h - fit_h) / 2u;

    for (uint32_t dy = 0; dy < fit_h; dy++) {
        if (s_job_stale(gen)) return;
        const uint32_t y0 = dy * src_h / fit_h;
        uint32_t y1 = (dy + 1u) * src_h / fit_h;
        if (y1 <= y0) y1 = y0 + 1u;
        if (y1 > src_h) y1 = src_h;
        for (uint32_t dx = 0; dx < fit_w; dx++) {
            const uint32_t x0 = dx * src_w / fit_w;
            uint32_t x1 = (dx + 1u) * src_w / fit_w;
            if (x1 <= x0) x1 = x0 + 1u;
            if (x1 > src_w) x1 = src_w;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (uint32_t y = y0; y < y1; y++) {
                const uint8_t* row = rgb888 + (static_cast<size_t>(y) * src_w + x0) * 3u;
                for (uint32_t x = x0; x < x1; x++) {
                    r += row[0];
                    g += row[1];
                    b += row[2];
                    row += 3;
                    n++;
                }
            }
            if (n == 0) continue;
            r /= n;
            g /= n;
            b /= n;
            const uint16_t px = static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
            dst[(off_y + dy) * dst_w + (off_x + dx)] = px;
        }
        if ((dy & 0x07u) == 0u) {
            s_yield_bus();
        }
    }
}

static void s_fill_lv_dsc(uint8_t* buf, uint16_t w, uint16_t h, lv_img_dsc_t& out_dsc) {
    lv_image_header_t hdr = {};
    hdr.magic      = LV_IMAGE_HEADER_MAGIC;
    hdr.cf         = LV_COLOR_FORMAT_RGB565;
    hdr.flags      = 0;
    hdr.w          = w;
    hdr.h          = h;
    hdr.stride     = static_cast<uint32_t>(w) * 2u;
    hdr.reserved_2 = 0;
    out_dsc.header    = hdr;
    out_dsc.data_size = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 2u;
    out_dsc.data      = buf;
}

static bool s_prepare_jpeg(const char* fs_path, uint16_t dst_w, uint16_t dst_h,
                           uint16_t fill_rgb565, uint32_t gen, bool boot,
                           uint8_t*& out_buf, lv_img_dsc_t& out_dsc) {
    out_buf = nullptr;
    out_dsc = {};
    if (!fs_path || dst_w == 0 || dst_h == 0) return false;
    s_live_gen = gen;
    const uint32_t t0 = millis();
    const char* kind = s_path_is_user(fs_path) ? "user" : "factory";
    if (boot) {
        Serial.println("[MAIN_BG] Preparing active theme background...");
        Serial.println("[MAIN_BG] UI animation may pause briefly during boot preparation");
    } else {
        Serial.println("[MAIN_BG] Preparing background in worker...");
    }
    Serial.printf("[MAIN_BG] source=%s path=%s\n", kind, fs_path);

    uint8_t* jpeg = nullptr;
    size_t file_sz = 0;
    {
        SourceFdHold hold; // lock before open — no unlink of an open FD / lock до open
        File f = LittleFS.open(fs_path, "r");
        if (!f) {
            Serial.printf("[MAIN_BG] open fail (%s) %s\n", kind, fs_path);
            return false;
        }
        file_sz = static_cast<size_t>(f.size());
        jpeg = static_cast<uint8_t*>(ps_malloc(file_sz));
        if (!jpeg) {
            f.close();
            Serial.printf("[MAIN_BG] jpeg alloc fail %u\n", (unsigned)file_sz);
            return false;
        }
        const int nread = f.read(jpeg, file_sz);
        f.close();
        if (nread < 0 || static_cast<size_t>(nread) != file_sz) {
            free(jpeg);
            Serial.println("[MAIN_BG] jpeg read fail");
            return false;
        }
    }
    if (s_job_stale(gen)) {
        free(jpeg);
        return false;
    }

    void* pool = ps_malloc(kTjpgdPoolBytes);
    if (!pool) {
        free(jpeg);
        Serial.println("[MAIN_BG] tjpgd pool fail");
        return false;
    }

    MemJpeg mem;
    mem.data = jpeg;
    mem.len  = file_sz;
    mem.pos  = 0;

    JDEC jd;
    memset(&jd, 0, sizeof(jd));
    JRESULT jr = jd_prepare(&jd, s_tjpgd_in, pool, kTjpgdPoolBytes, &mem);
    if (jr != JDR_OK) {
        Serial.printf("[MAIN_BG] jd_prepare rc=%d (%s) %s\n", (int)jr, kind, fs_path);
        free(pool);
        free(jpeg);
        return false;
    }
    if (jd.width < kMinSrcSide || jd.height < kMinSrcSide) {
        Serial.printf("[MAIN_BG] geometry %ux%u too small (%s)\n",
                      (unsigned)jd.width, (unsigned)jd.height, kind);
        free(pool);
        free(jpeg);
        return false;
    }

    Serial.printf("[MAIN_BG] Decoding JPEG... src=%ux%u\n", (unsigned)jd.width, (unsigned)jd.height);

    const size_t rgb_bytes = static_cast<size_t>(jd.width) * static_cast<size_t>(jd.height) * 3u;
    uint8_t* rgb888 = static_cast<uint8_t*>(ps_malloc(rgb_bytes));
    if (!rgb888) {
        Serial.printf("[MAIN_BG] rgb888 alloc fail %u\n", (unsigned)rgb_bytes);
        free(pool);
        free(jpeg);
        return false;
    }
    memset(rgb888, 0, rgb_bytes);

    DecodeSink sink;
    sink.rgb888 = rgb888;
    sink.src_w  = jd.width;
    sink.src_h  = jd.height;
    sink.mcu_n  = 0;
    s_sink = &sink;
    jr = jd_decomp(&jd, s_tjpgd_out, 0);
    s_sink = nullptr;
    free(pool);
    free(jpeg);
    if (jr != JDR_OK || s_job_stale(gen)) {
        free(rgb888);
        if (jr != JDR_OK && !s_job_stale(gen)) {
            Serial.printf("[MAIN_BG] jd_decomp rc=%d\n", (int)jr);
        }
        return false;
    }

    const size_t dst_bytes = static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 2u;
    uint8_t* dst = static_cast<uint8_t*>(ps_malloc(dst_bytes));
    if (!dst) {
        free(rgb888);
        Serial.printf("[MAIN_BG] rgb565 alloc fail %u\n", (unsigned)dst_bytes);
        return false;
    }

    Serial.printf("[MAIN_BG] Scaling to %ux%u RGB565...\n", (unsigned)dst_w, (unsigned)dst_h);
    s_fit_scale_area_average(rgb888, sink.src_w, sink.src_h,
                             reinterpret_cast<uint16_t*>(dst), dst_w, dst_h, fill_rgb565, gen);
    free(rgb888);
    if (s_job_stale(gen)) {
        free(dst);
        return false;
    }

    s_fill_lv_dsc(dst, dst_w, dst_h, out_dsc);
    out_buf = dst;
    Serial.printf("[MAIN_BG] Background ready in %u ms src=%ux%u dst=%ux%u %s\n",
                  (unsigned)(millis() - t0), (unsigned)sink.src_w, (unsigned)sink.src_h,
                  (unsigned)dst_w, (unsigned)dst_h, kind);
    return true;
}

static bool s_prepare_with_fallback(uint8_t slot, const char* preferred, uint16_t dst_w, uint16_t dst_h,
                                    uint16_t fill, uint32_t gen, bool boot,
                                    uint8_t*& out_buf, lv_img_dsc_t& out_dsc, char* out_path, size_t out_path_len) {
    out_buf = nullptr;
    out_dsc = {};
    if (out_path && out_path_len) out_path[0] = '\0';
    const char* factory = (slot == 1) ? kPathLight : (slot == 2) ? kPathCustom : kPathDark;
    bool ok = s_prepare_jpeg(preferred, dst_w, dst_h, fill, gen, boot, out_buf, out_dsc);
    if (ok) {
        if (out_path && out_path_len) strlcpy(out_path, preferred, out_path_len);
        return true;
    }
    if (s_job_stale(gen)) return false;
    if (preferred && std::strcmp(preferred, factory) != 0) {
        Serial.printf("[MAIN_BG] user JPEG failed, factory fallback (%s) slot=%s\n",
                      factory, s_slot_name(slot));
        if (out_buf) {
            free(out_buf);
            out_buf = nullptr;
        }
        ok = s_prepare_jpeg(factory, dst_w, dst_h, fill, gen, boot, out_buf, out_dsc);
        if (ok && out_path && out_path_len) strlcpy(out_path, factory, out_path_len);
        return ok;
    }
    return false;
}

static void s_worker_task(void* /*arg*/) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        JpegJob job;
        portENTER_CRITICAL(&s_mux);
        job = s_req;
        portEXIT_CRITICAL(&s_mux);
        if (job.path[0] == '\0' || s_job_stale(job.gen)) continue;

        uint8_t* buf = nullptr;
        lv_img_dsc_t dsc = {};
        char used[64] = {};
        const bool ok = s_prepare_with_fallback(job.slot, job.path, job.dst_w, job.dst_h, job.fill,
                                                job.gen, false, buf, dsc, used, sizeof(used));
        if (!ok || s_job_stale(job.gen)) {
            if (buf) free(buf);
            portENTER_CRITICAL(&s_mux);
            if (job.gen == s_req_gen) {
                s_runtime_busy = false;
            }
            portEXIT_CRITICAL(&s_mux);
            continue;
        }

        portENTER_CRITICAL(&s_mux);
        if (s_done_ready && s_done_buf) {
            uint8_t* stale = s_done_buf;
            s_done_buf = nullptr;
            s_done_ready = false;
            portEXIT_CRITICAL(&s_mux);
            free(stale);
            portENTER_CRITICAL(&s_mux);
        }
        s_done_buf = buf;
        s_done_dsc = dsc;
        strlcpy(s_done_path, used[0] ? used : job.path, sizeof(s_done_path));
        s_done_gen = job.gen;
        s_done_ready = true;
        portEXIT_CRITICAL(&s_mux);
    }
}

static void s_ensure_worker() {
    if (s_worker) return;
    // Core 0, low priority — audio stays on core 1; DspTask (pri 3) keeps LVGL.
    // Ядро 0, низкий приоритет — аудио на ядре 1; DspTask (pri 3) продолжает LVGL.
    xTaskCreatePinnedToCore(s_worker_task, "MainBgJpeg", kWorkerStack, nullptr, 1, &s_worker, 0);
}

}  // namespace

const char* mainBgJpegPathForSlot(uint8_t slot) {
    if (slot == 1) return kPathLight;
    if (slot == 2) return kPathCustom;
    return kPathDark;
}

const char* mainBgUserJpegPathForSlot(uint8_t slot) {
    if (slot == 1) return kUserPathLight;
    if (slot == 2) return kUserPathCustom;
    return kUserPathDark;
}

const char* mainBgResolvedJpegPathForSlot(uint8_t slot) {
    const char* user = mainBgUserJpegPathForSlot(slot);
    if (user && LittleFS.exists(user)) return user;
    return mainBgJpegPathForSlot(slot);
}

void mainBgCacheInvalidate() {
    if (s_cache.data) {
        free(s_cache.data);
        s_cache.data = nullptr;
    }
    s_cache.path[0] = '\0';
    s_cache.dsc = {};
}

bool mainBgCacheGet(const char* fs_path, lv_img_dsc_t& out_dsc) {
    if (!fs_path || fs_path[0] == '\0') return false;
    if (s_cache.valid() &&
        strncmp(s_cache.path, fs_path, sizeof(s_cache.path) - 1) == 0) {
        out_dsc = s_cache.dsc;
        return true;
    }
    return false;
}

bool mainBgCachePreloadActive() {
    const uint8_t slot = static_cast<uint8_t>(yoradio_theme_active_preset());
    const char* path = mainBgResolvedJpegPathForSlot(slot);
    const uint16_t dst_w = LV_ACTIVE_PROFILE.width;
    const uint16_t dst_h = LV_ACTIVE_PROFILE.height;
    const uint16_t fill  = lv_color_to_u16(yoradio_palette().device_background);

    uint8_t* buf = nullptr;
    lv_img_dsc_t dsc = {};
    char used[64] = {};
    const bool ok = s_prepare_with_fallback(slot, path, dst_w, dst_h, fill, s_req_gen, true,
                                            buf, dsc, used, sizeof(used));
    if (!ok) return false;

    mainBgCacheInvalidate();
    s_cache.data = buf;
    s_cache.dsc  = dsc;
    strlcpy(s_cache.path, used[0] ? used : path, sizeof(s_cache.path));
    Serial.println("[MAIN_BG] Continuing to Main");
    return true;
}

void mainBgCacheRequestAsync(const char* fs_path) {
    if (!fs_path || fs_path[0] == '\0') return;
    s_ensure_worker();
    if (!s_worker) return;

    const uint16_t dst_w = LV_ACTIVE_PROFILE.width;
    const uint16_t dst_h = LV_ACTIVE_PROFILE.height;
    const uint16_t fill  = lv_color_to_u16(yoradio_palette().device_background);

    portENTER_CRITICAL(&s_mux);
    const uint32_t next_gen = s_req_gen + 1u;
    s_req_gen = next_gen;
    s_runtime_busy = true;
    strlcpy(s_req.path, fs_path, sizeof(s_req.path));
    s_req.dst_w = dst_w;
    s_req.dst_h = dst_h;
    s_req.fill  = fill;
    s_req.slot  = 0;
    if (std::strstr(fs_path, "light")) s_req.slot = 1;
    else if (std::strstr(fs_path, "custom")) s_req.slot = 2;
    s_req.gen   = s_req_gen;
    portEXIT_CRITICAL(&s_mux);
    xTaskNotifyGive(s_worker);
}

bool mainBgCachePollApply() {
    uint8_t* buf = nullptr;
    lv_img_dsc_t dsc = {};
    char path[64] = {};
    uint32_t gen = 0;

    portENTER_CRITICAL(&s_mux);
    if (!s_done_ready) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    buf = s_done_buf;
    dsc = s_done_dsc;
    strlcpy(path, s_done_path, sizeof(path));
    gen = s_done_gen;
    s_done_buf = nullptr;
    s_done_dsc = {};
    s_done_path[0] = '\0';
    s_done_ready = false;
    // Gen mismatch only: factory fallback changes path while this job is still current.
    // Только gen: factory fallback меняет path, job при этом ещё актуальный.
    const bool stale = (gen != s_req_gen);
    portEXIT_CRITICAL(&s_mux);

    if (stale || !buf) {
        if (buf) free(buf);
        return false;
    }

    mainBgCacheInvalidate();
    s_cache.data = buf;
    s_cache.dsc  = dsc;
    strlcpy(s_cache.path, path, sizeof(s_cache.path));
    s_runtime_busy = false;
    return true;
}

bool mainBgCacheRuntimeBusy() {
    return s_runtime_busy;
}

bool mainBgTryLockSourceFd() {
    return xSemaphoreTake(s_src_fd_sem(), 0) == pdTRUE;
}

void mainBgUnlockSourceFd() {
    xSemaphoreGive(s_src_fd_sem());
}

}  // namespace lvgl_ui
