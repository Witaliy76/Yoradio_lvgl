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

  function setFsLine(el, data, stationAvailable) {
    if (!el) { return; }
    if (!data) {
      el.textContent = 'Artwork status is unavailable.';
      return;
    }
    if (!stationAvailable) {
      el.textContent = 'Artwork on device: unavailable until a station is playing.';
      return;
    }
    var present = data.art_present === true || data.art_present === 'true';
    var n = parseInt(data.art_size, 10) || 0;
    var key = data.normalized_key ? String(data.normalized_key) : '';
    if (present && key && n > 0) {
      el.textContent = 'On device: /logo/' + key + '.bin \u00b7 ' + n + ' B';
    } else if (present && key) {
      el.textContent = 'On device: /logo/' + key + '.bin';
    } else {
      el.textContent = 'No custom artwork on device for this station.';
    }
  }

  var artSlotW = 120;
  var artSlotH = 120;
  var workCanvas = document.createElement('canvas');
  workCanvas.width  = artSlotW;
  workCanvas.height = artSlotH;
  var hasImage = false;

  function pullStatus() {
    return fetch(apiBase() + statusPath(), { cache: 'no-store' }).then(function (response) {
      if (!response.ok) {
        return Promise.reject(new Error('HTTP ' + response.status));
      }
      return response.json();
    });
  }

  function readResponse(response) {
    return response.text().then(function (body) {
      var json = null;
      try { json = JSON.parse(body); } catch (e) {}
      return { httpOk: response.ok, status: response.status, body: body, json: json };
    });
  }

  function responseError(result, fallback) {
    if (result && result.json && result.json.error) return String(result.json.error);
    if (result && result.body) return result.body;
    if (result && !result.httpOk && result.status) return 'HTTP ' + result.status;
    return fallback || 'failed';
  }

  function init() {
    var preview  = root.querySelector('.art-preview');
    var fileInput = root.querySelector('.art-file');
    var btnChoose = root.querySelector('.art-choose');
    var btnUpload = root.querySelector('.art-upload');
    var btnRemove = root.querySelector('.art-remove');
    var fsEl     = root.querySelector('.art-fs');
    var stateEl  = root.querySelector('.art-state');
    var selectedEl = root.querySelector('.art-selected');
    var nameEl = root.querySelector('.art-station-name');
    var dimEl = root.querySelector('.art-dim');
    var emptyEl = root.querySelector('.art-empty');
    var stationAvailable = false;
    var artPresent = false;
    var artActionBusy = false;
    var imageSelectionGen = 0;

    if (!btnChoose || !fileInput || !btnUpload || !preview || !stateEl || !selectedEl || !nameEl || !emptyEl) { return; }

    function setSelectedFile(file) {
      selectedEl.textContent = 'Selected image: ' + (file ? file.name : 'none');
    }

    function updateActions() {
      btnChoose.disabled = artActionBusy;
      btnUpload.disabled = artActionBusy || !hasImage || !stationAvailable;
      if (btnRemove) btnRemove.disabled = artActionBusy || !stationAvailable || !artPresent;
    }

    function applyStatus(data) {
      if (!data) {
        stationAvailable = false;
        artPresent = false;
        nameEl.textContent = '\u2014';
        emptyEl.hidden = false;
        emptyEl.textContent = 'Current station status is unavailable. Connect to the device and reload this page.';
        if (dimEl) dimEl.textContent = 'The artwork size is unavailable. Connect to the device and reload this page.';
        setFsLine(fsEl, null, false);
        updateActions();
        return;
      }
      var stationName = data.station_name ? String(data.station_name).trim() : '';
      stationAvailable = stationName.length > 0;
      artPresent = stationAvailable && (data.art_present === true || data.art_present === 'true');
      nameEl.textContent = stationAvailable ? stationName : '\u2014';
      emptyEl.hidden = stationAvailable;
      emptyEl.textContent = 'Start a station before uploading artwork.';
      artSlotW = parseInt(data.slot_w, 10) || 120;
      artSlotH = parseInt(data.slot_h, 10) || 120;
      if (dimEl) dimEl.textContent = 'Artwork is center-cropped to ' + artSlotW + ' \u00d7 ' + artSlotH + ' and converted before upload.';
      setFsLine(fsEl, data, stationAvailable);
      updateActions();
    }

    function refreshStationStatus() {
      // Server owns the station key; re-check it immediately before mutations / Ключ станции серверный — перепроверяем перед записью
      return pullStatus().then(function (data) {
        applyStatus(data);
        return data;
      }, function (error) {
        applyStatus(null);
        return Promise.reject(error);
      });
    }

    refreshStationStatus().then(function () {
      if (preview) {
        preview.width  = artSlotW;
        preview.height = artSlotH;
      }
      workCanvas.width  = artSlotW;
      workCanvas.height = artSlotH;
    }).catch(function () {
      applyStatus(null);
    });

    if (btnRemove) {
      btnRemove.addEventListener('click', function () {
        if (artActionBusy) { return; }
        artActionBusy = true;
        setState(stateEl, 'Checking the current station\u2026', '');
        updateActions();
        refreshStationStatus()
          .then(function () {
            if (!stationAvailable) {
              return Promise.reject(new Error('Start a station before removing artwork.'));
            }
            setState(stateEl, 'Removing artwork\u2026', '');
            return fetch(apiBase() + removePath(), { method: 'POST' }).then(readResponse);
          })
          .then(function (result) {
            if (!result.httpOk || !result.json || result.json.ok !== true) {
              return Promise.reject(new Error(responseError(result, 'remove failed')));
            }
            artPresent = false;
            setState(stateEl, 'Artwork removed from this station.', 'success');
            return refreshStationStatus().catch(function () {
              setFsLine(fsEl, { art_present: false }, stationAvailable);
            });
          })
          .catch(function (error) {
            setState(stateEl, 'Remove failed: ' + error.message, 'error');
          })
          .then(function () {
            artActionBusy = false;
            updateActions();
          });
      });
    }

    btnChoose.addEventListener('click', function () { fileInput.click(); });

    fileInput.addEventListener('change', function () {
      var f = fileInput.files && fileInput.files[0];
      if (!f) { return; }
      var selectionGen = ++imageSelectionGen;
      setSelectedFile(f);
      hasImage = false;
      updateActions();
      setState(stateEl, 'Preparing selected image\u2026', '');
      var url = URL.createObjectURL(f);
      var img = new Image();
      img.onload = function () {
        URL.revokeObjectURL(url);
        if (selectionGen !== imageSelectionGen) { return; }
        workCanvas.width  = artSlotW;
        workCanvas.height = artSlotH;
        var wctx = workCanvas.getContext('2d');
        drawCoverCenter(wctx, img, artSlotW, artSlotH);
        drawPreview(preview, workCanvas);
        hasImage = true;
        updateActions();
        if (stationAvailable) {
          setState(stateEl, 'Ready to upload.', '');
        } else {
          setState(stateEl, 'Image ready. Start a station before uploading artwork.', '');
        }
      };
      img.onerror = function () {
        URL.revokeObjectURL(url);
        if (selectionGen !== imageSelectionGen) { return; }
        setState(stateEl, 'Image could not be loaded.', 'error');
      };
      img.src = url;
    });

    btnUpload.addEventListener('click', function () {
      if (artActionBusy || !hasImage || !stationAvailable) { return; }
      artActionBusy = true;
      setState(stateEl, 'Checking the current station\u2026', '');
      updateActions();
      refreshStationStatus()
        .then(function () {
          if (!stationAvailable) {
            return Promise.reject(new Error('Start a station before uploading artwork.'));
          }
          setState(stateEl, 'Uploading processed artwork\u2026', '');
          var bin  = canvasToLvglBinAlpha(workCanvas);
          var blob = new Blob([bin], { type: 'application/octet-stream' });
          var fd   = new FormData();
          fd.append('file', blob, 'art.bin');
          return fetch(apiBase() + uploadPath(), { method: 'POST', body: fd }).then(readResponse);
        })
        .then(function (result) {
          var j = result.json;
          /* One real success: JSON from handleUploadArt after commit — not empty 200 from onRequest */
          var okCommit =
            result.httpOk && j && j.ok === true &&
            (j.target_exists === true || j.target_exists === 'true') &&
            Number(j.final_size) > 0;
          if (!okCommit) {
            return Promise.reject(new Error(responseError(result, 'invalid response')));
          }
          artPresent = true;
          return refreshStationStatus().then(function (data) {
            var committedKey = j.normalized_key ? String(j.normalized_key) : '';
            var currentKey = data && data.normalized_key ? String(data.normalized_key) : '';
            if (committedKey && currentKey && committedKey !== currentKey) {
              setState(stateEl, 'Uploaded to the station that was active during upload; the current station has changed.', 'success');
            } else {
              setState(stateEl, 'Uploaded to device for the current station.', 'success');
            }
          }).catch(function () {
            setFsLine(fsEl, {
              art_present: true,
              art_size: Number(j.final_size),
              normalized_key: j.normalized_key || ''
            }, stationAvailable);
            setState(stateEl, 'Uploaded to device; status refresh failed.', 'success');
          });
        })
        .catch(function (error) {
          setState(stateEl, 'Upload failed: ' + error.message, 'error');
        })
        .then(function () {
          artActionBusy = false;
          updateActions();
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
