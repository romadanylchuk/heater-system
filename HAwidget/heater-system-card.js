/*
 * heater-system-card — Home Assistant Lovelace card that draws the full
 * hydraulic diagram of the heater-system (boiler-room + home-heating
 * controllers) and shows live temperatures, pumps, K1 mixing valve and
 * K2 diverter states.
 *
 * No build step, no dependencies. Copy to /config/www/heater-system/ and add
 * it as a dashboard resource (see README.md in this folder).
 *
 * Card YAML:
 *   type: custom:heater-system-card
 *   # optional:
 *   title: Heating system
 *   entities:            # override only the IDs that differ in your HA
 *     t1: sensor.boiler_room_t1_temperature
 */

const HSC_VERSION = "1.1.0";

// Default entity IDs: what HA creates from the firmware's MQTT discovery
// (device "Boiler room" / "Home heating" + entity name).
const HSC_DEFAULTS = {
  // boiler-room
  t1: "sensor.boiler_room_t1_temperature",   // boiler flow
  t2: "sensor.boiler_room_t2_temperature",   // boiler return
  t3: "sensor.boiler_room_t3_temperature",   // accumulator top
  t4: "sensor.boiler_room_t4_temperature",   // accumulator middle
  t5: "sensor.boiler_room_t5_temperature",   // accumulator bottom
  t6: "sensor.boiler_room_t6_temperature",   // supply to home
  p1: "binary_sensor.boiler_room_p1",
  p2: "binary_sensor.boiler_room_p2",
  p3: "binary_sensor.boiler_room_p3",
  p3_mode: "sensor.boiler_room_p3_mode",
  energy: "sensor.boiler_room_stored_energy",
  // home-heating
  h1: "sensor.home_heating_h1_temperature",  // radiator return (K1 port 2)
  h2: "sensor.home_heating_h2_temperature",  // radiator supply after P4
  h3: "sensor.home_heating_h3_temperature",  // hot supply in (K1 port 3)
  h4: "sensor.home_heating_h4_temperature",  // DHW tank
  p4: "binary_sensor.home_heating_p4",
  k2: "sensor.home_heating_k2_mode",         // "tank" / "bypass"
  k2_relay: "binary_sensor.home_heating_k2", // fallback: ON = bypass
  k1_position: "sensor.home_heating_k1_position",
  k1_mode: "sensor.home_heating_k1_mode",
  k1_power: "binary_sensor.home_heating_k1_power",
  k1_direction: "binary_sensor.home_heating_k1_direction", // ON = opening
  h2_set: "number.home_heating_radiator_supply_setpoint_h2",
  no_need: "binary_sensor.home_heating_no_need",
};

// Alarm / warning binary sensors shown in the bottom strip when ON.
const HSC_DEFAULT_ALARMS = [
  "binary_sensor.boiler_room_alarm_overheat",
  "binary_sensor.boiler_room_alarm_t1_fail_safe",
  "binary_sensor.boiler_room_alarm_t1_t2_fail_safe",
  "binary_sensor.boiler_room_alarm_t2_fail_safe",
  "binary_sensor.boiler_room_alarm_t3_fail_safe",
  "binary_sensor.boiler_room_p3_no_flow_b1",
  "binary_sensor.boiler_room_p1_not_charging_b3",
  "binary_sensor.boiler_room_p2_no_effect_b6",
  "binary_sensor.home_heating_alarm_h1_fail_safe",
  "binary_sensor.home_heating_alarm_h2_fail_safe",
  "binary_sensor.home_heating_alarm_h3_fail_safe",
  "binary_sensor.home_heating_alarm_h4_fail_safe",
  "binary_sensor.home_heating_alarm_sensors_fail_safe",
  "binary_sensor.home_heating_p4_no_flow_h1",
];

// Keys that must resolve for the diagram to be meaningful (others optional).
const HSC_REQUIRED = ["t1", "t2", "t3", "t4", "t5", "t6", "p1", "p2", "p3",
  "h1", "h2", "h3", "h4", "p4", "k1_position"];

const BAD = new Set(["unavailable", "unknown", "none", "", undefined, null]);

function tempColor(t) {
  if (t === null || Number.isNaN(t)) return "var(--hsc-pipe-unknown)";
  // 20 °C -> blue (hue 215), 85 °C -> red (hue 0)
  const k = Math.min(1, Math.max(0, (t - 20) / 65));
  const hue = Math.round(215 - 215 * k);
  return `hsl(${hue}, 80%, 50%)`;
}

