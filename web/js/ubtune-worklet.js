// UBTune — browser port of the plugin engine (YIN + TD-PSOLA), runs in an AudioWorklet.
// Same algorithm as plugin/source/dsp/UBTuneEngine.h. by unique & Mbezza — UNIQUEBABY
const MIN_F = 75, MAX_F = 1050, FORMANT_RANGE = 6, FRAMES = 256, DETECT_SEC = 8;
const SCALES = [0xfff, [0,2,4,5,7,9,11], [0,2,3,5,7,8,10], [0,2,4,7,9], [0,3,5,7,10], [0,3,5,6,7,10], [0,2,3,5,7,8,11], [0,2,3,5,7,9,10]];
function scaleMask(root, s) {
  if (s === 0) return 0xfff;
  let m = 0; for (const i of SCALES[s]) m |= 1 << ((root + i) % 12); return m;
}

class Engine {
  constructor(fs, nch) {
    this.fs = fs; this.nch = nch;
    this.D = Math.max(1, Math.floor(fs / 11000)); this.fsd = fs / this.D;
    this.tauMaxD = Math.ceil(this.fsd / MIN_F) + 2; this.tauMinD = Math.max(2, Math.floor(this.fsd / MAX_F));
    this.winD = 2 * this.tauMaxD; this.hopD = Math.max(8, Math.round(0.0053 * this.fsd)); this.hopFull = this.hopD * this.D;
    this.Pmax = fs / MIN_F; this.Punv = Math.round(0.0045 * fs); this.refineWin = Math.min(Math.floor(0.022 * fs), 4096);
    this.centerDelay = Math.ceil((this.winD + this.tauMaxD) * 0.5 * this.D) + this.D * 4;
    const fFmin = Math.pow(2, -FORMANT_RANGE / 12);
    this.latency = Math.max(Math.ceil(this.Pmax * (1 + 1 / fFmin)) + 64, this.centerDelay + 2 * this.hopFull + 64);
    let need = 4 * this.latency + 8 * this.Pmax + this.refineWin + 8192, rs = 1; while (rs < need) rs <<= 1;
    this.ringSize = rs; this.mask = rs - 1;
    this.inp = []; this.out = [];
    for (let c = 0; c < nch; c++) { this.inp.push(new Float32Array(rs)); this.out.push(new Float32Array(rs)); }
    this.mono = new Float32Array(rs);
    let ds = 1; while (ds < 4 * (this.winD + this.tauMaxD) + 64) ds <<= 1; this.decSize = ds; this.dec = new Float32Array(ds);
    const taps = 8 * this.D + 1, fc = 0.5 / this.D * 0.8; this.fir = new Float32Array(taps); let sum = 0;
    for (let i = 0; i < taps; i++) { const x = i - (taps - 1) / 2; const s = x === 0 ? 2 * fc : Math.sin(2 * Math.PI * fc * x) / (Math.PI * x);
      const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (taps - 1)); this.fir[i] = s * w; sum += s * w; }
    for (let i = 0; i < taps; i++) this.fir[i] /= sum;
    this.diff = new Float32Array(this.tauMaxD + 2); this.cmnd = new Float32Array(this.tauMaxD + 2);
    this.raw = Array.from({ length: FRAMES }, () => ({ period: 0, voiced: false, rms: 0, center: 0 }));
    this.filt = Array.from({ length: FRAMES }, () => ({ period: 0, voiced: false, rms: 0, center: 0 }));
    this.n = 0; this.decN = 0; this.latestRaw = -1; this.latestFilt = -1;
    this.nextMark = 0; this.inMark = 0; this.lastHop = 200; this.wasVoiced = false; this.polarity = 1;
    this.curNote = -1; this.shift = 0; this.center = 0;
    this.p = { mode: 1, mask: scaleMask(9, 4), retuneMs: 20, humanize: 0.25, formant: 0, transpose: 0, mix: 1, outGain: 1 };
    this.t = { inputMidi: -1, targetNote: -1, correction: 0, level: 0, detect: -1, detectedKey: -1, detectedMinor: 0, detectSerial: 0 };
    this.detecting = false; this.pcHist = new Float64Array(12); this.detectVoiced = 0;
  }

  process(chs, len) {
    const C = Math.min(chs.length, this.nch), p = this.p, rm = this.mask;
    const fF = Math.pow(2, Math.max(-FORMANT_RANGE, Math.min(FORMANT_RANGE, p.formant)) / 12);
    let lvl = 0;
    for (let i = 0; i < len; i++) {
      const w = this.n & rm; let m = 0;
      for (let c = 0; c < C; c++) { this.inp[c][w] = chs[c][i]; m += chs[c][i]; }
      m /= C; this.mono[w] = m; lvl += m * m;
      if (this.n % this.D === 0) {
        let acc = 0; const f = this.fir;
        for (let k = 0; k < f.length; k++) acc += f[k] * this.mono[(this.n - k) & rm];
        this.dec[this.decN & (this.decSize - 1)] = acc; this.decN++;
        if (this.decN % this.hopD === 0 && this.decN >= this.winD + this.tauMaxD) this.analyse();
      }
      this.synthesise(C, fF);
      const r = this.n & rm, d = (this.n - this.latency) & rm;
      for (let c = 0; c < C; c++) {
        const wet = this.out[c][r]; this.out[c][r] = 0; const dry = this.inp[c][d];
        chs[c][i] = (dry + (wet - dry) * p.mix) * p.outGain;
      }
      this.n++;
    }
    if (len > 0) this.t.level = Math.sqrt(lvl / len);
  }

  analyse() {
    const len = this.winD + this.tauMaxD, start = this.decN - len, dm = this.decSize - 1, dec = this.dec;
    const X = (j) => dec[(start + j) & dm];
    let e = 0; for (let j = 0; j < len; j++) e += X(j) * X(j);
    const rms = Math.sqrt(e / len);
    const f = { period: 0, voiced: false, rms, center: this.n - this.centerDelay };
    if (rms > 0.0025) {
      const xs = new Float32Array(len); for (let j = 0; j < len; j++) xs[j] = X(j);
      for (let tau = 1; tau <= this.tauMaxD; tau++) { let s = 0; for (let j = 0; j < this.winD; j++) { const dd = xs[j] - xs[j + tau]; s += dd * dd; } this.diff[tau] = s; }
      this.cmnd[0] = 1; let run = 0;
      for (let tau = 1; tau <= this.tauMaxD; tau++) { run += this.diff[tau]; this.cmnd[tau] = run > 0 ? this.diff[tau] * tau / run : 1; }
      let best = -1;
      for (let tau = this.tauMinD; tau < this.tauMaxD; tau++) {
        if (this.cmnd[tau] < 0.18) { while (tau + 1 < this.tauMaxD && this.cmnd[tau + 1] < this.cmnd[tau]) tau++; best = tau; break; }
      }
      let voiced = best > 0;
      if (!voiced) { let mn = 1e9; for (let tau = this.tauMinD; tau < this.tauMaxD; tau++) if (this.cmnd[tau] < mn) { mn = this.cmnd[tau]; best = tau; } voiced = mn < 0.3; }
      if (voiced && best > 1 && best < this.tauMaxD) {
        const a = this.cmnd[best - 1], b = this.cmnd[best], c = this.cmnd[best + 1], den = a - 2 * b + c;
        const tau = best + (Math.abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0);
        f.period = this.refine(tau * this.D);
        f.voiced = f.period > this.fs / MAX_F * 0.9 && f.period < this.fs / MIN_F * 1.1;
      }
    }
    const k = ++this.latestRaw; this.raw[k & (FRAMES - 1)] = f;
    if (k >= 2) {
      const a = this.raw[(k - 2) & (FRAMES - 1)], b = this.raw[(k - 1) & (FRAMES - 1)], c = f;
      const o = { period: b.period, voiced: (a.voiced + b.voiced + c.voiced) >= 2, rms: b.rms, center: b.center };
      if (o.voiced) {
        const ps = []; if (a.voiced) ps.push(a.period); if (b.voiced) ps.push(b.period); if (c.voiced) ps.push(c.period);
        ps.sort((x, y) => x - y); o.period = ps.length === 3 ? ps[1] : (b.voiced ? b.period : ps[0]);
      }
      this.filt[(k - 1) & (FRAMES - 1)] = o; this.latestFilt = k - 1;
      if (o.voiced) {
        const midi = 69 + 12 * Math.log2(this.fs / o.period / 440); this.t.inputMidi = midi;
        if (this.detecting) {
          const pc = ((Math.round(midi) % 12) + 12) % 12;
          this.pcHist[pc] += Math.min(1, o.rms * 20); this.detectVoiced += this.hopFull / this.fs;
          this.t.detect = Math.min(1, this.detectVoiced / DETECT_SEC);
          if (this.detectVoiced >= DETECT_SEC) this.finishDetect();
        }
      } else this.t.inputMidi = -1;
    }
  }

  refine(est) {
    const D = this.D, lo = Math.max(2, Math.floor(est) - D - 1), hi = Math.min(Math.ceil(this.fs / MIN_F) + 4, Math.ceil(est) + D + 1);
    if (hi - lo < 2) return est;
    const W = Math.min(this.refineWin, Math.floor(3 * est)), s0 = this.n - W - hi, rm = this.mask, mono = this.mono;
    let best = 1e30, bt = lo; const vals = [];
    for (let t = lo; t <= hi && vals.length < 64; t++) {
      let s = 0; for (let j = 0; j < W; j++) { const dd = mono[(s0 + j) & rm] - mono[(s0 + j + t) & rm]; s += dd * dd; }
      vals.push(s); if (s < best) { best = s; bt = t; }
    }
    const i = bt - lo;
    if (i > 0 && i < vals.length - 1) { const a = vals[i - 1], b = vals[i], c = vals[i + 1], den = a - 2 * b + c; if (den > 1e-12) return bt + 0.5 * (a - c) / den; }
    return bt;
  }

  lookup(t) {
    if (this.latestFilt < 0) return null;
    const last = this.filt[this.latestFilt & (FRAMES - 1)];
    let back = Math.floor((last.center - t) / this.hopFull + 0.5);
    back = Math.max(0, Math.min(back, Math.min(this.latestFilt, FRAMES - 4)));
    return this.filt[(this.latestFilt - back) & (FRAMES - 1)];
  }

  finishDetect() {
    const maj = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88], mnr = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
    const h = this.pcHist;
    const corr = (prof, root) => { let mx = 0, my = 0; for (let i = 0; i < 12; i++) { mx += h[(i + root) % 12]; my += prof[i]; } mx /= 12; my /= 12;
      let sxy = 0, sxx = 0, syy = 0; for (let i = 0; i < 12; i++) { const x = h[(i + root) % 12] - mx, y = prof[i] - my; sxy += x * y; sxx += x * x; syy += y * y; }
      return sxy / Math.sqrt(sxx * syy + 1e-12); };
    let best = -2, bk = 0, bm = 0;
    for (let r = 0; r < 12; r++) { const a = corr(maj, r), b = corr(mnr, r); if (a > best) { best = a; bk = r; bm = 0; } if (b > best) { best = b; bk = r; bm = 1; } }
    this.t.detectedKey = bk; this.t.detectedMinor = bm; this.t.detectSerial++; this.detecting = false; this.t.detect = -1;
  }

  nearestAllowed(midi) {
    const base = Math.round(midi); let best = base, bd = 1e9;
    for (let o = -7; o <= 7; o++) { const c = base + o; if (this.p.mask & (1 << (((c % 12) + 12) % 12))) { const d = Math.abs(midi - c); if (d < bd) { bd = d; best = c; } } }
    return best;
  }

  updateShift(midi, dt, onset) {
    const p = this.p, cand = this.nearestAllowed(midi), prev = this.curNote;
    const hyst = p.mode === 0 ? 0.22 : p.mode === 1 ? 0.12 : 0.03;
    if (onset || this.curNote < 0 || !(p.mask & (1 << (((this.curNote % 12) + 12) % 12)))) this.curNote = cand;
    else if (cand !== this.curNote && Math.abs(midi - this.curNote) - Math.abs(midi - cand) > hyst) this.curNote = cand;
    if (onset || this.curNote !== prev || Math.abs(this.center - midi) > 1) this.center = midi;
    else this.center += (midi - this.center) * (1 - Math.exp(-dt / 0.12));
    const h = Math.max(0, Math.min(1, p.humanize)), eff = midi + (this.center - midi) * h, desired = this.curNote - eff;
    let tauR = Math.max(0, p.retuneMs) / 1000; if (p.mode === 2) tauR = Math.min(tauR, 0.004);
    if (onset || tauR <= 0.0005) this.shift = desired; else this.shift += (desired - this.shift) * (1 - Math.exp(-dt / tauR));
    return this.shift;
  }

  synthesise(C, fF) {
    if (this.nextMark < this.n - 4 * this.Pmax) this.nextMark = this.n;
    for (let guard = 0; guard < 64; guard++) {
      const target = this.nextMark - this.latency, f = this.lookup(target);
      const voiced = !!(f && f.voiced && f.period > 0), P = voiced ? f.period : this.Punv, H = P / fF;
      if (this.nextMark - H > this.n) break;
      let hop = P, src;
      if (voiced) {
        const onset = !this.wasVoiced;
        if (onset || this.inMark > target || this.inMark < target - 3 * P) this.inMark = this.peakIn(target - P, target, true);
        while (this.inMark + P <= target) this.inMark += P;
        this.inMark = this.peakIn(this.inMark - P * 0.12, this.inMark + P * 0.12, false);
        if (this.inMark > target) this.inMark -= P;
        const midi = 69 + 12 * Math.log2(this.fs / P / 440);
        const s = this.updateShift(midi, this.lastHop / this.fs, onset);
        const ratio = Math.pow(2, (s + this.p.transpose) / 12);
        hop = P / Math.max(0.25, Math.min(4, ratio)); this.lastHop = hop;
        src = this.inMark; this.wasVoiced = true; this.t.targetNote = this.curNote; this.t.correction = s;
      } else {
        src = target; if (this.wasVoiced) { this.t.targetNote = -1; this.t.correction = 0; } this.wasVoiced = false;
      }
      this.addGrain(C, src, this.nextMark, H, fF, Math.min(hop / H, 2));
      this.nextMark += hop;
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

  addGrain(C, srcC, dstC, H, fF, gain) {
    const k0 = Math.max(this.n, Math.ceil(dstC - H)), k1 = Math.floor(dstC + H), invH = 1 / H, rm = this.mask;
    for (let k = k0; k <= k1; k++) {
      const u = (k - dstC) * invH, w = (0.5 + 0.5 * Math.cos(Math.PI * u)) * gain;
      const sp = srcC + (k - dstC) * fF, fl = Math.floor(sp), fr = sp - fl, a = fl & rm, b = (fl + 1) & rm, o = k & rm;
      for (let c = 0; c < C; c++) { const x = this.inp[c]; this.out[c][o] += (x[a] + (x[b] - x[a]) * fr) * w; }
    }
  }
}

class UBTuneProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.eng = new Engine(sampleRate, 1);
    this.bypass = false; this.frame = 0;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'params') {
        const p = this.eng.p, q = m.p;
        p.mode = q.mode; p.mask = q.mask || scaleMask(q.key, q.scale); p.retuneMs = q.retune; p.humanize = q.humanize / 100;
        p.formant = q.formant; p.transpose = q.transpose; p.mix = q.mix / 100; p.outGain = Math.pow(10, (q.output || 0) / 20);
        this.bypass = !!q.bypass;
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
    const len = out[0].length;
    const buf = [out[0]];
    if (inp && inp.length) { out[0].set(inp[0]); } else out[0].fill(0);
    if (this.bypass) { this.eng.p.mix = 0; }
    this.eng.process(buf, len);
    for (let c = 1; c < out.length; c++) out[c].set(out[0]);
    if (++this.frame % 8 === 0) this.port.postMessage({ type: 'tel', t: this.eng.t });
    return true;
  }
}
registerProcessor('ubtune', UBTuneProcessor);
