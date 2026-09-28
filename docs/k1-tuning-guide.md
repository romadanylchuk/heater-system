# K1 mixing-valve tuning guide (home-heating)

This guide is for the owner of the `home-heating` controller. It explains what the K1 tuning settings do, how
to run the built-in **step test** from the web UI, how to read its result, and how to tune by hand. The
background (controller rules, sensors H1–H4) is in the README, sections "Home-heating controller (stage 08)"
and "Diagnostics & K1 tuning (stage 09)".

Quick reminder of the sensors: **H1** is the radiator return, **H2** the radiator supply after P4 (the value K1
controls, setpoint `h2Set`), **H3** the hot supply from the boiler room. K1 has no position feedback; the
position is an estimate built from the motor run time (0–100 %, ~120 s full travel).

## 1. What the tuning settings do

All of these are in the settings group **k1** (web UI → Settings, and as HA numbers).

| Setting | Default | Range | What it does |
|---------|---------|-------|--------------|
| `k1Period` | 30 s | 10–300 s | How often the feedback looks at H2. After each move the controller waits one period before judging the result. Too short → it corrects again before the previous move has shown up in H2 (swinging). Too long → slow. |
| `k1Gain` | 2 s/°C | 0.5–10 (step 0.5) | Pulse length per °C of error: `pulse = k1Gain × error` seconds, error = the absolute difference between `h2Set` and H2. Higher → stronger, faster corrections; too high → overshoot and swinging. |
| `k1Deadband` | 1 °C | 0.2–5 (step 0.1) | Errors smaller than this are ignored (no pulse). A larger deadband means fewer pulses but a looser H2. |
| `k1MaxPulse` | 10 s | 1–60 s | Upper limit of one feedback pulse, whatever the error. Protects against large jumps after a big disturbance. |
| `k1MinPulse` | 1 s | 0.5–5 (step 0.5) | Moves shorter than this are skipped (feed-forward and feedback alike). Avoids useless relay clicks. |
| `k1FfStep` | 5 % | 1–25 % | The feed-forward position (computed from H1, H3 and the setpoint) is re-applied only when it has moved by at least this much since the last feed-forward move. |
| `k1StepPulse` | 10 s | 2–30 s | Length of the OPEN pulse used by the step test (see below). Not used by normal control. |

In short, every `k1Period` seconds, if K1 is idle and `|h2Set − H2| ≥ k1Deadband`, K1 gets a pulse of
`min(k1Gain × |error|, k1MaxPulse)` seconds (OPEN when H2 is too cold, CLOSE when too hot), unless the pulse
would be shorter than `k1MinPulse`.

The two values that decide how the loop behaves are **`k1Gain`** and **`k1Period`**. The step test measures the
valve + pipe response and suggests both.

## 2. The step test

The step test gives K1 one OPEN pulse of `k1StepPulse` seconds (default 10 s) while everything else is steady,
and then watches how H2 reacts. Normal K1 control (feed-forward and feedback) is **held** during the test;
P4, K2 and everything else keep running as usual.

### When to run it

- On a steady heating evening: heating enabled, P4 running, the boiler-room supply H3 stable (not while the
  boiler is being fired up or the accumulator is running empty), and H2 already sitting close to its setpoint.
- At least **5 minutes after boot** (the test needs 5 minutes of sensor history).
- Not while an OTA update or an anti-seize exercise runs.
- K1 must have some room to open: its estimated position plus the pulse must stay below 100 %.

### How to run it

1. Log in to the web UI of the home-heating controller and open the status page. The **K1 tuning** panel
   shows the H2 error, the last K1 pulse, the pulse counters and the **K1 step test** block.
2. If the test cannot start, the panel shows the reason as a chip next to the test pulse length (table
   below). The **Start step test** button stays disabled until the reason is gone.
3. Press **Start step test**. The panel shows **running**, the elapsed time (`… / 600 s`), and the dead time
   as soon as H2 has started to move. A **Cancel test** button is shown while the test runs.
4. Wait. The test ends by itself after H2 has settled (usually 3–5 minutes, at most 10 minutes).
5. Read the **Last result** (section 3) and, if it looks right, press **Apply period … s, gain …**.

Start and Cancel go through the controller's command queue (`POST /api/project/cmd`, op 1 = start, 2 =
cancel). A second Start while a test is running or pending is answered `rejected`; Cancel with no test is
answered `unchanged`. There are no step-test controls in Home Assistant.

### Why the test cannot start (block reasons)

The first reason that applies is shown:

| Key | Panel text | What to do |
|-----|------------|------------|
| `unavailable` | diagnostics settings unavailable | the firmware could not read its diagnostics settings (see the event log); also shown for a moment after boot |
| `running` | test running | a test is already running |
| `heating_off` | heating disabled | turn `heatingEnabled` on |
| `ota` | OTA update running | wait for the update to finish |
| `p4_off` | P4 is off | wait until P4 is requested **and** its relay is actually ON (e.g. H3 must be ≥ `h2Set`) |
| `anti_seize` | anti-seize running | wait for the exercise to finish |
| `sensors` | H2/H3 not OK | one of H1, H2, H3 is not reading OK; fix the sensor first |
| `k1_mode` | K1 not in normal mode | K1 is recalibrating, waiting, closed or in a fail-safe mode; it must be in `normal` (feed-forward + feedback) |
| `k1_unknown` | K1 position unknown | wait for the boot recalibration to finish |
| `k1_busy` | K1 moving | K1 is running a pulse right now; try again in a few seconds |
| `k1_headroom` | K1 too close to fully open | position + `100 × k1StepPulse / k1Travel` must be below 100 %; lower `k1StepPulse` or wait for colder H3 / a lower demand |
| `history` | not enough history yet | fewer than 5 minutes of history (right after boot, or after a sensor fault); wait |
| `h3_unsteady` | H3 not steady | H3 moved by more than ±1 °C in the last 5 minutes; wait for a steady supply |
| `h2_unsteady` | H2 not steady | H2 moved by more than ±0.5 °C in the last 5 minutes; wait until H2 has settled |

"Steady" means: all 11 history samples of the last 5 minutes (one every 30 s) were read OK and lie within the
band around the current value (H3 ±1 °C, H2 ±0.5 °C).

A Start that was accepted but becomes blocked before the next controller tick is silently dropped; the panel
then shows the new reason.

### Why a running test stops (abort reasons)

The test is aborted, and normal K1 control resumes, when one of these happens:

| Key | Panel text | Cause |
|-----|------------|-------|
| `cancel` | cancelled | you pressed **Cancel test** |
| `heating_off` | heating disabled | `heatingEnabled` was turned off (K1 then recalibrates) |
| `ota` | OTA update | an OTA update started (all relays drop) |
| `p4_off` | P4 switched off | P4 was no longer requested or its relay went OFF |
| `anti_seize` | anti-seize | an anti-seize exercise took K1 |
| `recal` | K1 recalibration | a K1 recalibration started |
| `sensors` | sensor fault | H1, H2 or H3 stopped reading OK |
| `k1_mode` | K1 mode changed | K1 left `normal` mode |
| `h3_changed` | H3 changed | H3 moved more than 2 °C away from its value at the start; the supply was not steady enough |

A reboot also ends the test; the test and its result live in RAM only and are gone after a restart.

When the test ends (result, no response or abort), K1 control is released: the feed-forward position is
re-applied as soon as the valve is idle, and feedback continues one period later. So the test pulse is undone
automatically.

## 3. Reading the result

The test measures three things from the moment the OPEN pulse is requested:

- **Dead time** (`Td`, seconds): the time until H2 has moved by 0.5 °C from its start value. It includes the
  motor start-up, the valve's own delay and the pipe transport delay to the H2 probe. A plausible value is
  10–60 s.
- **Settled H2**: after the dead time, the test waits until H2 stays inside a 0.2 °C window for 2 minutes.
  If that never happens, the H2 value at 10 minutes is used.
- **Response** (`R`, °C/s): the settled H2 rise divided by the pulse length. Example: a 10 s pulse that raises
  H2 by 2.5 °C gives R = 0.25 °C/s.

Possible outcomes:

| Outcome | Panel | Meaning |
|---------|-------|---------|
| `result` | dead time, response °C/s, **Suggested period … s, gain …** | a usable measurement; **Apply** is offered |
| `no_response` | "no response" chip | H2 did not move by 0.5 °C within 10 minutes, or it settled at or below its start value. No suggestion. Check that P4 really runs, that H3 is well above H1, and try a longer `k1StepPulse`. |
| `aborted` | "aborted" chip + reason | see the abort table above; no suggestion |

The result stays on the panel until the next test ends or the controller restarts. Each test also writes
events to the log: "K1 step test start" (value = pulse s, aux = H2 at start), "K1 step test result" (value =
dead time s, or −1 when H2 never moved; aux = response °C/s, 0 for no response) and "K1 step test aborted"
(value = abort code, aux = elapsed s).

### How the suggestion is computed

- **Period ≈ 1.5 × dead time.** The feedback must wait long enough to see the effect of its last pulse before
  it judges again. `k1Period = round(1.5 × Td)`, limited to 10–300 s.
- **Gain = correct half of the error per period.** A feedback pulse of `k1Gain × error` seconds moves H2 by
  about `R × k1Gain × error`. The suggestion picks `k1Gain = 0.5 / R`, so each pulse removes about **half** of
  the error. The error then shrinks 1 → 0.5 → 0.25 …: a 1 °C error is at least 75 % corrected within two
  periods, and the loop does not overshoot even if the real response is up to twice the measured one. A
  "full" gain (`1 / R`) would try to remove the whole error at once and swings as soon as the valve reacts a
  bit more than measured. The gain is rounded to 0.5 and limited to 0.5–10 s/°C.