// ---------------------------------------------------------------------------
// Static SVG. Coordinates are in a 1000 x 540 viewBox; the card scales it.
// Pipes: every pipe is drawn twice — a coloured base line (temperature) and a
// dashed overlay that is animated while water actually flows in it.
// ---------------------------------------------------------------------------

// id, path, temperature key used for the colour
const HSC_PIPES = [
  ["flowA",     "M120,140 H220",                 "t1"], // boiler flow -> bypass junction
  ["flowB",     "M220,140 H290",                 "t1"], // -> accumulator top
  ["bypass",    "M220,140 V390",                 "t1"], // P2 bypass flow -> return
  ["retA",      "M290,390 H220",                 "t5"], // accumulator bottom -> P1
  ["retB",      "M220,390 H160 V310 H120",       "t2"], // -> boiler return
  ["supplyA",   "M410,130 H600",                 "t6"], // accumulator top -> P3 -> home
  ["supplyB",   "M600,130 H760 V162",            "h3"], // -> K1 port 3
  ["supplyK2",  "M600,130 V204",                 "h3"], // -> K2
  ["k2Tank",    "M600,236 V300",                 "h3"], // K2 -> DHW coil
  ["coilOut",   "M600,450 V480",                 "h4"], // DHW coil -> return
  ["k2Bypass",  "M616,220 H680 V480",            "h3"], // K2 bypass -> return
  ["mixed",     "M778,180 H930",                 "h2"], // K1 port 1 -> P4 -> radiators
  ["radRet",    "M930,300 H760 V198",            "h1"], // radiators -> K1 port 2
  ["radToMain", "M760,300 V480",                   "h1"], // excess -> return main
  ["retMain",   "M760,480 H430 V400 H410",       "h1"], // home return -> accumulator bottom
];

function pumpSvg(key, x, y, dir, label) {
  return `
  <g class="pump" id="pump-${key}" data-key="${key}" transform="translate(${x},${y})">
    <circle class="pump-body" r="15"/>
    <circle class="pump-ring" r="15"/>
    <path class="pump-arrow" d="M-6,-8 L9,0 L-6,8 Z" transform="rotate(${dir})"/>
    <text class="lbl" y="32" text-anchor="middle">${label}</text>
  </g>`;
}

function tempSvg(key, x, y, anchor = "middle") {
  return `<text class="temp" id="v-${key}" data-key="${key}" x="${x}" y="${y}"
    text-anchor="${anchor}">${key.toUpperCase()} --</text>`;
}

