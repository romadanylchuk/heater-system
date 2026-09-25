/* SPA pages, stage 05: Sensors, Network, Setup, System.
 * Loaded after app.js; registers its pages with App.page(). Every POST goes
 * through App.api/App.command (CSRF header); the OTA XHR upload sets
 * X-CSRF-Token itself. Untrusted values (SSIDs, sensor
 * addresses and names) reach the DOM only through App.el (textContent) or
 * UI.* HTML builders (escaped). */
'use strict';

var Pages = (function () {
  var t = App.t;
  var el = App.el;
  var clear = App.clear;
  var card = App.card;

  var SENSORS_POLL_MS = 2000;
  var WIFI_STATUS_POLL_MS = 3000;
  var SCAN_POLL_MS = 1000;
  var SCAN_LIMIT_MS = 20000;
  // Mirrors checkWifiCreds (NetEngine/WifiCredsMailbox.h), in UTF-8 bytes.
  var WIFI_SSID_MAX = 32;
  var WIFI_PASS_MIN = 8;
  var WIFI_PASS_MAX = 64;

  function $(id) { return document.getElementById(id); }

  var utf8Len = App.utf8Len;   // UTF-8 bytes (the server counts bytes)

  // Client copy of the server's Wi-Fi pair check. Returns '' when valid,
  // otherwise a language key. An empty password means an open network.
  function wifiCheck(ssid, pass) {
    ssid = String(ssid || '');
    pass = String(pass || '');
    if (!ssid.length) return 'net.err_ssid_empty';
    if (ssid.indexOf('\u0000') >= 0 || pass.indexOf('\u0000') >= 0) return 'err.embedded_nul';
    if (utf8Len(ssid) > WIFI_SSID_MAX) return 'net.err_ssid_long';
    var n = utf8Len(pass);
    if (n === 0) return '';
    if (n < WIFI_PASS_MIN) return 'net.err_pass_short';
    if (n > WIFI_PASS_MAX) return 'net.err_pass_long';
    if (n === WIFI_PASS_MAX && !/^[0-9a-fA-F]{64}$/.test(pass)) return 'net.err_pass_hex';
    return '';
  }

  // One message line in box. kind: ok | warn | err | '' (neutral).
  function note(box, text, kind, retry) {
    if (!box) return;
    clear(box);
    if (!text) return;
    var cls = kind === 'ok' ? 'msg-ok' : kind === 'err' ? 'msg-error' : kind === 'warn' ? 'warn-banner' : 'info-text';
    var n = el('div', cls, text);
    if (retry) {
      n.appendChild(document.createTextNode(' '));
      n.appendChild(el('button', {
        type: 'button', 'class': 'btn btn-secondary', text: t('common.retry'),
        onclick: function () { this.disabled = true; retry(); }
      }));
    }
    box.appendChild(n);
  }

  // ─── Sensors ─────────────────────────────────────────────────────────────
  // sen.picked keeps the user's unsent dropdown choices (addr -> logical)
  // across the 2 s redraws; sen.sig skips redraws when nothing changed.
  var sen = { data: null, sig: '', picked: {}, busy: false };

  function loadSensors() {
    return App.api('GET', '/api/sensors').then(function (r) {
      if (r.ok && r.data && Array.isArray(r.data.bus)) sen.data = r.data;
      return r;
    });
  }

  function logicalList() { return (sen.data && sen.data.logical) || []; }

  function logicalName(i) {
    var l = logicalList();
    for (var k = 0; k < l.length; k++) if (l[k].i === i) return l[k].name || String(i);
    return String(i);
  }

  function busRow(addr) {
    var b = (sen.data && sen.data.bus) || [];
    for (var k = 0; k < b.length; k++) if (b[k].addr === addr) return b[k];
    return null;
  }

  // Notice for an assignment: moving the sensor away from another logical
  // sensor, and/or replacing the sensor the target currently has.
  function assignNotice(row, target) {
    var parts = [];
    if (row && row.logical >= 0 && row.logical !== target) {
      parts.push(t('sensors.move', { from: logicalName(row.logical) }));
    }
    var l = logicalList();
    for (var k = 0; k < l.length; k++) {
      if (l[k].i === target && l[k].assigned && l[k].addr && (!row || l[k].addr !== row.addr)) {
        parts.push(t('sensors.replace', { name: l[k].name || String(target) }));
      }
    }
    return parts.join(' ');
  }

  async function sensorAction(path, body, notice) {
    if (sen.busy) return;
    sen.busy = true;
    var prefix = notice ? notice + ' ' : '';
    note($('sn-msg'), prefix + t('cmd.pending'), '');
    var res;
    try {
      res = await App.command(path, body);
    } finally {
      sen.busy = false;
    }
    var rt = App.resultText(res);
    note($('sn-msg'), prefix + rt.text, rt.kind, rt.retry ? function () { sensorAction(path, body, notice); } : null);
    if (body && body.addr) delete sen.picked[body.addr];
    await refreshSensors(true);
  }

  function onAssign(addr) {
    var row = busRow(addr);
    if (!row) return;
    var v = Object.prototype.hasOwnProperty.call(sen.picked, addr) ? sen.picked[addr] : row.logical;
    var target = parseInt(v, 10);
    if (isNaN(target)) return;
    if (target < 0) {
      // "unassigned": clear the logical sensor this address belongs to.
      if (row.logical >= 0) sensorAction('/api/sensors/clear', { logical: row.logical });
      else delete sen.picked[addr];
      return;
    }
    if (target === row.logical) {
      note($('sn-msg'), t('cmd.unchanged'), 'ok');
      return;
    }
    sensorAction('/api/sensors/assign', { logical: target, addr: addr }, assignNotice(row, target));
  }

  function drawBus(host) {
    var d = sen.data;
    var bus = (d && d.bus) || [];
    if (!bus.length) {
      clear(host).appendChild(el('p', { 'class': 'muted', text: t('sensors.none') }));
      return;
    }
    var opts = logicalList().map(function (l) { return { i: l.i, name: l.name || String(l.i) }; });
    host.innerHTML = UI.sensorTable(bus.map(function (b) {
      return { addr: b.addr, t: b.t, logical: typeof b.logical === 'number' ? b.logical : -1, 'new': !!b['new'] };
    }), opts, t);
    // Re-apply the user's unsent choices.
    Array.prototype.forEach.call(host.querySelectorAll('select[data-addr]'), function (s) {
      var a = s.getAttribute('data-addr');
      if (Object.prototype.hasOwnProperty.call(sen.picked, a)) s.value = sen.picked[a];
    });
  }

  function stateText(l) {
    if (l.missing) return t('sensors.missing');
    if (!l.assigned || l.state === 'unassigned') return t('sensors.unassigned');
    if (l.state === 'fault') return t('status.sensor_fault');
    if (l.state === 'ok') return t('sensors.ok');
    return t('common.na');
  }

  function drawLogical(host) {
    var list = logicalList();
    clear(host);
    if (!list.length) {
      host.appendChild(el('p', { 'class': 'muted', text: t('common.na') }));
      return;
    }
    var head = el('tr', null, ['sensors.name', 'sensors.address', 'sensors.temp', 'sensors.state', null].map(function (k) {
      return el('th', { text: k ? t(k) : '' });
    }));
    var rows = list.map(function (l) {
      var bad = l.missing || l.state === 'fault';
      return el('tr', null, [
        el('td', { text: l.name || String(l.i) }),
        el('td', { 'class': 'addr', text: l.assigned && l.addr ? l.addr : '—' }),
        el('td', { text: UI.fmtTemp(l.t) }),
        el('td', null, el('span', { 'class': bad ? 'badge-missing' : '', text: stateText(l) })),
        el('td', null, l.assigned ? el('button', {
          type: 'button', 'class': 'btn btn-secondary', text: t('sensors.clear'),
          onclick: function () { sensorAction('/api/sensors/clear', { logical: l.i }); }
        }) : null)
      ]);
    });
    host.appendChild(el('div', 'log-wrap', el('table', 'sensor-table', [el('thead', null, head), el('tbody', null, rows)])));
  }

  function drawInfo(host) {
    var d = sen.data || {};
    clear(host);
    if (d.scanDone === false) host.appendChild(el('p', { 'class': 'info-text', text: t('sensors.scanning') }));
    else if (typeof d.scanCount === 'number') host.appendChild(el('p', { 'class': 'info-text', text: t('sensors.found', { n: d.scanCount }) }));
    if (d.overflow) host.appendChild(el('div', { 'class': 'warn-banner', text: t('sensors.overflow') }));
  }

  // Redraws the tables when the data changed. The bus table is left alone
  // while one of its dropdowns has focus, so an open list is not closed.
  function drawSensors(force) {
    var bus = $('sn-bus');
    var log = $('sn-log');
    if (!bus || !log) return;
    var sig = JSON.stringify(sen.data);
    if (!force && sig === sen.sig) return;
    var act = document.activeElement;
    var editing = act && act.tagName === 'SELECT' && bus.contains(act);
    if (!editing) { drawBus(bus); sen.sig = sig; }
    drawLogical(log);
    drawInfo($('sn-info'));
  }

  function refreshSensors(force) {
    return loadSensors().then(function () { drawSensors(force === true); });
  }

  App.page('sensors', {
    load: function () {
      sen.sig = '';
      App.poll(refreshSensors, SENSORS_POLL_MS);
      return loadSensors();
    },
    render: function (v) {
      var bus = el('div', { id: 'sn-bus' });
      bus.addEventListener('change', function (e) {
        var s = e.target;
        if (s && s.tagName === 'SELECT' && s.hasAttribute('data-addr')) sen.picked[s.getAttribute('data-addr')] = s.value;
      });
      bus.addEventListener('click', function (e) {
        var b = e.target && e.target.closest ? e.target.closest('[data-act="assign"]') : null;
        if (b) onAssign(b.getAttribute('data-addr'));
      });
      var rescan = el('button', {
        type: 'button', 'class': 'btn btn-secondary', text: t('sensors.rescan'),
        onclick: function () { sensorAction('/api/sensors/rescan', null); }
      });
      v.appendChild(card(t('sensors.title'), [
        el('p', { 'class': 'muted', text: t('sensors.hint') }),
        el('p', null, rescan), el('div', { id: 'sn-msg' }), el('div', { id: 'sn-info' })
      ]));
      v.appendChild(card(t('sensors.bus'), [bus]));
      v.appendChild(card(t('sensors.logical'), [el('div', { id: 'sn-log' })]));
      if (!sen.data) note($('sn-msg'), App.errText(App.online() ? 'unknown' : 'network'), 'err');
      drawSensors(true);
    }
  });

  // ─── Wi-Fi card (Network and Setup) ──────────────────────────────────────
  // Wi-Fi is only changed through the atomic POST /api/wifi (ssid + pass
  // together, D16). wifi.gen invalidates the scan loop and status poller of
  // an older card (re-render on a language switch or navigation).
  var wifi = { gen: 0, stopPoll: null, scan: null, status: null };

  function wifiStatusRows(s) {
    if (!s) return [el('p', { 'class': 'muted', text: t('status.no_data') })];
    var rows = [
      el('p', { text: t('net.status') + ': ' + t(s.connected ? 'status.connected' : 'status.disconnected') +
        (s.ssid ? ' (' + s.ssid + ')' : '') }),
      s.connected && typeof s.rssi === 'number' ? el('p', { text: t('status.rssi') + ': ' + t('status.dbm', { v: s.rssi }) }) : null,
      s.ip ? el('p', { text: t('net.ip') + ': ' + s.ip }) : null,
      s.hostname ? el('p', { text: t('net.host') + ': ' + s.hostname + '.local' }) : null,
      s.apActive ? el('p', { text: t('net.ap_mode') + (s.apSsid ? ': ' + s.apSsid : '') + (s.apIp ? ' ' + s.apIp : '') }) : null
    ];
    return rows;
  }

  function loadWifiStatus() {
    return App.api('GET', '/api/wifi/status').then(function (r) {
      if (r.ok && r.data && typeof r.data.connected === 'boolean') wifi.status = r.data;
      var box = $('wf-status');
      if (box) { clear(box); wifiStatusRows(wifi.status).forEach(function (n) { if (n) box.appendChild(n); }); }
    });
  }

  function drawScan(gen) {
    var list = $('wf-list');
    if (!list || gen !== wifi.gen) return;
    clear(list);
    var d = wifi.scan;
    if (!d) return;
    if (d.running) list.appendChild(el('p', { 'class': 'info-text', text: t('net.scanning') }));
    var nets = (d.networks || []).filter(function (n) { return n && typeof n.ssid === 'string' && n.ssid.length; });
    if (!nets.length && !d.running) { list.appendChild(el('p', { 'class': 'muted', text: t('net.no_networks') })); return; }
    nets.forEach(function (n) {
      var sec = t(n.secure ? 'net.secure' : 'net.open');
      list.appendChild(el('p', null, el('button', {
        type: 'button', 'class': 'btn btn-secondary',
        onclick: function () {
          var s = $('wf-ssid');
          var p = $('wf-pass');
          if (s) s.value = n.ssid;
          if (p) { p.value = ''; p.focus(); }
        }
      }, n.ssid + ' · ' + t('status.dbm', { v: n.rssi }) + ' · ' + sec)));
    });
  }

  function getScan() {
    return App.api('GET', '/api/wifi/scan').then(function (r) {
      if (r.ok && r.data && Array.isArray(r.data.networks)) wifi.scan = r.data;
      return r;
    });
  }

  // POST starts a scan (202); GET is polled while "running".
  async function runScan(btn, gen) {
    btn.disabled = true;
    var msg = $('wf-scan-msg');
    note(msg, '', '');
    var r = await App.api('POST', '/api/wifi/scan', null);
    if (!r.ok) {
      btn.disabled = false;
      var rt = App.resultText({ error: r.data.error || (r.status === 0 ? 'network' : 'unknown'), http: r.status, data: r.data });
      note(msg, rt.text, 'err');
      return;
    }
    wifi.scan = { running: true, networks: (wifi.scan && wifi.scan.networks) || [] };
    drawScan(gen);
    var start = Date.now();
    var seen = false;   // the snapshot may lag the request by a loop tick
    while (gen === wifi.gen && Date.now() - start < SCAN_LIMIT_MS) {
      await new Promise(function (res) { setTimeout(res, SCAN_POLL_MS); });
      if (gen !== wifi.gen) return;
      var g = await getScan();
      if (g.ok && wifi.scan.running) seen = true;
      if (g.ok && !wifi.scan.running && (seen || Date.now() - start > 3 * SCAN_POLL_MS)) break;
      if (g.status === 401) return;
    }
    if (gen !== wifi.gen) return;
    if (wifi.scan && wifi.scan.running) { wifi.scan.running = false; note(msg, t('cmd.timeout'), 'err'); }
    btn.disabled = false;
    drawScan(gen);
  }

  function wifiErrText(key) {
    return t(key, { n: WIFI_SSID_MAX, min: WIFI_PASS_MIN, max: WIFI_PASS_MAX - 1 });
  }

  async function saveWifi(btn, setup) {
    var s = $('wf-ssid');
    var p = $('wf-pass');
    var msg = $('wf-msg');
    if (!s || !p || btn.disabled) return;
    var ssid = s.value;
    var pass = p.value;
    var bad = wifiCheck(ssid, pass);
    if (bad) { note(msg, wifiErrText(bad), 'err'); return; }
    if (!pass && !window.confirm(t('net.open_confirm', { ssid: ssid }))) return;
    btn.disabled = true;
    note(msg, t('common.saving'), '');
    var r = await App.api('POST', '/api/wifi', { ssid: ssid, pass: pass });
    btn.disabled = false;
    if (r.ok) {
      p.value = '';
      note(msg, t('net.wifi_saved') + (setup ? ' ' + t('setup.restart') : ''), 'ok');
      return;
    }
    if (r.status === 401) return;   // App.api shows the login card
    var retry = function () { saveWifi(btn, setup); };
    if (r.status === 400) note(msg, t('net.err_invalid'), 'err');
    else if (r.status === 503 || r.data.error === 'busy') note(msg, t('net.err_busy'), 'err', retry);
    else if (r.status === 0) note(msg, t('err.network'), 'err', retry);
    else note(msg, App.errText(r.data.error), 'err');
  }

  // Form groups carry data-key wifiSsid/wifiPass so a language switch keeps
  // unsaved input (App's pendingEdits); App.saveFields never sends them.
  function wifiForm(setup) {
    var ssid = el('input', { type: 'text', id: 'wf-ssid', maxlength: WIFI_SSID_MAX, autocomplete: 'off', spellcheck: 'false' });
    var pass = el('input', { type: 'password', id: 'wf-pass', maxlength: WIFI_PASS_MAX, autocomplete: 'new-password' });
    var save = el('button', { type: 'button', 'class': 'btn btn-primary', text: t('net.save_wifi') });
    save.addEventListener('click', function () { saveWifi(save, setup); });
    return [
      el('div', { 'class': 'form-group', 'data-key': 'wifiSsid', 'data-type': 'text', 'data-secret': '0', 'data-orig': '' }, [
        el('label', { 'for': 'wf-ssid', text: t('net.ssid') }), ssid,
        el('div', { 'class': 'hint', text: t('net.ssid_hint', { n: WIFI_SSID_MAX }) })
      ]),
      el('div', { 'class': 'form-group', 'data-key': 'wifiPass', 'data-type': 'text', 'data-secret': '1', 'data-orig': '' }, [
        el('label', { 'for': 'wf-pass', text: t('net.pass') }),
        el('div', 'pw-wrap', [pass, el('button', { type: 'button', 'class': 'pw-show', 'data-act': 'pw', 'data-for': 'wf-pass', text: t('login.show') })]),
        el('div', { 'class': 'hint', text: t('net.pass_hint', { min: WIFI_PASS_MIN, max: WIFI_PASS_MAX - 1 }) })
      ]),
      el('div', { 'class': 'warn-banner', text: t('net.reconnect_warn') }),
      el('p', null, save),
      el('div', { id: 'wf-msg' })
    ];
  }

  // Appends the Wi-Fi card to v. intro: optional nodes shown first.
  function wifiCard(v, setup, intro) {
    var gen = ++wifi.gen;
    if (wifi.stopPoll) wifi.stopPoll();
    wifi.stopPoll = App.poll(loadWifiStatus, WIFI_STATUS_POLL_MS);
    var scanBtn = el('button', { type: 'button', 'class': 'btn btn-secondary', text: t('net.scan') });
    scanBtn.addEventListener('click', function () { runScan(scanBtn, gen); });
    var status = el('div', { id: 'wf-status' });
    wifiStatusRows(wifi.status).forEach(function (n) { if (n) status.appendChild(n); });
    v.appendChild(card(t(setup ? 'setup.title' : 'net.wifi'), (intro || []).concat([
      status,
      el('h3', { text: t('net.networks') }),
      el('p', null, scanBtn), el('div', { id: 'wf-scan-msg' }), el('div', { id: 'wf-list' }),
      el('p', { 'class': 'info-text', text: t('net.manual') })
    ], wifiForm(setup))));
    drawScan(gen);
    if (!wifi.scan || !wifi.scan.running) getScan().then(function () { drawScan(gen); });
  }

  // ─── Network page ────────────────────────────────────────────────────────
  function mqttStatus(st) {
    var n = $('nt-mqtt-st');
    if (!n) return;
    var net = (st && st.net) || {};
    n.textContent = net.mqttEnabled
      ? t('net.status') + ': ' + t(net.mqtt ? 'status.connected' : 'status.disconnected')
      : t('net.mqtt_disabled');
  }

  function groupCard(g) {
    var fields = el('div', 'form-grid');
    if (!App.renderFields(fields, g)) return null;
    var msg = el('div');
    var save = el('button', {
      type: 'button', 'class': 'btn btn-primary', text: t('settings.save_all'),
      onclick: async function () {
        if (save.disabled) return;
        save.disabled = true;
        clear(msg);
        var r = await App.saveFields(fields);
        save.disabled = false;
        if (!r.changed) msg.appendChild(el('p', { 'class': 'muted', text: t('settings.no_changes') }));
      }
    });
    return card(App.groupLabel(g), [
      g === 'mqtt' ? el('p', { id: 'nt-mqtt-st', 'class': 'muted' }) : null,
      fields, el('p', null, save), msg
    ]);
  }

  App.page('network', {
    load: function () { return Promise.all([App.loadSettings(), loadWifiStatus()]); },
    render: function (v, st) {
      wifiCard(v, false);
      if (!App.schema() || !App.config()) {
        v.appendChild(card(t('net.mqtt'), [el('p', { 'class': 'msg-error', text: App.errText(App.online() ? 'unknown' : 'network') })]));
        return;
      }
      ['network', 'mqtt'].forEach(function (g) {
        var c = groupCard(g);
        if (c) v.appendChild(c);
      });
      mqttStatus(st);
    },
    update: function (v, st) { mqttStatus(st); }
  });

  // ─── Setup page (setup access point) ─────────────────────────────────────
  App.page('setup', {
    load: loadWifiStatus,
    // The AP name/address is part of the Wi-Fi status rows.
    render: function (v) {
      wifiCard(v, true, [
        el('p', { text: t('setup.mode') + ' ' + t('setup.intro') }),
        el('p', null, el('a', { href: '#status', 'class': 'btn btn-secondary', text: t('setup.skip') }))
      ]);
    }
  });

  // ─── System page ─────────────────────────────────────────────────────────
  var BACKUP_MAX = 8192;             // BACKUP_MAX_BYTES (CoreEngine/BackupCodec.h)
  var EXPORT_POLL_MS = 500;
  var EXPORT_LIMIT_MS = 10000;
  var REBOOT_POLL_MS = 2000;
  var REBOOT_LIMIT_MS = 120000;
  var REBOOT_ASSUME_MS = 30000;      // never saw it go down: reload anyway
  var REBOOT_SLOW_MS = 10000;        // after giving up: slow probe while the note is shown
  var RESET_WORD = 'RESET';
  var RESET_CAUSES = 10;             // ResetCause (CoreEngine/EventTypes.h)
  // sys.ver: /api/version; sys.boot: the newest reboot event (or null).
  var sys = { ver: null, boot: null, ota: false, modal: null };

  // 'boiler-room' -> 'BoilerRoom-Setup' (NetIdentity apSsid).
  function apName(project) {
    return String(project || '').split('-').map(function (w) {
      return w.charAt(0).toUpperCase() + w.slice(1);
    }).join('') + '-Setup';
  }

  function projectName() {
    var st = App.state();
    return (st && st.project) || (sys.ver && sys.ver.project) || (window.PROJECT && window.PROJECT.name) || 'controller';
  }

  // <project>-backup-YYYYMMDD.json (browser local date).
  function backupName(project, d) {
    d = d || new Date();
    var p2 = function (n) { return (n < 10 ? '0' : '') + n; };
    return project + '-backup-' + d.getFullYear() + p2(d.getMonth() + 1) + p2(d.getDate()) + '.json';
  }

  // The newest 'reboot' event's cause (log is newest first); -1 = not in log.
  function bootCause(events) {
    for (var i = 0; i < (events || []).length; i++) {
      var e = events[i];
      if (e && e.key === 'reboot') {
        var c = Math.round(Number(e.val));
        return c >= 0 && c < RESET_CAUSES ? c : 0;
      }
    }
    return -1;
  }

  // A modal with kids and Cancel/OK. onOk(ok) may return a Promise; the
  // modal closes when it resolves true. One modal at a time.
  function closeModal() {
    if (sys.modal && sys.modal.parentNode) sys.modal.parentNode.removeChild(sys.modal);
    sys.modal = null;
  }

  function modal(title, kids, okText, onOk, okClass) {
    closeModal();
    var ok = el('button', { type: 'button', 'class': 'btn ' + (okClass || 'btn-primary'), text: okText });
    var cancel = el('button', { type: 'button', 'class': 'btn btn-secondary', text: t('common.cancel'), onclick: closeModal });
    var box = el('div', 'modal', el('div', { 'class': 'modal-box', role: 'dialog', 'aria-modal': 'true' }, [
      el('h3', { text: title })].concat(kids, [el('div', 'actions', [cancel, ok])])));
    ok.addEventListener('click', async function () {
      if (ok.disabled) return;
      ok.disabled = true;
      var done = false;
      try { done = await onOk(ok); } finally { ok.disabled = false; }
      if (done) closeModal();
    });
    box.addEventListener('keydown', function (e) { if (e.key === 'Escape') closeModal(); });
    document.body.appendChild(box);
    sys.modal = box;
    return ok;
  }

  function row(label, value) {
    return el('p', null, [el('span', { 'class': 'muted', text: label + ': ' }), String(value)]);
  }

  function timeRows(st) {
    var tm = (st && st.time) || {};
    var src = tm.source === 'ntp' ? 'NTP' : tm.source === 'rtc' ? 'RTC' : t('common.na');
    return [
      tm.valid && tm.local ? row(t('sys.time'), tm.local) : el('p', { 'class': 'warn-banner', text: t('sys.time_not_set') }),
      row(t('sys.time_source'), src),
      row(t('sys.rtc'), t('sys.rtc_' + (tm.rtc === 'ok' || tm.rtc === 'invalid' ? tm.rtc : 'missing'))),
      row(t('sys.last_ntp'), tm.lastNtp || t('sys.never'))
    ];
  }

  function deviceRows(st) {
    var up = st && typeof st.uptimeS === 'number' ? t('status.uptime_fmt', App.fmtUptime(st.uptimeS)) : t('common.na');
    return [
      row(t('status.uptime'), up),
      row(t('sys.reset_reason'), sys.boot === null || sys.boot < 0 ? t('rc.none') : t('rc.' + sys.boot))
    ];
  }

  function versionRows(st) {
    var v = sys.ver || {};
    var sv = (st && st.ver) || {};
    var fw = v.fw || sv.fw || '';
    var web = v.web !== undefined ? v.web : (sv.web || '');
    var bad = !!(v.mismatch || sv.mismatch);
    var ota = v.ota || {};
    return [
      row(t('sys.fw'), fw || t('common.na')),
      row(t('sys.web'), web || t('common.na')),
      bad ? el('p', { 'class': 'warn-banner', text: t('sys.mismatch') }) : null,
      !web ? el('p', { 'class': 'warn-banner', text: t('sys.web_missing') }) : null,
      ota.bootOutcome === 'rolled_back' ? el('p', { 'class': 'warn-banner', text: t('sys.rolled_back') }) : null,
      ota.pendingVerify || (st && st.ota && st.ota.pendingVerify) ? el('p', { 'class': 'info-text', text: t('status.ota_pending') }) : null
    ];
  }

  function fill(id, nodes) {
    var box = $(id);
    if (!box) return;
    clear(box);
    nodes.forEach(function (n) { if (n) box.appendChild(n); });
  }

  function loadVersionInfo() {
    return App.api('GET', '/api/version', null, { noAuth: true }).then(function (r) {
      if (r.ok) sys.ver = r.data;
    });
  }

  function loadBootCause() {
    return App.api('GET', '/api/log').then(function (r) {
      sys.boot = r.ok ? bootCause(r.data.events) : null;
    });
  }

  // ─── Access (web login): saving it ends every session (epoch bump) ─────
  function fieldStatus(g, text, kind) { note(g.querySelector('.field-status'), text, kind); }

  function accessChanged(g) {
    var inp = g.querySelector('input');
    if (!inp) return false;
    return g.getAttribute('data-secret') === '1' ? inp.value !== '' : inp.value !== g.getAttribute('data-orig');
  }

  function maxLenOf(key) {
    var list = (App.schema() && App.schema().settings) || [];
    for (var i = 0; i < list.length; i++) if (list[i].k === key) return list[i].ml;
    return 0;
  }

  // One credential per save: posting login and password together can half-
  // apply (the first one ends this session), leaving a pair the user does
  // not know. The core poll is held so its 401 cannot hide the outcome; the
  // login view then states exactly which credential now applies.
  async function saveAccess(fields, msg) {
    var groups = Array.prototype.filter.call(fields.querySelectorAll('.form-group[data-key]'), accessChanged);
    note(msg, '');
    if (!groups.length) { note(msg, t('settings.no_changes'), ''); return; }
    if (groups.length > 1) { note(msg, t('sys.access_one'), 'err'); return; }
    var g = groups[0];
    var key = g.getAttribute('data-key');
    var v = g.querySelector('input').value;
    var ml = maxLenOf(key);
    if (!v.length) { fieldStatus(g, t('err.missing'), 'err'); return; }
    if (ml && utf8Len(v) > ml) { fieldStatus(g, App.errText('too_long'), 'err'); return; }
    fieldStatus(g, t('cmd.pending'), '');
    App.holdPolling();
    var r = await App.api('POST', '/api/config', { key: key, value: v }, { noAuth: true });
    var res;
    if (r.status === 202 && r.data.id !== undefined) res = await App.pollCmd(r.data.id, { noAuth: true });
    else if (r.status === 401) res = { error: 'not_saved' };
    else res = { error: r.data.error || (r.status === 0 ? 'network' : 'unknown'), http: r.status, data: r.data };
    var unknown = t(key === 'webPass' ? 'sys.access_unknown' : 'sys.access_unknown_user');
    if (res.status === 'ok') {
      App.requireLogin('system', t(key === 'webPass' ? 'sys.access_saved' : 'sys.access_saved_user'), 'ok');
      return;
    }
    // A 401 while polling the command may also be an unrelated session end.
    if (res.status === 'auth') { App.requireLogin('system', unknown); return; }
    if (res.error === 'not_saved') { App.requireLogin('system', t('sys.access_not_saved')); return; }
    // No definite result: the one credential may or may not be applied.
    if (res.status === 'timeout' || res.status === 'unknown' || res.error === 'network') {
      App.setLoginHint(unknown);
      App.resumePolling();
      fieldStatus(g, unknown, 'warn');
      return;
    }
    App.resumePolling();
    var rt = App.resultText(res);
    fieldStatus(g, rt.text, rt.kind);
  }

  function accessCard() {
    var fields = el('div', 'form-grid');
    if (!App.renderFields(fields, 'access')) return null;
    var msg = el('div');
    var save = el('button', {
      type: 'button', 'class': 'btn btn-primary', text: t('common.save'),
      onclick: async function () {
        if (save.disabled) return;
        save.disabled = true;
        try { await saveAccess(fields, msg); } finally { save.disabled = false; }
      }
    });
    return card(t('sys.access'), [el('p', { 'class': 'info-text', text: t('sys.access_hint') }), fields, el('p', null, save), msg]);
  }

  // ─── Web OTA: grant (password) -> /ota/start -> XHR upload -> reboot ───
  // started: /ota/start succeeded, so ElegantOTA may keep the update open
  // until its stall timeout: ask the user to wait before retrying.
  function otaFail(msg, r, started) {
    var text = r.status === 409 ? t('sys.ota_espota')
      : r.status === 0 ? t('err.network')
        : r.data && r.data.error ? App.errText(r.data.error)
          : t('sys.ota_failed') + (r.text ? ': ' + String(r.text).slice(0, 120) : '');
    note(msg, started ? text + '. ' + t('sys.ota_retry_later') : text, 'err');
  }

  // After a successful upload the controller reboots: wait until
  // /api/version stops and then answers again, then reload the SPA. The
  // poll is held so the reboot's 401 does not replace this message. On
  // giving up the hold ends (the core poll resumes and backs off; its 401
  // opens the login view) and a slow probe reloads once the controller
  // answers, while this note is still shown.
  async function waitReboot(msg) {
    var start = Date.now();
    App.holdPolling();
    var down = false;
    note(msg, t('sys.ota_done') + ' ' + t('sys.ota_wait'), 'ok');
    while (Date.now() - start < REBOOT_LIMIT_MS) {
      await new Promise(function (res) { setTimeout(res, REBOOT_POLL_MS); });
      var r = await App.api('GET', '/api/version', null, { noAuth: true });
      if (!r.ok) { down = true; continue; }
      if (down || Date.now() - start > REBOOT_ASSUME_MS) {
        note(msg, t('sys.ota_back'), 'ok');
        location.reload();
        return;
      }
    }
    App.resumePolling();
    note(msg, t('sys.ota_noreply'), 'warn', function () { location.reload(); });
    while (msg.isConnected) {
      await new Promise(function (res) { setTimeout(res, REBOOT_SLOW_MS); });
      if (!msg.isConnected) return;
      var v = await App.api('GET', '/api/version', null, { noAuth: true });
      if (v.ok && msg.isConnected) { location.reload(); return; }
    }
  }

  function otaIdle(parts) {
    sys.ota = false;
    parts.go.disabled = false;
  }

  async function runOta(fs, file, parts) {
    sys.ota = true;
    parts.go.disabled = true;
    note(parts.msg, t('sys.ota_progress', { p: 0 }), '');
    var r = await App.api('GET', '/ota/start?mode=' + (fs ? 'fs' : 'fr'));
    if (r.status !== 200) {
      otaIdle(parts);
      if (r.status !== 401) otaFail(parts.msg, r);
      return;
    }
    parts.bar.hidden = false;
    var fillEl = parts.bar.firstChild;
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/ota/upload');
    xhr.setRequestHeader('X-CSRF-Token', App.csrf());
    xhr.upload.onprogress = function (e) {
      if (!e.lengthComputable) return;
      var p = Math.min(100, Math.floor(e.loaded * 100 / e.total));
      fillEl.style.width = p + '%';
      note(parts.msg, t('sys.ota_progress', { p: p }), '');
    };
    xhr.onload = function () {
      if (xhr.status === 200) { fillEl.style.width = '100%'; waitReboot(parts.msg); return; }
      otaIdle(parts);
      var data = {};
      try { data = JSON.parse(xhr.responseText) || {}; } catch (e) { /* plain-text error */ }
      otaFail(parts.msg, { status: xhr.status, data: data, text: xhr.responseText }, true);
    };
    xhr.onerror = function () {
      otaIdle(parts);
      otaFail(parts.msg, { status: 0, data: {} }, true);
    };
    var fd = new FormData();
    fd.append('file', file, file.name);
    xhr.send(fd);
  }

  // Password re-entry (D5): POST /api/ota/grant, then the upload.
  function otaGrant(fs, file, parts) {
    var pw = el('input', { id: 'sy-ota-pw', type: 'password', autocomplete: 'current-password', maxlength: '64' });
    var box = el('div');
    var ok = modal(t('sys.ota'), [
      el('p', { 'class': 'warn-banner', text: t('sys.ota_relays') }),
      el('p', { text: (fs ? t('sys.ota_fs') : t('sys.ota_fw')) + ': ' + file.name }),
      el('div', 'form-group', [el('label', { 'for': 'sy-ota-pw', text: t('sys.ota_pass') }), el('div', 'pw-wrap', [pw,
        el('button', { type: 'button', 'class': 'pw-show', 'data-act': 'pw', 'data-for': 'sy-ota-pw', text: t('login.show') })])]),
      box
    ], t('sys.ota_start'), async function () {
      if (!pw.value) { note(box, t('login.empty'), 'err'); return false; }
      var r = await App.api('POST', '/api/ota/grant', { pass: pw.value }, { noAuth: true });
      pw.value = '';
      if (r.status === 200) { runOta(fs, file, parts); return true; }
      if (r.status === 401 && r.data.error === 'auth') { closeModal(); App.requireLogin('system'); return false; }
      note(box, r.status === 401 ? t('err.credentials')
        : r.status === 429 ? App.errText('throttled', { s: r.data.retryS })
          : App.errText(r.status === 0 ? 'network' : r.data.error), 'err');
      return false;
    });
    pw.addEventListener('keydown', function (e) { if (e.key === 'Enter') ok.click(); });
    pw.focus();
  }

  function otaCard(st) {
    var mode = el('select', { id: 'sy-ota-mode' }, [
      el('option', { value: 'fw', text: t('sys.ota_fw') }), el('option', { value: 'fs', text: t('sys.ota_fs') })]);
    var file = el('input', { id: 'sy-ota-file', type: 'file', accept: '.bin,application/octet-stream' });
    var parts = {
      msg: el('div'),
      bar: el('div', { 'class': 'progress', hidden: true }, el('div', { 'class': 'progress-fill', style: 'width:0%' })),
      go: el('button', { id: 'sy-ota-go', type: 'button', 'class': 'btn btn-primary', text: t('sys.ota_start') })
    };
    parts.go.disabled = sys.ota || !!(st && st.ota && st.ota.inProgress);
    parts.go.addEventListener('click', function () {
      var f = file.files && file.files[0];
      if (!f) { note(parts.msg, t('sys.choose_file'), 'err'); return; }
      otaGrant(mode.value === 'fs', f, parts);
    });
    return card(t('sys.ota'), [
      el('p', { 'class': 'warn-banner', text: t('sys.ota_relays') }),
      el('div', 'form-group', [el('label', { 'for': 'sy-ota-mode', text: t('sys.ota_mode') }), mode]),
      el('div', 'form-group', [el('label', { 'for': 'sy-ota-file', text: t('sys.file') }), file]),
      el('p', null, parts.go), parts.bar, parts.msg
    ]);
  }

  // ─── Backup (D9) ─────────────────────────────────────────────────────────
  function download(bytes, name) {
    var url = URL.createObjectURL(new Blob([bytes], { type: 'application/json' }));
    var a = el('a', { href: url, download: name, hidden: true });
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
  }

  function httpFail(msg, r, retry) {
    if (r.status === 401) return;   // App.api opened the login view
    var busy = r.status === 503 && r.data.error !== 'no_memory';
    note(msg, busy ? t('err.busy') : App.errText(r.status === 0 ? 'network' : r.data.error, r.data), 'err',
      busy || r.status === 0 ? retry : null);
  }

  // POST asks the loop task to build the file; GET answers 202 until ready.
  async function exportBackup(msg) {
    var retry = function () { exportBackup(msg); };
    note(msg, t('sys.backup_preparing'), '');
    var r = await App.api('POST', '/api/backup/export', {});
    if (r.status !== 202) { httpFail(msg, r, retry); return; }
    var start = Date.now();
    while (Date.now() - start < EXPORT_LIMIT_MS) {
      await new Promise(function (res) { setTimeout(res, EXPORT_POLL_MS); });
      r = await App.api('GET', '/api/backup/export', null, { raw: true });
      if (r.status === 202 || r.status === 0) continue;
      if (r.status === 200 && r.buf && r.buf.byteLength) {
        download(r.buf, backupName(projectName()));   // the exact bytes sent
        note(msg, t('sys.backup_saved'), 'ok');
        return;
      }
      if (r.status === 404 || r.status === 200) { note(msg, t('sys.backup_failed'), 'err', retry); return; }
      httpFail(msg, r, retry);
      return;
    }
    note(msg, t('cmd.timeout'), 'err', retry);
  }

  // Raw file bytes (<= 8192 B, checked here too), then the command result.
  async function importBackup(bytes, msg) {
    var retry = function () { importBackup(bytes, msg); };
    note(msg, t('sys.backup_restoring') + ' ' + t('sys.backup_relogin'), '');
    var r = await App.api('POST', '/api/backup/import', null, { json: bytes });
    if (r.status !== 202 || r.data.id === undefined) {
      if (r.data.error === 'wrong_type') note(msg, App.errText('wrong_type', { got: r.data.got || '?' }), 'err');
      else httpFail(msg, r, retry);
      return;
    }
    var res = await App.pollCmd(r.data.id);
    if (res.status === 'auth') return;   // the backup changed the login
    if (res.status === 'ok') { note(msg, t('sys.backup_restored') + '. ' + t('sys.backup_relogin'), 'ok'); return; }
    var rt = App.resultText(res);
    note(msg, rt.text, rt.kind, rt.retry ? retry : null);
  }

  // The file's bytes as an ArrayBuffer (no text decode, so a BOM or bad
  // UTF-8 reaches the server unchanged and its precheck decides).
  function readFile(f) {
    if (f.arrayBuffer) return f.arrayBuffer();
    return new Promise(function (res, rej) {
      var fr = new FileReader();
      fr.onload = function () { res(fr.result); };
      fr.onerror = rej;
      fr.readAsArrayBuffer(f);
    });
  }

  // '' when the file may be sent, else the error text.
  function backupFileError(size) {
    if (!size) return App.errText('empty');
    if (size > BACKUP_MAX) return App.errText('too_large') + ' (' + t('sys.max_size', { n: BACKUP_MAX }) + ')';
    return '';
  }

  function backupCard() {
    var msg = el('div');
    var file = el('input', { id: 'sy-bk-file', type: 'file', accept: '.json,application/json' });
    var exp = el('button', {
      type: 'button', 'class': 'btn btn-primary', text: t('sys.backup_export'),
      onclick: function () {
        modal(t('sys.backup_export'), [el('p', { 'class': 'warn-banner', text: t('sys.backup_plaintext') })],
          t('sys.backup_export'), function () { exportBackup(msg); return true; });
      }
    });
    var imp = el('button', {
      type: 'button', 'class': 'btn btn-secondary', text: t('sys.backup_import'),
      onclick: function () {
        var f = file.files && file.files[0];
        if (!f) { note(msg, t('sys.choose_file'), 'err'); return; }
        var bad = backupFileError(f.size);
        if (bad) { note(msg, bad, 'err'); return; }
        modal(t('sys.backup_import'), [
          el('p', { text: t('sys.backup_replace') }), el('p', { 'class': 'info-text', text: t('sys.backup_relogin') })
        ], t('sys.backup_import'), async function () {
          var bytes;
          try { bytes = await readFile(f); } catch (e) { note(msg, App.errText('corrupt'), 'err'); return true; }
          var e2 = backupFileError(bytes.byteLength);
          if (e2) note(msg, e2, 'err');
          else importBackup(bytes, msg);
          return true;
        });
      }
    });
    return card(t('sys.backup'), [
      el('p', { 'class': 'info-text', text: t('sys.backup_plaintext') }),
      el('p', null, exp),
      el('div', 'form-group', [el('label', { 'for': 'sy-bk-file', text: t('sys.backup_file') }), file]),
      el('p', null, imp), msg
    ]);
  }

  // ─── Factory reset: typed confirmation, then the setup AP text ──────────
  function resetCard() {
    var msg = el('div');
    var go = el('button', {
      type: 'button', 'class': 'btn btn-danger', text: t('sys.reset'),
      onclick: function () {
        var inp = el('input', { id: 'sy-rs-word', type: 'text', autocomplete: 'off', maxlength: '8' });
        var box = el('div');
        var ok = modal(t('sys.reset'), [
          el('p', { 'class': 'warn-banner', text: t('sys.reset_text') }),
          el('div', 'form-group', [el('label', { 'for': 'sy-rs-word', text: t('sys.reset_confirm') }), inp]), box
        ], t('sys.reset'), async function () {
          if (inp.value !== RESET_WORD) { note(box, App.errText('confirm'), 'err'); return false; }
          var r = await App.api('POST', '/api/factory-reset', { confirm: RESET_WORD });
          if (r.status === 202) {
            note(msg, t('sys.reset_done', { ap: apName(projectName()) }), 'warn');
            return true;
          }
          httpFail(box, r, null);
          return r.status === 401;
        }, 'btn-danger');
        var sync = function () { ok.disabled = inp.value !== RESET_WORD; };
        inp.addEventListener('input', sync);
        sync();
        inp.focus();
      }
    });
    return card(t('sys.reset'), [el('p', { text: t('sys.reset_text') }), el('p', null, go), msg]);
  }

  // ─── Page ────────────────────────────────────────────────────────────────
  function systemUpdate(st) {
    fill('sy-time', timeRows(st));
    fill('sy-dev', deviceRows(st));
    fill('sy-ver', versionRows(st));
    var go = $('sy-ota-go');
    if (go && !sys.ota) go.disabled = !!(st && st.ota && st.ota.inProgress);
  }

  App.page('system', {
    load: function () { return Promise.all([App.loadSettings(), loadVersionInfo(), loadBootCause()]); },
    render: function (v, st) {
      v.appendChild(el('div', 'grid-3', [
        card(t('sys.clock'), [el('div', { id: 'sy-time' })]),
        card(t('sys.device'), [el('div', { id: 'sy-dev' })]),
        card(t('sys.versions'), [el('div', { id: 'sy-ver' })])
      ]));
      if (App.schema() && App.config()) {
        [groupCard('time'), accessCard()].forEach(function (c) { if (c) v.appendChild(c); });
      } else {
        v.appendChild(card(t('sys.access'), [el('p', { 'class': 'msg-error', text: App.errText(App.online() ? 'unknown' : 'network') })]));
      }
      v.appendChild(otaCard(st));
      v.appendChild(backupCard());
      v.appendChild(resetCard());
      systemUpdate(st);
    },
    update: function (v, st) { systemUpdate(st); },
    stop: closeModal
  });

  return {
    _test: {
      utf8Len: utf8Len, wifiCheck: wifiCheck, assignNotice: assignNotice, sen: sen,
      apName: apName, backupName: backupName, bootCause: bootCause, backupFileError: backupFileError,
      saveAccess: saveAccess, otaFail: otaFail, readFile: readFile, waitReboot: waitReboot
    }
  };
})();
