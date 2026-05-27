/**
 * Minimal LVGL filesystem driver wrapping Arduino LittleFS (same partition as legacy WebUI/AI).
 * Минимальный драйвер LVGL для LittleFS — тот же раздел, что WebUI/AI.
 */

#include "lv_fs_littlefs.h"

#include "../core/options.h"
#include <LittleFS.h>
#include <FS.h>
#include <new>

#include "lvgl.h"

namespace {

void* fs_open_cb(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
    if (mode != LV_FS_MODE_RD) {
        return nullptr;
    }
    if (!path) {
        return nullptr;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return nullptr;
    }
    File* fp = new (std::nothrow) File();
    if (!fp) {
        f.close();
        return nullptr;
    }
    *fp = f;
    return fp;
}

lv_fs_res_t fs_close_cb(lv_fs_drv_t*, void* file_p) {
    File* fp = static_cast<File*>(file_p);
    if (fp) {
        fp->close();
        delete fp;
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_read_cb(lv_fs_drv_t*, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    File* fp = static_cast<File*>(file_p);
    if (!fp || !buf) {
        if (br) {
            *br = 0;
        }
        return LV_FS_RES_INV_PARAM;
    }
#ifdef LV_FS_DIAG
    // DIAG: measure blocking time of each LittleFS read on DspTask
    const uint32_t t0 = micros();
#endif
    const int n = fp->read(static_cast<uint8_t*>(buf), btr);
#ifdef LV_FS_DIAG
    const uint32_t dt_us = micros() - t0;
    static uint32_t s_total_us = 0;
    static uint32_t s_calls = 0;
    static uint32_t s_max_us = 0;
    s_total_us += dt_us;
    s_calls++;
    if (dt_us > s_max_us) s_max_us = dt_us;
    // Print summary every 50 calls or if single read > 20ms
    if (s_calls >= 50 || dt_us > 20000) {
        Serial.printf("[LV_FS_DIAG] calls=%u total=%ums max=%ums last=%ums btr=%u\n",
            s_calls, s_total_us / 1000, s_max_us / 1000, dt_us / 1000, btr);
        s_total_us = 0;
        s_calls = 0;
        s_max_us = 0;
    }
#endif
    if (br) {
        *br = (n < 0) ? 0u : static_cast<uint32_t>(n);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_seek_cb(lv_fs_drv_t*, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    File* fp = static_cast<File*>(file_p);
    if (!fp) {
        return LV_FS_RES_INV_PARAM;
    }
    SeekMode sm = SeekSet;
    switch (whence) {
        case LV_FS_SEEK_SET:
            sm = SeekSet;
            break;
        case LV_FS_SEEK_CUR:
            sm = SeekCur;
            break;
        case LV_FS_SEEK_END:
            sm = SeekEnd;
            break;
        default:
            return LV_FS_RES_INV_PARAM;
    }
    if (!fp->seek(pos, sm)) {
        return LV_FS_RES_UNKNOWN;
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t fs_tell_cb(lv_fs_drv_t*, void* file_p, uint32_t* pos_p) {
    File* fp = static_cast<File*>(file_p);
    if (!fp || !pos_p) {
        return LV_FS_RES_INV_PARAM;
    }
    *pos_p = static_cast<uint32_t>(fp->position());
    return LV_FS_RES_OK;
}

} // namespace

extern "C" void lv_fs_littlefs_register(void) {
    static lv_fs_drv_t drv;
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    lv_fs_drv_init(&drv);
    drv.letter = 'L';
    drv.cache_size = 0;
    drv.open_cb = fs_open_cb;
    drv.close_cb = fs_close_cb;
    drv.read_cb = fs_read_cb;
    drv.seek_cb = fs_seek_cb;
    drv.tell_cb = fs_tell_cb;
    lv_fs_drv_register(&drv);
}