const HSC_SVG = `
<svg viewBox="0 0 1000 540" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="hsc-acc" x1="0" y1="0" x2="0" y2="1">
      <stop id="acc-top" offset="0.05" stop-color="#888"/>
      <stop id="acc-mid" offset="0.5" stop-color="#888"/>
      <stop id="acc-bot" offset="0.95" stop-color="#888"/>
    </linearGradient>
  </defs>

  <rect class="area" x="10" y="40" width="465" height="480" rx="14"/>
  <rect class="area" x="485" y="40" width="505" height="480" rx="14"/>
  <text class="area-title" x="24" y="30">BOILER ROOM</text>
  <text class="area-title" x="499" y="30">HOME</text>
  <text class="badge" id="b-p3mode" data-key="p3_mode" x="24" y="66">P3 mode --</text>
  <text class="badge" id="b-energy" data-key="energy" x="460" y="66" text-anchor="end">-- kWh</text>
  <text class="badge" id="b-noneed" data-key="no_need" x="499" y="66">No need --</text>
  <text class="badge" id="b-h2set" data-key="h2_set" x="975" y="66" text-anchor="end">H2 set --</text>

  <g id="pipes">
    ${HSC_PIPES.map(([id, d]) => `<path class="pipe" id="p-${id}" d="${d}"/>`).join("")}
    ${HSC_PIPES.map(([id, d]) => `<path class="flow" id="f-${id}" d="${d}"/>`).join("")}
  </g>

  <!-- boiler -->
  <rect class="vessel" x="40" y="110" width="80" height="230" rx="10"/>
  <text class="lbl big" x="80" y="215" text-anchor="middle">Boiler</text>
  <text class="lbl small" x="80" y="235" text-anchor="middle">solid fuel</text>
  ${tempSvg("t1", 166, 128)}
  ${tempSvg("t2", 80, 372)}

  <!-- accumulator -->
  <rect class="vessel acc" x="290" y="90" width="120" height="340" rx="45" fill="url(#hsc-acc)"/>
  ${tempSvg("t3", 350, 145)}
  ${tempSvg("t4", 350, 265)}
  ${tempSvg("t5", 350, 385)}
  <text class="lbl" x="350" y="452" text-anchor="middle">Accumulator 500 L</text>

  ${pumpSvg("p1", 255, 390, 180, "P1")}
  ${pumpSvg("p2", 220, 265, 90, "P2")}
  ${pumpSvg("p3", 445, 130, 0, "P3")}
  ${tempSvg("t6", 440, 103)}

  <!-- home: supply, K2, DHW tank -->
  ${tempSvg("h3", 556, 118)}
  <g class="valve" id="k2" data-key="k2" transform="translate(600,220)">
    <circle class="valve-body" r="16"/>
    <path class="k2-leg" id="k2-leg-tank" d="M0,0 V14"/>
    <path class="k2-leg" id="k2-leg-bypass" d="M0,0 H14"/>
    <path class="k2-leg in" d="M0,0 V-14"/>
  </g>
  <text class="lbl" id="k2-label" data-key="k2" x="578" y="215" text-anchor="end">K2</text>
  <text class="lbl small" id="k2-state" data-key="k2" x="578" y="232" text-anchor="end">--</text>
  <rect class="vessel dhw" id="dhw" x="550" y="300" width="100" height="150" rx="22"/>
  <path class="coil" d="M600,300 v12 h-28 v14 h56 v14 h-56 v14 h56 v14 h-56 v14 h56 v14 h-56 v14 h56 v14 h-28 v26"/>
  ${tempSvg("h4", 600, 296)}
  <text class="lbl small" x="545" y="380" text-anchor="end">DHW</text>
  <text class="lbl small" x="545" y="394" text-anchor="end">tank</text>

  <!-- home: K1 mixing valve, P4, radiators -->
  <g class="valve" id="k1" data-key="k1_position" transform="translate(760,180)">
    <path class="valve-body" d="M-18,-18 L18,-18 L0,0 Z M-18,18 L18,18 L0,0 Z M0,0 L18,-12 L18,12 Z"/>
  </g>
  <text class="lbl" id="k1-pos" data-key="k1_position" x="736" y="176" text-anchor="end">K1 --</text>
  <text class="lbl small" id="k1-move" data-key="k1_mode" x="736" y="194" text-anchor="end"></text>
  ${pumpSvg("p4", 830, 180, 0, "P4")}
  ${tempSvg("h2", 866, 165)}
  <g class="radiator" transform="translate(930,150)">
    <rect class="vessel" width="40" height="170" rx="4"/>
    <path class="fins" d="M8,10 V160 M16,10 V160 M24,10 V160 M32,10 V160"/>
  </g>
  <text class="lbl small" x="950" y="340" text-anchor="middle">Radiators</text>
  ${tempSvg("h1", 850, 318)}
  <text class="lbl small" x="560" y="500">return to boiler room</text>
</svg>`;

