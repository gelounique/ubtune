// UBTune 1.1 — browser port of the plugin engine (YIN + low-latency TD-PSOLA), runs in an AudioWorklet.
// Same algorithm as plugin/source/dsp/UBTuneEngine.h. by unique & Mbezza — UNIQUEBABY
const MIN_F = 70, MAX_F = 1100, FORMANT_RANGE = 6, DETECT_SEC = 8, MAX_GRAINS = 48, REPORTED_MS = 2;
const SCALES = [null, [0,2,4,5,7,9,11], [0,2,3,5,7,8,10], [0,2,4,7,9], [0,3,5,7,10], [0,3,5,6,7,10], [0,2,3,5,7,8,11], [0,2,3,5,7,9,10]];
function scaleMask(root, s) { if (!SCALES[s]) return 0xfff; let m = 0; for (const i of SCALES[s]) m |= 1 << ((root + i) % 12); return m; }

class Engine {
  constructor(fs, nch) {
    this.fs = fs; this.nch = nch;
    this.D = Math.max(1, Math.floor(fs / 11000)); this.fsd = fs / this.D;
    this.tauMaxD = Math.ceil(this.fsd / MIN_F) + 2; this.tauMinD = Math.max(2, Math.floor(this.fsd / MAX_F));
    this.winD = this.tauMaxD + 8; this.hopD = Math.max(8, Math.round(0.0027 * this.fsd));
    this.Pmax = fs / MIN_F; this.Punv = Math.round(0.004 * fs); this.refineWin = Math.min(Math.floor(0.016 * fs), 4096);
    this.latency = Math.max(1, Math.round(REPORTED_MS * 0.001 * fs));
    let need = 16 * this.Pmax + this.refineWin + 8192, rs = 1; while (rs < need) rs <<= 1;
    this.mask = rs - 1; this.inp = []; for (let c = 0; c < nch; c++) this.inp.push(new Float32Array(rs));
    this.mono = new Float32Array(rs);
    let ds = 1; while (ds < 4 * (this.winD + this.tauMaxD) + 64) ds <<= 1; this.decSize = ds; this.dec = new Float32Array(ds);
    const taps = 6 * this.D + 1, fc = 0.5 / this.D * 0.8; this.fir = new Float32Array(taps); let sum = 0;
    for (let i = 0; i < taps; i++) { const x = i - (taps - 1) / 2; const s = x === 0 ? 2 * fc : Math.sin(2 * Math.PI * fc * x) / (Math.PI * x);
      const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (taps - 1)); this.fir[i] = s * w; sum += s * w; }
    for (let i = 0; i < taps; i++) this.fir[i] /= sum;
    this.diff = new Float32Array(this.tauMaxD + 2); this.cmnd = new Float32Array(this.tauMaxD + 2);
    this.aLvl = 1 - Math.exp(-1 / (0.08 * fs)); this.aComp = 1 - Math.exp(-1 / (0.05 * fs));
    this.grains = Array.from({ length: MAX_GRAINS }, () => ({ src: 0, dst: 0, invH: 0, fF: 1, gain: 1 }));
    this.reset();
    this.p = { mode: 1, mask: scaleMask(9, 4), retuneMs: 20, humanize: 0.25, formant: 0, transpose: 0, mix: 1, outGain: 1 };
    this.t = { inputMidi: -1, targetNote: -1, correction: 0, level: 0, delayMs: 0, detect: -1, detectedKey: -1, detectedMinor: 0, detectSerial: 0 };
    this.detecting = false; this.pcHist = new Float64Array(12); this.detectVoiced = 0;
  }
  reset() {
    this.n = 0; this.decN = 0; this.raw = [{ period: 0, voiced: false, rms: 0 }, { period: 0, voiced: false, rms: 0 }, { period: 0, voiced: false, rms: 0 }]; this.nRaw = 0;
    this.cur = { period: 0, voiced: false, rms: 0 }; this.numGrains = 0;
    this.nextMark = 0; this.lastMark = 0; this.markValid = false; this.wasVoiced = false; this.polarity = 1; this.lastHop = 200;
    this.curNote = -1; this.shift = 0; this.center = 0; this.delaySm = 0; this.eIn = 0; this.eOut = 0; this.comp = 1;
  }

  process(chs, len) {
    const C = Math.min(chs.length, this.nch), p = this.p, rm = this.mask, inp = this.inp;
    const fF = Math.pow(2, Math.max(-FORMANT_RANGE, Math.min(FORMANT_RANGE, p.formant)) / 12);
    const mix = Math.max(0, Math.min(1, p.mix)); let lvl = 0; const y = new Float64Array(C);
    for (let i = 0; i < len; i++) {
      const w = this.n & rm; let m = 0;
      for (let c = 0; c < C; c++) { inp[c][w] = chs[c][i]; m += chs[c][i]; }
      m /= C; this.mono[w] = m; lvl += m * m;
      if (this.n % this.D === 0) {
        let acc = 0; const f = this.fir;
        for (let k = 0; k < f.length; k++) acc += f[k] * this.mono[(this.n - k) & rm];
        this.dec[this.decN & (this.decSize - 1)] = acc; this.decN++;
        if (this.decN % this.hopD === 0 && this.decN >= this.winD + this.tauMaxD) this.analyse();
      }
      this.trackMarks(); this.spawnGrains(fF);
      y.fill(0); const t = this.n;
      for (let g = 0; g < this.numGrains;) {
        const gr = this.grains[g], u = (t - gr.dst) * gr.invH;
        if (u >= 1) { const last = this.grains[--this.numGrains]; this.grains[this.numGrains] = gr; this.grains[g] = last; continue; }
        if (u > -1) {
          const wv = (0.5 + 0.5 * Math.cos(Math.PI * u)) * gr.gain, sp = gr.src + (t - gr.dst) * gr.fF, fl = Math.floor(sp), fr = sp - fl, a = fl & rm, b = (fl + 1) & rm;
          for (let c = 0; c < C; c++) { const x = inp[c]; y[c] += (x[a] + (x[b] - x[a]) * fr) * wv; }
        }
        g++;
      }
      let yo = 0; for (let c = 0; c < C; c++) yo += y[c]; yo /= C;
      this.eIn += (m * m - this.eIn) * this.aLvl; this.eOut += (yo * yo - this.eOut) * this.aLvl;
      const target = (this.eIn > 1e-7 && this.eOut > 1e-9) ? Math.max(0.5, Math.min(8, Math.sqrt(this.eIn / this.eOut))) : this.comp;
      this.comp += (target - this.comp) * this.aComp;
      const d = (this.n - this.latency) & rm;
      for (let c = 0; c < C; c++) { const dry = inp[c][d], wet = y[c] * this.comp; chs[c][i] = (dry + (wet - dry) * mix) * p.outGain; }
      this.n++;
    }
    if (len > 0) { this.t.level = Math.sqrt(lvl / len); this.t.delayMs = this.delaySm * 1000 / this.fs; }
  }

  analyse() {
    const len = this.winD + this.tauMaxD, start = this.decN - len, dm = this.decSize - 1, xs = new Float32Array(len);
    let e = 0; for (let j = 0; j < len; j++) { xs[j] = this.dec[(start + j) & dm]; e += xs[j] * xs[j]; }
    const rms = Math.sqrt(e / len), f = { period: 0, voiced: false, rms };
    if (rms > 0.0025) {
      for (let tau = 1; tau <= this.tauMaxD; tau++) { let s = 0; for (let j = 0; j < this.winD; j++) { const dd = xs[j] - xs[j + tau]; s += dd * dd; } this.diff[tau] = s; }
      this.cmnd[0] = 1; let run = 0;
      for (let tau = 1; tau <= this.tauMaxD; tau++) { run += this.diff[tau]; this.cmnd[tau] = run > 0 ? this.diff[tau] * tau / run : 1; }
      let best = -1;
      for (let tau = this.tauMinD; tau < this.tauMaxD; tau++) if (this.cmnd[tau] < 0.17) { while (tau + 1 < this.tauMaxD && this.cmnd[tau + 1] < this.cmnd[tau]) tau++; best = tau; break; }
      let voiced = best > 0;
      if (!voiced) { let mn = 1e9; for (let tau = this.tauMinD; tau < this.tauMaxD; tau++) if (this.cmnd[tau] < mn) { mn = this.cmnd[tau]; best = tau; } voiced = mn < 0.28; }
      if (voiced && best > 1 && best < this.tauMaxD) {
        const a = this.cmnd[best - 1], b = this.cmnd[best], c = this.cmnd[best + 1], den = a - 2 * b + c;
        f.period = this.refine((best + (Math.abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0)) * this.D);
        f.voiced = f.period > this.fs / MAX_F * 0.9 && f.period < this.fs / MIN_F * 1.1;
      }
    }
    const r = this.raw; r[0] = r[1]; r[1] = r[2]; r[2] = f; this.nRaw++;
    const vc = r[0].voiced + r[1].voiced + r[2].voiced, o = { period: f.period, voiced: this.nRaw >= 3 && vc >= 2, rms };
    if (o.voiced) {
      if (vc === 3) { const a = r[0].period, b = r[1].period, c = r[2].period; o.period = Math.max(Math.min(a, b), Math.min(Math.max(a, b), c)); }
      else o.period = f.voiced ? f.period : r[1].period;
    }
    this.cur = o;
    if (o.voiced) {
      const midi = 69 + 12 * Math.log2(this.fs / o.period / 440); this.t.inputMidi = midi;
      if (this.detecting) {
        const pc = ((Math.round(midi) % 12) + 12) % 12; this.pcHist[pc] += Math.min(1, o.rms * 20); this.detectVoiced += this.hopD * this.D / this.fs;
        this.t.detect = Math.min(1, this.detectVoiced / DETECT_SEC); if (this.detectVoiced >= DETECT_SEC) this.finishDetect();
      }
    } else this.t.inputMidi = -1;
  }

  refine(est) {
    const D = this.D, lo = Math.max(2, Math.floor(est) - D - 1), hi = Math.min(Math.ceil(this.fs / MIN_F) + 4, Math.ceil(est) + D + 1);
    if (hi - lo < 2) return est;
    const W = Math.min(this.refineWin, Math.floor(2 * est)), s0 = this.n - W - hi, rm = this.mask, mono = this.mono;
    let best = 1e30, bt = lo; const vals = [];
    for (let t = lo; t <= hi && vals.length < 64; t++) {
      let s = 0; for (let j = 0; j < W; j++) { const dd = mono[(s0 + j) & rm] - mono[(s0 + j + t) & rm]; s += dd * dd; }
      vals.push(s); if (s < best) { best = s; bt = t; }
    }
    const i = bt - lo;
    if (i > 0 && i < vals.length - 1) { const a = vals[i - 1], b = vals[i], c = vals[i + 1], den = a - 2 * b + c; if (den > 1e-12) return bt + 0.5 * (a - c) / den; }
    return bt;
  }

  finishDetect() {
    const maj = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88], mnr = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17], h = this.pcHist;
    const corr = (prof, root) => { let mx = 0, my = 0; for (let i = 0; i < 12; i++) { mx += h[(i + root) % 12]; my += prof[i]; } mx /= 12; my /= 12;
      let sxy = 0, sxx = 0, syy = 0; for (let i = 0; i < 12; i++) { const x = h[(i + root) % 12] - mx, y = prof[i] - my; sxy += x * y; sxx += x * x; syy += y * y; }
      return sxy / Math.sqrt(sxx * syy + 1e-12); };
    let best = -2, bk = 0, bm = 0;
    for (let r = 0; r < 12; r++) { const a = corr(maj, r), b = corr(mnr, r); if (a > best) { best = a; bk = r; bm = 0; } if (b > best) { best = b; bk = r; bm = 1; } }
    this.t.detectedKey = bk; this.t.detectedMinor = bm; this.t.detectSerial++; this.detecting = false; this.t.detect = -1;
  }

  trackMarks() {
    if (!this.cur.voiced) { this.markValid = false; return; }
    const P = this.cur.period, guard = Math.ceil(0.13 * P) + 1, n = this.n;
    if (!this.markValid || this.lastMark < n - 3 * P) {
      if (n < P + guard + 2) return;
      this.lastMark = this.peakIn(n - P - guard, n - guard, true); this.markValid = true; return;
    }
    while (this.lastMark + P + guard <= n) {
      const cand = this.lastMark + P; let m = this.peakIn(cand - 0.12 * P, cand + 0.12 * P, false);
      if (m <= this.lastMark + 0.5 * P) m = cand; this.lastMark = m;
    }
  }

  peakIn(a, b, choose) {
    const ia = Math.ceil(a), ib = Math.floor(b), rm = this.mask, mono = this.mono;
    if (ib <= ia) return a;
    if (choose) { let mx = -1e9, mn = 1e9; for (let t = ia; t <= ib; t++) { const v = mono[t & rm]; if (v > mx) mx = v; if (v < mn) mn = v; } this.polarity = mx >= -mn ? 1 : -1; }
    let best = -1e9, bt = ia;
    for (let t = ia; t <= ib; t++) { const v = this.polarity * mono[t & rm]; if (v > best) { best = v; bt = t; } }
    return bt;
  }

  nearestAllowed(midi) {
    const base = Math.round(midi); let best = base, bd = 1e9;
    for (let o = -7; o <= 7; o++) { const c = base + o; if (this.p.mask & (1 << (((c % 12) + 12) % 12))) { const d = Math.abs(midi - c); if (d < bd) { bd = d; best = c; } } }
    return best;
  }

  updateShift(midi, dt, onset) {
    const p = this.p, cand = this.nearestAllowed(midi), prev = this.curNote, hyst = p.mode === 0 ? 0.22 : p.mode === 1 ? 0.12 : 0.03;
    if (onset || this.curNote < 0 || !(p.mask & (1 << (((this.curNote % 12) + 12) % 12)))) this.curNote = cand;
    else if (cand !== this.curNote && Math.abs(midi - this.curNote) - Math.abs(midi - cand) > hyst) this.curNote = cand;
    if (onset || this.curNote !== prev || Math.abs(this.center - midi) > 1) this.center = midi;
    else this.center += (midi - this.center) * (1 - Math.exp(-dt / 0.12));
    const h = Math.max(0, Math.min(1, p.humanize)), desired = this.curNote - (midi + (this.center - midi) * h);
    let tauR = Math.max(0, p.retuneMs) / 1000; if (p.mode === 2) tauR = Math.min(tauR, 0.004);
    if (onset || tauR <= 0.0005) this.shift = desired; else this.shift += (desired - this.shift) * (1 - Math.exp(-dt / tauR));
    return this.shift;
  }

  spawnGrains(fF) {
    const t = this.n;
    if (this.nextMark < t - 2 * this.Pmax) this.nextMark = t;
    for (let guard = 0; guard < 16 && this.numGrains < MAX_GRAINS; guard++) {
      const voiced = this.cur.voiced && this.markValid, P = voiced ? this.cur.period : this.Punv, H = P / fF;
      if (this.nextMark - H > t) break;
      const limit = this.nextMark - H * Math.abs(fF - 1) - 3;
      let src, hop = P;
      if (voiced) {
        const onset = !this.wasVoiced; src = this.lastMark; while (src > limit) src -= P;
        const midi = 69 + 12 * Math.log2(this.fs / P / 440), s = this.updateShift(midi, this.lastHop / this.fs, onset);
        const ratio = Math.pow(2, (s + this.p.transpose) / 12); hop = P / Math.max(0.25, Math.min(4, ratio)); this.lastHop = hop;
        this.wasVoiced = true; this.t.targetNote = this.curNote; this.t.correction = s;
      } else {
        src = Math.min(limit, this.nextMark - 0.5 * this.Punv);
        if (this.wasVoiced) { this.t.targetNote = -1; this.t.correction = 0; } this.wasVoiced = false;
      }
      const g = this.grains[this.numGrains++];
      g.src = src; g.dst = this.nextMark; g.invH = 1 / H; g.fF = fF; g.gain = Math.min(hop / H, 2);
      this.delaySm += ((this.nextMark - src) - this.delaySm) * 0.05;
      this.nextMark += hop;
    }
  }
}

