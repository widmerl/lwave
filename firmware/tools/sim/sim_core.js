// L-wave simulator — JavaScript wrapper around lwave_sim.wasm.
// Written as a factory function so the page can embed the same code in the
// AudioWorklet (via factory.toString()) and on the main thread.
function simCoreFactory() {
  class SimCore {
    /** Wraps an instance of lwave_sim.wasm (use createSync or create). */
    constructor(instance) {
      this.inst = instance;
      this.x = instance.exports;
      this.x._initialize();
    }
    /** Stub WASI imports (the core does no I/O). */
    static importsFor(module) {
      const imports = {};
      for (const imp of WebAssembly.Module.imports(module)) {
        imports[imp.module] = imports[imp.module] || {};
        imports[imp.module][imp.name] = () => 0;
      }
      return imports;
    }
    /** Synchronous instantiation (audio thread, Node). */
    static createSync(module) { return new SimCore(new WebAssembly.Instance(module, SimCore.importsFor(module))); }
    /** Asynchronous instantiation (browser main thread). */
    static async create(module) { return new SimCore(await WebAssembly.instantiate(module, SimCore.importsFor(module))); }
    mem() { return this.x.memory.buffer; }
    /** Boots the simulated synth at this sample rate. */
    init(sampleRate) { this.x.sim_init(sampleRate); }
    /** Renders n samples; returns a view into WebAssembly memory (copy before the next call). */
    render(n) { return new Float32Array(this.mem(), this.x.sim_render(n), n); }
    /** Sends one console line to the firmware controller. */
    line(text) {
      const b = new Uint8Array(this.mem(), this.x.sim_line_buffer(), 128);
      const n = Math.min(text.length, 126);
      for (let i = 0; i < n; i++) b[i] = text.charCodeAt(i) & 0x7f;
      b[n] = 0;
      this.x.sim_line();
    }
    /** Returns and clears the console output collected since the last call. */
    takeOutput() {
      const n = this.x.sim_out_len();
      if (!n) return '';
      const b = new Uint8Array(this.mem(), this.x.sim_out_ptr(), n);
      let s = '';
      for (let i = 0; i < n; i += 4096) s += String.fromCharCode.apply(null, b.subarray(i, i + 4096));
      this.x.sim_out_clear();
      return s;
    }
    /** Snapshot for the page: key travel and note state, LEDs, voices, levels. */
    state() {
      const travel = new Array(48), on = new Array(48), leds = new Array(9);
      for (let k = 0; k < 48; k++) { travel[k] = this.x.sim_key_travel(k); on[k] = this.x.sim_note_on(k); }
      for (let i = 0; i < 9; i++) leds[i] = this.x.sim_led(i);
      return {
        travel, on, leds,
        voices: this.x.sim_voices(), level: this.x.sim_level_db(), limit: this.x.sim_limit_db(),
        mode: this.x.sim_mode(), octave: this.x.sim_octave_shift(),
      };
    }
    /** Applies one message from the page. */
    handle(m) {
      switch (m.t) {
        case 'pot': this.x.sim_set_pot(m.i, m.v); break;
        case 'sw': this.x.sim_set_switch(m.i, m.p); break;
        case 'key': this.x.sim_key(m.k, m.down ? 1 : 0, m.down ? this.x.sim_press_ms(m.v) : 0); break;
        case 'line': this.line(m.s); break;
        case 'bat': this.x.sim_set_battery(m.v); break;
        case 'lo': this.x.sim_set_lineout(m.on ? 1 : 0); break;
        default: break;
      }
    }
    /** Engine-unit value of pot `pot` at position `pos`, with the firmware's curves. */
    mapValue(pot, pos) { return this.x.map_value(pot, pos); }
    /** The firmware's own text for pot `pot` at position `pos`. */
    formatPot(pot, pos) {
      const p = this.x.format_pot(pot, pos), b = new Uint8Array(this.mem(), p, 48);
      let s = '';
      for (let i = 0; i < 48 && b[i]; i++) s += String.fromCharCode(b[i]);
      return s;
    }
    /** Lets labels follow a console "set …" or "defaults" line (main-thread instance). */
    labelLine(text) {
      const b = new Uint8Array(this.mem(), this.x.sim_line_buffer(), 128);
      const n = Math.min(text.length, 126);
      for (let i = 0; i < n; i++) b[i] = text.charCodeAt(i) & 0x7f;
      b[n] = 0;
      this.x.label_line();
    }
    setLabelMaxDelay(s) { this.x.set_label_max_delay(s); }
    keyNote(k) { return this.x.key_note(k); }
  }
  return SimCore;
}
if (typeof module !== 'undefined') module.exports = { simCoreFactory };
