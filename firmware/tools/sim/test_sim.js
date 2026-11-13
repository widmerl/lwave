// Node test of the simulator core: boots the WebAssembly build, plays a key,
// checks sound, console output and the MON monitor line.
//   node tools/sim/test_sim.js path/to/lwave_sim.wasm
const fs = require('fs');
const path = require('path');
const { simCoreFactory } = require(path.join(__dirname, 'sim_core.js'));

const SimCore = simCoreFactory();
const wasm = fs.readFileSync(process.argv[2] || path.join(__dirname, 'lwave_sim.wasm'));
let failures = 0;
const check = (cond, msg) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${msg}`); if (!cond) failures++; };

const core = SimCore.createSync(new WebAssembly.Module(wasm));
const sr = 48000;
core.init(sr);
// Neutral panel: saw up, master up, short attack, full sustain.
const pots = new Array(30).fill(0);
pots[4] = 0.0; pots[5] = 0.47; pots[6] = 1.0; pots[7] = 0.4;  // ADSR: 10 ms, 0.25 s, 100 %, 0.16 s
pots[13] = 0.8; pots[16] = 0.5; pots[18] = 0.5; pots[20] = 0.5;  // filter freq, res, tune, pwm
pots[22] = 0.9; pots[25] = 0.8;                                // volume, saw
pots.forEach((v, i) => core.handle({ t: 'pot', i, v }));
[6, 7, 3, 5].forEach((p, i) => core.handle({ t: 'sw', i, p }));  // pitch, sine, octave 4, LP12

const peakOf = (seconds) => {
  let peak = 0;
  for (let i = 0; i < (seconds * sr) / 128; i++) {
    const b = core.render(128);
    for (let j = 0; j < 128; j++) peak = Math.max(peak, Math.abs(b[j]));
  }
  return peak;
};

core.takeOutput();
check(peakOf(0.2) < 1e-6, 'silent before any key');
const d5 = 28;  // key D5 = C4
check(core.keyNote(d5) === 60, 'key 28 is D5 = MIDI 60');
core.handle({ t: 'key', k: d5, down: true, v: 0.9 });
const playing = peakOf(0.3);
let st = core.state();
check(playing > 0.02, `a pressed key sounds (peak ${playing.toFixed(3)})`);
check(st.on[d5] === 1 && st.voices === 1, 'note on and one voice');
check(st.travel[d5] > 0.99, 'key travel reached the bottom');
check(st.leds[0] === 1, 'meter LED 0 lit');
core.handle({ t: 'key', k: d5, down: false });
peakOf(1.5);
st = core.state();
check(st.voices === 0 && st.on[d5] === 0, 'released and silent again');

// Velocity: a soft press must be quieter than a hard one.
core.handle({ t: 'key', k: d5, down: true, v: 1.0 });
const hard = peakOf(0.3);
core.handle({ t: 'key', k: d5, down: false }); peakOf(1.5);
core.handle({ t: 'key', k: d5, down: true, v: 0.0 });
const soft = peakOf(0.6);
core.handle({ t: 'key', k: d5, down: false }); peakOf(1.5);
check(soft < hard * 0.3, `soft press quieter (${(20 * Math.log10(soft / hard)).toFixed(1)} dB)`);

// Console and monitor stream.
core.takeOutput();
core.line('info');
const info = core.takeOutput();
check(info.includes('browser simulator') && info.includes('max_delay='), 'info answers');
core.line('m');
core.handle({ t: 'key', k: d5, down: true, v: 0.8 });
peakOf(0.2);
const out = core.takeOutput();
const mon = out.split('\n').filter((l) => l.startsWith('MON '));
check(mon.length >= 5, `MON lines streamed (${mon.length} in 0.2 s)`);
const f = Object.fromEntries(mon[mon.length - 1].trim().split(' ').slice(1).map((t) => t.split('=')));
check(f.n[d5] === '1' && f.k.substr(2 * d5, 2) === 'ff', 'MON shows D5 down and on');
check(parseInt(f.p.substr(3 * 22, 3), 16) > 3600, 'MON shows master volume near the top');
check(f.s === '7846', `MON switch positions (${f.s})`);
check(out.includes('EV on  D5'), 'event log reports the note');

// Labels use the firmware curves.
check(Math.abs(core.mapValue(4, 0.5) - 0.3162) < 1e-3, 'attack centre = 0.32 s');
check(Math.abs(core.mapValue(4, 0) - 0.01) < 1e-4, 'attack minimum = 10 ms');
check(Math.abs(core.mapValue(0, 1) - 10) < 1e-3, 'LFO period maximum = 10 s');
check(Math.abs(core.mapValue(13, 0.5) - 640) < 0.5, 'filter centre = 640 Hz');

console.log(failures ? `\n${failures} FAILED` : '\nall passed');
process.exit(failures ? 1 : 0);
