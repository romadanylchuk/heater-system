/* boiler-room project widgets (stage 07). The Status page renders each slot as
 * an empty .slot container and calls PROJECT.render(slotId, el, state) inside
 * try/catch. The widgets read the firmware's "ctl" object (BoilerRoomJson.cpp)
 * plus the base relays[] (actual state, lock) and sensors[]. Every piece of
 * text goes through UI.esc / UI.chip / UI.modeBadge (all escape). Project
 * strings live in lang.en / lang.uk (merged over the common language files;
 * both key sets must stay identical). */
'use strict';

(function () {
  var MODE_KIND = { normal: 'on', off: 'off', offer: 'heat' };

  // App loads after this file; resolve the translator lazily.
  function t(key, vars) {
    return window.App && typeof App.t === 'function' ? App.t(key, vars) : key;
  }

  function esc(v) { return UI.esc(v); }

  function mins(s) { return Math.ceil((s > 0 ? s : 0) / 60); }

  // Sensor by name (T1..T6), falling back to its table index.
  function sensorT(state, name, idx) {
    var list = state.sensors || [];
    for (var i = 0; i < list.length; i++) {
      if (list[i] && list[i].name === name) return list[i].t;
    }
    return list[idx] ? list[idx].t : null;
  }

  function line(label, value) {
    return '<p><span class="muted">' + esc(label) + ':</span> ' + value + '</p>';
  }

  function tempLine(state, name, idx) {
    return line(name, esc(UI.fmtTemp(sensorT(state, name, idx))));
  }

  // One pump row: actual relay state, requested reason and the chips.
  function pumpRow(p, relay) {
    p = p || {};
    var on = !!(relay && relay.on);
    var h = '<p><b>' + esc(p.n) + '</b> ' + UI.modeBadge(t(on ? 'common.on' : 'common.off'), on ? 'on' : 'off') +
      ' <span class="muted">' + esc(t('br.r.' + p.r)) + '</span>';
    if (p.sf) h += ' ' + UI.chip(t('br.safety'), 'red');
    if (relay && relay.lockS > 0) h += ' ' + UI.chip(t('br.lock', { s: relay.lockS }), 'amber');
    if (p.as) h += ' ' + UI.chip(t('br.anti_seize'), '');
    return h + '</p>';
  }

  function boiler(ctl, state) {
    var pumps = ctl.pumps || [];
    var relays = state.relays || [];
    return (ctl.oh ? '<p>' + UI.modeBadge(t('br.overheat'), 'alarm') + '</p>' : '') +
      tempLine(state, 'T1', 0) + tempLine(state, 'T2', 1) +
      pumpRow(pumps[0], relays[0]) + pumpRow(pumps[1], relays[1]);
  }

  function accu(ctl, state) {
    var en = ctl.energy || {};
    var layers = [
      { label: 'T3', t: sensorT(state, 'T3', 2) },
      { label: 'T4', t: sensorT(state, 'T4', 3) },
      { label: 'T5', t: sensorT(state, 'T5', 4) }
    ];
    var kwh;
    if (en.q === 'na' || typeof en.kwh !== 'number') {
      kwh = esc(t('br.na'));
    } else {
      kwh = esc(t('br.kwh', { v: en.kwh.toFixed(1) })) + (en.q === 'est' ? ' ' + UI.chip(t('br.estimated'), 'amber') : '');
    }
    return UI.accumulator(layers, 30, 90) + line(t('br.energy'), kwh);
  }

  function supply(ctl, state) {
    var p3 = ctl.p3 || {};
    var pumps = ctl.pumps || [];
    var relays = state.relays || [];
    var badges = UI.modeBadge(t('br.mode.' + p3.mode), MODE_KIND[p3.mode] || '');
    if (p3.af) badges += ' ' + UI.modeBadge(t('br.anti_freeze'), 'frost');
    if (p3.dump) badges += ' ' + UI.modeBadge(t('br.dump'), 'alarm');
    if (p3.noOffer) badges += ' ' + UI.modeBadge(t('br.no_offer'), 'warn');
    var h = '<p>' + badges + '</p>' + tempLine(state, 'T6', 5) + pumpRow(pumps[2], relays[2]);
    if (p3.mode === 'offer' && p3.winS > 0) h += line(t('br.window'), esc(t('br.min', { m: mins(p3.winS) })));
    if (p3.waitS > 0) h += line(t('br.wait'), esc(t('br.min', { m: mins(p3.waitS) })));
    var flag = esc(t('br.flag.saved', { v: t(p3.saved ? 'common.yes' : 'common.no') })) + ', ' +
      esc(t('br.flag.effective', { v: t(p3.flag ? 'common.yes' : 'common.no') }));
    if (p3.saved && !p3.flag && !p3.link) flag += ' ' + UI.chip(t('br.flag.ignored'), 'amber');
    h += line(t('br.flag'), flag);
    if (!p3.af && p3.afInS > 0) h += line(t('br.anti_freeze'), esc(t('br.af_in', { m: mins(p3.afInS) })));
    return h;
  }

  var RENDER = { boiler: boiler, accu: accu, supply: supply };

  window.PROJECT = {
    name: 'boiler-room',
    slots: [
      { id: 'boiler', titleKey: 'br.slot.boiler' },
      { id: 'accu', titleKey: 'br.slot.accu' },
      { id: 'supply', titleKey: 'br.slot.supply' }
    ],
    render: function (slotId, el, state) {
      var ctl = state && state.ctl;
      var fn = RENDER[slotId];
      if (!fn) return;
      el.innerHTML = ctl && ctl.ok ? fn(ctl, state) : '<p class="muted">' + esc(t('br.starting')) + '</p>';
    },
    lang: {
      en: {
        'br.slot.boiler': 'Boiler', 'br.slot.accu': 'Accumulator', 'br.slot.supply': 'Supply',
        'br.starting': 'Controller starting…',
        'br.r.none': 'idle', 'br.r.wait': 'waiting for sensors', 'br.r.charge': 'charging',
        'br.r.charge_t1': 'charging (T1 only)', 'br.r.overheat': 'overheat', 'br.r.t1_fault': 'T1 fail: forced',
        'br.r.return': 'return protection', 'br.r.return_t2': 'return (T2 only)', 'br.r.burn_gate': 'burn gate',
        'br.r.t1t2_fault': 'T1+T2 fail: forced', 'br.r.normal': 'normal', 'br.r.off': 'off (no need)',
        'br.r.offer': 'offer', 'br.r.anti_freeze': 'anti-freeze', 'br.r.dump': 'overheat dump',
        'br.mode.normal': 'Normal', 'br.mode.off': 'Off', 'br.mode.offer': 'Offer',
        'alarm.0': 'Boiler overheat', 'alarm.1': 'T1 fail: P1 forced', 'alarm.2': 'T1+T2 fail: P2 forced',
        'alarm.3': 'T2 fail: P2 burn gate', 'alarm.4': 'T3 fail: no offer',
        'alarm.8': 'T1 fault/unassigned', 'alarm.9': 'T2 fault/unassigned', 'alarm.10': 'T3 fault/unassigned',
        'alarm.11': 'T4 fault/unassigned', 'alarm.12': 'T5 fault/unassigned', 'alarm.13': 'T6 fault/unassigned',
        'ev.t1000': 'P3 mode changed', 'ev.t1001': 'Pump request',
        'group.p1': 'Pump P1 (charge)', 'group.p2': 'Pump P2 (return)', 'group.p3': 'Pump P3 (supply)',
        'group.accumulator': 'Accumulator',
        'br.overheat': 'Overheat', 'br.safety': 'safety (lock bypass)', 'br.lock': 'lock {s} s',
        'br.anti_seize': 'anti-seize', 'br.anti_freeze': 'Anti-freeze', 'br.dump': 'Overheat dump',
        'br.no_offer': 'Offer disabled (T3)', 'br.energy': 'Stored energy', 'br.kwh': '{v} kWh',
        'br.estimated': 'estimated', 'br.na': 'n/a', 'br.window': 'Offer window', 'br.wait': 'Next offer in',
        'br.min': '{m} min', 'br.af_in': 'next run in {m} min', 'br.flag': '"No need" flag',
        'br.flag.saved': 'saved: {v}', 'br.flag.effective': 'effective: {v}', 'br.flag.ignored': 'ignored (MQTT down)'
      },
      uk: {
        'br.slot.boiler': 'Котел', 'br.slot.accu': 'Акумулятор', 'br.slot.supply': 'Подача',
        'br.starting': 'Контролер запускається…',
        'br.r.none': 'простій', 'br.r.wait': 'очікування датчиків', 'br.r.charge': 'заряд',
        'br.r.charge_t1': 'заряд (лише T1)', 'br.r.overheat': 'перегрів', 'br.r.t1_fault': 'збій T1: примусово',
        'br.r.return': 'захист зворотки', 'br.r.return_t2': 'зворотка (лише T2)', 'br.r.burn_gate': 'за горінням',
        'br.r.t1t2_fault': 'збій T1+T2: примусово', 'br.r.normal': 'звичайний', 'br.r.off': 'вимкнено (не потрібно)',
        'br.r.offer': 'пропозиція', 'br.r.anti_freeze': 'захист від замерзання', 'br.r.dump': 'скидання перегріву',
        'br.mode.normal': 'Звичайний', 'br.mode.off': 'Вимкнено', 'br.mode.offer': 'Пропозиція',
        'alarm.0': 'Перегрів котла', 'alarm.1': 'Збій T1: P1 примусово', 'alarm.2': 'Збій T1+T2: P2 примусово',
        'alarm.3': 'Збій T2: P2 за горінням', 'alarm.4': 'Збій T3: без пропозиції',
        'alarm.8': 'Збій/не призначено T1', 'alarm.9': 'Збій/не призначено T2', 'alarm.10': 'Збій/не призначено T3',
        'alarm.11': 'Збій/не призначено T4', 'alarm.12': 'Збій/не призначено T5', 'alarm.13': 'Збій/не призначено T6',
        'ev.t1000': 'Зміна режиму P3', 'ev.t1001': 'Запит насоса',
        'group.p1': 'Насос P1 (заряд)', 'group.p2': 'Насос P2 (зворотка)', 'group.p3': 'Насос P3 (подача)',
        'group.accumulator': 'Акумулятор',
        'br.overheat': 'Перегрів', 'br.safety': 'безпека (обхід блокування)', 'br.lock': 'блокування {s} с',
        'br.anti_seize': 'антизаклинювання', 'br.anti_freeze': 'Захист від замерзання', 'br.dump': 'Скидання перегріву',
        'br.no_offer': 'Пропозицію вимкнено (T3)', 'br.energy': 'Запасена енергія', 'br.kwh': '{v} кВт·год',
        'br.estimated': 'орієнтовно', 'br.na': 'н/д', 'br.window': 'Вікно пропозиції', 'br.wait': 'Наступна пропозиція через',
        'br.min': '{m} хв', 'br.af_in': 'наступний запуск через {m} хв', 'br.flag': 'Прапорець «не потрібно»',
        'br.flag.saved': 'збережено: {v}', 'br.flag.effective': 'діє: {v}', 'br.flag.ignored': 'ігнорується (MQTT недоступний)'
      }
    }
  };
})();
