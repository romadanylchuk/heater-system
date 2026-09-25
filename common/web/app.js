/* SPA core (stage 05): API client, auth, i18n, hash router, polling, the
 * header/banner, and the Status, Log and Settings pages. Plain ES2017, no
 * build step. Every piece of text reaches the DOM through textContent or
 * through the escaping UI.* helpers from components.js; server data is never
 * put into innerHTML unescaped. pages.js adds the other pages through
 * App.page(name, {load, render, update, stop}). */
'use strict';

var App = (function () {
  var ROUTES = ['status', 'settings', 'sensors', 'log', 'network', 'system', 'setup'];
  var NAV = ['status', 'settings', 'sensors', 'log', 'network', 'system'];
  // D17: these groups are shown on other pages, never as Settings tabs.
  var HIDDEN_GROUPS = ['network', 'mqtt', 'access', 'time', 'sensors'];
  // Wi-Fi keys only go through /api/wifi, never through the config set.
  var NEVER_FIELDS = ['wifiSsid', 'wifiPass'];
  var BOOT_RETRY_MS = 3000;
  var POLL_MS = 2000;
  var HOLD_MAX_MS = 180000;          // a hold never outlives this: polling resumes
  var LOGIN_HINT_MS = 600000;        // how long a pending login hint stays relevant
  var BACKOFF_MAX_MS = 16000;
  var CMD_POLL_MS = 400;
  var CMD_POLL_LIMIT_MS = 6000;

  var S = {
    lang: 'en', csrf: '', authed: false, online: true,
    state: null, ver: null, schema: null, config: null,
    route: null, returnTo: null, seq: 0, page: null, hold: false, holdTimer: null, loginHint: null,
    pages: {}, pollers: [], setTab: null
  };

  function noop() {}
  function $(id) { return document.getElementById(id); }

  // Small DOM builder: el('div', 'card', 'text') or el('div', {class, ...}, [children]).
  function el(tag, attrs, kids) {
    var n = document.createElement(tag);
    if (typeof attrs === 'string') attrs = { 'class': attrs };
    Object.keys(attrs || {}).forEach(function (k) {
      if (attrs[k] === null || attrs[k] === undefined || attrs[k] === false) return;
      if (k === 'text') n.textContent = attrs[k];
      else if (k === 'onclick') n.addEventListener('click', attrs[k]);
      else n.setAttribute(k, attrs[k] === true ? '' : attrs[k]);
    });
    if (kids !== undefined && kids !== null) {
      (Array.isArray(kids) ? kids : [kids]).forEach(function (c) {
        if (c === null || c === undefined || c === false) return;
        n.appendChild(typeof c === 'object' ? c : document.createTextNode(String(c)));
      });
    }
    return n;
  }

  function clear(n) { while (n && n.firstChild) n.removeChild(n.firstChild); return n; }

  // localStorage can throw (private mode, blocked storage): never fatal.
  function storeGet(k) {
    try { return window.localStorage.getItem(k); } catch (e) { return null; }
  }
  function storeSet(k, v) {
    try { window.localStorage.setItem(k, v); } catch (e) { /* ignore */ }
  }

  // ─── i18n ────────────────────────────────────────────────────────────────
  var I18N = (function () {
    var files = {};
    var cur = {};
    var en = {};

    function fetchLang(code) {
      if (files[code]) return Promise.resolve(files[code]);
      return fetch('lang/' + code + '.json', { credentials: 'same-origin' })
        .then(function (r) { return r.ok ? r.json() : {}; })
        .catch(function () { return {}; })
        .then(function (d) {
          d = d && typeof d === 'object' ? d : {};
          if (Object.keys(d).length) files[code] = d;
          return d;
        });
    }

    function merged(code, base) {
      var out = {};
      var proj = window.PROJECT && window.PROJECT.lang ? window.PROJECT.lang[code] : null;
      Object.keys(base || {}).forEach(function (k) { out[k] = base[k]; });
      Object.keys(proj || {}).forEach(function (k) { out[k] = proj[k]; });
      return out;
    }

    // English is always loaded (fallback); other languages on demand.
    function load(code) {
      return fetchLang('en').then(function (e) {
        en = merged('en', e);
        if (code === 'en') { cur = en; return; }
        return fetchLang(code).then(function (d) { cur = merged(code, d); });
      });
    }

    // Lookup order: current language, English, then the key itself. Returning
    // the key verbatim on a miss is a contract: UI.unitText (components.js)
    // detects unknown units by comparing the result with the key.
    function t(key, vars) {
      var s = Object.prototype.hasOwnProperty.call(cur, key) ? cur[key]
        : (Object.prototype.hasOwnProperty.call(en, key) ? en[key] : key);
      if (vars) {
        s = String(s).replace(/\{(\w+)\}/g, function (m, n) {
          return vars[n] === undefined || vars[n] === null ? m : String(vars[n]);
        });
      }
      return s;
    }

    // Test hook: set the dictionaries directly (no fetch).
    function use(curDict, enDict) { cur = curDict || {}; en = enDict || {}; }

    return { load: load, t: t, use: use };
  })();

  var t = I18N.t;

  function labelOf(desc) { return UI.labelOf(desc, S.lang); }

  // Server error key -> text. Unknown keys fall back to err.unknown.
  function errText(key, vars) {
    var k = 'err.' + (key || 'unknown');
    var s = t(k, vars);
    return s === k ? t('err.unknown') : s;
  }

  // ─── Connection indicator ────────────────────────────────────────────────
  function setOnline(on) {
    S.online = on;
    var c = $('conn');
    if (!c) return;
    c.textContent = t('conn.lost');
    c.hidden = on;
  }

  // ─── API client ──────────────────────────────────────────────────────────
  function parseJson(text) {
    try {
      var d = JSON.parse(text);
      return d && typeof d === 'object' ? d : {};
    } catch (e) {
      return {};
    }
  }

  function formBody(obj) {
    return Object.keys(obj).map(function (k) {
      var v = obj[k] === null || obj[k] === undefined ? '' : obj[k];
      return encodeURIComponent(k) + '=' + encodeURIComponent(v);
    }).join('&');
  }

  function needsCsrf(method, path) {
    return method !== 'GET' || path.indexOf('/ota/') === 0;
  }

  // Resolves {status, ok, data, text}; status 0 = network failure (never rejects).
  // body: object -> urlencoded form; opts.json -> raw JSON text/object or
  // bytes (ArrayBuffer/Blob, sent unchanged); opts.raw adds the body bytes
  // as res.buf (ArrayBuffer). A 401 hands over to the login view (unless
  // opts.noAuth or polling is on hold); a 403 "csrf" refreshes the token
  // once and retries.
  function api(method, path, body, opts) {
    opts = opts || {};
    var headers = {};
    var init = { method: method, credentials: 'same-origin', headers: headers };
    if (S.csrf && needsCsrf(method, path)) headers['X-CSRF-Token'] = S.csrf;
    if (opts.json !== undefined) {
      headers['Content-Type'] = 'application/json';
      init.body = isBytes(opts.json) || typeof opts.json === 'string' ? opts.json : JSON.stringify(opts.json);
    } else if (body && typeof body === 'object') {
      headers['Content-Type'] = 'application/x-www-form-urlencoded';
      init.body = formBody(body);
    }
    return fetch(path, init).then(function (r) {
      setOnline(true);
      if (opts.raw) {
        return r.arrayBuffer().then(function (buf) {
          var tx = new TextDecoder().decode(buf);
          return { status: r.status, ok: r.ok, data: parseJson(tx), text: tx, buf: buf };
        }, function () {
          return { status: r.status, ok: r.ok, data: {} };
        });
      }
      return r.text().then(function (tx) {
        return { status: r.status, ok: r.ok, data: parseJson(tx), text: tx };
      }, function () {
        return { status: r.status, ok: r.ok, data: {} };
      });
    }, function () {
      setOnline(false);
      return { status: 0, ok: false, data: { error: 'network' } };
    }).then(function (res) {
      if (res.status === 401 && !opts.noAuth) {
        if (!S.hold) requireLogin();
      } else if (res.status === 403 && res.data.error === 'csrf' && !opts.retried) {
        return refreshSession().then(function (okS) {
          if (!okS) return res;
          var o = Object.assign({}, opts, { retried: true });
          return api(method, path, body, o);
        });
      }
      return res;
    });
  }

  function refreshSession() {
    return api('GET', '/api/session', null, { noAuth: true }).then(function (r) {
      if (r.status === 200 && r.data.csrf) { S.csrf = r.data.csrf; return true; }
      if (r.status === 401) requireLogin();
      return false;
    });
  }

  function delay(ms) {
    return new Promise(function (res) { setTimeout(res, ms); });
  }

  // D7: poll GET /api/cmd?id every 400 ms for up to 6 s.
  // Resolves {status, value?}; status 'timeout' when no result arrived.
  // opts go to api() (e.g. {noAuth: true}: a 401 resolves {status:'auth'}
  // without opening the login view).
  async function pollCmd(id, opts) {
    var start = Date.now();
    while (Date.now() - start < CMD_POLL_LIMIT_MS) {
      await delay(CMD_POLL_MS);
      var r = await api('GET', '/api/cmd?id=' + encodeURIComponent(id), null, opts);
      if (r.status === 401) return { status: 'auth' };
      if (r.ok && r.data.done) {
        var out = { status: r.data.status || 'ok' };
        if (r.data.value !== undefined) out.value = r.data.value;
        return out;
      }
      // Unknown/evicted id: the server answers 404 {"error":"not_found"}.
      if (r.status === 404 || r.data.error === 'not_found' || (r.ok && r.data.error === 'unknown')) {
        return { status: 'unknown' };
      }
      // Any other non-2xx reply is final; only network errors (0) keep polling.
      if (!r.ok && r.status !== 0) {
        return { error: r.data.error || 'unknown', http: r.status, data: r.data };
      }
    }
    return { status: 'timeout' };
  }

  // POST a write command. 202 {id} is followed by pollCmd. Resolves
  // {status, value?} on a command result, or {error, http, data} otherwise.
  async function command(path, body, opts) {
    var r = await api('POST', path, body, opts);
    if (r.status === 202 && r.data.id !== undefined) return pollCmd(r.data.id);
    if (r.ok) return { status: 'ok', data: r.data };
    return { error: r.data.error || (r.status === 0 ? 'network' : 'unknown'), http: r.status, data: r.data };
  }

  // Text for a command() result: {text, kind: ok|warn|err}.
  function resultText(res) {
    if (res.error) {
      if (res.http === 503 || res.error === 'busy') return { text: t('err.busy'), kind: 'err', retry: true };
      if (res.error === 'throttled') return { text: errText('throttled', { s: res.data.retryS }), kind: 'err' };
      return { text: errText(res.error, res.data), kind: 'err', retry: res.http === 0 };
    }
    var st = res.status;
    if (st === 'ok') return { text: t('cmd.ok'), kind: 'ok' };
    if (st === 'clamped') return { text: t('cmd.clamped', { v: res.value }), kind: 'warn' };
    if (st === 'unchanged') return { text: t('cmd.unchanged'), kind: 'ok' };
    if (st === 'queue_full') return { text: t('cmd.queue_full'), kind: 'err', retry: true };
    if (st === 'timeout') return { text: t('cmd.timeout'), kind: 'err', retry: true };
    return { text: tOr('cmd.' + st, errText(st)), kind: 'err' };
  }

  // ─── Polling ─────────────────────────────────────────────────────────────
  // A poller runs fn every ms while its owner is active and the tab is
  // visible. While offline the interval doubles up to BACKOFF_MAX_MS.
  function hidden() { return document.visibilityState === 'hidden'; }

  function schedule(p, ms) {
    clearTimeout(p.timer);
    p.timer = setTimeout(function () { tick(p); }, ms);
  }

  function tick(p) {
    p.timer = null;
    if (!p.live || hidden() || p.busy) return;
    p.busy = true;
    Promise.resolve().then(p.fn).catch(noop).then(function () {
      p.busy = false;
      if (!p.live) return;
      p.delay = S.online ? p.ms : Math.min(Math.max(p.delay, p.ms) * 2, BACKOFF_MAX_MS);
      if (!hidden()) schedule(p, p.delay);
    });
  }

  // owner: 'core' or the page sequence number (stopped on navigation).
  function addPoller(fn, ms, owner, now) {
    var p = { fn: fn, ms: ms || POLL_MS, delay: ms || POLL_MS, owner: owner, live: true, timer: null, busy: false };
    S.pollers.push(p);
    if (now) tick(p); else schedule(p, p.ms);
    return function () { stopPoller(p); };
  }

  function stopPoller(p) {
    p.live = false;
    clearTimeout(p.timer);
    S.pollers = S.pollers.filter(function (q) { return q !== p; });
  }

  function stopPollers(owner) {
    S.pollers.slice().forEach(function (p) {
      if (owner === undefined || p.owner === owner) stopPoller(p);
    });
  }

  function isBytes(v) {
    return (typeof ArrayBuffer !== 'undefined' && v instanceof ArrayBuffer) ||
      (typeof Blob !== 'undefined' && v instanceof Blob);
  }

  // Hold: stop the core state poll and keep any 401 from opening the login
  // view, so a page can finish a credential save or an OTA reboot wait and
  // show its own message. requireLogin(), resumePolling(), logout(), a
  // route change and HOLD_MAX_MS all end the hold.
  function holdPolling() {
    endHold();
    S.hold = true;
    stopPollers('core');
    S.holdTimer = setTimeout(function () { S.holdTimer = null; if (S.hold) resumePolling(); }, HOLD_MAX_MS);
  }

  function endHold() {
    S.hold = false;
    if (S.holdTimer) { clearTimeout(S.holdTimer); S.holdTimer = null; }
  }

  function resumePolling() {
    endHold();
    if (!S.authed) return;
    stopPollers('core');
    addPoller(refreshState, POLL_MS, 'core');
  }

  // Public: App.poll(fn, ms) belongs to the current page.
  function poll(fn, ms, now) { return addPoller(fn, ms, S.seq, now); }

  function onVisibility() {
    if (hidden()) {
      S.pollers.forEach(function (p) { clearTimeout(p.timer); p.timer = null; });
    } else {
      S.pollers.forEach(function (p) { if (!p.busy && !p.timer) tick(p); });
    }
  }

  // ─── Navigation bar and header ───────────────────────────────────────────
  function langSwitch() {
    return el('span', 'nav-lang', ['en', 'uk'].map(function (code) {
      return el('button', {
        type: 'button', 'class': S.lang === code ? 'active' : '', title: t('nav.lang'),
        text: code.toUpperCase(), onclick: function () { setLang(code); }
      });
    }));
  }

  function renderNav() {
    var nav = clear($('nav'));
    if (!nav) return;
    nav.appendChild(el('span', { 'class': 'brand', text: t('app.title') }));
    if (S.authed) {
      NAV.forEach(function (r) {
        nav.appendChild(el('a', { href: '#' + r, 'class': S.route === r ? 'active' : '', text: t('nav.' + r) }));
      });
    }
    nav.appendChild(el('span', 'spacer'));
    nav.appendChild(langSwitch());
    if (S.authed) {
      nav.appendChild(el('button', { type: 'button', 'class': 'logout-btn', text: t('nav.logout'), onclick: logout }));
    }
  }

  function bits(v) {
    var out = [];
    for (var i = 0; i < 32; i++) if ((v >>> i) & 1) out.push(i);
    return out;
  }

  // Alarm texts: bits 24+ are "sensor N missing" (N = logical sensor index).
  function alarmItems(st) {
    var sensors = st.sensors || [];
    return bits(st.alarms >>> 0).map(function (b) {
      if (b >= 24) {
        var s = sensors[b - 24];
        return t('status.sensor_missing', { name: s && s.name ? s.name : String(b - 24 + 1) });
      }
      return t('status.alarm_bit', { n: b });
    });
  }

  var DIAG = ['nvs', 'rtcMissing', 'rtcInvalid', 'tzInvalid', 'wdt', 'queueFull'];

  function headerHtml(st) {
    var net = st.net || {};
    var tm = st.time || {};
    var diag = st.diag || {};
    var ota = st.ota || {};
    var reset = st.reset || {};
    var chips = [];
    var name = st.project || (window.PROJECT && window.PROJECT.name) || '';
    chips.push(UI.chip(name, ''));
    chips.push(UI.chip(tm.valid && tm.local ? tm.local : t('status.time_not_set'), tm.valid ? '' : 'amber'));
    chips.push(UI.chip(t('status.wifi') + ': ' + t(net.wifi ? 'status.connected' : 'status.disconnected'),
      net.wifi ? 'ok' : 'red'));
    if (net.mqttEnabled) {
      chips.push(UI.chip(t('status.mqtt') + ': ' + t(net.mqtt ? 'status.connected' : 'status.disconnected'),
        net.mqtt ? 'ok' : 'amber'));
    }
    if (net.ap) chips.push(UI.chip(t('net.ap_mode'), 'amber'));
    bits(st.warnings >>> 0).forEach(function (b) { chips.push(UI.chip(t('status.warning', { n: b }), 'amber')); });
    DIAG.forEach(function (k) { if (diag[k]) chips.push(UI.chip(t('status.diag.' + k), 'amber')); });
    if (st.inhibited) chips.push(UI.chip(t('status.inhibited'), 'amber'));
    if (ota.inProgress) chips.push(UI.chip(t('status.ota_running'), 'amber'));
    if (ota.pendingVerify) chips.push(UI.chip(t('status.ota_pending'), 'amber'));
    if (reset.phase === 'countdown') chips.push(UI.chip(t('status.reset_countdown', { s: reset.left }), 'red'));
    if (st.ver && st.ver.mismatch) chips.push(UI.chip(t('status.version_mismatch'), 'amber'));
    return UI.alarmBanner(alarmItems(st)) + '<div class="status-bar">' + chips.join('') + '</div>';
  }

  // The banner shows the latest state on every page (HTML from UI.* only).
  function renderHeader() {
    var b = $('banner');
    if (!b) return;
    b.innerHTML = S.authed && S.state ? headerHtml(S.state) : '';
  }

  // ─── State polling (core, every page) ────────────────────────────────────
  function refreshState() {
    return api('GET', '/api/state').then(function (r) {
      if (!r.ok || !S.authed) return false;
      S.state = r.data;
      renderHeader();
      var p = S.page;
      if (p && p.def.update && p.ready) {
        try { p.def.update(p.el, S.state); } catch (e) { /* a page bug must not stop polling */ }
      }
      return true;
    });
  }

  function loadVersion() {
    return api('GET', '/api/version', null, { noAuth: true }).then(function (r) {
      if (r.ok) { S.ver = r.data; showVersionNote(); }
    });
  }

  // The login card's version-mismatch warning (D10: /api/version is public).
  function showVersionNote() {
    var n = $('lver');
    if (!n) return;
    n.textContent = t('sys.mismatch');
    n.hidden = !(S.ver && S.ver.mismatch);
  }

  // ─── Auth ────────────────────────────────────────────────────────────────
  // msg/kind: optional text for the login view (kind 'ok' = success style);
  // without it an ended session shows login.expired.
  // A pending hint (setLoginHint) replaces login.expired once.
  function requireLogin(route, msg, kind) {
    var was = S.authed;
    var hint = takeLoginHint();
    endHold();
    if (route || S.route) S.returnTo = route || S.route;
    S.authed = false;
    S.csrf = '';
    clearCache();
    leavePage();
    stopPollers();
    if (msg) showLogin(msg, kind);
    else if (hint && was) showLogin(hint);
    else if (was || !$('login-form')) showLogin(was ? t('login.expired') : '');
  }

  // Text for the next ended session (e.g. an unconfirmed credential save).
  function setLoginHint(text) {
    S.loginHint = text ? { text: text, until: Date.now() + LOGIN_HINT_MS } : null;
  }

  function takeLoginHint() {
    var h = S.loginHint;
    S.loginHint = null;
    return h && Date.now() < h.until ? h.text : '';
  }

  function showLogin(msg, kind) {
    S.authed = false;
    S.route = null;
    renderNav();
    renderHeader();
    var v = clear($('view'));
    var user = el('input', { id: 'lu', type: 'text', autocomplete: 'username', maxlength: '32' });
    var pass = el('input', { id: 'lp', type: 'password', autocomplete: 'current-password', maxlength: '64' });
    var show = el('button', { type: 'button', 'class': 'pw-show', 'data-act': 'pw', 'data-for': 'lp', text: t('login.show') });
    var note = el('div', { id: 'lmsg' });
    if (msg) note.appendChild(el('div', { 'class': kind === 'ok' ? 'msg-ok' : 'msg-error', text: msg }));
    var form = el('form', { id: 'login-form' }, [
      el('div', 'form-group', [el('label', { 'for': 'lu', text: t('login.user') }), user]),
      el('div', 'form-group', [el('label', { 'for': 'lp', text: t('login.pass') }), el('div', 'pw-wrap', [pass, show])]),
      el('button', { type: 'submit', 'class': 'btn btn-primary', text: t('login.submit') })
    ]);
    form.addEventListener('submit', function (e) { e.preventDefault(); doLogin(user.value, pass.value, note); });
    v.appendChild(el('div', 'auth-page', el('div', 'auth-card', [
      el('h2', { text: t('login.title') }),
      el('p', { 'class': 'subtitle', text: t('login.subtitle') }),
      el('div', { id: 'lver', 'class': 'warn-banner', hidden: true }),
      note, form
    ])));
    showVersionNote();
    user.focus();
  }

  function loginError(note, text) {
    clear(note).appendChild(el('div', { 'class': 'msg-error', text: text }));
  }

  async function doLogin(user, pass, note) {
    if (!user || !pass) { loginError(note, t('login.empty')); return; }
    var r = await api('POST', '/api/login', { user: user, pass: pass }, { noAuth: true });
    if (r.status === 200 && r.data.csrf) {
      S.csrf = r.data.csrf;
      await afterAuth();
    } else if (r.status === 429) {
      loginError(note, errText('throttled', { s: r.data.retryS }));
    } else if (r.status === 401) {
      loginError(note, t('err.credentials'));
    } else {
      loginError(note, errText(r.data.error));
    }
  }

  // Cached per-session data; dropped on logout and on a lost session.
  function clearCache() {
    S.config = null;
    S.schema = null;
    S.setTab = null;
  }

  // The client state is cleared even when the POST fails; on a network
  // failure the login card says the server session may still be open.
  async function logout() {
    var r = await api('POST', '/api/logout', {}, { noAuth: true });
    S.csrf = '';
    S.returnTo = null;
    S.state = null;
    S.authed = false;
    S.loginHint = null;
    endHold();
    clearCache();
    leavePage();
    stopPollers();
    showLogin(r.status === 0 ? t('err.network') : '');
  }

  // After a restored session or a login: fetch the state, start the core
  // poll and go to the saved route. With the setup AP active and no
  // explicit route, go to #setup.
  async function afterAuth() {
    S.authed = true;
    S.loginHint = null;
    endHold();
    S.schema = null;
    await refreshState();
    if (!S.authed) return;
    stopPollers('core');
    addPoller(refreshState, POLL_MS, 'core');
    var target = S.returnTo || parseRoute(location.hash);
    S.returnTo = null;
    if (!target && S.state && S.state.net && S.state.net.ap) target = 'setup';
    go(target || 'status');
  }

  // ─── Router and page lifecycle ───────────────────────────────────────────
  // '#status', '#/log', '#settings?x' -> route name; unknown -> null.
  function parseRoute(hash) {
    var h = String(hash || '').replace(/^#\/?/, '').split(/[?/]/)[0].toLowerCase();
    return ROUTES.indexOf(h) >= 0 ? h : null;
  }

  function go(name) {
    if (location.hash !== '#' + name) location.hash = '#' + name;   // -> hashchange -> enter
    else enter(name);
  }

  function onHash() {
    var r = parseRoute(location.hash);
    if (!S.authed) { if (r) S.returnTo = r; return; }
    enter(r || 'status');
  }

  // A page's hold ends with the page: resume the core poll (no-op when
  // signed out).
  function leavePage() {
    if (S.hold) resumePolling();
    var p = S.page;
    if (!p) return;
    S.page = null;
    stopPollers(p.seq);
    if (p.def.stop) { try { p.def.stop(); } catch (e) { /* ignore */ } }
  }

  function renderPage(p) {
    clear(p.el);
    try {
      p.def.render(p.el, S.state);
      p.ready = true;
    } catch (e) {
      p.el.appendChild(el('div', { 'class': 'msg-error', text: t('err.unknown') }));
    }
  }

  async function enter(name) {
    leavePage();
    var seq = ++S.seq;
    S.route = name;
    renderNav();
    var v = clear($('view'));
    var def = S.pages[name];
    if (!def) {
      v.appendChild(el('div', 'card', el('p', { 'class': 'muted', text: t('common.not_available') })));
      return;
    }
    var p = { name: name, def: def, el: v, seq: seq, ready: false };
    S.page = p;
    v.appendChild(el('div', { 'class': 'spinner', text: t('common.loading') }));
    try {
      if (def.load) await def.load(v, S.state);
    } catch (e) { /* render shows what is available */ }
    if (S.page === p) renderPage(p);
  }

  // Pages (here and in pages.js) register {load?, render, update?, stop?}.
  // load(el, state) may return a Promise; render(el, state) draws the page
  // (called again on a language switch); update(el, state) runs on every
  // state poll; App.poll() timers stop automatically on navigation.
  function page(name, def) { S.pages[name] = def; }

  // ─── Language switch ─────────────────────────────────────────────────────
  async function setLang(code) {
    S.lang = code === 'uk' ? 'uk' : 'en';
    storeSet('lang', S.lang);
    await I18N.load(S.lang);
    document.documentElement.lang = S.lang;
    document.title = t('app.title');
    renderNav();
    renderHeader();
    setOnline(S.online);
    if (!S.authed) {
      var u = $('lu') ? $('lu').value : '';
      showLogin('');
      if (u) $('lu').value = u;
    } else if (S.page && S.page.ready) {
      var edits = pendingEdits(S.page.el);
      renderPage(S.page);
      restoreEdits(S.page.el, edits);
    }
  }

  // Unsaved field edits (key -> value) inside root, so a re-render (language
  // switch) does not drop them.
  function pendingEdits(root) {
    var out = {};
    Array.prototype.forEach.call(root.querySelectorAll('.form-group[data-key]'), function (g) {
      if (isChanged(g)) out[g.getAttribute('data-key')] = fieldValue(fieldInput(g));
    });
    return out;
  }

  function restoreEdits(root, edits) {
    Array.prototype.forEach.call(root.querySelectorAll('.form-group[data-key]'), function (g) {
      var k = g.getAttribute('data-key');
      var inp = fieldInput(g);
      if (!inp || !Object.prototype.hasOwnProperty.call(edits, k)) return;
      if (inp.type === 'checkbox') inp.checked = edits[k] === '1';
      else inp.value = edits[k];
    });
  }

  // Show/hide toggle for every password field (login and UI.field secrets).
  function onDocClick(e) {
    var b = e.target && e.target.closest ? e.target.closest('[data-act="pw"]') : null;
    if (!b) return;
    var inp = $(b.getAttribute('data-for'));
    if (!inp) return;
    var show = inp.type === 'password';
    inp.type = show ? 'text' : 'password';
    b.textContent = t(show ? 'login.hide' : 'login.show');
  }

  // ─── Boot ────────────────────────────────────────────────────────────────
  // Offline at boot: show conn.lost in #view and retry with the same
  // doubling back-off as the pollers (BOOT_RETRY_MS .. BACKOFF_MAX_MS).
  async function startSession(wait) {
    var r = await api('GET', '/api/session', null, { noAuth: true });
    if (r.status === 200 && r.data.csrf) {
      S.csrf = r.data.csrf;
      await afterAuth();
    } else if (r.status === 0) {
      var ms = Math.min(wait ? wait * 2 : BOOT_RETRY_MS, BACKOFF_MAX_MS);
      var v = $('view');
      if (v) clear(v).appendChild(el('div', 'card', el('p', { 'class': 'msg-error', text: t('conn.lost') })));
      setTimeout(function () { startSession(ms); }, ms);
    } else {
      showLogin('');
    }
  }

  async function boot() {
    S.lang = storeGet('lang') === 'uk' ? 'uk' : 'en';
    await I18N.load(S.lang);
    document.documentElement.lang = S.lang;
    document.title = t('app.title');
    window.addEventListener('hashchange', onHash);
    document.addEventListener('visibilitychange', onVisibility);
    document.addEventListener('click', onDocClick);
    renderNav();
    loadVersion();
    startSession();
  }

  // ─── Formatting ──────────────────────────────────────────────────────────
  function pad2(n) { return (n < 10 ? '0' : '') + n; }

  // Seconds -> {d, hms: 'hh:mm:ss'}.
  function fmtUptime(sec) {
    var s = Math.max(0, Math.floor(Number(sec) || 0));
    var d = Math.floor(s / 86400);
    s %= 86400;
    return { d: d, hms: pad2(Math.floor(s / 3600)) + ':' + pad2(Math.floor(s / 60) % 60) + ':' + pad2(s % 60) };
  }

  // t() with a fallback when the key has no translation.
  function tOr(key, fallback, vars) {
    var s = t(key, vars);
    return s === key ? fallback : s;
  }

  function card(title, kids) {
    return el('div', 'card', [title ? el('h3', { text: title }) : null].concat(kids || []));
  }

  // ─── Status page ─────────────────────────────────────────────────────────
  function statusCore(st) {
    if (!st) return [el('div', { 'class': 'spinner', text: t('status.no_data') })];
    var net = st.net || {};
    var up = card(t('status.uptime'), [el('p', { text: t('status.uptime_fmt', fmtUptime(st.uptimeS)) })]);
    var netRows = [
      el('p', { text: t('status.wifi') + ': ' + t(net.wifi ? 'status.connected' : 'status.disconnected') +
        (net.ssid ? ' (' + net.ssid + ')' : '') }),
      net.ip ? el('p', { text: t('status.ip') + ': ' + net.ip }) : null,
      net.wifi && typeof net.rssi === 'number' ? el('p', { text: t('status.rssi') + ': ' + t('status.dbm', { v: net.rssi }) }) : null,
      net.ap ? el('p', { text: t('net.ap_mode') + (net.apSsid ? ': ' + net.apSsid : '') + (net.apIp ? ' ' + net.apIp : '') }) : null,
      net.mqttEnabled ? el('p', { text: t('status.mqtt') + ': ' + t(net.mqtt ? 'status.connected' : 'status.disconnected') }) : null
    ];
    var netCard = card(t('status.network'), netRows);
    var relays = (st.relays || []).map(function (r) {
      var extra = (r.safety ? ' · ' + t('status.relay_safety') : '') +
        (r.lockS > 0 ? ' · ' + t('status.relay_lock', { s: r.lockS }) : '');
      return el('div', 'status-pill', [el('div', 'status-dot' + (r.on ? ' ok' : '')),
        r.name + ': ' + t(r.on ? 'common.on' : 'common.off') + extra]);
    });
    var relCard = card(t('status.relays'), [el('div', 'status-bar', relays)]);
    var sensors = (st.sensors || []).map(function (s) {
      var bad = s.missing ? t('sensors.missing') : (s.state === 'fault' ? t('status.sensor_fault') :
        (s.state === 'unassigned' ? t('sensors.unassigned') : ''));
      return el('div', 'temp-card', [
        el('div', { 'class': 't-label', text: s.name }),
        el('div', { 'class': 't-value' + (typeof s.t === 'number' ? '' : ' na'), text: UI.fmtTemp(s.t) }),
        bad ? el('div', { 'class': 't-fault', text: bad }) : null
      ]);
    });
    var senCard = card(t('status.sensors'), [el('div', 'grid-4', sensors)]);
    return [el('div', 'grid-3', [up, netCard, relCard]), senCard];
  }

  function statusSlots(v, st) {
    var proj = window.PROJECT;
    if (!proj || typeof proj.render !== 'function') return;
    v.querySelectorAll('.slot[data-slot]').forEach(function (slot) {
      try {
        proj.render(slot.getAttribute('data-slot'), slot, st);
      } catch (e) { /* a widget bug must not break the page */ }
    });
  }

  page('status', {
    load: function () { return S.state ? null : refreshState(); },
    render: function (v, st) {
      v.appendChild(el('div', { id: 'st-core' }, statusCore(st)));
      var slots = (window.PROJECT && window.PROJECT.slots) || [];
      slots.forEach(function (s) {
        v.appendChild(card(tOr(s.titleKey, s.id), [el('div', { 'class': 'slot', 'data-slot': s.id })]));
      });
      statusSlots(v, st);
    },
    update: function (v, st) {
      var core = v.querySelector('#st-core');
      if (core) { clear(core); statusCore(st).forEach(function (n) { core.appendChild(n); }); }
      statusSlots(v, st);
    }
  });

  // ─── Log page ────────────────────────────────────────────────────────────
  var logData = null;

  function logRow(e) {
    var time = e.rt && e.lt ? e.lt : t('log.after_boot', fmtUptime(e.ts));
    var ev = e.key ? tOr('ev.' + e.key, t('ev.other', { type: e.type })) : t('ev.other', { type: e.type });
    var val = (e.val !== undefined ? String(e.val) : '') + (e.aux ? ' / ' + e.aux : '');
    return el('tr', null, [
      el('td', { text: time }), el('td', { text: ev }), el('td', { text: e.src === undefined ? '' : String(e.src) }),
      el('td', { text: val }), el('td', { text: tOr('rsn.' + e.rsn, String(e.rsn)) })
    ]);
  }

  function loadLog() {
    return api('GET', '/api/log').then(function (r) { logData = r.ok ? r.data : null; });
  }

  page('log', {
    load: loadLog,
    render: function (v) {
      var refresh = el('button', {
        type: 'button', 'class': 'btn btn-secondary', text: t('common.refresh'),
        onclick: function () {
          var p = S.page;
          refresh.disabled = true;
          loadLog().then(function () { if (S.page === p) renderPage(p); });
        }
      });
      var events = logData && Array.isArray(logData.events) ? logData.events : null;
      var body;
      if (!events) {
        body = el('p', { 'class': 'msg-error', text: errText(S.online ? 'unknown' : 'network') });
      } else if (!events.length) {
        body = el('p', { 'class': 'muted', text: t('log.empty') });
      } else {
        var head = el('tr', null, ['log.time', 'log.event', 'log.source', 'log.value', 'log.reason'].map(function (k) {
          return el('th', { text: t(k) });
        }));
        body = el('div', 'log-wrap', el('table', 'log-table', [el('thead', null, head),
          el('tbody', null, events.map(logRow))]));
      }
      v.appendChild(card(t('log.title'), [el('p', null, refresh), body]));
    }
  });

  // ─── Settings (generated from /api/schema) ───────────────────────────────
  // The schema is cached per page load; the values are fetched every time.
  // Resolves true when both are available.
  async function loadSettings() {
    var rs = await Promise.all([S.schema ? null : api('GET', '/api/schema'), api('GET', '/api/config')]);
    if (rs[0] && rs[0].ok && Array.isArray(rs[0].data.settings)) S.schema = rs[0].data;
    if (rs[1].ok) S.config = { v: rs[1].data.v || {}, s: rs[1].data.s || {} };
    return !!(S.schema && S.config);
  }

  // Groups in schema order (first appearance).
  function settingGroups() {
    var out = [];
    ((S.schema && S.schema.settings) || []).forEach(function (d) {
      if (out.indexOf(d.g) < 0) out.push(d.g);
    });
    return out;
  }

  function groupLabel(g) { return tOr('group.' + g, g); }

  // filter: a group name, an array of groups, or fn(desc). Returns the
  // number of fields rendered. UI.field escapes every value.
  function renderFields(container, filter) {
    var f = typeof filter === 'function' ? filter
      : Array.isArray(filter) ? function (d) { return filter.indexOf(d.g) >= 0; }
        : filter ? function (d) { return d.g === filter; } : function () { return true; };
    var cfg = S.config || { v: {}, s: {} };
    var items = ((S.schema && S.schema.settings) || []).filter(function (d) {
      return NEVER_FIELDS.indexOf(d.k) < 0 && f(d);
    });
    container.innerHTML = items.map(function (d) {
      return UI.field(d, cfg.v[d.k], !!cfg.s[d.k], t, S.lang);
    }).join('');
    return items.length;
  }

  function setStatus(g, text, kind, retry) {
    var st = g.querySelector('.field-status');
    if (!st) return;
    st.className = 'field-status' + (kind ? ' ' + kind : '');
    clear(st).appendChild(document.createTextNode(text));
    if (retry) {
      st.appendChild(document.createTextNode(' '));
      st.appendChild(el('button', { type: 'button', 'class': 'btn btn-secondary', text: t('common.retry'), onclick: retry }));
    }
  }

  function fieldInput(g) { return g.querySelector('input, select, textarea'); }

  function fieldValue(inp) { return inp.type === 'checkbox' ? (inp.checked ? '1' : '0') : inp.value; }

  function isChanged(g) {
    var inp = fieldInput(g);
    if (!inp) return false;
    var val = fieldValue(inp);
    if (g.getAttribute('data-secret') === '1') return val !== '';   // blank secret = unchanged (D16)
    return val !== g.getAttribute('data-orig');
  }

  function typedValue(type, val) {
    if (type === 'bool') return val === '1';
    if (type === 'int' || type === 'float') return Number(val);
    return val;
  }

  // Length in UTF-8 bytes (the server's text limit counts bytes, not chars).
  function utf8Len(s) {
    s = String(s);
    if (typeof TextEncoder !== 'undefined') return new TextEncoder().encode(s).length;
    var n = 0;
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c < 0x80) n += 1;
      else if (c < 0x800) n += 2;
      else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.length &&
               (s.charCodeAt(i + 1) & 0xFC00) === 0xDC00) { n += 4; i++; }
      else n += 3;
    }
    return n;
  }

  function schemaDesc(key) {
    var list = (S.schema && S.schema.settings) || [];
    for (var i = 0; i < list.length; i++) if (list[i].k === key) return list[i];
    return null;
  }

  // Client-side checks the browser cannot do: returns an err.* key or ''.
  function precheck(type, val, desc) {
    if (type === 'int' && !/^-?\d+$/.test(val)) return 'bad_number';
    if (type === 'text' && desc && typeof desc.ml === 'number' && utf8Len(val) > desc.ml) return 'too_long';
    return '';
  }

  // Saves one field (POST /api/config key/value, then the command result).
  // Resolves true when the controller accepted it. A field that is already
  // being saved is skipped (resolves false), so Retry and "Save all" can
  // never send the same key twice at once.
  async function saveField(g) {
    var inp = fieldInput(g);
    var key = g.getAttribute('data-key');
    if (!inp || !key || NEVER_FIELDS.indexOf(key) >= 0) return false;
    if (g.getAttribute('data-busy') === '1') return false;
    if (inp.checkValidity && !inp.checkValidity()) {
      setStatus(g, inp.validationMessage || errText('invalid'), 'err');
      return false;
    }
    var val = fieldValue(inp);
    var type = g.getAttribute('data-type');
    var bad = type === 'bool' ? '' : precheck(type, val, schemaDesc(key));
    if (bad && !(g.getAttribute('data-secret') === '1' && val === '')) {
      setStatus(g, errText(bad), 'err');
      return false;
    }
    g.setAttribute('data-busy', '1');
    setStatus(g, t('cmd.pending'), '');
    var res;
    try {
      res = await command('/api/config', { key: key, value: val });
    } finally {
      g.removeAttribute('data-busy');
    }
    var rt = resultText(res);
    setStatus(g, rt.text, rt.kind, rt.retry ? function () { this.disabled = true; saveField(g); } : null);
    var okSt = !res.error && ['ok', 'clamped', 'unchanged'].indexOf(res.status) >= 0;
    if (!okSt) return false;
    if (res.status === 'clamped' && typeof res.value === 'number' && inp.type !== 'checkbox') {
      inp.value = String(res.value);
      val = inp.value;
    }
    var cfg = S.config || (S.config = { v: {}, s: {} });
    if (g.getAttribute('data-secret') === '1') {
      inp.value = '';
      inp.placeholder = t('common.unchanged');
      cfg.s[key] = true;
    } else {
      g.setAttribute('data-orig', val);
      cfg.v[key] = typedValue(g.getAttribute('data-type'), val);
    }
    return true;
  }

  // Saves every changed field in container, one request at a time.
  // Resolves {changed, failed}.
  async function saveFields(container) {
    var groups = Array.prototype.slice.call(container.querySelectorAll('.form-group[data-key]'));
    var changed = 0;
    var failed = 0;
    for (var i = 0; i < groups.length; i++) {
      if (NEVER_FIELDS.indexOf(groups[i].getAttribute('data-key')) >= 0) continue;
      if (!isChanged(groups[i])) continue;
      changed++;
      if (!(await saveField(groups[i]))) failed++;
    }
    return { changed: changed, failed: failed };
  }

  page('settings', {
    load: loadSettings,
    render: function (v) {
      if (!S.schema || !S.config) {
        v.appendChild(card(t('settings.title'), [el('p', { 'class': 'msg-error', text: errText(S.online ? 'unknown' : 'network') })]));
        return;
      }
      var groups = settingGroups().filter(function (g) { return HIDDEN_GROUPS.indexOf(g) < 0; });
      if (groups.indexOf(S.setTab) < 0) S.setTab = groups[0] || null;
      var tabs = el('div', 'tabs', groups.map(function (g) {
        return el('button', {
          type: 'button', 'class': 'tab' + (g === S.setTab ? ' active' : ''), text: groupLabel(g),
          onclick: function () { S.setTab = g; if (S.page) renderPage(S.page); }
        });
      }));
      var fields = el('div', 'form-grid');
      var n = S.setTab ? renderFields(fields, S.setTab) : 0;
      var msg = el('div');
      var save = el('button', {
        type: 'button', 'class': 'btn btn-primary', text: t('settings.save_all'),
        onclick: async function () {
          if (save.disabled) return;
          save.disabled = true;
          clear(msg);
          var r = await saveFields(fields);
          save.disabled = false;
          if (!r.changed) msg.appendChild(el('p', { 'class': 'muted', text: t('settings.no_changes') }));
        }
      });
      v.appendChild(card(t('settings.title'), [
        tabs,
        n ? fields : el('p', { 'class': 'muted', text: t('settings.empty') }),
        n ? el('p', null, save) : null, msg
      ]));
    }
  });

  if (typeof document !== 'undefined' && typeof window !== 'undefined' && window.fetch) {
    document.addEventListener('DOMContentLoaded', boot);
  }

  return {
    page: page, poll: poll, api: api, command: command, pollCmd: pollCmd, resultText: resultText,
    t: t, errText: errText, tOr: tOr, el: el, clear: clear, card: card, labelOf: labelOf,
    state: function () { return S.state; }, lang: function () { return S.lang; },
    csrf: function () { return S.csrf; }, online: function () { return S.online; },
    navigate: go, refreshState: refreshState, requireLogin: requireLogin, refreshSession: refreshSession,
    holdPolling: holdPolling, resumePolling: resumePolling, setLoginHint: setLoginHint,
    loadSettings: loadSettings, renderFields: renderFields, saveFields: saveFields,
    schema: function () { return S.schema; }, config: function () { return S.config; },
    groups: settingGroups, groupLabel: groupLabel, fmtUptime: fmtUptime, utf8Len: utf8Len,
    _test: {
      parseRoute: parseRoute, i18n: I18N, bits: bits, alarmItems: alarmItems, formBody: formBody,
      needsCsrf: needsCsrf, resultText: resultText, fmtUptime: fmtUptime, typedValue: typedValue,
      utf8Len: utf8Len, precheck: precheck, pollCmd: pollCmd, leavePage: leavePage,
      takeLoginHint: takeLoginHint, logout: logout, held: function () { return S.hold; }
    }
  };
})();
