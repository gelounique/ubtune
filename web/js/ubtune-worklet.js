// UBTune 1.1.2 — browser port of the plugin engine (YIN + low-latency TD-PSOLA), runs in an AudioWorklet.
// Same algorithm as plugin/source/dsp/UBTuneEngine.h. by unique & Mbezza — UNIQUEBABY
const MIN_F = 55, MAX_F = 1100, FORMANT_RANGE = 6, DETECT_SEC = 8, MAX_GRAINS = 48, REPORTED_MS = 2, MARKS = 64;
const SCALES = [null, [0,2,4,5,7,9,11], [0,2,3,5,7,8,10], [0,2,4,7,9], [0,3,5,7,10], [0,3,5,6,7,10], [0,2,3,5,7,8,11], [0,2,3,5,7,9,10]];
function scaleMask(root, s) { if (!SCALES[s]) return 0xfff; let m = 0; for (const i of SCALES[s]) m |= 1 << ((root + i) % 12); return m; }
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const blankFrame = () => ({ period: 0, voiced: false, rms: 0 });

class Engine {
  constructor(fs, nch) {
    this.fs = fs; this.nch = nch;
    this.D = Math.max(1, Math.floor(fs / 11000)); this.fsd = fs / this.D;
    this.tauMaxD = Math.ceil(this.fsd / MIN_F) + 2; this.tauMinD = Math.max(2, Math.floor(this.fsd / MAX_F));
    this.winD = this.tauMaxD + 8; this.hopD = Math.max(8, Math.round(0.0027 * this.fsd));
    this.rmsDecay = Math.exp(-(this.hopD * this.D / fs) / 1.5);
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
    this.diff = new Float32Array(this.tauMaxD + 2); this.cmnd = new Float32Array(this.tauMaxD + 2); this.xs = new Float32Array(this.winD + this.tauMaxD);
    this.aLvl = 1 - Math.exp(-1 / (0.08 * fs)); this.aComp = 1 - Math.exp(-1 / (0.05 * fs));
    this.grains = Array.from({ length: MAX_GRAINS }, () => ({ src: 0, dst: 0, invH: 0, fF: 1, gain: 1 }));
    this.marks = new Float64Array(MARKS);
    this.reset();
    this.p = { mode: 1, mask: scaleMask(9, 4), retuneMs: 20, humanize: 0.25, formant: 0, transpose: 0, mix: 1, amount: 1, outGain: 1 };
    this.t = { inputMidi: -1, targetNote: -1, correction: 0, level: 0, delayMs: 0, detect: -1, detectedKey: -1, detectedMinor: 0, detectSerial: 0 };
    this.detecting = false; this.pcHist = new Float64Array(12); this.detectVoiced = 0;
  }
  reset() {
    this.n = 0; this.decN = 0; this.raw = [blankFrame(), blankFrame(), blankFrame(), blankFrame(), blankFrame()]; this.nRaw = 0;
    this.stablePeriod = 0; this.rmsPeak = 0; this.unvoicedFrames = 0;
    this.cur = blankFrame(); this.numGrains = 0;
    this.nextMark = 0; this.lastMark = 0; this.delayCur = 0; this.markCount = 0; this.markValid = false; this.wasVoiced = false; this.polarity = 1; this.lastHop = 200;
    this.curNote = -1; this.shift = 0; this.center = 0; this.decPitch = 0; this.pendSlow = 0; this.pendFast = 0; this.pendSlowNote = -1;
    this.slideVel = 0; this.slideAmt = 0; this.sinceSwitch = 1; this.sinceOnset = 1; this.pendRefine = 0; this.provisional = false; this.lastNote = -1; this.sliding = false;
    this.delaySm = 0; this.eIn = 0; this.eOut = 0; this.comp = 1;
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
    const len = this.winD + this.tauMaxD, start = this.decN - len, dm = this.decSize - 1, xs = this.xs, cmnd = this.cmnd;
    let e = 0; for (let j = 0; j < len; j++) { xs[j] = this.dec[(start + j) & dm]; e += xs[j] * xs[j]; }
    const rms = Math.sqrt(e / len), f = { period: 0, voiced: false, rms };
    if (rms > 0.0025) {
      for (let tau = 1; tau <= this.tauMaxD; tau++) { let s = 0; for (let j = 0; j < this.winD; j++) { const dd = xs[j] - xs[j + tau]; s += dd * dd; } this.diff[tau] = s; }
      cmnd[0] = 1; let run = 0;
      for (let tau = 1; tau <= this.tauMaxD; tau++) { run += this.diff[tau]; cmnd[tau] = run > 0 ? this.diff[tau] * tau / run : 1; }
      // candidates: local minima of the normalised difference; global minimum = periodicity
      const candT = [], candV = []; let gmin = 1e9, gtau = -1;
      for (let tau = this.tauMinD; tau < this.tauMaxD && candT.length < 32; tau++) {
        const v = cmnd[tau];
        if (v < gmin) { gmin = v; gtau = tau; }
        if (v < 0.6 && v <= cmnd[tau - 1] && v <= cmnd[tau + 1]) { candT.push(tau); candV.push(v); }
      }
      const was = this.cur.voiced;
      const voiced = gtau > 0 && gmin < (was ? 0.33 : 0.22) && rms > 0.03 * this.rmsPeak;
      let best = -1;
      if (voiced) {
        const thr = Math.max(0.12, gmin * 1.6 + 0.05);
        for (let c = 0; c < candT.length; c++) if (candV[c] < thr) { best = candT[c]; break; }
        if (best < 0) best = gtau;
        if (was && this.stablePeriod > 0) {   // continuity: stay in the octave you are singing in
          const prevT = this.stablePeriod / this.D, r = best / prevT;
          if (Math.abs(r - 2) < 0.15 || Math.abs(r - 0.5) < 0.04 || Math.abs(r - 3) < 0.2 || Math.abs(r - 1 / 3) < 0.03) {
            let alt = -1, altV = 1e9;
            for (let c = 0; c < candT.length; c++) if (Math.abs(Math.log2(candT[c] / prevT)) < 0.12 && candV[c] < altV) { alt = candT[c]; altV = candV[c]; }
            if (alt > 0 && altV < cmnd[best] + 0.10) best = alt;
          }
        }
      }
      if (voiced && best > 1 && best < this.tauMaxD) {
        const a = cmnd[best - 1], b = cmnd[best], c = cmnd[best + 1], den = a - 2 * b + c;
        f.period = this.refine((best + (Math.abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0)) * this.D);
        f.voiced = f.period > this.fs / MAX_F * 0.9 && f.period < this.fs / MIN_F * 1.1;
      }
    }
    this.rmsPeak = Math.max(rms, this.rmsPeak * this.rmsDecay);
    // smoothing: 2-of-3 voicing, period = median of the voiced frames among the last 5
    const r = this.raw; for (let k = 0; k < 4; k++) r[k] = r[k + 1]; r[4] = f; this.nRaw++;
    const vc3 = r[2].voiced + r[3].voiced + r[4].voiced, o = { period: f.period, voiced: this.nRaw >= 3 && vc3 >= 2, rms };
    if (o.voiced) {
      const pv = []; for (let k = 0; k < 5; k++) if (r[k].voiced) pv.push(r[k].period);
      pv.sort((a, b) => a - b);
      if (pv.length) o.period = pv[pv.length >> 1]; else o.voiced = false;
    }
    if (o.voiced) { this.stablePeriod = o.period; this.unvoicedFrames = 0; }
    else if (++this.unvoicedFrames > 15) this.stablePeriod = 0;
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

  // ---- pitch marks: first = waveform peak, then cross-correlated period by period ----
  pushMark(m) { this.marks[this.markCount % MARKS] = m; this.markCount++; this.lastMark = m; }
  markAt(i) { return this.marks[i % MARKS]; }
  trackMarks() {
    if (!this.cur.voiced) { this.markValid = false; return; }
    const P = this.cur.period, guard = Math.ceil(0.16 * P) + 2, n = this.n;
    if (!this.markValid || this.lastMark < n - 3 * P) {
      if (n < 2 * P + guard + 2) return;
      this.markCount = 0; this.pushMark(this.peakIn(n - P - guard, n - guard, true)); this.markValid = true; return;
    }
    while (this.lastMark + P + guard <= n) {
      const cand = this.lastMark + P; let m = this.corrMark(this.lastMark, cand, P);
      if (m <= this.lastMark + 0.6 * P || m >= this.lastMark + 1.5 * P) m = cand;
      this.pushMark(m);
    }
  }
  corrMark(prev, cand, P) {
    const L = Math.max(16, Math.min(Math.round(P), 768)), R = Math.max(2, Math.round(0.14 * P)), ip = Math.round(prev), ic = Math.round(cand), rm = this.mask, mono = this.mono;
    const step = L > 256 ? 2 : 1; let best = -1e30, bk = 0, prevV = -1e30, bestL = 0, bestR = 0;
    for (let k = -R; k <= R; k++) {
      let xy = 0, yy = 0;
      for (let j = 0; j < L; j += step) { const a = mono[(ip - L + j) & rm], b = mono[(ic + k - L + j) & rm]; xy += a * b; yy += b * b; }
      const v = xy / Math.sqrt(yy + 1e-12);
      if (v > best) { best = v; bk = k; bestL = prevV; bestR = -1e30; } else if (k === bk + 1) bestR = v;
      prevV = v;
    }
    let frac = 0;
    if (bestL > -1e29 && bestR > -1e29) { const den = bestL - 2 * best + bestR; if (Math.abs(den) > 1e-12) frac = clamp(0.5 * (bestL - bestR) / den, -0.5, 0.5); }
    return ic + bk + frac;
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

  // Capture zones with dead band, minimum note length, glide pass-through and one-time "landing" refinement.
  zoneCfg() { const m = this.p.mode; return m === 0 ? [0.090, 0.25, 0.060, 0.40, 0.035, 0.120, 12] : m === 1 ? [0.075, 0.22, 0.045, 0.40, 0.018, 0.090, 20] : [0.065, 0.20, 0.030, 0.40, 0.012, 0.060, 1e9]; }
  static past(m, from, to) { const b = 0.5 * (from + to); return to > from ? m - b : b - m; }
  setNote(c, midi, newEvent) {
    this.lastNote = this.curNote; this.curNote = c; this.decPitch = midi; this.provisional = newEvent; this.pendRefine = 0;
    this.pendSlow = this.pendFast = 0; this.pendSlowNote = -1; if (newEvent) this.sinceSwitch = 0;
  }
  decideNote(midi, dt, onset) {
    const [tauDec, margin, hold, fastMargin, fastHold, minDur, slideThr] = this.zoneCfg(), mask = this.p.mask, past = Engine.past;
    const prevDec = this.decPitch;
    if (onset) { this.decPitch = midi; this.slideVel = 0; } else this.decPitch += (midi - this.decPitch) * (1 - Math.exp(-dt / tauDec));
    if (!onset && dt > 0) this.slideVel += ((this.decPitch - prevDec) / dt - this.slideVel) * (1 - Math.exp(-dt / 0.03));
    this.sinceSwitch += dt;
    const wasSliding = this.sliding; this.sliding = Math.abs(this.slideVel) > slideThr;
    if (onset) this.sinceOnset = 0; else this.sinceOnset += dt;
    if (onset || this.curNote < 0 || !(mask & (1 << (((this.curNote % 12) + 12) % 12)))) {
      this.curNote = this.nearestAllowed(this.decPitch); this.lastNote = -1;
      this.pendSlow = this.pendFast = 0; this.pendSlowNote = -1; this.sinceSwitch = 0; this.provisional = true; this.pendRefine = 0; return;
    }
    if (this.provisional && this.sinceSwitch < 0.25 && this.sinceSwitch > 0.03 && !this.sliding) {
      const c = this.nearestAllowed(this.decPitch);
      if (c !== this.curNote && c !== this.lastNote && past(this.decPitch, this.curNote, c) > 0.15) this.pendRefine += dt; else this.pendRefine = 0;
      if (this.pendRefine >= 0.06) {
        this.lastNote = this.curNote; this.curNote = c; this.decPitch = midi; this.provisional = false; this.pendRefine = 0;
        this.pendSlow = this.pendFast = 0; this.pendSlowNote = -1; return;
      }
    } else if (this.sinceSwitch >= 0.25) this.provisional = false;
    if (wasSliding && !this.sliding) {
      const c = this.nearestAllowed(midi);
      if (c !== this.curNote && past(midi, this.curNote, c) > 0.08) { this.setNote(c, midi, true); return; }
    }
    if (this.sinceOnset < 0.08 && !this.sliding) {
      const c = this.nearestAllowed(midi);
      if (c !== this.curNote && past(midi, this.curNote, c) > 1.0) { this.setNote(c, midi, true); return; }
    }
    const cs = this.nearestAllowed(this.decPitch), cf = this.nearestAllowed(midi); let target = -1;
    const pf = cf !== this.curNote ? past(midi, this.curNote, cf) : -1;
    const canSwitch = this.sinceSwitch >= minDur && !this.sliding;
    if (pf > 2.0 && !this.sliding) target = cf;
    else if (pf > 1.0) target = cf;
    else if (pf > fastMargin) { this.pendFast += dt; if (this.pendFast >= fastHold) target = cf; }
    else this.pendFast = 0;
    if (target < 0 && cs !== this.curNote && past(this.decPitch, this.curNote, cs) > margin) {
      if (this.pendSlowNote !== cs) { this.pendSlowNote = cs; this.pendSlow = 0; }
      this.pendSlow += dt; if (this.pendSlow >= hold) target = cs;
    } else if (target < 0) { this.pendSlowNote = -1; this.pendSlow = 0; }
    if (target >= 0) {
      const leap = pf > 2.0 && past(this.decPitch, this.curNote, target) > 1.0;
      if (leap ? (this.sliding && this.sinceSwitch < 0.15) : !canSwitch) target = -1;
    }
    if (target >= 0) this.setNote(target, midi, true);
  }

  updateShift(midi, dt, onset) {
    const p = this.p, prev = this.curNote;
    this.decideNote(midi, dt, onset);
    if (onset || this.curNote !== prev || Math.abs(this.center - midi) > 1) this.center = midi;
    else this.center += (midi - this.center) * (1 - Math.exp(-dt / 0.12));
    const h = clamp(p.humanize, 0, 1), eff = midi + (this.center - midi) * h;
    this.slideAmt += ((this.sliding ? 1 : 0) - this.slideAmt) * (1 - Math.exp(-dt / (this.sliding ? 0.02 : 0.06)));
    let away = 0;
    if (p.mode !== 2) { const nn = this.nearestAllowed(midi); if (nn !== this.curNote) away = clamp(Engine.past(midi, this.curNote, nn) - 1, 0, 1); }
    const desired = (this.curNote - eff) * clamp(p.amount, 0, 1) * (1 - 0.85 * this.slideAmt) * (1 - away);
    let tauR = Math.max(0, p.retuneMs) / 1000; if (p.mode === 2) tauR = Math.min(tauR, 0.004);
    if (onset || tauR <= 0.0005) this.shift = desired; else this.shift += (desired - this.shift) * (1 - Math.exp(-dt / tauR));
    return this.shift;
  }

  // Low-latency PSOLA: every output grain takes the confirmed input mark nearest to (output time - delay),
  // with the delay held inside a one-period window -> 1:1 reproduction at 0 % correction.
  spawnGrains(fF) {
    const t = this.n;
    if (this.nextMark < t - 2 * this.Pmax) this.nextMark = t;
    for (let guard = 0; guard < 16 && this.numGrains < MAX_GRAINS; guard++) {
      const voiced = this.cur.voiced && this.markValid && this.markCount > 0, Pdet = voiced ? this.cur.period : this.Punv, H = Pdet / fF;
      if (this.nextMark - H > t) break;
      const limit = this.nextMark - H * Math.abs(fF - 1) - 3;
      let src, hop, Hs = H;
      if (voiced) {
        const onset = !this.wasVoiced, dLo = H * Math.abs(fF - 1) + 3, dHi = dLo + Pdet * 1.05 + Math.ceil(0.16 * Pdet) + H;
        const a = this.nextMark - clamp(this.delayCur, dLo, dHi), first = Math.max(0, this.markCount - MARKS + 1);
        let bi = -1, bd = 1e30;
        for (let k = this.markCount - 1; k >= first; k--) {
          const m = this.markAt(k); if (m > limit) continue;
          const dist = Math.abs(m - a); if (dist < bd) { bd = dist; bi = k; }
          if (m < a - 2 * Pdet) break;
        }
        if (bi < 0) bi = this.markCount - 1;
        src = this.markAt(bi); while (src > limit) src -= Pdet;
        let Ploc = Pdet, Pavg = Pdet;
        if (bi > 0 && bi - 1 >= first) {
          Ploc = clamp(src - this.markAt(bi - 1), 0.75 * Pdet, 1.33 * Pdet);
          const back = Math.min(this.p.mode === 2 ? 8 : 4, bi - first), avg = (src - this.markAt(bi - back)) / back;
          if (avg > 0.8 * Pdet && avg < 1.25 * Pdet) Pavg = avg;
        }
        Hs = Ploc / fF;
        const midi = 69 + 12 * Math.log2(this.fs / Pavg / 440), s = this.updateShift(midi, this.lastHop / this.fs, onset);
        const ratio = Math.pow(2, (s + this.p.transpose) / 12);
        hop = (this.p.mode === 2 ? Pavg : Ploc) / clamp(ratio, 0.25, 4); this.lastHop = hop;
        this.wasVoiced = true; this.t.targetNote = this.curNote; this.t.correction = s;
      } else {
        const dTarget = H * Math.abs(fF - 1) + 3 + 0.5 * this.Punv;
        this.delayCur += (dTarget - this.delayCur) * 0.05;
        src = Math.min(limit, this.nextMark - this.delayCur); hop = this.Punv;
        if (this.wasVoiced) { this.t.targetNote = -1; this.t.correction = 0; } this.wasVoiced = false;
      }
      this.delayCur = this.nextMark - src;
      const g = this.grains[this.numGrains++];
      g.src = src; g.dst = this.nextMark; g.invH = 1 / Hs; g.fF = fF; g.gain = Math.min(hop / Hs, 2);
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
          p.transpose = q.transpose || 0; p.amount = q.amount === undefined ? 1 : q.amount / 100; p.mix = q.bypass ? 0 : q.mix / 100; p.outGain = Math.pow(10, (q.output || 0) / 20);
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
