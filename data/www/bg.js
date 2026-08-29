/**
 * Main background upload: browser decode → FIT max-side 1280 → Canvas JPEG → /upload_bg.
 * Фон Main: декод в браузере → FIT 1280 → JPEG Canvas → /upload_bg.
 * Author: Witaliy76 - https://github.com/Witaliy76
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

  /** Cache-bust GET /bg_status after theme change / Сброс кэша после смены темы */
  function statusUrl() {
    var p = statusPath();
    var sep = p.indexOf('?') >= 0 ? '&' : '?';
    return apiBase() + p + sep + '_ts=' + Date.now();
  }

  /** Stale /bg_status must not undo a newer theme request / Защита от устаревшего active_theme */
  var themeUiGen = 0;

  function uploadPath() {
    return root.getAttribute('data-bg-api-upload') || '/upload_bg';
  }

  function removePath() {
    return root.getAttribute('data-bg-api-remove') || '/remove_bg';
  }

  function delay(ms) {
    return new Promise(function (resolve) {
      setTimeout(resolve, ms);
    });
  }

  function readResponse(response) {
    return response.text().then(function (body) {
      var json = null;
      try {
        json = JSON.parse(body);
      } catch (e) {}
      return {
        httpOk: response.ok,
        status: response.status,
        body: body,
        json: json
      };
    });
  }

  function responseError(result, fallback) {
    if (result && result.json && result.json.error) {
      return String(result.json.error);
    }
    if (result && result.body) {
      return result.body;
    }
    if (result && !result.httpOk && result.status) {
      return 'HTTP ' + result.status;
    }
    return fallback || 'failed';
  }

  /** WebUI JPEG normalization (Stage 5 Slice 2). / Нормализация JPEG в браузере. */
  var BG_MAX_SIDE = 1280;
  var BG_JPEG_QUALITY = 0.90;
  var BG_LOADING_STATUS =
    'Loading background… The interface may respond more slowly for a few seconds.';

  function themeActiveStatus(preset) {
    return themeDisplayName(preset) + ' theme active';
  }

  function statusIsLoading(data) {
    return !!(data && (data.bg_loading === true || data.bg_loading === 'true'));
  }

  function applyThemeSelectorStatusFromData(data) {
    var at = data && data.active_theme ? String(data.active_theme) : null;
    if (statusIsLoading(data)) {
      setThemeSelectorStatus(BG_LOADING_STATUS, '');
      return;
    }
    if (at) {
      setThemeSelectorStatus(themeActiveStatus(at), 'success');
      return;
    }
    setThemeSelectorStatus('Active theme status is unavailable.', 'error');
  }

  function fitMaxSide(sw, sh, maxSide) {
    var w = sw;
    var h = sh;
    if (!w || !h) {
      return { w: 1, h: 1 };
    }
    var longest = Math.max(w, h);
    if (longest <= maxSide) {
      return { w: w, h: h };
    }
    var scale = maxSide / longest;
    return {
      w: Math.max(1, Math.round(w * scale)),
      h: Math.max(1, Math.round(h * scale))
    };
  }

  function drawPreview(previewCanvas, sourceCanvas) {
    var pw = previewCanvas.width;
    var ph = previewCanvas.height;
    var pctx = previewCanvas.getContext('2d');
    pctx.fillStyle = '#111';
    pctx.fillRect(0, 0, pw, ph);
    var sw = sourceCanvas.width;
    var sh = sourceCanvas.height;
    if (!sw || !sh) {
      return;
    }
    var scale = Math.min(pw / sw, ph / sh);
    var dw = Math.max(1, Math.round(sw * scale));
    var dh = Math.max(1, Math.round(sh * scale));
    var ox = Math.round((pw - dw) / 2);
    var oy = Math.round((ph - dh) / 2);
    pctx.drawImage(sourceCanvas, 0, 0, sw, sh, ox, oy, dw, dh);
  }

  function sourceSize(src) {
    if (src.naturalWidth) {
      return { w: src.naturalWidth, h: src.naturalHeight };
    }
    return { w: src.width, h: src.height };
  }

  function paintFittedCanvas(canvas, src) {
    var sz = sourceSize(src);
    var fit = fitMaxSide(sz.w, sz.h, BG_MAX_SIDE);
    canvas.width = fit.w;
    canvas.height = fit.h;
    var ctx = canvas.getContext('2d');
    ctx.imageSmoothingEnabled = true;
    if (typeof ctx.imageSmoothingQuality === 'string') {
      ctx.imageSmoothingQuality = 'high';
    }
    ctx.drawImage(src, 0, 0, sz.w, sz.h, 0, 0, fit.w, fit.h);
  }

  function decodeBrowserImage(file) {
    if (typeof createImageBitmap === 'function') {
      return createImageBitmap(file, { imageOrientation: 'from-image' }).catch(function () {
        return createImageBitmap(file);
      });
    }
    return new Promise(function (resolve, reject) {
      var url = URL.createObjectURL(file);
      var img = new Image();
      img.onload = function () {
        URL.revokeObjectURL(url);
        resolve(img);
      };
      img.onerror = function () {
        URL.revokeObjectURL(url);
        reject(new Error('decode'));
      };
      img.src = url;
    });
  }

  function canvasToJpegBlob(canvas) {
    return new Promise(function (resolve, reject) {
      if (typeof canvas.toBlob !== 'function') {
        reject(new Error('toBlob unavailable'));
        return;
      }
      canvas.toBlob(function (blob) {
        if (!blob || blob.size === 0) {
          reject(new Error('jpeg encode failed'));
          return;
        }
        resolve(blob);
      }, 'image/jpeg', BG_JPEG_QUALITY);
    });
  }

  function waitForBackgroundIdle(attempt) {
    attempt = attempt || 0;
    return fetchAppearanceStatus().then(function (data) {
      refreshAppearanceFromStatus(data);
      if (!statusIsLoading(data) || attempt >= 50) {
        return data;
      }
      return delay(200).then(function () {
        return waitForBackgroundIdle(attempt + 1);
      });
    });
  }

  function waitForSlotFile(slot, expectedSize, attempt) {
    attempt = attempt || 0;
    return fetchAppearanceStatus().then(function (data) {
      refreshAppearanceFromStatus(data);
      var key = 'bg_' + slot;
      var present = data && (data[key] === true || data[key] === 'true');
      var size = (data && parseInt(data[key + '_size'], 10)) || 0;
      if (present && expectedSize > 0 && size === expectedSize) {
        return data;
      }
      if (attempt >= 50) {
        return data;
      }
      return delay(200).then(function () {
        return waitForSlotFile(slot, expectedSize, attempt + 1);
      });
    });
  }

  function waitForSlotGone(slot, attempt) {
    attempt = attempt || 0;
    return fetchAppearanceStatus().then(function (data) {
      refreshAppearanceFromStatus(data);
      var key = 'bg_' + slot;
      var present = data && (data[key] === true || data[key] === 'true');
      if (!present || attempt >= 50) {
        return data;
      }
      return delay(200).then(function () {
        return waitForSlotGone(slot, attempt + 1);
      });
    });
  }

  var backgroundActionBusy = false;
  var backgroundActionRefreshers = [];
  // Presence stays in each slot closure; this list only fans out confirmed status / Наличие хранится в слоте; список только раздаёт status
  var backgroundStatusRefreshers = [];
  var customThemeExists = null;
  var customThemeActionBusy = false;

  function setState(el, text, kind) {
    el.textContent = text;
    el.className = 'bg-slot-state' + (kind ? ' ' + kind : '');
  }

  function bgPathForSlot(slot) {
    return '/bg/user_' + slot + '.jpg';
  }

  function setFsLine(el, slot, present, sizeBytes) {
    if (!el) {
      return;
    }
    if (present === null) {
      el.textContent = 'On-device background status is unavailable.';
      return;
    }
    var n = typeof sizeBytes === 'number' && !isNaN(sizeBytes) ? sizeBytes : parseInt(sizeBytes, 10) || 0;
    if (present && n > 0) {
      el.textContent = 'On device: ' + bgPathForSlot(slot) + ' · ' + n + ' B';
    } else if (present) {
      el.textContent = 'On device: ' + bgPathForSlot(slot);
    } else {
      el.textContent = 'No user background on device. Factory art is used.';
    }
  }

  function themeDisplayName(preset) {
    if (preset === 'light') return 'Light';
    if (preset === 'custom') return 'Custom';
    return 'Dark';
  }

  function setThemeSelectorStatus(text, kind) {
    var el = document.getElementById('theme-selector-status');
    if (!el) return;
    el.textContent = text;
    el.className = 'appearance-status' + (kind ? ' ' + kind : '');
  }

  /** Highlight confirmed active preset / Выделить подтверждённый пресет */
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
        opts[i].setAttribute('aria-pressed', 'true');
      } else {
        opts[i].classList.remove('is-active');
        opts[i].setAttribute('aria-pressed', 'false');
      }
    }
  }

  function setThemeControlsBusy(busy) {
    var shell = document.getElementById('appearance-theme-shell');
    if (!shell) return;
    var opts = shell.querySelectorAll('.appearance-theme-opt');
    var i;
    for (i = 0; i < opts.length; i++) {
      opts[i].classList.toggle('is-busy', busy);
      opts[i].disabled = busy;
    }
  }

  function fetchAppearanceStatus() {
    return fetch(statusUrl(), { cache: 'no-store' }).then(function (response) {
      if (!response.ok) {
        return Promise.reject(new Error('HTTP ' + response.status));
      }
      return response.json();
    });
  }

  /** Poll because /set_theme confirms queueing before DspTask applies it / POST подтверждает очередь, не отрисовку */
  function waitForThemeConfirmation(preset, gen, attempt) {
    return fetchAppearanceStatus().then(function (data) {
      if (gen !== themeUiGen) {
        return Promise.reject(new Error('superseded'));
      }
      refreshAppearanceFromStatus(data, {
        skipTheme: true,
        skipThemeStatus: true,
        skipCustomActionLine: true
      });
      var loading = statusIsLoading(data);
      if (data && String(data.active_theme) === preset && !loading) {
        applyThemeShell(preset);
        return data;
      }
      if (attempt >= 50) {
        return Promise.reject(new Error('device confirmation timed out'));
      }
      return delay(200).then(function () {
        return waitForThemeConfirmation(preset, gen, attempt + 1);
      });
    });
  }

  /** Reusable preset path for direct selection and Upload & Apply / Общий путь выбора темы */
  function requestThemePreset(preset, holdControlsBusy) {
    var gen = ++themeUiGen;
    var label = themeDisplayName(preset);
    setThemeSelectorStatus(BG_LOADING_STATUS, '');

    var operation = fetch(apiBase() + '/set_theme?preset=' + encodeURIComponent(preset), { method: 'POST' })
      .then(readResponse)
      .then(function (result) {
        if (!result.httpOk || !result.json || result.json.ok !== true) {
          return Promise.reject(new Error(responseError(result, 'theme request failed')));
        }
        return delay(120).then(function () {
          return waitForThemeConfirmation(preset, gen, 0);
        });
      });

    return operation.then(function (data) {
      if (gen === themeUiGen) {
        setThemeControlsBusy(holdControlsBusy === true);
        applyThemeSelectorStatusFromData(data);
      }
      return data;
    }, function (error) {
      if (gen !== themeUiGen) {
        return Promise.reject(error);
      }
      return fetchAppearanceStatus().then(function (data) {
        refreshAppearanceFromStatus(data, {
          skipThemeStatus: true,
          skipCustomActionLine: true
        });
        return data;
      }, function () {
        return null;
      }).then(function (data) {
        setThemeControlsBusy(holdControlsBusy === true);
        if (data && String(data.active_theme) === preset && !statusIsLoading(data)) {
          applyThemeShell(preset);
          setThemeSelectorStatus(themeActiveStatus(preset), 'success');
          return data;
        }
        setThemeSelectorStatus('Could not apply ' + label + ' theme: ' + error.message, 'error');
        return Promise.reject(error);
      });
    });
  }

  function wireThemeSelector() {
    var shell = document.getElementById('appearance-theme-shell');
    if (!shell) return;
    var opts = shell.querySelectorAll('.appearance-theme-opt');
    var i;
    for (i = 0; i < opts.length; i++) {
      (function (opt) {
        opt.addEventListener('click', function () {
          var preset = opt.getAttribute('data-theme-preset');
          if (!preset || opt.disabled) return;
          requestThemePreset(preset).catch(function () {});
        });
      })(opts[i]);
    }
  }

  /** User-facing custom palette status from /bg_status / Статус custom palette без FS-жаргона */
  function formatCustomThemeFsLine(data) {
    if (!data) {
      return 'Custom palette on device: status unavailable.';
    }
    if (typeof data.custom_theme_exists === 'undefined' && typeof data.custom_theme_applied_keys === 'undefined') {
      return 'Custom palette status is not supported by this firmware.';
    }
    var exists = data.custom_theme_exists === true || data.custom_theme_exists === 'true';
    if (!exists) {
      return 'Custom palette on device: not loaded. Built-in Amber Hi-Fi colors are used.';
    }
    var sz = parseInt(data.custom_theme_size, 10) || 0;
    var applied = parseInt(data.custom_theme_applied_keys, 10) || 0;
    var invalid = parseInt(data.custom_theme_invalid_lines, 10) || 0;
    var unknown = parseInt(data.custom_theme_unknown_keys, 10) || 0;
    var line = 'Custom palette on device: theme_custom.txt';
    if (sz > 0) {
      line += ' · ' + sz + ' B';
    }
    if (applied > 0) {
      line += ' · ' + applied + ' value' + (applied === 1 ? '' : 's') + ' loaded';
    } else {
      line += ' · no custom color values loaded';
    }
    if (invalid + unknown > 0) {
      line += ' · ' + (invalid + unknown) + ' line(s) ignored';
    }
    return line;
  }

  function setCustomThemeActionStatus(text, kind) {
    var el = document.getElementById('custom-theme-status');
    if (!el) {
      return;
    }
    el.textContent = text;
    el.className = 'appearance-status appearance-custom-theme-status' + (kind ? ' ' + kind : '');
  }

  function applyCustomThemeDeviceStatus(data, opts) {
    opts = opts || {};
    var fsEl = document.getElementById('custom-theme-fs');
    if (fsEl) {
      fsEl.textContent = formatCustomThemeFsLine(data);
    }
    if (opts.skipActionLine) {
      return;
    }
    if (!data) {
      setCustomThemeActionStatus('Ready to choose a file.', '');
      return;
    }
    var exists = data.custom_theme_exists === true || data.custom_theme_exists === 'true';
    customThemeExists = exists;
    var removeButton = document.getElementById('custom-theme-remove');
    if (removeButton && !customThemeActionBusy) {
      removeButton.disabled = !exists;
    }
    var applied = parseInt(data.custom_theme_applied_keys, 10) || 0;
    if (exists && applied > 0) {
      setCustomThemeActionStatus('Ready to replace the current custom palette.', '');
    } else if (exists && (parseInt(data.custom_theme_size, 10) || 0) > 0) {
      setCustomThemeActionStatus('Custom palette file has no valid color values; built-in colors are used.', 'warning');
    } else if (exists) {
      setCustomThemeActionStatus('Custom palette file is empty or unavailable.', 'error');
    } else {
      setCustomThemeActionStatus('Ready to choose a file. Custom currently uses built-in Amber Hi-Fi colors.', '');
    }
  }

  function wireCustomThemeFile() {
    var block = document.getElementById('appearance-custom-theme');
    if (!block) {
      return;
    }
    var fileInput = document.getElementById('custom-theme-file');
    var btnChoose = document.getElementById('custom-theme-choose');
    var btnUpload = document.getElementById('custom-theme-upload');
    var btnRemove = document.getElementById('custom-theme-remove');
    var selectedEl = document.getElementById('custom-theme-selected');
    if (!fileInput || !btnChoose || !btnUpload || !btnRemove || !selectedEl) {
      return;
    }
    var uploadPathTheme = block.getAttribute('data-theme-upload') || '/upload_theme';
    var removePathTheme = block.getAttribute('data-theme-remove') || '/remove_theme';
    var pendingFile = null;

    function setSelectedFile(file) {
      selectedEl.textContent = 'Selected file: ' + (file ? file.name : 'none');
    }

    function pullStatus(opts) {
      opts = opts || {};
      return fetchAppearanceStatus()
        .then(function (d) {
          refreshAppearanceFromStatus(d, opts);
          return d;
        });
    }

    function reloadGeneration(data) {
      if (!data || typeof data.custom_theme_reload_generation === 'undefined') {
        return null;
      }
      var generation = Number(data.custom_theme_reload_generation);
      return isFinite(generation) && generation >= 0 ? generation : null;
    }

    /** Parser stats arrive asynchronously / Статистика parser обновляется асинхронно в DspTask */
    function pullStatusAfterReload(attempt, baseline) {
      attempt = attempt || 0;
      return pullStatus({ skipCustomActionLine: true, skipThemeStatus: true }).then(function (d) {
        var generation = reloadGeneration(d);
        var canAcknowledge = baseline !== null && generation !== null;
        var reloadAcknowledged = canAcknowledge && generation !== baseline;
        var shouldRetry = canAcknowledge ? !reloadAcknowledged && attempt < 14 : attempt < 2;
        if (shouldRetry) {
          return new Promise(function (resolve) {
            setTimeout(function () {
              resolve(pullStatusAfterReload(attempt + 1, baseline));
            }, 200);
          });
        }
        applyCustomThemeDeviceStatus(d);
        return d;
      });
    }

    btnChoose.addEventListener('click', function () {
      fileInput.click();
    });

    fileInput.addEventListener('change', function () {
      pendingFile = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
      setSelectedFile(pendingFile);
      if (pendingFile) {
        if (!/\.txt$/i.test(pendingFile.name)) {
          btnUpload.disabled = true;
          setCustomThemeActionStatus('Upload failed: filename must end with .txt.', 'error');
          return;
        }
        if (!pendingFile.size || pendingFile.size > 4096) {
          btnUpload.disabled = true;
          setCustomThemeActionStatus('Upload failed: the file must be between 1 and 4096 bytes.', 'error');
          return;
        }
        btnUpload.disabled = false;
        setCustomThemeActionStatus('Ready to upload and apply.', '');
      } else if (fileInput.files && fileInput.files.length === 0) {
        btnUpload.disabled = true;
        pullStatus().catch(function () {
          setCustomThemeActionStatus('Ready to choose a file.', '');
        });
      }
    });

    btnUpload.addEventListener('click', function () {
      if (!pendingFile) {
        return;
      }
      var uploadCommitted = false;
      var applyConfirmed = false;
      var uploadedSize = 0;
      var reloadBaseline = null;
      customThemeActionBusy = true;
      setThemeControlsBusy(true);
      setCustomThemeActionStatus('Checking device status…', '');
      btnUpload.disabled = true;
      btnChoose.disabled = true;
      btnRemove.disabled = true;
      var fd = new FormData();
      // Backend checks the multipart filename case-sensitively; normalize transport only / Backend проверяет имя с учётом регистра; нормализуем только transport
      fd.append('file', pendingFile, 'theme_custom.txt');
      fetchAppearanceStatus()
        .then(function (data) {
          reloadBaseline = reloadGeneration(data);
        }, function () {
          reloadBaseline = null;
        })
        .then(function () {
          setCustomThemeActionStatus('Uploading custom palette…', '');
          return fetch(apiBase() + uploadPathTheme, { method: 'POST', body: fd });
        })
        .then(readResponse)
        .then(function (result) {
          var committedSize = result.json ? Number(result.json.final_size) : 0;
          if (!result.httpOk || !result.json || result.json.ok !== true || committedSize <= 0) {
            return Promise.reject(new Error(responseError(result, 'upload failed')));
          }
          uploadCommitted = true;
          customThemeExists = true;
          uploadedSize = committedSize;
          pendingFile = null;
          fileInput.value = '';
          setSelectedFile(null);
          setCustomThemeActionStatus('Palette uploaded. Applying Custom theme…', '');
          return requestThemePreset('custom', true);
        })
        .then(function () {
          applyConfirmed = true;
          return delay(180).then(function () {
            return pullStatusAfterReload(0, reloadBaseline);
          });
        })
        .then(function (data) {
          var generation = reloadGeneration(data);
          if (reloadBaseline === null || generation === null || generation === reloadBaseline) {
            setCustomThemeActionStatus('Palette uploaded and Custom selected, but the device could not confirm that the new palette was reloaded.', 'error');
            return;
          }
          if (!data || String(data.active_theme) !== 'custom') {
            setCustomThemeActionStatus('Palette uploaded and reloaded, but Custom is no longer the active theme.', 'warning');
            return;
          }
          var exists = data && (data.custom_theme_exists === true || data.custom_theme_exists === 'true');
          var deviceSize = parseInt(data && data.custom_theme_size, 10) || 0;
          if (!exists || (uploadedSize > 0 && deviceSize !== uploadedSize)) {
            setCustomThemeActionStatus('Palette uploaded, but the device could not confirm the new file.', 'error');
            return;
          }
          var applied = parseInt(data && data.custom_theme_applied_keys, 10) || 0;
          var ignored = (parseInt(data && data.custom_theme_invalid_lines, 10) || 0) +
            (parseInt(data && data.custom_theme_unknown_keys, 10) || 0);
          if (applied === 0) {
            setCustomThemeActionStatus('Uploaded. Custom is active, but no custom color values were applied; built-in Amber Hi-Fi colors are used.', 'warning');
          } else if (ignored > 0) {
            setCustomThemeActionStatus('Uploaded and applied. ' + ignored + ' line(s) were ignored.', 'warning');
          } else {
            setCustomThemeActionStatus('Uploaded and applied.', 'success');
          }
        })
        .catch(function (error) {
          if (applyConfirmed) {
            setCustomThemeActionStatus('Palette uploaded and Custom selected, but palette reload status could not be refreshed.', 'warning');
          } else if (uploadCommitted) {
            setCustomThemeActionStatus('Palette uploaded, but Custom could not be applied: ' + error.message, 'error');
          } else {
            setCustomThemeActionStatus('Upload failed: ' + error.message, 'error');
          }
        })
        .then(function () {
          customThemeActionBusy = false;
          setThemeControlsBusy(false);
          btnChoose.disabled = false;
          btnRemove.disabled = customThemeExists !== true;
          btnUpload.disabled = !pendingFile;
        });
    });

    btnRemove.addEventListener('click', function () {
      setCustomThemeActionStatus('Checking device status…', '');
      btnRemove.disabled = true;
      btnChoose.disabled = true;
      btnUpload.disabled = true;
      var removeCommitted = false;
      var reloadBaseline = null;
      customThemeActionBusy = true;
      setThemeControlsBusy(true);
      fetchAppearanceStatus()
        .then(function (data) {
          reloadBaseline = reloadGeneration(data);
        }, function () {
          reloadBaseline = null;
        })
        .then(function () {
          setCustomThemeActionStatus('Removing custom palette…', '');
          return fetch(apiBase() + removePathTheme, { method: 'POST' });
        })
        .then(readResponse)
        .then(function (result) {
          if (!result.httpOk || !result.json || result.json.ok !== true) {
            return Promise.reject(new Error(responseError(result, 'remove failed')));
          }
          removeCommitted = true;
          customThemeExists = false;
          pendingFile = null;
          fileInput.value = '';
          setSelectedFile(null);
          return delay(220).then(function () {
            return pullStatusAfterReload(0, reloadBaseline);
          });
        })
        .then(function (data) {
          var generation = reloadGeneration(data);
          var exists = data && (data.custom_theme_exists === true || data.custom_theme_exists === 'true');
          if (reloadBaseline === null || generation === null || generation === reloadBaseline || exists) {
            setCustomThemeActionStatus('Custom palette file was removed, but the device could not confirm the built-in color reload.', 'warning');
            return;
          }
          setCustomThemeActionStatus('Custom palette removed. Custom now uses the built-in Amber Hi-Fi colors.', 'success');
        })
        .catch(function (error) {
          if (removeCommitted) {
            setCustomThemeActionStatus('Custom palette removed, but device status could not be refreshed.', 'warning');
          } else {
            setCustomThemeActionStatus('Remove failed: ' + error.message, 'error');
          }
        })
        .then(function () {
          customThemeActionBusy = false;
          setThemeControlsBusy(false);
          btnRemove.disabled = customThemeExists !== true;
          btnChoose.disabled = false;
          btnUpload.disabled = !pendingFile;
        });
    });
  }

  function refreshAppearanceFromStatus(data, opts) {
    opts = opts || {};
    applyBgStatusToSlots(data);
    applyCustomThemeDeviceStatus(data, { skipActionLine: opts.skipCustomActionLine });
    if (opts.skipTheme) {
      return;
    }
    var at = data && data.active_theme ? String(data.active_theme) : null;
    applyThemeShell(at);
    if (!opts.skipThemeStatus) {
      applyThemeSelectorStatusFromData(data);
    }
  }

  /** Apply /bg_status JSON to background cards / Статусы фонов по ответу backend */
  function applyBgStatusToSlots(data) {
    var i;
    for (i = 0; i < backgroundStatusRefreshers.length; i++) {
      backgroundStatusRefreshers[i](data);
    }
  }

  function setBackgroundActionsBusy(busy) {
    backgroundActionBusy = busy;
    var i;
    for (i = 0; i < backgroundActionRefreshers.length; i++) {
      backgroundActionRefreshers[i]();
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
    var selectedEl = slotEl.querySelector('.bg-slot-selected');

    var hasImage = false;
    var slotPresent = null;
    var imageSelectionGen = 0;
    // One prepared image per slot; shared canvas mixed files between cards / Отдельный canvas не перепутывает файлы слотов
    var workCanvas = document.createElement('canvas');

    if (!btnChoose || !fileInput || !btnUpload || !preview || !fsLine || !stateEl || !selectedEl) {
      return;
    }

    function setSelectedFile(file) {
      selectedEl.textContent = 'Selected image: ' + (file ? file.name : 'none');
    }

    function updateActions() {
      btnChoose.disabled = backgroundActionBusy;
      btnUpload.disabled = backgroundActionBusy || !hasImage;
      if (btnRemove) {
        btnRemove.disabled = backgroundActionBusy || slotPresent !== true;
      }
    }

    function applySlotStatus(data) {
      var key = 'bg_' + slot;
      if (!data || typeof data[key] === 'undefined') {
        slotPresent = null;
        setFsLine(fsLine, slot, null, 0);
        updateActions();
        return;
      }
      slotPresent = data[key] === true || data[key] === 'true';
      setFsLine(fsLine, slot, slotPresent, parseInt(data[key + '_size'], 10) || 0);
      updateActions();
    }

    backgroundActionRefreshers.push(updateActions);
    backgroundStatusRefreshers.push(applySlotStatus);
    updateActions();

    function pullStatus() {
      return fetchAppearanceStatus()
        .then(function (d) {
          refreshAppearanceFromStatus(d);
          return d;
        });
    }

    if (btnRemove) {
      btnRemove.addEventListener('click', function () {
        if (backgroundActionBusy || slotPresent !== true) {
          return;
        }
        setBackgroundActionsBusy(true);
        setState(stateEl, 'Removing image…', '');
        fetch(apiBase() + removePath() + '?slot=' + encodeURIComponent(slot), { method: 'POST' })
          .then(readResponse)
          .then(function (result) {
            if (result.httpOk && result.json && result.json.ok === true) {
              var queuedRemove = result.json.queued === true || result.json.queued === 'true';
              slotPresent = false;
              setFsLine(fsLine, slot, false, 0);
              updateActions();
              if (queuedRemove) {
                setState(stateEl, 'Waiting for the current background to finish loading…', '');
                return waitForSlotGone(slot)
                  .then(function () {
                    return waitForBackgroundIdle(0);
                  })
                  .then(function () {
                    setState(stateEl, 'Image removed from this theme slot.', 'success');
                  })
                  .catch(function () {
                    setState(
                      stateEl,
                      'Image removed from this theme slot. Live status refresh failed.',
                      'success'
                    );
                  });
              }
              setState(stateEl, 'Image removed from this theme slot.', 'success');
              return pullStatus()
                .then(function () {
                  return waitForBackgroundIdle(0);
                })
                .catch(function () {
                  setState(
                    stateEl,
                    'Image removed from this theme slot. Live status refresh failed.',
                    'success'
                  );
                });
            }
            applySlotStatus(null);
            setState(stateEl, 'Remove failed: ' + responseError(result, 'failed'), 'error');
          })
          .catch(function () {
            applySlotStatus(null);
            setState(stateEl, 'Remove failed.', 'error');
          })
          .then(function () {
            setBackgroundActionsBusy(false);
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
      var selectionGen = ++imageSelectionGen;
      setSelectedFile(f);
      hasImage = false;
      updateActions();
      setState(stateEl, 'Preparing selected image…', '');
      decodeBrowserImage(f)
        .then(function (src) {
          if (selectionGen !== imageSelectionGen) {
            if (src && typeof src.close === 'function') {
              src.close();
            }
            return;
          }
          paintFittedCanvas(workCanvas, src);
          if (src && typeof src.close === 'function') {
            src.close();
          }
          drawPreview(preview, workCanvas);
          hasImage = true;
          updateActions();
          setState(
            stateEl,
            'Ready to upload (' + workCanvas.width + '\u00d7' + workCanvas.height + ' JPEG).',
            ''
          );
        })
        .catch(function () {
          if (selectionGen !== imageSelectionGen) {
            return;
          }
          updateActions();
          setState(stateEl, 'Image could not be loaded.', 'error');
        });
    });

    btnUpload.addEventListener('click', function () {
      if (!hasImage) {
        return;
      }
      if (backgroundActionBusy) {
        setState(stateEl, 'Another background is uploading. Try again in a moment.', '');
        return;
      }
      setBackgroundActionsBusy(true);
      setState(stateEl, 'Uploading processed image…', '');
      canvasToJpegBlob(workCanvas)
        .then(function (blob) {
          function postOnce() {
            var fd = new FormData();
            fd.append('file', blob, 'user_' + slot + '.jpg');
            var url = apiBase() + uploadPath() + '?slot=' + encodeURIComponent(slot);
            return fetch(url, { method: 'POST', body: fd }).then(readResponse);
          }
          function postRetry(attempt) {
            return postOnce().then(function (result) {
              if (result.json && result.json.error === 'busy' && attempt < 15) {
                return delay(200).then(function () {
                  return postRetry(attempt + 1);
                });
              }
              return result;
            });
          }
          return postRetry(0);
        })
        .then(function (result) {
          var j = result.json;
          var queued = j && (j.queued === true || j.queued === 'true');
          var okCommit =
            result.httpOk &&
            j &&
            j.ok === true &&
            (queued ||
              ((j.target_exists === true || j.target_exists === 'true') &&
                Number(j.final_size) > 0));
          if (!okCommit) {
            applySlotStatus(null);
            setState(stateEl, 'Upload failed: ' + responseError(result, 'invalid response'), 'error');
            return;
          }
          var expected = queued ? Number(j.written_bytes) : Number(j.final_size);
          slotPresent = true;
          setFsLine(fsLine, slot, true, expected);
          updateActions();
          if (queued) {
            setState(stateEl, 'Waiting for the current background to finish loading…', '');
          }
          return waitForSlotFile(slot, expected)
            .then(function (d) {
              refreshAppearanceFromStatus(d);
              setState(stateEl, 'Uploaded to device.', 'success');
              if (d && String(d.active_theme) === slot) {
                return waitForBackgroundIdle(0);
              }
              return d;
            })
            .catch(function () {
              setState(stateEl, 'Uploaded to device; live status refresh failed.', 'success');
            });
        })
        .catch(function () {
          applySlotStatus(null);
          setState(stateEl, 'Upload failed.', 'error');
        })
        .then(function () {
          setBackgroundActionsBusy(false);
        });
    });
  }

  function init() {
    wireThemeSelector();
    wireCustomThemeFile();
    var dimEl = document.getElementById('bg-device-dim');
    fetchAppearanceStatus()
      .then(function (data) {
        if (dimEl) {
          dimEl.textContent =
            'Images keep their aspect ratio. The longest side is limited to 1280 px (no upscale, crop, or stretch). The browser converts the file to JPEG before upload. Factory art remains the fallback.';
        }
        var slots = root.querySelectorAll('.bg-slot');
        var i;
        for (i = 0; i < slots.length; i++) {
          wireSlot(slots[i]);
        }
        refreshAppearanceFromStatus(data);
      })
      .catch(function () {
        if (dimEl) {
          dimEl.textContent =
            'Images keep their aspect ratio. The longest side is limited to 1280 px (no upscale, crop, or stretch). The browser converts the file to JPEG before upload. Factory art remains the fallback.';
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
      dim.textContent = 'This browser cannot upload backgrounds because fetch() is unavailable.';
    }
  } else {
    // defer on appearance.html — DOM is ready when this runs / defer — DOM уже готов
    init();
  }
})();