Worked example: Td = 22 s, R = 0.25 °C/s → period round(33) = 33 s, gain 0.5 / 0.25 = 2.0 s/°C. The factory
defaults (30 s, 2 s/°C) correspond to Td ≈ 20 s and R = 0.25 °C/s.

### When to Apply

**Apply period … s, gain …** writes `k1Period` first and then `k1Gain` (two normal settings writes; the
status line shows the result of each, e.g. `Clamped` if a value was limited). If the period write fails, the
gain is not written. Nothing is applied automatically.

Apply when:

- the outcome is `result`, the dead time is plausible (10–60 s) and the response is not tiny;
- H3 stayed steady during the test (otherwise the test would have aborted with `h3_changed`);
- ideally a second test under similar conditions gives a similar suggestion.

Do **not** apply blindly when the dead time is very short (< 5 s: H2 probably moved for another reason) or when
the suggestion differs a lot from a second run; repeat the test first.

**Test at high and low H3.** The response R scales with the temperature difference H3 − H1 (a hotter supply
moves H2 more per % of valve travel). Run the test once with a hot supply (e.g. H3 ≈ 80 °C) and once with a
cooler one (e.g. H3 ≈ 55 °C). If the suggested gains differ a lot, use the gain from the **hotter** case (the
smaller gain): it is stable in both cases, only a bit slower when the supply is cooler.

## 4. Manual tuning tips

Watch H2 and the H2 error in HA (history graph) for an hour or two, then change one value at a time:

| Symptom | Change |
|---------|--------|
| H2 **swings** around the setpoint (overshoots, K1 alternates OPEN/CLOSE) | lower `k1Gain` and/or a longer `k1Period` |
| H2 **slow** to reach the setpoint, many small pulses in the same direction | higher `k1Gain` |
| **High daily pulse count** (see below) | a longer `k1Period`, a larger `k1Deadband`, a lower `k1Gain` |
| Big jumps after a disturbance (e.g. the boiler room switching supply) | lower `k1MaxPulse` |
| Many tiny pulses that do nothing | raise `k1MinPulse` or `k1Deadband` |
| Feed-forward moves too often while H3 drifts slowly | raise `k1FfStep` |

Rules of thumb:

- `k1Period` should be at least about 1.5 × the dead time; shorter periods almost always swing.
- Change `k1Gain` in steps of 0.5–1 and wait at least 20–30 minutes before judging.
- A deadband of 0.5–1 °C is normal for radiators; tighter control costs many relay operations.

## 5. K1 pulse counters (relay wear)

The controller counts every time the K1 motor relay is switched on (every new K1 run, from any source:
feedback, feed-forward, recalibration, anti-seize, step test). Extending a run in the same direction is not a
new count. Each count is one switching cycle of the K1 power and direction relays, so the counters are a
relay-wear indicator.

- **K1 pulses today** counts since local midnight; at local midnight it moves to **K1 pulses yesterday**. If
  more than one day was skipped (e.g. the controller was off), yesterday reads 0.
- While the clock is not set, the counts keep accumulating in "today".
- The counters are kept **in RAM only**: after a reboot "today" starts at 0 and "yesterday" is `n/a`
  (unavailable in HA) until the first midnight.

As a guide, a well-tuned K1 in steady weather makes a few dozen to a couple of hundred pulses per day. A count
that is much higher, day after day, means the loop is too nervous (see the table above); typical relays are
rated for hundreds of thousands of operations, so a high count shortens their life.

The **last K1 pulse** (length and direction) shows the last command issued by control: feed-forward,
feedback or the step test. Recalibration and anti-seize strokes are not shown there, and the length is the
commanded length.

## 6. Home Assistant entities

Default entity IDs (device `home_heating`; adjust them if HA renamed them):

| Entity | What it shows |
|--------|---------------|
| `sensor.home_heating_h2_error` | `H2 − h2Set` in °C (1 decimal); positive = H2 above the setpoint (too warm). Unavailable while H2 is not OK or heating is disabled. |
| `sensor.home_heating_k1_position` | estimated K1 position, %; unavailable while unknown |
| `sensor.home_heating_k1_last_pulse` | length of the last control pulse, s; unavailable until the first one |
| `sensor.home_heating_k1_last_pulse_dir` | `open` / `close` / `none` (diagnostic) |
| `sensor.home_heating_k1_pulses_today` | K1 runs since local midnight (diagnostic, `total_increasing`) |
| `sensor.home_heating_k1_pulses_yesterday` | K1 runs of the previous local day (diagnostic); unavailable until the first rollover |
| `binary_sensor.home_heating_warn_p4_no_flow` | H1 diagnostic: P4 runs but H2 hardly rises above H1 (see the README) |

The tuning settings are HA numbers too (`number.home_heating_k1_period`, `number.home_heating_k1_gain`,
`number.home_heating_k1_step_pulse`, …), but the step test itself can only be started from the web UI.
