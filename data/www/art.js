/**
 * Station Art MVP — art.js
 * Manual station art upload: browse, preview (120×120), convert to LVGL TRUE_COLOR_ALPHA .bin,
 * POST /upload_art; remove via POST /remove_art; status via GET /art_status.
 *
 * Station Art MVP — ручная загрузка арта станции.
 * Превью 120×120, конвертация RGB565+alpha (CF=5), загрузка в LittleFS /logo/<key>.bin.
 * Key contract: server-side only (stationByNum → normalize). Client never computes the key.
 */
(function () {
  'use strict';

  var root = document.getElementById('art-upload-root');
  if (!root) { return; }

  /** Same host pattern as bg.js / script.js */
  function apiBase() {
    var q = window.location.search;
    var h = q !== '' ? q.substring(1) : window.location.hostname;
    return 'http://' + h;
  }

  function statusPath() { return root.getAttribute('data-art-api-status') || '/art_status'; }
  function uploadPath() { return root.getAttribute('data-art-api-upload') || '/upload_art'; }
  function removePath() { return root.getAttribute('data-art-api-remove') || '/remove_art'; }

  /** LVGL 8.x 4-byte image header (same packing as bg.js) */
  function buildLvglHeader(cf, w, h) {
    var v = (cf & 0x1f) | ((w & 0x7ff) << 10) | ((h & 0x7ff) << 21);
    var b = new Uint8Array(4);
    b[0] = v & 0xff;
    b[1] = (v >>> 8) & 0xff;
    b[2] = (v >>> 16) & 0xff;
    b[3] = (v >>> 24) & 0xff;
    return b;
  }

  var LV_IMG_CF_TRUE_COLOR_ALPHA = 5;

  /** RGB888 → RGB565 LE into out[off], out[off+1] */
  function pushRgb565Le(r, g, b, out, off) {
    var r5 = (r >> 3) & 0x1f;
    var g6 = (g >> 2) & 0x3f;
    var b5 = (b >> 3) & 0x1f;
    var u16 = (r5 << 11) | (g6 << 5) | b5;
    out[off]     = u16 & 0xff;
    out[off + 1] = (u16 >>> 8) & 0xff;
  }

  /**
   * Convert canvas to LVGL TRUE_COLOR_ALPHA .bin (CF=5).
   * Layout: 4-byte header + (RGB565 LE [2 bytes] + alpha [1 byte]) per pixel.
   * Конвертация canvas → LVGL TRUE_COLOR_ALPHA .bin (CF=5).
   */
  function canvasToLvglBinAlpha(canvas) {
    var tw = canvas.width;
    var th = canvas.height;
    var ctx = canvas.getContext('2d');
    var imgd = ctx.getImageData(0, 0, tw, th);
    var d = imgd.data;
    var pix = tw * th;
    var header = buildLvglHeader(LV_IMG_CF_TRUE_COLOR_ALPHA, tw, th);
    var body = new Uint8Array(pix * 3);
    var i, p;
    for (i = 0, p = 0; i < pix; i++, p += 4) {
      pushRgb565Le(d[p], d[p + 1], d[p + 2], body, i * 3);
      body[i * 3 + 2] = d[p + 3]; // alpha channel
    }
    var out = new Uint8Array(4 + body.length);
    out.set(header, 0);
    out.set(body, 4);
    return out.buffer;
  }

  /** Cover + center crop to tw×th (object-fit: cover semantics) */
  function drawCoverCenter(ctx, img, tw, th) {
    var sw = img.naturalWidth || img.width;
    var sh = img.naturalHeight || img.height;
    if (!sw || !sh) { return; }
    var scale = Math.max(tw / sw, th / sh);
    var dw = sw * scale;
    var dh = sh * scale;
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, tw, th);
    ctx.drawImage(img, 0, 0, sw, sh, (tw - dw) / 2, (th - dh) / 2, dw, dh);
  }

  /** Draw scaled copy of source canvas into preview canvas */
  function drawPreview(previewCanvas, sourceCanvas) {
    var pw = previewCanvas.width;
    var ph = previewCanvas.height;
    var pctx = previewCanvas.getContext('2d');
    pctx.fillStyle = '#111111';
    pctx.fillRect(0, 0, pw, ph);
    pctx.drawImage(sourceCanvas, 0, 0, sourceCanvas.width, sourceCanvas.height, 0, 0, pw, ph);
  }

  function setState(el, text, kind) {
    el.textContent = text;
    el.className = 'art-state' + (kind ? ' ' + kind : '');
  }

  function setFsLine(el, present, sizeBytes) {
    if (!el) { return; }
    var n = typeof sizeBytes === 'number' ? sizeBytes : (parseInt(sizeBytes, 10) || 0);
    if (present && n > 0) {
      el.textContent = 'FS: present \u00b7 ' + n + ' B';
    } else if (present) {
      el.textContent = 'FS: present';
    } else {
      el.textContent = 'FS: empty';
    }
  }

  var artSlotW = 120;
  var artSlotH = 120;
  var workCanvas = document.createElement('canvas');
  workCanvas.width  = artSlotW;
  workCanvas.height = artSlotH;
  var hasImage = false;

  function pullStatus(cb) {
    fetch(apiBase() + statusPath())
      .then(function (r) { return r.ok ? r.json() : Promise.reject(new Error('http_' + r.status)); })
      .then(function (d) { if (cb) { cb(d); } })
      .catch(function () { if (cb) { cb(null); } });
  }

  function applyStatus(data) {
    var nameEl = root.querySelector('.art-station-name');
    var fsEl   = root.querySelector('.art-fs');
    var dimEl  = root.querySelector('.art-dim');
    if (!data) {
      if (nameEl) { nameEl.textContent = '\u2014'; }
      setFsLine(fsEl, false, 0);
      return;
    }
    if (nameEl) { nameEl.textContent = data.station_name || '\u2014'; }
    artSlotW = parseInt(data.slot_w, 10) || 120;
    artSlotH = parseInt(data.slot_h, 10) || 120;
    if (dimEl) { dimEl.textContent = 'Target: ' + artSlotW + ' \u00d7 ' + artSlotH + ' (LVGL RGB565+alpha)'; }
    setFsLine(fsEl, data.art_present === true || data.art_present === 'true', data.art_size);
  }

  function init() {
    var preview  = root.querySelector('.art-preview');
    var fileInput = root.querySelector('.art-file');
    var btnChoose = root.querySelector('.art-choose');
    var btnUpload = root.querySelector('.art-upload');
    var btnRemove = root.querySelector('.art-remove');
    var fsEl     = root.querySelector('.art-fs');
    var stateEl  = root.querySelector('.art-state');

    if (!btnChoose || !fileInput || !btnUpload || !preview || !stateEl) { return; }

    pullStatus(function (data) {
      applyStatus(data);
      if (preview) {
        preview.width  = artSlotW;
        preview.height = artSlotH;
      }
      workCanvas.width  = artSlotW;
      workCanvas.height = artSlotH;
    });

    if (btnRemove) {
      btnRemove.addEventListener('click', function () {
        setState(stateEl, 'removing\u2026', '');
        btnRemove.disabled = true;
        fetch(apiBase() + removePath(), { method: 'POST' })
          .then(function (r) {
            return r.text().then(function (t) { return { ok: r.ok, text: t }; });
          })
          .then(function (res) {
            var j = {};
            try { j = JSON.parse(res.text); } catch (e) {}
            if (res.ok && j.ok !== false) {
              setState(stateEl, 'removed', '');
              pullStatus(function (d) { applyStatus(d); });
            } else {
              setState(stateEl, 'error: ' + (j.error || res.text), 'error');
            }
          })
          .catch(function () { setState(stateEl, 'error', 'error'); })
          .then(function () { btnRemove.disabled = false; });
      });
    }

    btnChoose.addEventListener('click', function () { fileInput.click(); });

    fileInput.addEventListener('change', function () {
      var f = fileInput.files && fileInput.files[0];
      if (!f) { return; }
      hasImage = false;
      btnUpload.disabled = true;
      setState(stateEl, 'loading\u2026', '');
      var url = URL.createObjectURL(f);
      var img = new Image();
      img.onload = function () {
        URL.revokeObjectURL(url);
        workCanvas.width  = artSlotW;
        workCanvas.height = artSlotH;
        var wctx = workCanvas.getContext('2d');
        drawCoverCenter(wctx, img, artSlotW, artSlotH);
        drawPreview(preview, workCanvas);
        hasImage = true;
        btnUpload.disabled = false;
        setState(stateEl, 'ready', '');
      };
      img.onerror = function () {
        URL.revokeObjectURL(url);
        setState(stateEl, 'error: could not load image', 'error');
      };
      img.src = url;
    });

    btnUpload.addEventListener('click', function () {
      if (!hasImage) { return; }
      setState(stateEl, 'uploading\u2026', '');
      btnUpload.disabled = true;
      var bin  = canvasToLvglBinAlpha(workCanvas);
      var blob = new Blob([bin], { type: 'application/octet-stream' });
      var fd   = new FormData();
      fd.append('file', blob, 'art.bin');
      fetch(apiBase() + uploadPath(), { method: 'POST', body: fd })
        .then(function (r) {
          return r.text().then(function (t) { return { ok: r.ok, status: r.status, text: t }; });
        })
        .then(function (res) {
          var j = null;
          try { j = JSON.parse(res.text); } catch (e) {}
          /* One real success: JSON from handleUploadArt after commit — not empty 200 from onRequest */
          var okCommit =
            res.ok && j && j.ok === true &&
            (j.target_exists === true || j.target_exists === 'true') &&
            Number(j.final_size) > 0;
          if (!okCommit) {
            var errMsg = j && j.error ? j.error : (!res.ok ? 'HTTP ' + res.status : 'invalid response');
            setState(stateEl, 'error: ' + errMsg, 'error');
            btnUpload.disabled = !hasImage;
            return;
          }
          pullStatus(function (d) {
            applyStatus(d);
            var line = 'uploaded \u00b7 ' + Number(j.final_size) + ' B';
            if (j.normalized_key) { line += ' [' + j.normalized_key + ']'; }
            setState(stateEl, line, 'uploaded');
            btnUpload.disabled = !hasImage;
          });
        })
        .catch(function () {
          setState(stateEl, 'error', 'error');
          btnUpload.disabled = !hasImage;
        });
    });
  }

  if (typeof fetch !== 'function') {
    var nameEl = root.querySelector('.art-station-name');
    if (nameEl) { nameEl.textContent = 'fetch() not supported in this browser'; }
  } else {
    // defer on appearance.html — DOM ready when this runs
    init();
  }
})();
