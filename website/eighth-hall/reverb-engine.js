/*
 * reverb-engine.js - Eighth Hall's reverb engine, ported to JavaScript so it
 * runs live in the browser. It is a line-for-line port of Source/dsp/FdnReverb.h
 * and Source/dsp/BuildingBlocks.h (same algorithm, same constants), so what you
 * hear on the website is the plugin's actual reverb.
 *
 * The same file works three ways:
 *   - as an AudioWorklet module (modern browsers on HTTPS)
 *   - as a plain <script> for the ScriptProcessor fallback (plain HTTP / older browsers)
 *   - as a Node.js module, for testing against the C++ version
 */
(function (global) {
  "use strict";

  const TWO_PI = 2 * Math.PI;

  // ---------------------------------------------------------------- building blocks
  class DelayLine {
    constructor(maxDelaySamples) {
      let size = 1;
      while (size < maxDelaySamples + 4) size <<= 1;
      this.buf = new Float32Array(size);
      this.mask = size - 1;
      this.w = 0;
    }
    clear() { this.buf.fill(0); }
    push(x) { this.buf[this.w] = x; this.w = (this.w + 1) & this.mask; }
    read(d) { return this.buf[(this.w - d) & this.mask]; }
    readHermite(delay) {
      const whole = delay | 0, frac = delay - whole, b = this.buf, m = this.mask, w = this.w;
      const xm1 = b[(w - whole + 1) & m], x0 = b[(w - whole) & m];
      const x1 = b[(w - whole - 1) & m], x2 = b[(w - whole - 2) & m];
      const c1 = 0.5 * (x1 - xm1);
      const c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2;
      const c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
      return ((c3 * frac + c2) * frac + c1) * frac + x0;
    }
  }

  class Allpass {
    constructor(delaySamples) {
      this.delay = Math.max(1, delaySamples);
      this.line = new DelayLine(this.delay);
      this.gain = 0.5;
    }
    clear() { this.line.clear(); }
    process(x) {
      const d = this.line.read(this.delay);
      const v = x + this.gain * d;
      this.line.push(v);
      return d - this.gain * v;
    }
  }

  // 2nd-order TPT state-variable filter (Zavalishin)
  class Svf {
    constructor(highpass) { this.hp = highpass; this.ic1 = 0; this.ic2 = 0; this.setCutoff(1000, 48000); }
    setCutoff(hz, sr, q = 0.7071) {
      hz = Math.min(Math.max(hz, 10), 0.49 * sr);
      const g = Math.tan(Math.PI * hz / sr);
      this.k = 1 / q;
      this.a1 = 1 / (1 + g * (g + this.k));
      this.a2 = g * this.a1;
      this.a3 = g * this.a2;
    }
    process(x) {
      const v3 = x - this.ic2;
      const v1 = this.a1 * this.ic1 + this.a2 * v3;
      const v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
      this.ic1 = 2 * v1 - this.ic1;
      this.ic2 = 2 * v2 - this.ic2;
      return this.hp ? x - this.k * v1 - v2 : v2;
    }
    reset() { this.ic1 = this.ic2 = 0; }
  }

  class Smoother {
    constructor(sr, seconds) { this.len = Math.max(1, (sr * seconds) | 0); this.cur = 0; this.target = 0; this.step = 0; this.steps = 0; }
    snap(v) { this.cur = this.target = v; this.steps = 0; }
    set(v) {
      if (v === this.target) return;
      this.target = v; this.steps = this.len; this.step = (v - this.cur) / this.steps;
    }
    advance(n) {
      if (this.steps <= 0) return this.cur;
      if (n >= this.steps) { this.cur = this.target; this.steps = 0; }
      else { this.cur += this.step * n; this.steps -= n; }
      return this.cur;
    }
  }

  // ---------------------------------------------------------------- the engine
  const N = 8;
  const BASE_LINE_MS = [29.71, 37.13, 41.11, 43.73, 53.27, 59.93, 67.69, 79.31];
  const DIFFUSER_MS = [[4.771, 3.595, 12.73, 9.307], [4.913, 3.719, 13.11, 9.071]];
  const MIN_SIZE = 0.25, MAX_SIZE = 1.75, MAX_MOD_MS = 0.65, CONTROL_RATE = 16;
  const TAP_L = [1, 1, 1, 1, -1, -1, -1, -1];
  const TAP_R = [1, -1, 1, -1, 1, -1, 1, -1];
  const INJ_L = [1, 0, -1, 0, 1, 0, -1, 0];
  const INJ_R = [0, 1, 0, -1, 0, 1, 0, -1];

  const DEFAULTS = {
    mix: 0.30, preDelayMs: 20, size: 0.60, decaySec: 2.5, dampingHz: 7000, diffusion: 0.75,
    modRateHz: 0.6, modDepth: 0.35, lowCutHz: 120, highCutHz: 12000, width: 1.0, freeze: false
  };

  const sizeToScale = s => { s = Math.min(Math.max(s, 0), 1); return MIN_SIZE + (MAX_SIZE - MIN_SIZE) * s * s; };

  class FdnEngine {
    constructor(sampleRate) {
      const sr = this.sr = sampleRate;
      const ms = v => Math.ceil(v * 0.001 * sr);
      this.preDelay = [new DelayLine(((0.26 * sr) | 0) + 8), new DelayLine(((0.26 * sr) | 0) + 8)];
      this.diffusers = DIFFUSER_MS.map(row => row.map(v => new Allpass(ms(v))));
      const maxLineMs = BASE_LINE_MS[N - 1] * MAX_SIZE + MAX_MOD_MS + 1;
      this.lines = []; this.dampState = new Float64Array(N); this.lfoPhase = new Float64Array(N);
      for (let i = 0; i < N; i++) { this.lines.push(new DelayLine(ms(maxLineMs))); this.lfoPhase[i] = i / N; }
      this.lineDelay = new Float64Array(N); this.lineGain = new Float64Array(N);
      this.dampCoeff = 1; this.y = new Float64Array(N);
      this.lowCut = [new Svf(true), new Svf(true)];
      this.highCut = [new Svf(false), new Svf(false)];
      this.sm = { mix: new Smoother(sr, 0.05), width: new Smoother(sr, 0.05), pre: new Smoother(sr, 0.25),
                  size: new Smoother(sr, 0.40), freeze: new Smoother(sr, 0.10) };
      // Start at the defaults, but (like the C++ engine) jump straight to the
      // first parameters we're given instead of gliding to them.
      this.setParameters(DEFAULTS);
      this.initialised = false;
    }

    reset() {
      this.preDelay.forEach(d => d.clear());
      this.diffusers.forEach(r => r.forEach(a => a.clear()));
      this.lines.forEach(l => l.clear());
      this.dampState.fill(0);
      this.lowCut.concat(this.highCut).forEach(f => f.reset());
    }

    setParameters(p) {
      this.p = Object.assign({}, DEFAULTS, this.p || {}, p);
      const q = this.p, sm = this.sm, fr = q.freeze ? 1 : 0;
      if (!this.initialised) {
        sm.mix.snap(q.mix); sm.width.snap(q.width); sm.pre.snap(q.preDelayMs);
        sm.size.snap(sizeToScale(q.size)); sm.freeze.snap(fr);
        this.initialised = true;
      }
      sm.mix.set(q.mix); sm.width.set(q.width); sm.pre.set(q.preDelayMs);
      sm.size.set(sizeToScale(q.size)); sm.freeze.set(fr);
    }

    updateCoefficients(n) {
      const q = this.p, sr = this.sr, sm = this.sm;
      const freeze = sm.freeze.advance(n);
      const scale = sm.size.advance(n);
      this.mixNow = sm.mix.advance(n);
      this.widthNow = sm.width.advance(n);
      this.preNow = Math.max(0, sm.pre.advance(n) * 0.001 * sr);
      this.inGain = 1 - freeze;

      const rt60 = Math.min(Math.max(q.decaySec, 0.05), 100);
      const modSamples = q.modDepth * q.modDepth * MAX_MOD_MS * 0.001 * sr;
      const damp = 1 - Math.exp(-TWO_PI * Math.min(Math.max(q.dampingHz, 200), 0.49 * sr) / sr);
      this.dampCoeff = damp + freeze * (1 - damp);

      for (let i = 0; i < N; i++) {
        const inc = q.modRateHz * (0.85 + 0.043 * i) / sr;
        let ph = this.lfoPhase[i] + inc * n; ph -= Math.floor(ph); this.lfoPhase[i] = ph;
        const wobble = Math.sin(TWO_PI * ph) * modSamples;
        const d = Math.max(4, BASE_LINE_MS[i] * scale * 0.001 * sr + wobble);
        this.lineDelay[i] = d;
        const g = Math.pow(10, -3 * d / (rt60 * sr));
        this.lineGain[i] = g + freeze * (1 - g);
      }

      const dg = 0.72 * Math.min(Math.max(q.diffusion, 0), 1);
      this.diffusers.forEach(r => r.forEach(a => { a.gain = dg; }));
      for (let ch = 0; ch < 2; ch++) {
        this.lowCut[ch].setCutoff(q.lowCutHz, sr);
        this.highCut[ch].setCutoff(q.highCutHz, sr);
      }
    }

    // In-place stereo processing of Float32Arrays.
    process(left, right) {
      const total = left.length;
      for (let done = 0; done < total; done += CONTROL_RATE) {
        const n = Math.min(CONTROL_RATE, total - done);
        this.updateCoefficients(n);
        this.processChunk(left, right, done, n);
      }
    }

    processChunk(left, right, off, n) {
      const dry = Math.cos(0.5 * Math.PI * this.mixNow), wet = Math.sin(0.5 * Math.PI * this.mixNow);
      const halfWidth = 0.5 * this.widthNow, a = this.dampCoeff, y = this.y;
      const pd0 = this.preDelay[0], pd1 = this.preDelay[1], pre = this.preNow;
      const dL = this.diffusers[0], dR = this.diffusers[1];

      for (let s = off; s < off + n; s++) {
        const xL = left[s], xR = right[s];
        pd0.push(xL * this.inGain); pd1.push(xR * this.inGain);
        let inL = pre < 2 ? pd0.read(1) : pd0.readHermite(pre);
        let inR = pre < 2 ? pd1.read(1) : pd1.readHermite(pre);
        for (let i = 0; i < 4; i++) { inL = dL[i].process(inL); inR = dR[i].process(inR); }

        let sum = 0;
        for (let i = 0; i < N; i++) {
          const v = this.lines[i].readHermite(this.lineDelay[i]);
          this.dampState[i] += a * (v - this.dampState[i]);
          y[i] = this.dampState[i] * this.lineGain[i];
          sum += y[i];
        }
        let wL = 0, wR = 0;
        for (let i = 0; i < N; i++) { wL += TAP_L[i] * y[i]; wR += TAP_R[i] * y[i]; }

        const h = (2 / N) * sum;
        for (let i = 0; i < N; i++) this.lines[i].push(y[i] - h + INJ_L[i] * inL + INJ_R[i] * inR);

        wL = this.highCut[0].process(this.lowCut[0].process(wL * 0.35));
        wR = this.highCut[1].process(this.lowCut[1].process(wR * 0.35));
        const mid = 0.5 * (wL + wR), side = halfWidth * (wL - wR);
        left[s] = dry * xL + wet * (mid + side);
        right[s] = dry * xR + wet * (mid - side);
      }
    }
  }

  FdnEngine.DEFAULTS = DEFAULTS;

  // ---------------------------------------------------------------- AudioWorklet wrapper
  if (typeof AudioWorkletProcessor !== "undefined" && typeof registerProcessor !== "undefined") {
    class EighthHallProcessor extends AudioWorkletProcessor {
      constructor() {
        super();
        this.engine = new FdnEngine(sampleRate);
        this.bypass = false;
        this.port.onmessage = e => {
          const m = e.data;
          if (m.params) this.engine.setParameters(m.params);
          if ("bypass" in m) this.bypass = m.bypass;
          if (m.reset) this.engine.reset();
        };
      }
      process(inputs, outputs) {
        const inp = inputs[0], out = outputs[0];
        const L = out[0], R = out[1] || out[0];
        if (inp && inp.length) {
          L.set(inp[0]);
          R.set(inp[1] || inp[0]);
        } else {
          L.fill(0); R.fill(0);
        }
        if (!this.bypass) this.engine.process(L, R);
        return true;
      }
    }
    registerProcessor("eighth-hall", EighthHallProcessor);
  }

  if (typeof module !== "undefined" && module.exports) module.exports = FdnEngine;
  else global.FdnEngine = FdnEngine;
})(typeof globalThis !== "undefined" ? globalThis : this);