const HSC_CSS = `
  :host {
    --hsc-pipe-unknown: var(--disabled-text-color, #888);
    --hsc-line: var(--primary-text-color, #222);
    --hsc-muted: var(--secondary-text-color, #777);
    --hsc-on: var(--success-color, #2e7d32);
    --hsc-off: var(--disabled-text-color, #9e9e9e);
    --hsc-alarm: var(--error-color, #c62828);
    --hsc-warn: var(--warning-color, #ef6c00);
    --hsc-scale: 1;
    --hsc-temp-scale: 1;
    display: block;
  }
  ha-card { display: block; padding: 12px 12px 8px; }
  .title { font-size: 1.2em; font-weight: 500; margin: 0 4px 6px; color: var(--hsc-line); }
  svg { width: 100%; height: auto; display: block; font-family: var(--paper-font-body1_-_font-family, sans-serif); }
  .area { fill: none; stroke: var(--divider-color, #ccc); stroke-width: 1.5; stroke-dasharray: 6 4; }
  .area-title { font-size: calc(15px * var(--hsc-scale)); font-weight: 600; fill: var(--hsc-muted); letter-spacing: 1px; }
  .badge { font-size: calc(15px * var(--hsc-scale)); fill: var(--hsc-line); cursor: pointer; }
  .pipe { fill: none; stroke-width: 7; stroke-linecap: round; stroke-linejoin: round; stroke: var(--hsc-pipe-unknown); transition: stroke 1s; }
  .flow { fill: none; stroke-width: 2.5; stroke: rgba(255,255,255,0.9); stroke-dasharray: 6 14; stroke-linecap: round;
          opacity: 0; transition: opacity .4s; pointer-events: none; }
  .flow.active { opacity: 1; animation: hsc-flow 1.1s linear infinite; }
  @keyframes hsc-flow { to { stroke-dashoffset: -20; } }
  .vessel { fill: var(--secondary-background-color, #eee); stroke: var(--hsc-line); stroke-width: 2; }
  .vessel.acc { fill: url(#hsc-acc); }
  .vessel.dhw { transition: fill 1s; fill-opacity: .75; }
  .coil { fill: none; stroke: var(--hsc-line); stroke-width: 2; opacity: .6; }
  .fins { stroke: var(--hsc-muted); stroke-width: 2; }
  .lbl { font-size: calc(14px * var(--hsc-scale)); fill: var(--hsc-line); }
  .lbl.small { font-size: calc(12px * var(--hsc-scale)); fill: var(--hsc-muted); }
  .lbl.big { font-size: calc(17px * var(--hsc-scale)); font-weight: 600; }
  .temp { font-size: calc(17px * var(--hsc-temp-scale)); font-weight: 600; fill: var(--hsc-line); cursor: pointer;
          paint-order: stroke; stroke: var(--card-background-color, #fff); stroke-width: 4px; stroke-linejoin: round; }
  .temp.bad { fill: var(--hsc-alarm); }
  .pump { cursor: pointer; }
  .pump-body { fill: var(--card-background-color, #fff); stroke: var(--hsc-off); stroke-width: 2.5; }
  .pump-ring { fill: none; stroke: transparent; stroke-width: 3; stroke-dasharray: 8 6; transform-box: fill-box; transform-origin: center; }
  .pump-arrow { fill: var(--hsc-off); }
  .pump.on .pump-body { stroke: var(--hsc-on); }
  .pump.on .pump-arrow { fill: var(--hsc-on); }
  .pump.on .pump-ring { stroke: var(--hsc-on); animation: hsc-spin 1.2s linear infinite; }
  .pump.bad .pump-body { stroke: var(--hsc-alarm); stroke-dasharray: 3 3; }
  @keyframes hsc-spin { to { transform: rotate(360deg); } }
  .valve { cursor: pointer; }
  .valve-body { fill: var(--card-background-color, #fff); stroke: var(--hsc-line); stroke-width: 2; }
  #k1 .valve-body { fill: var(--secondary-background-color, #eee); }
  #k1.moving .valve-body { stroke: var(--hsc-warn); }
  .k2-leg { stroke: var(--hsc-off); stroke-width: 4; stroke-linecap: round; }
  .k2-leg.in, .k2-leg.sel { stroke: var(--hsc-on); }
  .footer { display: flex; flex-wrap: wrap; gap: 6px; margin: 6px 4px 2px; font-size: calc(13px * var(--hsc-scale)); }
  .chip { padding: 2px 10px; border-radius: 12px; cursor: pointer; }
  .chip.ok { color: var(--hsc-on); border: 1px solid var(--hsc-on); cursor: default; }
  .chip.alarm { color: #fff; background: var(--hsc-alarm); }
  .chip.missing { color: var(--hsc-warn); border: 1px dashed var(--hsc-warn); cursor: default; }
  .ver { margin-left: auto; color: var(--hsc-muted); font-size: 11px; align-self: center; }
`;

// Which pipes carry water, given the pump/valve states.
function activePipes(s) {
  const a = new Set();
  if (s.p1 || s.p2) a.add("flowA").add("retB");
  if (s.p1) a.add("flowB").add("retA");
  if (s.p2) a.add("bypass");
  if (s.p3) {
    a.add("supplyA").add("supplyK2").add("retMain");
    if (s.k2 === "tank") a.add("k2Tank").add("coilOut");
    if (s.k2 === "bypass") a.add("k2Bypass");
  }
  if (s.p4) a.add("supplyB").add("mixed").add("radRet").add("radToMain");
  return a;
}

