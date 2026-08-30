/**
 * User text font upload/remove/status for Appearance.
 * Пользовательский текстовый TTF: загрузка, удаление, статус.
 * Apply requires reboot; no live preview.
 * Author: Witaliy76 - https://github.com/Witaliy76
 */
(function () {
  'use strict';

  var root = document.getElementById('font-upload-root');
  if (!root) {
    return;
  }

  var MAX_BYTES = parseInt(root.getAttribute('data-font-max-bytes') || '524288', 10);

  function apiBase() {
    var q = window.location.search;
    var h = q !== '' ? q.substring(1) : window.location.hostname;
    return 'http://' + h;
  }

  function statusUrl() {
    var p = root.getAttribute('data-font-api-status') || '/font_status';
    var sep = p.indexOf('?') >= 0 ? '&' : '?';
    return apiBase() + p + sep + '_ts=' + Date.now();
  }

  function uploadPath() {
    return root.getAttribute('data-font-api-upload') || '/upload_font';
  }

  function removePath() {
    return root.getAttribute('data-font-api-remove') || '/remove_font';
  }

  var fileInput = document.getElementById('user-font-file');
  var chooseBtn = document.getElementById('user-font-choose');
  var uploadBtn = document.getElementById('user-font-upload');
  var removeBtn = document.getElementById('user-font-remove');
  var selectedEl = document.getElementById('user-font-selected');
  var deviceEl = document.getElementById('user-font-device');
  var statusEl = document.getElementById('user-font-status');
  var selectedFile = null;

  function setStatus(text, kind) {
    if (!statusEl) return;
    statusEl.textContent = text;
    statusEl.classList.remove('success', 'warning', 'error');
    if (kind) statusEl.classList.add(kind);
  }

  function runtimeLabel(runtime) {
    if (runtime === 'user') return 'User font is active';
    if (runtime === 'rejected') return 'User font file is present but was rejected — factory font is active';
    if (runtime === 'emergency') return 'Emergency compiled 16 px font is active';
    return 'Factory font is active';
  }

  function refreshStatus() {
    return fetch(statusUrl(), { cache: 'no-store' })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        if (!j || j.ok !== true) {
          if (deviceEl) deviceEl.textContent = 'Could not read font status.';
          return;
        }
        if (typeof j.max_bytes === 'number') MAX_BYTES = j.max_bytes;
        var parts = [];
        parts.push(runtimeLabel(j.runtime));
        if (j.file_present) {
          parts.push('On device: /fonts/user.ttf (' + j.file_size + ' bytes).');
        } else {
          parts.push('No user font file on the device.');
        }
        if (j.reboot_required) {
          parts.push('Reboot the device to apply the latest upload or remove.');
        }
        if (deviceEl) deviceEl.textContent = parts.join(' ');
        if (removeBtn) removeBtn.disabled = !j.file_present;
        if (j.reboot_required) {
          setStatus('Reboot required to apply the font change. The device does not reboot itself.', 'warning');
        }
      })
      .catch(function () {
        if (deviceEl) deviceEl.textContent = 'Could not read font status.';
      });
  }

  function looksTtfName(name) {
    if (!name) return false;
    return /\.ttf$/i.test(name);
  }

  if (chooseBtn && fileInput) {
    chooseBtn.addEventListener('click', function () { fileInput.click(); });
  }
  if (fileInput) {
    fileInput.addEventListener('change', function () {
      selectedFile = fileInput.files && fileInput.files[0] ? fileInput.files[0] : null;
      if (!selectedFile) {
        if (selectedEl) selectedEl.textContent = 'Selected file: none';
        if (uploadBtn) uploadBtn.disabled = true;
        return;
      }
      if (selectedEl) selectedEl.textContent = 'Selected file: ' + selectedFile.name + ' (' + selectedFile.size + ' bytes)';
      if (!looksTtfName(selectedFile.name)) {
        setStatus('Only a .ttf file is accepted.', 'error');
        if (uploadBtn) uploadBtn.disabled = true;
        return;
      }
      if (selectedFile.size > MAX_BYTES) {
        setStatus('File is larger than ' + MAX_BYTES + ' bytes. Choose a smaller TrueType font.', 'error');
        if (uploadBtn) uploadBtn.disabled = true;
        return;
      }
      if (selectedFile.size < 1) {
        setStatus('File is empty.', 'error');
        if (uploadBtn) uploadBtn.disabled = true;
        return;
      }
      setStatus('Ready to upload. The new font is applied after reboot.', '');
      if (uploadBtn) uploadBtn.disabled = false;
    });
  }

  if (uploadBtn) {
    uploadBtn.addEventListener('click', function () {
      if (!selectedFile) return;
      if (selectedFile.size > MAX_BYTES) {
        setStatus('File is larger than ' + MAX_BYTES + ' bytes.', 'error');
        return;
      }
      uploadBtn.disabled = true;
      setStatus('Uploading…', '');
      var fd = new FormData();
      fd.append('file', selectedFile, selectedFile.name);
      fetch(apiBase() + uploadPath(), { method: 'POST', body: fd, cache: 'no-store' })
        .then(function (r) { return r.json().then(function (j) { return { okHttp: r.ok, j: j }; }); })
        .then(function (res) {
          if (!res.j || res.j.ok !== true) {
            var reason = (res.j && (res.j.reason || res.j.error)) || 'upload_failed';
            setStatus('Upload rejected: ' + reason + '. The previous font file was left unchanged.', 'error');
            uploadBtn.disabled = false;
            return refreshStatus();
          }
          setStatus('Uploaded. Reboot the device to apply the new font. Live text is unchanged until reboot.', 'warning');
          return refreshStatus();
        })
        .catch(function () {
          setStatus('Upload failed.', 'error');
          uploadBtn.disabled = false;
        });
    });
  }

  if (removeBtn) {
    removeBtn.addEventListener('click', function () {
      removeBtn.disabled = true;
      fetch(apiBase() + removePath(), { method: 'POST', cache: 'no-store' })
        .then(function (r) { return r.json(); })
        .then(function (j) {
          if (!j || j.ok !== true) {
            setStatus('Could not remove the user font.', 'error');
            return refreshStatus();
          }
          setStatus('User font file removed. Reboot the device to return to the factory font. Live text is unchanged until reboot.', 'warning');
          selectedFile = null;
          if (fileInput) fileInput.value = '';
          if (selectedEl) selectedEl.textContent = 'Selected file: none';
          if (uploadBtn) uploadBtn.disabled = true;
          return refreshStatus();
        })
        .catch(function () {
          setStatus('Could not remove the user font.', 'error');
          return refreshStatus();
        });
    });
  }

  refreshStatus();
})();
