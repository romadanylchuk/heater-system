/* home-heating widgets (stage 08): PROJECT.render(slotId, el, state, api) reads
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

  window.PROJECT = {
    name: 'home-heating',
    slots: [
      { id: 'heating', titleKey: 'hh.slot.heating' },
      { id: 'dhw', titleKey: 'hh.slot.dhw' }
    ],
    render: function (slotId, el, state, api) {
      if (slotId !== 'heating' && slotId !== 'dhw') return;
      var ctl = state && state.ctl;
      if (!ctl || !ctl.ok) {
        el.innerHTML = '<p class="muted">' + esc(t('hh.starting')) + '</p>';
      } else if (slotId === 'heating') {
        renderHeating(ctl, state, el, api);
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
        'hh.enabled': 'Heating enabled', 'hh.set': 'H2 setpoint', 'hh.heating_off': 'Heating disabled'
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
        'hh.enabled': 'Опалення увімкнено', 'hh.set': 'Уставка H2', 'hh.heating_off': 'Опалення вимкнено'
      }
    }
  };
})();
