/* Shared render helpers (stage 05). Pure functions: each returns an HTML
 * string and never touches the DOM, the network or controller state. Every
 * piece of text goes through UI.esc. No controller-specific logic here;
 * project widgets live in <project>/web/project.js. */
'use strict';

var UI = (function () {
  var ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

  function esc(v) {
    if (v === null || v === undefined) return '';
    return String(v).replace(/[&<>"']/g, function (c) { return ESC[c]; });
  }

  // t may be missing (tests, early boot): fall back to the key itself.
  function tr(t, key, vars) {
    return typeof t === 'function' ? t(key, vars) : key;
  }

  function isNum(v) {
    return typeof v === 'number' && isFinite(v);
  }

  function fmtTemp(t) {
    return isNum(t) ? t.toFixed(1) + ' °C' : '—';
  }

  // items: array of already-translated strings. '' when there is nothing.
  function alarmBanner(items) {
    if (!items || !items.length) return '';
    var rows = items.map(function (s) {
      return '<div class="alarm-item">' + esc(s) + '</div>';
    }).join('');
    return '<div class="alarm-banner"><span class="alarm-icon">&#9888;</span><div>' + rows + '</div></div>';
  }

  // kind: 'ok' | 'amber' | 'red' | '' (neutral).
  function chip(text, kind) {
    var cls = kind ? ' chip-' + esc(kind) : '';
    return '<span class="chip' + cls + '">' + esc(text) + '</span>';
  }

  // kind: off | on | ok | heat | cool | frost | warn | alarm.
  function modeBadge(text, kind) {
    var cls = kind ? ' ' + esc(kind) : '';
    return '<span class="mode-badge' + cls + '">' + esc(text) + '</span>';
  }

  function clampPct(p) {
    if (!isNum(p)) return 0;
    return p < 0 ? 0 : (p > 100 ? 100 : p);
  }

  function k1Bar(pct, label) {
    var p = clampPct(pct);
    return '<div class="k1-bar"><div class="k1-label"><span>' + esc(label) + '</span><span>' +
      Math.round(p) + ' %</span></div><div class="k1-track"><div class="k1-fill" style="width:' +
      p + '%"></div></div></div>';
  }

  // Blue (cold, <= min) to red (hot, >= max).
  function tempColor(t, min, max) {
    var span = max - min;
    var f = span > 0 ? (t - min) / span : 0.5;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return 'hsl(' + Math.round(220 - 220 * f) + ',70%,' + Math.round(42 + 6 * f) + '%)';
  }

  // layers: [{label, t}] top first; t may be null (sensor missing).
  function accumulator(layers, min, max) {
    var out = (layers || []).map(function (l) {
      if (!isNum(l.t)) {
        return '<div class="accu-layer na"><span>' + esc(l.label) + '</span><span class="accu-t">—</span></div>';
      }
      var c = tempColor(l.t, min, max);
      var c2 = tempColor(l.t - (max - min) * 0.08, min, max);
      return '<div class="accu-layer" style="background:linear-gradient(180deg,' + c + ',' + c2 + ')"><span>' +
        esc(l.label) + '</span><span class="accu-t">' + esc(fmtTemp(l.t)) + '</span></div>';
    }).join('');
    return '<div class="accu">' + out + '</div>';
  }

  // rows: bus sensors [{addr, t, logical (index or -1), new, missing}].
  // logicalOptions: [{i, name}]. Buttons carry data-act/data-addr so the
  // page can attach one delegated click handler.
  function sensorTable(rows, logicalOptions, t) {
    var opts = logicalOptions || [];
    var head = '<tr><th>' + esc(tr(t, 'sensors.address')) + '</th><th>' + esc(tr(t, 'sensors.temp')) +
      '</th><th>' + esc(tr(t, 'sensors.name')) + '</th><th></th></tr>';
    var body = (rows || []).map(function (r) {
      var badges = (r.new ? '<span class="badge-new">' + esc(tr(t, 'sensors.new')) + '</span>' : '') +
        (r.missing ? '<span class="badge-missing">' + esc(tr(t, 'sensors.missing')) + '</span>' : '');
      var sel = '<select data-addr="' + esc(r.addr) + '"><option value="-1">' +
        esc(tr(t, 'sensors.unassigned')) + '</option>' +
        opts.map(function (o) {
          return '<option value="' + esc(o.i) + '"' + (o.i === r.logical ? ' selected' : '') + '>' +
            esc(o.name) + '</option>';
        }).join('') + '</select>';
      return '<tr><td class="addr">' + esc(r.addr) + badges + '</td><td>' + esc(fmtTemp(r.t)) +
        '</td><td>' + sel + '</td><td><button class="btn btn-secondary" data-act="assign" data-addr="' +
        esc(r.addr) + '">' + esc(tr(t, 'sensors.assign')) + '</button></td></tr>';
    }).join('');
    return '<div class="log-wrap"><table class="sensor-table"><thead>' + head + '</thead><tbody>' +
      body + '</tbody></table></div>';
  }

  // Unit keys unknown to the language files fall back to the raw unit.
  // Relies on the translator returning the key verbatim on a miss (App's t()
  // in app.js guarantees this; keep both in sync).
  function unitText(u, t) {
    var s = tr(t, 'unit.' + u);
    return s === 'unit.' + u ? u : s;
  }

  function labelOf(desc, lang) {
    return (lang && desc[lang]) || desc.en || desc.k;
  }

  function fmtNum(v) {
    return isNum(v) ? String(v) : '';
  }

  // The generated settings input. desc is one /api/schema item
  // {k, g, t: int|float|bool|text, en, uk, u, min, max, step, def|defText, ml, secret};
  // value is its /api/config value; secretSet says whether a SECRET is set
  // (its value is never sent). Each field keeps its original value in
  // data-orig so the page can post only changed fields.
  function field(desc, value, secretSet, t, lang) {
    var k = esc(desc.k);
    var id = 'f-' + k;
    var label = '<label for="' + id + '">' + esc(labelOf(desc, lang)) +
      (desc.u ? '<span class="unit">(' + esc(unitText(desc.u, t)) + ')</span>' : '') + '</label>';
    var input;
    var hint = '';
    var orig;
    if (desc.t === 'bool') {
      orig = value ? '1' : '0';
      input = '<input type="checkbox" id="' + id + '" name="' + k + '"' + (value ? ' checked' : '') + '>';
    } else if (desc.t === 'int' || desc.t === 'float') {
      orig = fmtNum(value);
      var step = isNum(desc.step) && desc.step > 0 ? desc.step : (desc.t === 'int' ? 1 : 'any');
      input = '<input type="number" id="' + id + '" name="' + k + '" value="' + esc(orig) + '"' +
        (isNum(desc.min) ? ' min="' + esc(desc.min) + '"' : '') +
        (isNum(desc.max) ? ' max="' + esc(desc.max) + '"' : '') +
        ' step="' + esc(step) + '" required>';
      if (isNum(desc.min) && isNum(desc.max)) {
        hint = tr(t, 'common.range', { min: desc.min, max: desc.max });
      }
    } else {
      var secret = !!desc.secret;
      orig = secret ? '' : (value === null || value === undefined ? '' : String(value));
      var ph = secret ? tr(t, secretSet ? 'common.unchanged' : 'common.not_set') : '';
      input = '<input type="' + (secret ? 'password' : 'text') + '" id="' + id + '" name="' + k + '" value="' +
        esc(orig) + '"' + (isNum(desc.ml) ? ' maxlength="' + esc(desc.ml) + '"' : '') +
        (ph ? ' placeholder="' + esc(ph) + '"' : '') + (secret ? ' autocomplete="new-password"' : '') + '>';
      if (secret) {
        input = '<div class="pw-wrap">' + input + '<button type="button" class="pw-show" data-act="pw" data-for="' +
          id + '">' + esc(tr(t, 'login.show')) + '</button></div>';
        hint = tr(t, 'settings.secret_hint');
      } else if (isNum(desc.ml)) {
        hint = tr(t, 'common.max_len', { n: desc.ml });
      }
    }
    return '<div class="form-group" data-key="' + k + '" data-type="' + esc(desc.t) + '" data-secret="' +
      (desc.secret ? '1' : '0') + '" data-orig="' + esc(orig) + '">' + label + input +
      (hint ? '<div class="hint">' + esc(hint) + '</div>' : '') + '<div class="field-status"></div></div>';
  }

  return {
    esc: esc,
    alarmBanner: alarmBanner,
    chip: chip,
    modeBadge: modeBadge,
    k1Bar: k1Bar,
    accumulator: accumulator,
    sensorTable: sensorTable,
    field: field,
    fmtTemp: fmtTemp,
    labelOf: labelOf
  };
})();