class HeaterSystemCard extends HTMLElement {
  static _scale(v, dflt) {
    const n = parseFloat(v ?? dflt);
    return Number.isFinite(n) ? Math.min(1.6, Math.max(0.6, n)) : 1;
  }

  static getStubConfig() {
    return { type: "custom:heater-system-card" };
  }

  setConfig(config) {
    if (!config) throw new Error("heater-system-card: missing config");
    this._config = {
      title: config.title === undefined ? "Heating system" : config.title,
      show_missing: config.show_missing !== false,
      text_scale: HeaterSystemCard._scale(config.text_scale, 1),
      temp_scale: HeaterSystemCard._scale(config.temp_scale, config.text_scale ?? 1),
      entities: { ...HSC_DEFAULTS, ...(config.entities || {}) },
      alarms: Array.isArray(config.alarms) ? config.alarms : HSC_DEFAULT_ALARMS,
    };
    this._built = false;
    this._footerHtml = null;
    if (this._hass) this._render();
  }

  set hass(hass) {
    this._hass = hass;
    this._render();
  }

  getCardSize() {
    return 8;
  }

  // Sections view: take the full section width by default; the user can
  // still resize it. Older HA versions use getLayoutOptions.
  getGridOptions() {
    return { columns: "full", min_columns: 6, rows: "auto" };
  }

  getLayoutOptions() {
    return { grid_columns: "full", grid_min_columns: 4 };
  }

  // --- helpers -------------------------------------------------------------
  _id(key) { return this._config.entities[key]; }
  _st(key) {
    const id = this._id(key);
    return id && this._hass ? this._hass.states[id] : undefined;
  }
  _num(key) {
    const s = this._st(key);
    if (!s || BAD.has(s.state)) return null;
    const v = parseFloat(s.state);
    return Number.isNaN(v) ? null : v;
  }
  _on(key) {
    const s = this._st(key);
    if (!s || BAD.has(s.state)) return null;
    return s.state === "on";
  }
  _txt(key) {
    const s = this._st(key);
    return !s || BAD.has(s.state) ? null : s.state;
  }
  _q(sel) { return this.shadowRoot.querySelector(sel); }

  _build() {
    if (!this.shadowRoot) this.attachShadow({ mode: "open" });
    const title = this._config.title
      ? `<div class="title">${this._config.title}</div>` : "";
    this.shadowRoot.innerHTML = `
      <style>${HSC_CSS}</style>
      <ha-card>
        ${title}
        ${HSC_SVG}
        <div class="footer" id="footer"></div>
      </ha-card>`;
    this.shadowRoot.addEventListener("click", (ev) => {
      const el = ev.composedPath().find((n) => n.dataset && (n.dataset.key || n.dataset.entity));
      if (!el) return;
      const entityId = el.dataset.entity || this._id(el.dataset.key);
      if (!entityId || !this._hass.states[entityId]) return;
      const e = new Event("hass-more-info", { bubbles: true, composed: true });
      e.detail = { entityId };
      this.dispatchEvent(e);
    });
    this.style.setProperty("--hsc-scale", String(this._config.text_scale));
    this.style.setProperty("--hsc-temp-scale", String(this._config.temp_scale));
    this._built = true;
  }