if (typeof AudioWorkletProcessor !== 'undefined') {
  class UBTuneProcessor extends AudioWorkletProcessor {
    constructor() {
      super();
      this.eng = new Engine(sampleRate, 1); this.bypass = false; this.frame = 0;
      this.port.onmessage = (e) => {
        const m = e.data;
        if (m.type === 'params') {
          const p = this.eng.p, q = m.p;
          p.mode = q.mode; p.mask = q.mask; p.retuneMs = q.retune; p.humanize = q.humanize / 100; p.formant = q.formant;
          p.transpose = q.transpose || 0; p.mix = q.bypass ? 0 : q.mix / 100; p.outGain = Math.pow(10, (q.output || 0) / 20);
        } else if (m.type === 'detect') {
          if (this.eng.detecting) { this.eng.detecting = false; this.eng.t.detect = -1; }
          else { this.eng.pcHist.fill(0); this.eng.detectVoiced = 0; this.eng.detecting = true; this.eng.t.detect = 0; }
        }
      };
      this.port.postMessage({ type: 'ready', latency: this.eng.latency, fs: sampleRate });
    }
    process(inputs, outputs) {
      const inp = inputs[0], out = outputs[0];
      if (!out || !out.length) return true;
      if (inp && inp.length) out[0].set(inp[0]); else out[0].fill(0);
      this.eng.process([out[0]], out[0].length);
      for (let c = 1; c < out.length; c++) out[c].set(out[0]);
      if (++this.frame % 8 === 0) this.port.postMessage({ type: 'tel', t: this.eng.t });
      return true;
    }
  }
  registerProcessor('ubtune', UBTuneProcessor);
}
if (typeof module !== 'undefined') module.exports = { Engine, scaleMask };
