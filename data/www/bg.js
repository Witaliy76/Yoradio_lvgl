/**
 * Stage 6.1F-d3 — Main background upload (Appearance): preview, RGB565 + LVGL header, POST /upload_bg
 * Фон Main: превью, конвертация, загрузка в слоты LittleFS
 */
(function () {
  'use strict';

  var root = document.getElementById('bg-upload-root');
  if (!root) {
    return;
  }

  /** Same host pattern as script.js / Тот же хост, что и в script.js */
  function apiBase() {
    var q = window.location.search;
    var h = q !== '' ? q.substring(1) : window.location.hostname;
    return 'http://' + h;
  }

  function statusPath() {
    return root.getAttribute('data-bg-api-status') || '/bg_status';
  }

  function uploadPath() {
    return root.getAttribute('data-bg-api-upload') || '/upload_bg';
  }

  function removePath() {
    return root.getAttribute('data-bg-api-remove') || '/remove_bg';
  }

  /** LVGL 8.x 4-byte image header (cf | w<<10 | h<<21), LE / Заголовок LVGL 8.x */
  function buildLvglHeader(cf, w, h) {
    var v = (cf & 0x1f) | ((w & 0x7ff) << 10) | ((h & 0x7ff) << 21);
    var b = new Uint8Array(4);
    b[0] = v & 0xff;
    b[1] = (v >>> 8) & 0xff;
    b[2] = (v >>> 16) & 0xff;
    b[3] = (v >>> 24) & 0xff;
    return b;
  }

  var LV_IMG_CF_TRUE_COLOR = 4;

  /** RGB565, 2 bytes per pixel LE / RGB565, 2 байта на пиксель LE */
  function pushRgb565Le(r, g, b, out, off) {
    var r5 = (r >> 3) & 0x1f;
    var g6 = (g >> 2) & 0x3f;
    var b5 = (b >> 3) & 0x1f;
    var u16 = (r5 << 11) | (g6 << 5) | b5;
    out[off] = u16 & 0xff;
    out[off + 1] = (u16 >>> 8) & 0xff;
  }

  /** Cover + center crop to tw×th (same as object-fit: cover) / Cover + центр */
  function drawCoverCenter(ctx, img, tw, th) {
    var sw = img.naturalWidth || img.width;
    var sh = img.naturalHeight || img.height;
    if (!sw || !sh) {
      return;
    }
    var scale = Math.max(tw / sw, th / sh);
    var dw = sw * scale;
    var dh = sh * scale;
    var ox = (tw - dw) / 2;
    var oy = (th - dh) / 2;
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, tw, th);
    ctx.drawImage(img, 0, 0, sw, sh, ox, oy, dw, dh);
  }

  /** Build .bin: header + RGB565 pixels / Сборка .bin для бэкенда */
  function canvasToLvglBin(canvas) {
    var tw = canvas.width;
    var th = canvas.height;
    var ctx = canvas.getContext('2d');
    var imgd = ctx.getImageData(0, 0, tw, th);
    var d = imgd.data;
    var pix = tw * th;
    var header = buildLvglHeader(LV_IMG_CF_TRUE_COLOR, tw, th);
    var body = new Uint8Array(pix * 2);
    var i;
    var p;
    for (i = 0, p = 0; i < pix; i++, p += 4) {
      pushRgb565Le(d[p], d[p + 1], d[p + 2], body, i * 2);
    }
    var out = new Uint8Array(4 + body.length);
    out.set(header, 0);
    out.set(body, 4);
    return out.buffer;
  }

  /** Preview: scaled copy of full-res canvas / Превью — масштаб с полноразмерного canvas */
  function drawPreview(previewCanvas, sourceCanvas) {
    var pw = previewCanvas.width;
    var ph = previewCanvas.height;
    var pctx = previewCanvas.getContext('2d');
    pctx.fillStyle = '#111';
    pctx.fillRect(0, 0, pw, ph);
    pctx.drawImage(sourceCanvas, 0, 0, sourceCanvas.width, sourceCanvas.height, 0, 0, pw, ph);
  }

  var dspW = 0;
  var dspH = 0;
  var workCanvas = document.createElement('canvas');

  function setState(el, text, kind) {
    el.textContent = text;
    el.className = 'bg-slot-state' + (kind ? ' ' + kind : '');
  }

  function setFsLine(el, present, sizeBytes) {
    if (!el) {
      return;
    }
    var n = typeof sizeBytes === 'number' && !isNaN(sizeBytes) ? sizeBytes : parseInt(sizeBytes, 10) || 0;
    if (present && n > 0) {
      el.textContent = 'FS: present · ' + n + ' B';
    } else if (present) {
      el.textContent = 'FS: present';
    } else {
      el.textContent = 'FS: empty';
    }
  }

  /** Highlight active theme preset from /bg_status.active_theme (read-only) / Активный пресет с устройства */
  function applyThemeShell(activeTheme) {
    var shell = document.getElementById('appearance-theme-shell');
    if (!shell) {
      return;
    }
    var opts = shell.querySelectorAll('.appearance-theme-opt');
    var i;
    for (i = 0; i < opts.length; i++) {
      var p = opts[i].getAttribute('data-theme-preset');
      if (activeTheme && p === activeTheme) {
        opts[i].classList.add('is-active');
      } else {
        opts[i].classList.remove('is-active');
      }
    }
  }

  function refreshAppearanceFromStatus(data) {
    applyBgStatusToSlots(data);
    applyThemeShell(data && data.active_theme ? String(data.active_theme) : null);
  }

  /** Apply /bg_status JSON to all .bg-slot-fs lines / Строки FS по ответу бэкенда */
  function applyBgStatusToSlots(data) {
    var slots = root.querySelectorAll('.bg-slot');
    var i;
    for (i = 0; i < slots.length; i++) {
      var slot = slots[i].getAttribute('data-slot');
      var fsLine = slots[i].querySelector('.bg-slot-fs');
      if (!data) {
        setFsLine(fsLine, false, 0);
        continue;
      }
      var key = 'bg_' + slot;
      var sizeKey = key + '_size';
      if (typeof data[key] === 'undefined') {
        setFsLine(fsLine, false, 0);
        continue;
      }
      var present = data[key] === true || data[key] === 'true';
      var sz = parseInt(data[sizeKey], 10) || 0;
      setFsLine(fsLine, present, sz);
    }
  }

  function wireSlot(slotEl) {
    var slot = slotEl.getAttribute('data-slot');
    var preview = slotEl.querySelector('.bg-slot-preview');
    var fileInput = slotEl.querySelector('.bg-slot-file');
    var btnChoose = slotEl.querySelector('.bg-slot-choose');
    var btnUpload = slotEl.querySelector('.bg-slot-upload');
    var btnRemove = slotEl.querySelector('.bg-slot-remove');
    var fsLine = slotEl.querySelector('.bg-slot-fs');
    var stateEl = slotEl.querySelector('.bg-slot-state');

    var hasImage = false;

    if (!btnChoose || !fileInput || !btnUpload || !preview || !fsLine || !stateEl) {
      return;
    }

    function pullStatus() {
      return fetch(apiBase() + statusPath())
        .then(function (r) {
          return r.ok ? r.json() : Promise.reject();
        })
        .then(function (d) {
          refreshAppearanceFromStatus(d);
        });
    }

    if (btnRemove) {
      btnRemove.addEventListener('click', function () {
        setState(stateEl, 'removing…', '');
        btnRemove.disabled = true;
        fetch(apiBase() + removePath() + '?slot=' + encodeURIComponent(slot), { method: 'POST' })
          .then(function (r) {
            return r.text().then(function (t) {
              return { ok: r.ok, text: t };
            });
          })
          .then(function (res) {
            var j = {};
            try {
              j = JSON.parse(res.text);
            } catch (e) {}
            if (res.ok && j.ok !== false) {
              setState(stateEl, 'removed', '');
              setFsLine(fsLine, false, 0);
              return pullStatus().catch(function () {});
            }
            setState(stateEl, 'error: ' + (j.error || res.text), 'error');
          })
          .catch(function () {
            setState(stateEl, 'error', 'error');
          })
          .then(function () {
            btnRemove.disabled = false;
          });
      });
    }

    btnChoose.addEventListener('click', function () {
      fileInput.click();
    });

    fileInput.addEventListener('change', function () {
      var f = fileInput.files && fileInput.files[0];
      if (!f) {
        return;
      }
      hasImage = false;
      btnUpload.disabled = true;
      setState(stateEl, 'loading…', '');
      var url = URL.createObjectURL(f);
      var img = new Image();
      img.onload = function () {
        URL.revokeObjectURL(url);
        if (!dspW || !dspH) {
          setState(stateEl, 'error', 'error');
          return;
        }
        workCanvas.width = dspW;
        workCanvas.height = dspH;
        var wctx = workCanvas.getContext('2d');
        drawCoverCenter(wctx, img, dspW, dspH);
        drawPreview(preview, workCanvas);
        hasImage = true;
        btnUpload.disabled = false;
        setState(stateEl, 'ready', '');
      };
      img.onerror = function () {
        URL.revokeObjectURL(url);
        setState(stateEl, 'error', 'error');
      };
      img.src = url;
    });

    btnUpload.addEventListener('click', function () {
      if (!hasImage || !dspW) {
        return;
      }
      setState(stateEl, 'uploading…', '');
      btnUpload.disabled = true;
      var bin = canvasToLvglBin(workCanvas);
      var blob = new Blob([bin], { type: 'application/octet-stream' });
      var fd = new FormData();
      fd.append('file', blob, 'main_' + slot + '.bin');
      var url = apiBase() + uploadPath() + '?slot=' + encodeURIComponent(slot);
      fetch(url, { method: 'POST', body: fd })
        .then(function (r) {
          return r.text().then(function (t) {
            return { ok: r.ok, status: r.status, text: t };
          });
        })
        .then(function (res) {
          var j = null;
          try {
            j = JSON.parse(res.text);
          } catch (e) {}
          /* One real success: JSON from handleUploadBg after commit — not empty 200 from onRequest / Один ответ после commit */
          var okCommit =
            res.ok &&
            j &&
            j.ok === true &&
            (j.target_exists === true || j.target_exists === 'true') &&
            Number(j.final_size) > 0;
          if (!okCommit) {
            var errMsg = j && j.error ? j.error : !res.ok ? 'HTTP ' + res.status : 'invalid response';
            setState(stateEl, 'error: ' + errMsg, 'error');
            btnUpload.disabled = !hasImage;
            return;
          }
          var szUp = Number(j.final_size);
          return fetch(apiBase() + statusPath())
            .then(function (r) {
              return r.ok ? r.json() : Promise.reject();
            })
            .then(function (d) {
              refreshAppearanceFromStatus(d);
              var line = 'uploaded';
              if (j.path) {
                var base = j.path.indexOf('/') >= 0 ? j.path.replace(/^.*\//, '') : j.path;
                line += ' · ' + base;
              }
              line += ' · ' + szUp + ' B';
              setState(stateEl, line, 'uploaded');
            })
            .catch(function () {
              setState(stateEl, 'error: status refresh failed', 'error');
              setFsLine(fsLine, true, szUp);
            })
            .then(function () {
              btnUpload.disabled = !hasImage;
            });
        })
        .catch(function () {
          setState(stateEl, 'error', 'error');
          btnUpload.disabled = !hasImage;
        });
    });
  }

  function init() {
    var dimEl = document.getElementById('bg-device-dim');
    fetch(apiBase() + statusPath())
      .then(function (r) {
        if (!r.ok) {
          return Promise.reject(new Error('http_' + r.status));
        }
        return r.json();
      })
      .then(function (data) {
        dspW = parseInt(data.dsp_w, 10) || 0;
        dspH = parseInt(data.dsp_h, 10) || 0;
        if (dimEl) {
          dimEl.textContent = 'Target: ' + dspW + ' × ' + dspH + ' (LVGL RGB565)';
        }
        var slots = root.querySelectorAll('.bg-slot');
        var maxPrev = 200;
        var i;
        for (i = 0; i < slots.length; i++) {
          var prev = slots[i].querySelector('.bg-slot-preview');
          if (prev && dspW > 0 && dspH > 0) {
            if (dspW >= dspH) {
              prev.width = maxPrev;
              prev.height = Math.max(1, Math.round(maxPrev * dspH / dspW));
            } else {
              prev.height = maxPrev;
              prev.width = Math.max(1, Math.round(maxPrev * dspW / dspH));
            }
          }
          wireSlot(slots[i]);
        }
        refreshAppearanceFromStatus(data);
      })
      .catch(function () {
        if (dimEl) {
          dimEl.textContent = 'Could not load /bg_status (offline or old firmware?)';
        }
        var slots = root.querySelectorAll('.bg-slot');
        var i;
        for (i = 0; i < slots.length; i++) {
          wireSlot(slots[i]);
        }
        refreshAppearanceFromStatus(null);
      });
  }

  if (typeof fetch !== 'function') {
    var dim = document.getElementById('bg-device-dim');
    if (dim) {
      dim.textContent = 'This browser needs fetch() for background upload.';
    }
  } else {
    // defer on appearance.html — DOM is ready when this runs / defer — DOM уже готов
    init();
  }
})();