  _render() {
    if (!this._config || !this._hass) return;
    if (!this._built) this._build();

    // temperatures
    const temps = {};
    for (const k of ["t1", "t2", "t3", "t4", "t5", "t6", "h1", "h2", "h3", "h4"]) {
      temps[k] = this._num(k);
      const el = this._q(`#v-${k}`);
      if (el) {
        el.textContent = `${k.toUpperCase()} ${temps[k] === null ? "--" : temps[k].toFixed(1) + "°"}`;
        el.classList.toggle("bad", temps[k] === null);
      }
    }

    // pumps
    const s = {};
    for (const k of ["p1", "p2", "p3", "p4"]) {
      s[k] = this._on(k);
      const g = this._q(`#pump-${k}`);
      if (g) {
        g.classList.toggle("on", s[k] === true);
        g.classList.toggle("bad", s[k] === null);
      }
    }

    // K2 diverter: mode sensor first, relay as fallback (relay ON = bypass)
    let k2 = this._txt("k2");
    if (k2 !== "tank" && k2 !== "bypass") {
      const r = this._on("k2_relay");
      k2 = r === null ? null : (r ? "bypass" : "tank");
    }
    s.k2 = k2;
    this._q("#k2-leg-tank").classList.toggle("sel", k2 === "tank");
    this._q("#k2-leg-bypass").classList.toggle("sel", k2 === "bypass");
    this._q("#k2-state").textContent = k2 === null ? "--" : k2.toUpperCase();

    // K1 mixing valve
    const pos = this._num("k1_position");
    this._q("#k1-pos").textContent = `K1 ${pos === null ? "--" : Math.round(pos) + "%"}`;
    const power = this._on("k1_power");
    const dir = this._on("k1_direction");
    const mode = this._txt("k1_mode");
    let move = mode ? mode.replace(/_/g, " ") : "";
    if (power) move = dir ? "▲ opening" : "▼ closing";
    this._q("#k1-move").textContent = move;
    this._q("#k1").classList.toggle("moving", power === true);

    // pipes: colour by temperature, animate where water flows
    const active = activePipes(s);
    for (const [id, , tkey] of HSC_PIPES) {
      this._q(`#p-${id}`).style.stroke = tempColor(temps[tkey]);
      this._q(`#f-${id}`).classList.toggle("active", active.has(id));
    }

    // accumulator stratification + DHW tank colour
    this._q("#acc-top").setAttribute("stop-color", tempColor(temps.t3));
    this._q("#acc-mid").setAttribute("stop-color", tempColor(temps.t4));
    this._q("#acc-bot").setAttribute("stop-color", tempColor(temps.t5));
    this._q("#dhw").style.fill = temps.h4 === null ? "" : tempColor(temps.h4);

    // badges
    const p3mode = this._txt("p3_mode");
    this._q("#b-p3mode").textContent = `P3 mode: ${p3mode || "--"}`;
    const energy = this._num("energy");
    this._q("#b-energy").textContent = `Stored ${energy === null ? "--" : energy.toFixed(1)} kWh`;
    const nn = this._on("no_need");
    this._q("#b-noneed").textContent = `No need: ${nn === null ? "--" : (nn ? "yes" : "no")}`;
    const h2set = this._num("h2_set");
    this._q("#b-h2set").textContent = `H2 set ${h2set === null ? "--" : h2set.toFixed(1) + "°"}`;

    this._renderFooter();
  }

  _renderFooter() {
    const footer = this._q("#footer");
    const parts = [];
    for (const id of this._config.alarms) {
      const st = this._hass.states[id];
      if (st && st.state === "on") {
        const name = (st.attributes && st.attributes.friendly_name) || id;
        parts.push(`<span class="chip alarm" data-entity="${id}">⚠ ${name}</span>`);
      }
    }
    if (parts.length === 0) parts.push(`<span class="chip ok">✓ No alarms</span>`);
    if (this._config.show_missing) {
      const missing = HSC_REQUIRED
        .map((k) => [k, this._id(k)])
        .filter(([, id]) => !id || !this._hass.states[id]);
      if (missing.length) {
        parts.push(`<span class="chip missing" title="${missing.map(([k, id]) => `${k}: ${id}`).join("\n")}">
          ${missing.length} entit${missing.length === 1 ? "y" : "ies"} not found: ${missing.map(([k]) => k).join(", ")}</span>`);
      }
    }
    parts.push(`<span class="ver">v${HSC_VERSION}</span>`);
    const html = parts.join("");
    if (this._footerHtml !== html) {
      footer.innerHTML = html;
      this._footerHtml = html;
    }
  }
}

if (!customElements.get("heater-system-card")) {
  customElements.define("heater-system-card", HeaterSystemCard);
}
window.customCards = window.customCards || [];
if (!window.customCards.some((c) => c.type === "heater-system-card")) {
  window.customCards.push({
    type: "heater-system-card",
    name: "Heater system hydraulic diagram",
    description: "Boiler room + home heating: temperatures, pumps, K1 mixing valve and K2 diverter.",
    preview: false,
  });
}
console.info(`%c HEATER-SYSTEM-CARD %c v${HSC_VERSION} `,
  "color:#fff;background:#c62828;font-weight:bold", "color:#c62828");
