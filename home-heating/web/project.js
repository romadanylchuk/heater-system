/* home-heating widgets (stage 08; stage 09 adds the "tuning" slot and
 * api.projectCmd(op) for the K1 step test): PROJECT.render(slotId, el, state, api) reads
 * state.ctl (HomeHeatingJson.cpp), relays[] and sensors[]; api (D22, optional)
 * = {setConfig(key, value) -> Promise<command result>}, else controls are
 * read-only. All text via UI.esc/chip/modeBadge; lang.en/uk keys identical. */
'use strict';

(function () {
  var RELAY_K2 = 0, RELAY_P4 = 3;
  var SET_MIN = 30, SET_MAX = 75, SET_STEP = 0.5;

  function t(key, vars) {
    return window.App && typeof App.t === 'function' ? App.t(key, vars) : key;
  }

  function esc(v) { return UI.esc(v); }

  function line(label, value) {
    return '<p><span class="muted">' + esc(label) + ':</span> ' + value + '</p>';
  }

  function sensor(state, name, idx) {
    var list = state.sensors || [];
    for (var i = 0; i < list.length; i++) {
      if (list[i] && list[i].name === name) return list[i];
    }
    return list[idx] || null;
  }

  function tempLine(state, name, idx) {
    var s = sensor(state, name, idx);
    var v = esc(UI.fmtTemp(s ? s.t : null));
    var bad = !s || s.state === 'unassigned' ? t('sensors.unassigned') :
      (s.missing ? t('sensors.missing') : (s.state === 'fault' ? t('status.sensor_fault') : ''));
    return line(name, v + (bad ? ' ' + UI.chip(bad, 'red') : ''));
  }

  function chips(relay, as) {
    var h = '';
    if (relay && relay.lockS > 0) h += ' ' + UI.chip(t('hh.lock', { s: relay.lockS }), 'amber');
    if (as) h += ' ' + UI.chip(t('hh.anti_seize'), '');
    return h;
  }

  function p4Row(ctl, state) {
    var p4 = ctl.p4 || {};
    var relay = (state.relays || [])[RELAY_P4];
    var on = !!(relay && relay.on);
    var h = '<p><b>P4</b> ' + UI.modeBadge(t(on ? 'common.on' : 'common.off'), on ? 'on' : 'off') +
      ' <span class="muted">' + esc(t('hh.p4.' + p4.r)) + '</span>';
    if (p4.dlyS > 0) h += ' ' + UI.chip(t('hh.delay', { s: p4.dlyS }), 'amber');
    return h + chips(relay, p4.as) + '</p>';
  }

  function k1Rows(ctl) {
    var k1 = ctl.k1 || {};
    var known = typeof k1.pos === 'number';
    var label = 'K1 ' + (known ? t('hh.k1.valve') : t(k1.m === 'recal' ? 'hh.k1.recal' : 'hh.k1.unknown'));
    var h = UI.k1Bar(known ? k1.pos : 0, label) + '<p>' + UI.modeBadge(t('hh.k1.' + k1.m), '');
    if (k1.mv > 0) h += ' ' + UI.chip(t('hh.open'), 'ok');
    else if (k1.mv < 0) h += ' ' + UI.chip(t('hh.close'), 'amber');
    if (k1.as) h += ' ' + UI.chip(t('hh.anti_seize'), '');
    h += '</p>';
    if (typeof k1.ff === 'number') h += line(t('hh.ff'), esc(k1.ff + ' %'));
    return h;
  }

  function heatingView(ctl, state, readOnly) {
    var h = '';
    if (ctl.fs && ctl.fs !== 'none') h += '<p>' + UI.modeBadge(t('hh.fs.' + ctl.fs), 'alarm') + '</p>';
    if (!ctl.en) h += '<p>' + UI.modeBadge(t('hh.heating_off'), 'off') + '</p>';
    h += tempLine(state, 'H1', 0) + tempLine(state, 'H2', 1) + tempLine(state, 'H3', 2);
    if (readOnly) {
      h += line(t('hh.enabled'), esc(t(ctl.en ? 'common.yes' : 'common.no'))) +
        line(t('hh.set'), esc(UI.fmtTemp(ctl.set)));
    }
    return h + p4Row(ctl, state) + k1Rows(ctl);
  }

  function dhwView(ctl, state) {
    var k2 = ctl.k2 || {};
    var relay = (state.relays || [])[RELAY_K2];
    var byp = !!(relay && relay.on);   // energised = bypass
    return tempLine(state, 'H3', 2) + tempLine(state, 'H4', 3) +
      '<p><b>K2</b> ' + UI.modeBadge(t(byp ? 'hh.bypass' : 'hh.tank'), byp ? 'off' : 'heat') +
      ' <span class="muted">' + esc(t('hh.k2.' + k2.r)) + '</span>' + chips(relay, k2.as) + '</p>' +
      line(t('hh.nn'), UI.modeBadge(t(ctl.nn ? 'common.yes' : 'common.no'), ctl.nn ? 'ok' : 'heat'));
  }

  function q(box, name) { return box.querySelector('[data-hh="' + name + '"]'); }

  function setSt(st, text, kind) {
    st.className = 'field-status' + (kind ? ' ' + kind : '');
    st.textContent = text;
  }

  // ctrl stays disabled until the command result arrives (disabled = busy).
  function write(api, ctrl, st, key, value, onOk) {
    ctrl.disabled = true;
    setSt(st, t('cmd.pending'), '');
    Promise.resolve(api.setConfig(key, value)).catch(function () { return { error: 'unknown' }; }).then(function (res) {
      ctrl.disabled = false;
      var rt = App.resultText(res);
      setSt(st, rt.text, rt.kind);
      if (!res.error && ['ok', 'clamped', 'unchanged'].indexOf(res.status) >= 0) {
        if (onOk) onOk(res);
        App.refreshState();
      }
    });
  }

  // Built once per slot, then only synced: the poll never rebuilds a focused/dirty input (D22).
  function buildControls(box, api) {
    box.innerHTML =
      '<div class="form-group"><label><input type="checkbox" data-hh="en">' + esc(t('hh.enabled')) +
      '</label><div class="field-status" data-hh="en-st"></div></div>' +
      '<form class="form-group" data-hh="set-form" novalidate><label>' + esc(t('hh.set')) +
      '<span class="unit">(°C)</span></label><input type="number" data-hh="set" min="' + SET_MIN +
      '" max="' + SET_MAX + '" step="' + SET_STEP + '" required> <button type="submit" class="btn btn-secondary">' +
      esc(t('common.save')) + '</button><div class="field-status" data-hh="set-st"></div></form>';
    var cb = q(box, 'en');
    var inp = q(box, 'set');
    cb.addEventListener('change', function () {
      write(api, cb, q(box, 'en-st'), 'heatingEnabled', cb.checked ? 1 : 0);
    });
    inp.addEventListener('input', function () { inp.setAttribute('data-dirty', '1'); });
    q(box, 'set-form').addEventListener('submit', function (e) {
      e.preventDefault();
      var st = q(box, 'set-st');
      if (inp.disabled) return;
      if (!inp.checkValidity()) {
        setSt(st, inp.validationMessage || App.errText('invalid'), 'err');
        return;
      }
      write(api, inp, st, 'h2Set', inp.value, function (res) {
        if (res.status === 'clamped' && typeof res.value === 'number') inp.value = res.value.toFixed(1);
        inp.removeAttribute('data-dirty');
      });
    });
  }

  function syncControls(box, ctl) {
    var cb = q(box, 'en');
    var inp = q(box, 'set');
    if (!cb.disabled) cb.checked = !!ctl.en;
    if (typeof ctl.set !== 'number' || inp.disabled || document.activeElement === inp) return;
    var v = ctl.set.toFixed(1);
    if (inp.getAttribute('data-dirty') !== '1' || inp.value === v) {
      inp.value = v;
      inp.removeAttribute('data-dirty');
    }
  }

  function renderHeating(ctl, state, el, api) {
    var hasApi = !!(api && typeof api.setConfig === 'function');
    var view = q(el, 'view');
    var box = q(el, 'ctl');
    if (!view || hasApi !== !!box) {
      el.innerHTML = '<div data-hh="view"></div>' + (hasApi ? '<div data-hh="ctl"></div>' : '');
      view = q(el, 'view');
      box = q(el, 'ctl');
      if (hasApi) buildControls(box, api);
    }
    view.innerHTML = heatingView(ctl, state, !hasApi);
    if (hasApi) syncControls(box, ctl);
  }

  // --- Stage 09 (C16): K1 tuning slot: tuning data + step-test panel ---
  var STEP_MAX_S = 600;
  var OK_ST = ['ok', 'clamped', 'unchanged'];

  function num(v, d) { return typeof v === 'number' ? v.toFixed(d) : null; }

  function tuningRows(tu) {
    var err = num(tu.err, 1);
    var lp = tu.lpd > 0 ? 'hh.tu.open' : (tu.lpd < 0 ? 'hh.tu.close' : '');
    var lps = num(tu.lps, 1);
    return line(t('hh.tu.err'), esc(err === null ? '—' : (tu.err > 0 ? '+' : '') + err + ' °C')) +
      line(t('hh.tu.last'), esc(lp ? t(lp) + (lps === null ? '' : ' ' + lps + ' s') : t('hh.tu.none'))) +
      line(t('hh.tu.today'), esc(typeof tu.pt === 'number' ? tu.pt : t('hh.tu.na'))) +
      line(t('hh.tu.yesterday'), esc(typeof tu.py === 'number' ? tu.py : t('hh.tu.na')));
  }

  function resultRows(res) {
    if (!res) return '';
    var h = '<p><b>' + esc(t('hh.st.last')) + '</b></p>';
    if (res.o === 'no_response') return h + '<p>' + UI.chip(t('hh.st.no_response'), 'amber') + '</p>';
    if (res.o === 'aborted') {
      return h + '<p>' + UI.chip(t('hh.st.aborted'), 'amber') + ' ' + esc(t('hh.st.ab.' + res.ab)) + '</p>';
    }
    if (res.o !== 'result') return '';
    if (num(res.dt, 1) !== null) h += line(t('hh.st.dead'), esc(num(res.dt, 1) + ' s'));
    if (num(res.r, 3) !== null) h += line(t('hh.st.response'), esc(num(res.r, 3) + ' °C/s'));
    if (typeof res.p === 'number' && typeof res.g === 'number') {
      h += line(t('hh.st.suggested'), esc(t('hh.st.pg', { p: res.p, g: res.g.toFixed(1) })));
    }
    return h;
  }

  function stepRows(st) {
    var h = '<p><b>' + esc(t('hh.st.title')) + '</b></p>';
    if (st.run) {
      h += '<p>' + UI.modeBadge(t('hh.st.running'), 'heat') + '</p>' +
        line(t('hh.st.elapsed'), esc((st.el || 0) + ' / ' + STEP_MAX_S + ' s'));
      if (num(st.dt, 1) !== null) h += line(t('hh.st.dead'), esc(num(st.dt, 1) + ' s'));
    } else {
      h += line(t('hh.st.pulse'), esc((st.ps || 0) + ' s'));
      if (st.blk && st.blk !== 'none') h += '<p>' + UI.chip(t('hh.st.blk.' + st.blk), 'amber') + '</p>';
    }
    return h + resultRows(st.res);
  }

  function applicable(res) {
    return !!(res && res.o === 'result' && typeof res.p === 'number' && typeof res.g === 'number');
  }

  function stepBtns(box) { return [q(box, 'start'), q(box, 'cancel'), q(box, 'apply')]; }

  // A pending box (data-busy) is never touched by the poll.
  function syncStep(box, st) {
    box.hhLast = st;
    if (box.getAttribute('data-busy') === '1') return;
    var b = stepBtns(box);
    b[0].hidden = !!st.run;
    b[0].disabled = !!(st.blk && st.blk !== 'none');
    b[1].hidden = !st.run;
    b[1].disabled = false;
    var ok = applicable(st.res);
    b[2].hidden = !ok;
    b[2].disabled = !ok;
    if (ok) {
      b[2].textContent = t('hh.st.apply', { p: st.res.p, g: st.res.g.toFixed(1) });
      b[2].hhPg = { p: st.res.p, g: Number(st.res.g.toFixed(1)) };
    }
  }

  function safeCmd(p) {
    return Promise.resolve(p).catch(function () { return { error: 'unknown' }; });
  }

  // Runs fn() -> Promise<{text, kind}>; buttons stay disabled until it resolves.
  function busy(box, fn) {
    if (box.getAttribute('data-busy') === '1') return;
    box.setAttribute('data-busy', '1');
    stepBtns(box).forEach(function (b) { b.disabled = true; });
    var st = q(box, 'st-st');
    setSt(st, t('cmd.pending'), '');
    Promise.resolve().then(fn).catch(function () {
      return App.resultText({ error: 'unknown' });
    }).then(function (rt) {
      box.removeAttribute('data-busy');
      setSt(st, rt.text, rt.kind);
      if (box.hhLast) syncStep(box, box.hhLast);
      App.refreshState();
    });
  }

  // D23: period first; the gain is written only if the period write succeeded.
  function applyPg(api, pg) {
    return safeCmd(api.setConfig('k1Period', pg.p)).then(function (r1) {
      var t1 = App.resultText(r1);
      var txt1 = t('hh.st.period') + ': ' + t1.text;
      if (r1.error || OK_ST.indexOf(r1.status) < 0) return { text: txt1, kind: 'err' };
      return safeCmd(api.setConfig('k1Gain', pg.g)).then(function (r2) {
        var t2 = App.resultText(r2);
        var kind = t2.kind === 'err' ? 'err' : (t1.kind === 'warn' || t2.kind === 'warn' ? 'warn' : 'ok');
        return { text: txt1 + '; ' + t('hh.st.gain') + ': ' + t2.text, kind: kind };
      });
    });
  }

  // Built once per slot, then only synced (D22 pattern).
  function buildStep(box, api) {
    box.innerHTML = '<div class="form-group">' +
      '<button type="button" class="btn btn-primary" data-hh="start">' + esc(t('hh.st.start')) + '</button> ' +
      '<button type="button" class="btn btn-secondary" data-hh="cancel" hidden>' + esc(t('hh.st.cancel')) +
      '</button> <button type="button" class="btn btn-secondary" data-hh="apply" hidden></button>' +
      '<div class="field-status" data-hh="st-st"></div></div>';
    function cmd(op) {
      busy(box, function () {
        return safeCmd(api.projectCmd(op)).then(function (res) { return App.resultText(res); });
      });
    }
    q(box, 'start').addEventListener('click', function () { cmd(1); });
    q(box, 'cancel').addEventListener('click', function () { cmd(2); });
    var ap = q(box, 'apply');
    ap.addEventListener('click', function () {
      var pg = ap.hhPg;
      if (pg) busy(box, function () { return applyPg(api, pg); });
    });
  }

  // Read-only (no buttons) without api.projectCmd/setConfig or without "st".
  function renderTuning(ctl, el, api) {
    var st = ctl.st;
    var wantBox = !!(st && api && typeof api.projectCmd === 'function' && typeof api.setConfig === 'function');
    var view = q(el, 'view');
    var box = q(el, 'ctl');
    if (!view || wantBox !== !!box) {
      el.innerHTML = '<div data-hh="view"></div>' + (wantBox ? '<div data-hh="ctl"></div>' : '');
      view = q(el, 'view');
      box = q(el, 'ctl');
      if (wantBox) buildStep(box, api);
    }
    view.innerHTML = tuningRows(ctl.tu || {}) + (st ? stepRows(st) : '');
    if (wantBox) syncStep(box, st);
  }

  window.PROJECT = {
    name: 'home-heating',
    slots: [
      { id: 'heating', titleKey: 'hh.slot.heating' },
      { id: 'dhw', titleKey: 'hh.slot.dhw' },
      { id: 'tuning', titleKey: 'hh.slot.tuning' }
    ],
    render: function (slotId, el, state, api) {
      if (slotId !== 'heating' && slotId !== 'dhw' && slotId !== 'tuning') return;
      var ctl = state && state.ctl;
      if (!ctl || !ctl.ok) {
        el.innerHTML = '<p class="muted">' + esc(t('hh.starting')) + '</p>';
      } else if (slotId === 'heating') {
        renderHeating(ctl, state, el, api);
      } else if (slotId === 'tuning') {
        renderTuning(ctl, el, api);
      } else {
        el.innerHTML = dhwView(ctl, state);
      }
    },
    lang: {
      en: {
        'hh.slot.heating': 'Heating', 'hh.slot.dhw': 'Hot water (DHW)',
        'hh.starting': 'Controller starting…',
        'hh.p4.none': 'idle', 'hh.p4.heating_off': 'heating disabled', 'hh.p4.sensor_wait': 'waiting for sensors',
        'hh.p4.demand': 'demand (H3 below set)', 'hh.p4.off_delay': 'off delay', 'hh.p4.supply_cold': 'supply cold',
        'hh.p4.h3_fault': 'H3 fail: forced on', 'hh.p4.multi_fault': 'sensors fail: forced on',
        'hh.p4.multi_fault_off': 'sensors fail: off',
        'hh.k2.none': 'idle', 'hh.k2.sensor_wait': 'waiting for sensors', 'hh.k2.charging': 'charging tank',
        'hh.k2.delta_low': 'H3−H4 too small', 'hh.k2.h3_low': 'H3 too low', 'hh.k2.h4_full': 'tank full (H4)',
        'hh.k2.h3_fault': 'H3 fail', 'hh.k2.h4_fault': 'H4 fail',
        'hh.k1.unknown': 'position unknown', 'hh.k1.recal': 'recalibrating', 'hh.k1.closed': 'closed',
        'hh.k1.wait': 'waiting', 'hh.k1.normal': 'normal', 'hh.k1.fb_only': 'feedback only',
        'hh.k1.ff_only': 'feed-forward only', 'hh.k1.failpos_fb': 'fail position + feedback',
        'hh.k1.failpos_fixed': 'fixed fail position', 'hh.k1.valve': 'mixing valve',
        'hh.fs.none': 'no fail-safe', 'hh.fs.h1': 'H1 fail: K1 feedback only', 'hh.fs.h2': 'H2 fail: K1 feed-forward only',
        'hh.fs.h3': 'H3 fail: K1 fixed, P4 on', 'hh.fs.multi': 'Sensors fail: K1 fixed',
        'alarm.0': 'H3 fail: K1 fixed', 'alarm.1': 'H2 fail: K1 FF only', 'alarm.2': 'H1 fail: K1 FB only',
        'alarm.3': 'Sensors fail: K1 fix', 'alarm.4': 'H4 fail: K2 bypass',
        'alarm.8': 'H1 fault/unassigned', 'alarm.9': 'H2 fault/unassigned', 'alarm.10': 'H3 fault/unassigned',
        'alarm.11': 'H4 fault/unassigned',
        'ev.t1000': 'P4 request', 'ev.t1001': 'K2 request', 'ev.t1002': 'Fail-safe mode',
        'ev.t1003': 'K1 recalibration', 'ev.t1004': '"No need" signal',
        'group.heating': 'Heating', 'group.k1': 'Mixing valve K1', 'group.k2': 'DHW valve K2',
        'hh.lock': 'lock {s} s', 'hh.anti_seize': 'anti-seize', 'hh.delay': 'off in {s} s',
        'hh.open': 'opening', 'hh.close': 'closing', 'hh.ff': 'Feed-forward target',
        'hh.tank': 'Tank', 'hh.bypass': 'Bypass', 'hh.nn': '"No need" to boiler room',
        'hh.enabled': 'Heating enabled', 'hh.set': 'H2 setpoint', 'hh.heating_off': 'Heating disabled',
        'hh.slot.tuning': 'K1 tuning', 'warn.0': 'P4 no flow (H1)',
        'ev.t1005': 'Pump diagnostic', 'ev.t1006': 'K1 step test start', 'ev.t1007': 'K1 step test result',
        'ev.t1008': 'K1 step test aborted', 'group.diag': 'Diagnostics',
        'hh.tu.err': 'H2 error', 'hh.tu.last': 'Last K1 pulse', 'hh.tu.open': 'open', 'hh.tu.close': 'close',
        'hh.tu.none': 'none', 'hh.tu.today': 'K1 pulses today', 'hh.tu.yesterday': 'K1 pulses yesterday',
        'hh.tu.na': 'n/a',
        'hh.st.title': 'K1 step test', 'hh.st.start': 'Start step test', 'hh.st.cancel': 'Cancel test',
        'hh.st.apply': 'Apply period {p} s, gain {g}', 'hh.st.running': 'running', 'hh.st.elapsed': 'Elapsed',
        'hh.st.pulse': 'Test pulse', 'hh.st.dead': 'Dead time', 'hh.st.response': 'Response',
        'hh.st.suggested': 'Suggested', 'hh.st.pg': 'period {p} s, gain {g}', 'hh.st.last': 'Last result',
        'hh.st.no_response': 'no response', 'hh.st.aborted': 'aborted',
        'hh.st.period': 'Period', 'hh.st.gain': 'Gain',
        'hh.st.blk.unavailable': 'diagnostics settings unavailable', 'hh.st.blk.running': 'test running',
        'hh.st.blk.heating_off': 'heating disabled', 'hh.st.blk.p4_off': 'P4 is off', 'hh.st.blk.ota': 'OTA update running',
        'hh.st.blk.anti_seize': 'anti-seize running', 'hh.st.blk.sensors': 'H1/H2/H3 not OK',
        'hh.st.blk.k1_mode': 'K1 not in normal mode', 'hh.st.blk.k1_unknown': 'K1 position unknown',
        'hh.st.blk.k1_busy': 'K1 moving', 'hh.st.blk.k1_headroom': 'K1 too close to fully open',
        'hh.st.blk.history': 'not enough history yet', 'hh.st.blk.h3_unsteady': 'H3 not steady',
        'hh.st.blk.h2_unsteady': 'H2 not steady',
        'hh.st.ab.cancel': 'cancelled', 'hh.st.ab.heating_off': 'heating disabled', 'hh.st.ab.p4_off': 'P4 switched off',
        'hh.st.ab.ota': 'OTA update', 'hh.st.ab.anti_seize': 'anti-seize', 'hh.st.ab.recal': 'K1 recalibration',
        'hh.st.ab.sensors': 'sensor fault', 'hh.st.ab.k1_mode': 'K1 mode changed', 'hh.st.ab.h3_changed': 'H3 changed'
      },
      uk: {
        'hh.slot.heating': 'Опалення', 'hh.slot.dhw': 'Гаряча вода (ГВП)',
        'hh.starting': 'Контролер запускається…',
        'hh.p4.none': 'простій', 'hh.p4.heating_off': 'опалення вимкнено', 'hh.p4.sensor_wait': 'очікування датчиків',
        'hh.p4.demand': 'запит (H3 нижче уставки)', 'hh.p4.off_delay': 'затримка вимкнення', 'hh.p4.supply_cold': 'подача холодна',
        'hh.p4.h3_fault': 'збій H3: примусово', 'hh.p4.multi_fault': 'збій датчиків: примусово',
        'hh.p4.multi_fault_off': 'збій датчиків: вимкнено',
        'hh.k2.none': 'простій', 'hh.k2.sensor_wait': 'очікування датчиків', 'hh.k2.charging': 'нагрів бойлера',
        'hh.k2.delta_low': 'H3−H4 замала', 'hh.k2.h3_low': 'H3 занизька', 'hh.k2.h4_full': 'бойлер нагрітий (H4)',
        'hh.k2.h3_fault': 'збій H3', 'hh.k2.h4_fault': 'збій H4',
        'hh.k1.unknown': 'положення невідоме', 'hh.k1.recal': 'калібрування', 'hh.k1.closed': 'закрито',
        'hh.k1.wait': 'очікування', 'hh.k1.normal': 'звичайний', 'hh.k1.fb_only': 'лише зворотний зв’язок',
        'hh.k1.ff_only': 'лише пряме керування', 'hh.k1.failpos_fb': 'аварійне положення + зв’язок',
        'hh.k1.failpos_fixed': 'фіксоване аварійне положення', 'hh.k1.valve': 'змішувальний клапан',
        'hh.fs.none': 'без аварійного режиму', 'hh.fs.h1': 'Збій H1: K1 лише зворотне', 'hh.fs.h2': 'Збій H2: K1 лише пряме',
        'hh.fs.h3': 'Збій H3: K1 фіксовано, P4 увімк.', 'hh.fs.multi': 'Збій датчиків: K1 фіксовано',
        'alarm.0': 'Збій H3: K1 фіксовано', 'alarm.1': 'Збій H2: K1 лише пряме', 'alarm.2': 'Збій H1: K1 лише зворотне',
        'alarm.3': 'Збій датчиків: K1 фікс.', 'alarm.4': 'Збій H4: K2 байпас',
        'alarm.8': 'Збій/не призначено H1', 'alarm.9': 'Збій/не призначено H2', 'alarm.10': 'Збій/не призначено H3',
        'alarm.11': 'Збій/не призначено H4',
        'ev.t1000': 'Запит P4', 'ev.t1001': 'Запит K2', 'ev.t1002': 'Аварійний режим',
        'ev.t1003': 'Калібрування K1', 'ev.t1004': 'Сигнал «не потрібно»',
        'group.heating': 'Опалення', 'group.k1': 'Змішувальний клапан K1', 'group.k2': 'Клапан ГВП K2',
        'hh.lock': 'блокування {s} с', 'hh.anti_seize': 'антизаклинювання', 'hh.delay': 'вимкнення через {s} с',
        'hh.open': 'відкривається', 'hh.close': 'закривається', 'hh.ff': 'Ціль прямого керування',
        'hh.tank': 'Бойлер', 'hh.bypass': 'Байпас', 'hh.nn': '«Не потрібно» для котельні',
        'hh.enabled': 'Опалення увімкнено', 'hh.set': 'Уставка H2', 'hh.heating_off': 'Опалення вимкнено',
        'hh.slot.tuning': 'Налаштування K1', 'warn.0': 'P4 немає потоку (H1)',
        'ev.t1005': 'Діагностика насоса', 'ev.t1006': 'Старт тесту кроку K1', 'ev.t1007': 'Результат тесту кроку K1',
        'ev.t1008': 'Тест кроку K1 перервано', 'group.diag': 'Діагностика',
        'hh.tu.err': 'Похибка H2', 'hh.tu.last': 'Останній імпульс K1', 'hh.tu.open': 'відкриття', 'hh.tu.close': 'закриття',
        'hh.tu.none': 'немає', 'hh.tu.today': 'Імпульси K1 сьогодні', 'hh.tu.yesterday': 'Імпульси K1 учора',
        'hh.tu.na': 'н/д',
        'hh.st.title': 'Тест кроку K1', 'hh.st.start': 'Почати тест кроку', 'hh.st.cancel': 'Скасувати тест',
        'hh.st.apply': 'Застосувати період {p} с, підсилення {g}', 'hh.st.running': 'виконується', 'hh.st.elapsed': 'Минуло',
        'hh.st.pulse': 'Тестовий імпульс', 'hh.st.dead': 'Час запізнення', 'hh.st.response': 'Відгук',
        'hh.st.suggested': 'Рекомендовано', 'hh.st.pg': 'період {p} с, підсилення {g}', 'hh.st.last': 'Останній результат',
        'hh.st.no_response': 'немає відгуку', 'hh.st.aborted': 'перервано',
        'hh.st.period': 'Період', 'hh.st.gain': 'Підсилення',
        'hh.st.blk.unavailable': 'налаштування діагностики недоступні', 'hh.st.blk.running': 'тест виконується',
        'hh.st.blk.heating_off': 'опалення вимкнено', 'hh.st.blk.p4_off': 'P4 вимкнено', 'hh.st.blk.ota': 'виконується оновлення OTA',
        'hh.st.blk.anti_seize': 'виконується антизаклинювання', 'hh.st.blk.sensors': 'H1/H2/H3 не в нормі',
        'hh.st.blk.k1_mode': 'K1 не у звичайному режимі', 'hh.st.blk.k1_unknown': 'положення K1 невідоме',
        'hh.st.blk.k1_busy': 'K1 рухається', 'hh.st.blk.k1_headroom': 'K1 майже повністю відкритий',
        'hh.st.blk.history': 'ще замало історії', 'hh.st.blk.h3_unsteady': 'H3 нестабільна',
        'hh.st.blk.h2_unsteady': 'H2 нестабільна',
        'hh.st.ab.cancel': 'скасовано', 'hh.st.ab.heating_off': 'опалення вимкнено', 'hh.st.ab.p4_off': 'P4 вимкнувся',
        'hh.st.ab.ota': 'оновлення OTA', 'hh.st.ab.anti_seize': 'антизаклинювання', 'hh.st.ab.recal': 'калібрування K1',
        'hh.st.ab.sensors': 'збій датчика', 'hh.st.ab.k1_mode': 'зміна режиму K1', 'hh.st.ab.h3_changed': 'H3 змінилася'
      }
    }
  };
})();
