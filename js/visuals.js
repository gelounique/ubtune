/* UBTune 1.1 – canvas visuals: shockwave orb, octave keys with heatmaps, mode curves. by unique & Mbezza */
(function () {
  const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const TAU = Math.PI * 2;
  const C = { lila: [168, 85, 247], deep: [109, 40, 217], blue: [59, 130, 246], sky: [125, 211, 252], white: [255, 255, 255] };
  const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  const rgba = (c, a) => `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${a})`;
  const ang = (m) => ((((m % 12) + 12) % 12) / 12) * TAU;
  function hueCol(h, bright) {
    let b;
    if (h < 0.6) b = mix(C.lila, C.deep, Math.abs(Math.sin(h * 9)) * 0.35);
    else if (h < 0.78) b = mix(C.lila, C.blue, (h - 0.6) / 0.18);
    else if (h < 0.9) b = mix(C.blue, C.sky, (h - 0.78) / 0.12);
    else b = mix(C.sky, C.lila, (h - 0.9) / 0.1);
    return mix(b, C.white, Math.max(0, bright - 0.78) * 2.2);
  }
  function fit(cv) {
    /* UBQ.s = adaptive Qualität (1 = volle Schärfe); sinkt nur, wenn ein Gerät nicht mehr flüssig hinterherkommt */
    const dpr = Math.max(0.75, Math.min(window.devicePixelRatio || 1, 2) * ((window.UBQ && window.UBQ.s) || 1)), w = cv.clientWidth, h = cv.clientHeight;
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
    return { w, h, dpr };
  }

  class Orb {
    constructor(cv, o = {}) {
      this.cv = cv; this.ctx = cv.getContext('2d'); this.o = Object.assign({ n: 1500, text: true, rFrac: 0.30 }, o);
      this.trail = document.createElement('canvas'); this.tctx = this.trail.getContext('2d');
      this.s = { level: 0, inputMidi: -1, targetNote: -1, correction: 0, key: 9, scaleName: 'PENTATONIC MINOR', mode: 1, detect: -1 };
      this.t = 0; this.E = 0; this.voiced = 0; this.spark = 0; this.corr = 0; this.waveT = 0; this.lastT = -1; this.lastV = false; this.waves = [];
      this.dust = Array.from({ length: this.o.n }, () => this.respawn({}));
      this.dust.forEach((d) => (d.life = Math.random() * d.maxLife));
      this.core = Array.from({ length: Math.round(this.o.n * 0.28) }, () => ({ a: Math.random() * TAU, r: Math.sqrt(Math.random()) * 0.62, va: (0.15 + Math.random() * 0.5) * (Math.random() < 0.5 ? -1 : 1), size: 0.6 + Math.random() * 1.6, hue: Math.random(), ph: Math.random() * TAU }));
      this.halo = Array.from({ length: Math.round(this.o.n * 0.45) }, () => ({ a: Math.random() * TAU, r: 1.18 + Math.pow(Math.random(), 0.6) * 0.42, ph: Math.random() * TAU }));
    }
    respawn(d) {
      d.a = Math.random() * TAU; d.r = 1 + (Math.random() - 0.5) * 0.1; d.va = (0.04 + Math.random() * 0.14) * (Math.random() < 0.15 ? -1 : 1);
      d.vr = 0; d.maxLife = 2 + Math.random() * 5; d.life = d.maxLife; d.size = 0.5 + Math.pow(Math.random(), 3) * 2.6; d.hue = Math.random(); d.ph = Math.random() * TAU; d.free = false; return d;
    }
    wave(str, a) { if (this.waves.length > 6) this.waves.shift(); this.waves.push({ age: 0, dur: 0.9 + 0.5 * Math.random(), str, a0: a }); }
    set(st) { Object.assign(this.s, st); }
    step(dt) {
      const s = this.s; this.t += dt;
      const tE = Math.min(1, s.level * 9); this.E += (tE - this.E) * (1 - Math.exp(-dt * (tE > this.E ? 20 : 3.5)));
      const v = s.inputMidi > 0; this.voiced += ((v ? 1 : 0) - this.voiced) * (1 - Math.exp(-dt * 10));
      this.corr += (Math.abs(s.correction || 0) - this.corr) * (1 - Math.exp(-dt * 8));
      if (v && !this.lastV) this.wave(0.8 + this.E, ang(s.inputMidi));
      if (s.targetNote >= 0 && this.lastT >= 0 && s.targetNote !== this.lastT) this.wave(s.mode === 2 ? 1.4 : s.mode === 1 ? 1 : 0.7, ang(s.targetNote));
      this.waveT -= dt;
      if (this.E > 0.55 && this.waveT <= 0) { this.wave(0.45 * this.E, Math.random() * TAU); this.waveT = 0.6; }
      if (!v && this.waveT <= -4) { this.wave(0.25, Math.random() * TAU); this.waveT = 0; }
      if (s.targetNote >= 0) this.lastT = s.targetNote; if (!v) this.lastT = -1; this.lastV = v;
      this.waves.forEach((w) => (w.age += dt)); this.waves = this.waves.filter((w) => w.age < w.dur);
      if (v) this.spark = ang(s.inputMidi);
      const speed = (s.mode === 0 ? 0.55 : s.mode === 1 ? 1 : 1.6) * (0.4 + this.E * 2), ero = 0.08 + this.E * 0.9 + this.corr * 0.5 + (s.mode === 2 ? 0.25 : 0), t = this.t;
      for (const d of this.dust) {
        d.life -= dt; if (d.life <= 0) { this.respawn(d); continue; }
        if (!d.free && Math.random() < ero * dt * 0.25) { d.free = true; d.vr = (Math.random() < 0.7 ? 1 : -0.6) * (0.05 + Math.random() * 0.25) * (1 + this.E); d.life = d.maxLife = Math.min(d.life, 0.6 + Math.random() * 1.8); }
        const n1 = Math.sin(d.a * 3 + t * 0.9 + d.ph) + Math.sin(d.a * 7 - t * 1.7);
        d.a += (d.va * speed + n1 * 0.02 * (0.3 + this.E)) * dt;
        if (d.free) { d.r += d.vr * dt; d.vr += Math.sin(d.a * 5 + t * 2 + d.ph) * 0.15 * dt; }
        else { const tg = 1 + 0.05 * Math.sin(d.a * 4 + t * 0.6 + d.ph) + 0.035 * Math.sin(d.a * 9 - t * 1.3); d.r += (tg - d.r) * (1 - Math.exp(-dt * 2)); }
        for (const w of this.waves) { const wr = 0.55 + (w.age / w.dur) * 1.25; if (Math.abs(d.r - wr) < 0.06) d.r += 0.2 * w.str * dt * (1 - w.age / w.dur); }
        if (v) { let da = Math.abs(d.a - this.spark); da = Math.min(da, TAU - da); if (da < 0.25 && !d.free && Math.random() < dt * (2 + this.E * 6)) { d.free = true; d.vr = 0.12 + Math.random() * 0.35; d.life = d.maxLife = 0.5 + Math.random(); } }
        if (d.a > TAU) d.a -= TAU; if (d.a < 0) d.a += TAU;
      }
      for (const c of this.core) { c.a += c.va * dt * (0.4 + this.E * 1.5); c.r = Math.max(0.05, Math.min(0.66, c.r + Math.sin(t * 0.7 + c.ph) * 0.02 * dt)); }
      this.render(dt);
    }
    render(dt) {
      const { dpr } = fit(this.cv), T = this.trail;
      if (T.width !== this.cv.width || T.height !== this.cv.height) { T.width = this.cv.width; T.height = this.cv.height; }
      const g = this.tctx, s = this.s, W = T.width, H = T.height, cx = W / 2, cy = H / 2, R = Math.min(W, H) * this.o.rFrac * (1 + 0.03 * this.E), robot = s.mode === 2, t = this.t;
      const keep = Math.pow(robot ? 0.8 : s.mode === 0 ? 0.93 : 0.88, dt * 60);
      g.globalCompositeOperation = 'destination-out'; g.fillStyle = `rgba(0,0,0,${1 - keep})`; g.fillRect(0, 0, W, H);
      g.globalCompositeOperation = 'lighter';
      for (const c of this.core) {
        const rr = R * c.r, b = 0.25 + 0.25 * Math.sin(t * 1.3 + c.ph) + this.E * 0.3;
        g.fillStyle = rgba(hueCol(0.62 + c.hue * 0.38, b), 0.08 + 0.12 * b); const sz = c.size * dpr;
        g.fillRect(cx + Math.sin(c.a) * rr, cy - Math.cos(c.a) * rr, sz, sz);
      }
      for (const d of this.dust) {
        const lk = Math.max(0, Math.min(1, d.life / d.maxLife)); let rr = R * d.r;
        if (robot) rr = R + Math.round((rr - R) / (R * 0.025)) * R * 0.025;
        const x = cx + Math.sin(d.a) * rr, y = cy - Math.cos(d.a) * rr, tw = 0.55 + 0.45 * Math.sin(t * 2.2 + d.ph);
        const br = Math.min(1, 0.3 + 0.45 * tw + this.E * 0.35 + (d.free ? 0.15 : 0));
        const al = (d.free ? 0.75 * lk : 0.55 + 0.4 * Math.min(1, lk * 3)) * (0.5 + 0.5 * br) * 0.8;
        const sz = d.size * dpr * (d.free ? 0.8 : 1) * (1 + this.E * 0.4);
        g.fillStyle = rgba(hueCol(d.hue, br), al);
        if (robot || d.free) g.fillRect(x - sz / 2, y - sz / 2, sz, sz); else { g.beginPath(); g.arc(x, y, sz / 2, 0, TAU); g.fill(); }
      }
      for (const wv of this.waves) {
        const k = wv.age / wv.dur, wr = R * (0.55 + k * 1.25), al = (1 - k) * (1 - k) * Math.min(1, wv.str);
        for (let i = 0; i < 200; i++) {
          const a = (i / 200) * TAU; let da = Math.abs(a - wv.a0); da = Math.min(da, TAU - da);
          const f = 0.45 + 0.55 * Math.exp(-da * da * 1.5), jag = 1 + 0.05 * Math.sin(a * 13 + wv.a0 * 5 + t * 4) + 0.03 * (Math.random() - 0.5);
          g.fillStyle = rgba(hueCol(0.82 + 0.12 * Math.sin(a * 3), 0.9), al * f * 0.7); const sz = (1.2 + 1.6 * f) * dpr;
          g.fillRect(cx + Math.sin(a) * wr * jag - sz / 2, cy - Math.cos(a) * wr * jag - sz / 2, sz, sz);
        }
      }
      if (this.voiced > 0.05) {
        const x = cx + Math.sin(this.spark) * R * 1.02, y = cy - Math.cos(this.spark) * R * 1.02, sz = (6 + this.E * 8) * dpr;
        g.fillStyle = rgba(C.sky, 0.22 * this.voiced); g.beginPath(); g.arc(x, y, sz, 0, TAU); g.fill();
        g.fillStyle = rgba(C.white, 0.9 * this.voiced); g.beginPath(); g.arc(x, y, sz * 0.3, 0, TAU); g.fill();
      }
      g.globalCompositeOperation = 'source-over';
    }
    draw() {
      const { w, h, dpr } = fit(this.cv), g = this.ctx, s = this.s, cx = w / 2, cy = h / 2, R = Math.min(w, h) * this.o.rFrac, t = this.t;
      g.setTransform(1, 0, 0, 1, 0, 0); g.clearRect(0, 0, this.cv.width, this.cv.height); g.setTransform(dpr, 0, 0, dpr, 0, 0);
      let gr = g.createRadialGradient(cx, cy, 0, cx, cy, R * 1.62);
      gr.addColorStop(0, 'rgba(255,255,255,0)'); gr.addColorStop(0.62, 'rgba(255,255,255,0.015)'); gr.addColorStop(0.86, 'rgba(255,255,255,0.05)'); gr.addColorStop(0.97, 'rgba(255,255,255,0.10)'); gr.addColorStop(1, 'rgba(255,255,255,0)');
      g.fillStyle = gr; g.beginPath(); g.arc(cx, cy, R * 1.62, 0, TAU); g.fill();
      for (const p of this.halo) {
        const tw = 0.5 + 0.5 * Math.sin(t * 1.7 + p.ph), edge = 1 - Math.abs(p.r - 1.55) * 2;
        g.fillStyle = `rgba(255,255,255,${Math.min(1, 0.05 + 0.22 * tw * Math.max(0.2, edge))})`;
        g.fillRect(cx + Math.sin(p.a) * R * p.r, cy - Math.cos(p.a) * R * p.r, 1.2, 1.2);
      }
      gr = g.createRadialGradient(cx, cy, 0, cx, cy, R * 1.35);
      gr.addColorStop(0, rgba(C.lila, 0)); gr.addColorStop(0.66, rgba(C.lila, 0.06 + 0.1 * this.E)); gr.addColorStop(0.8, rgba(C.blue, 0.04 + 0.05 * this.E)); gr.addColorStop(1, rgba(C.blue, 0));
      g.fillStyle = gr; g.beginPath(); g.arc(cx, cy, R * 1.35, 0, TAU); g.fill();
      g.drawImage(this.trail, 0, 0, w, h);
      if (s.detect >= 0) { g.strokeStyle = 'rgba(255,255,255,.9)'; g.lineWidth = 2; g.lineCap = 'round'; g.beginPath(); g.arc(cx, cy, R * 0.78, -Math.PI / 2, -Math.PI / 2 + TAU * Math.max(0.01, s.detect)); g.stroke(); }
      if (this.o.text) {
        g.textAlign = 'center'; g.textBaseline = 'middle';
        g.fillStyle = '#8b8b92'; g.font = '600 11px "JetBrains Mono", monospace'; g.fillText(s.detect >= 0 ? 'LISTENING …' : 'KEY', cx, cy - R * 0.55);
        const custom = s.scaleName === 'CUSTOM';
        g.fillStyle = '#f4f4f6'; g.font = `700 ${custom ? R * 0.26 : R * 0.64}px "Instrument Sans", sans-serif`; g.fillText(custom ? 'CUSTOM' : NOTES[s.key], cx, cy - R * 0.06);
        g.fillStyle = 'rgba(244,244,246,.75)'; g.font = '600 12px "JetBrains Mono", monospace'; g.fillText(s.scaleName, cx, cy + R * 0.36);
      }
    }
  }

  class Keys {
    constructor(cv, o = {}) {
      this.cv = cv; this.ctx = cv.getContext('2d'); this.o = Object.assign({ heat: true }, o);
      this.s = { inputMidi: -1, targetNote: -1, mask: 0xfff, key: 0, level: 0 };
      this.glow = new Float32Array(12); this.tglow = new Float32Array(12); this.hIn = new Float32Array(288); this.hOut = new Float32Array(288); this.t = 0; this.onToggle = null; this.hover = -1;
      cv.addEventListener('pointerdown', (e) => { if (!this.onToggle) return; const r = cv.getBoundingClientRect(); const pc = this.noteAt(e.clientX - r.left, e.clientY - r.top); if (pc >= 0) this.onToggle(pc); });
      cv.addEventListener('pointermove', (e) => { const r = cv.getBoundingClientRect(); this.hover = this.noteAt(e.clientX - r.left, e.clientY - r.top); });
      cv.addEventListener('pointerleave', () => (this.hover = -1));
    }
    static black(pc) { return [1, 3, 6, 8, 10].includes(pc); }
    area() { const top = this.o.heat ? 44 : 0; return { x: 0, y: top, w: this.cv.clientWidth, h: this.cv.clientHeight - top }; }
    rect(pc) {
      const a = this.area(), ww = a.w / 7, wi = [0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6];
      if (!Keys.black(pc)) return { x: a.x + wi[pc] * ww, y: a.y, w: ww, h: a.h };
      const bw = ww * 0.6; return { x: a.x + wi[pc - 1] * ww + ww - bw / 2, y: a.y, w: bw, h: a.h * 0.6 };
    }
    noteAt(x, y) {
      const inR = (r) => x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
      for (let pc = 0; pc < 12; pc++) if (Keys.black(pc) && inR(this.rect(pc))) return pc;
      for (let pc = 0; pc < 12; pc++) if (!Keys.black(pc) && inR(this.rect(pc))) return pc;
      return -1;
    }
    xFor(pc) {
      pc = ((pc % 12) + 12) % 12; const p0 = Math.floor(pc), f = pc - p0, a = this.area(), r0 = this.rect(p0);
      const x0 = r0.x + r0.w / 2, x1 = p0 === 11 ? a.x + a.w + this.rect(0).w / 2 : this.rect(p0 + 1).x + this.rect(p0 + 1).w / 2;
      let x = x0 + (x1 - x0) * f; if (x > a.x + a.w) x -= a.w; return x;
    }
    set(st) { Object.assign(this.s, st); }
    step(dt) {
      this.t += dt; const s = this.s, sung = s.inputMidi > 0 ? ((Math.round(s.inputMidi) % 12) + 12) % 12 : -1, tpc = s.targetNote >= 0 ? s.targetNote % 12 : -1;
      for (let i = 0; i < 12; i++) {
        const a = i === sung ? 1 : 0; this.glow[i] += (a - this.glow[i]) * (1 - Math.exp(-dt * (a > this.glow[i] ? 30 : 4)));
        const b = i === tpc ? 1 : 0; this.tglow[i] += (b - this.tglow[i]) * (1 - Math.exp(-dt * (b > this.tglow[i] ? 30 : 5)));
      }
      const dec = Math.exp(-dt * 1.6), w = this.cv.clientWidth, e = Math.min(1, 0.35 + (s.level || 0) * 8);
      for (let b = 0; b < 288; b++) { this.hIn[b] *= dec; this.hOut[b] *= dec; }
      const splat = (h, x, amt, sp) => { const c = (x / w) * 288; for (let b = Math.floor(c - sp * 3); b <= c + sp * 3; b++) { const bb = ((b % 288) + 288) % 288, d = (b - c) / sp; h[bb] = Math.min(1, h[bb] + amt * Math.exp(-d * d) * dt * 6); } };
      if (w > 0 && this.o.heat) {
        if (s.inputMidi > 0) splat(this.hIn, this.xFor(s.inputMidi), e, 2.5);
        if (s.targetNote >= 0) { const r = this.rect(s.targetNote % 12); splat(this.hOut, r.x + r.w / 2, e, 1.6); }
      }
    }
    draw() {
      const { w, h, dpr } = fit(this.cv), g = this.ctx, s = this.s; g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
      const rr = (r, rad) => { g.beginPath(); g.roundRect ? g.roundRect(r.x, r.y, r.w, r.h, rad) : g.rect(r.x, r.y, r.w, r.h); };
      if (this.o.heat) {
        [this.hIn, this.hOut].forEach((hm, row) => {
          const y = row * 20; g.fillStyle = 'rgba(255,255,255,.04)'; rr({ x: 0, y, w, h: 16 }, 4); g.fill();
          const bw = w / 288;
          for (let b = 0; b < 288; b++) {
            const v = hm[b]; if (v < 0.01) continue; const n = 0.75 + 0.25 * Math.sin(b * 12.9898 + this.t * 7);
            const hh = 16 * (row === 0 ? Math.max(0.3, Math.min(1, v * 1.3)) : 1);
            g.fillStyle = `rgba(255,255,255,${Math.min(1, v * n)})`; g.fillRect(b * bw, y + 8 - hh / 2, bw + 0.6, hh);
          }
          g.fillStyle = 'rgba(139,139,146,.85)'; g.font = '600 9px "JetBrains Mono", monospace'; g.textAlign = 'left'; g.textBaseline = 'middle'; g.fillText(row ? 'OUT' : 'IN', 8, y + 8);
        });
      }
      const key = (pc) => {
        const r0 = this.rect(pc), bl = Keys.black(pc), inS = (s.mask >> pc) & 1, gl = this.glow[pc], tg = this.tglow[pc];
        const r = bl ? r0 : { x: r0.x + 1.5, y: r0.y, w: r0.w - 3, h: r0.h };
        let base = bl ? (inS ? [58, 58, 65] : [12, 12, 14]) : (inS ? [92, 92, 101] : [25, 25, 28]);
        if (pc === this.hover) base = base.map((v) => v + 25);
        const lg = g.createLinearGradient(0, r.y, 0, r.y + r.h); lg.addColorStop(0, rgba(base.map((v) => v * 1.1), 1)); lg.addColorStop(1, rgba(base.map((v) => v * 0.75), 1));
        g.fillStyle = lg; rr(r, 6); g.fill();
        if (gl > 0.01) { const gg = g.createLinearGradient(0, r.y + r.h, 0, r.y); gg.addColorStop(0, `rgba(255,255,255,${0.95 * gl})`); gg.addColorStop(1, `rgba(255,255,255,${0.25 * gl})`); g.fillStyle = gg; rr(r, 6); g.fill(); }
        if (tg > 0.01) { g.fillStyle = `rgba(255,255,255,${tg})`; g.fillRect(r.x + 4, r.y + 34, r.w - 8, 3); }
        g.strokeStyle = 'rgba(0,0,0,.6)'; g.lineWidth = 1; rr(r, 6); g.stroke();
        g.fillStyle = gl > 0.5 ? '#050506' : inS ? '#f4f4f6' : '#6b6b72'; g.font = `700 ${pc === s.key ? 16 : 14}px "Instrument Sans", sans-serif`; g.textAlign = 'center'; g.textBaseline = 'middle';
        g.fillText(NOTES[pc], r.x + r.w / 2, r.y + 15);
        if (pc === s.key && inS) { g.beginPath(); g.arc(r.x + r.w / 2, r.y + 27, 2.5, 0, TAU); g.fill(); }
      };
      for (let pc = 0; pc < 12; pc++) if (!Keys.black(pc)) key(pc);
      for (let pc = 0; pc < 12; pc++) if (Keys.black(pc)) key(pc);
    }
  }

  function drawCurve(cv, mode, phase) {
    const { w, h, dpr } = fit(cv), g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
    const notes = [57, 60, 62, 64], lo = 55.5, hi = 65.5, y = (m) => h - ((m - lo) / (hi - lo)) * h;
    g.font = '9px "JetBrains Mono", monospace'; g.textAlign = 'left';
    for (const n of notes) { g.strokeStyle = 'rgba(255,255,255,.10)'; g.setLineDash([3, 5]); g.beginPath(); g.moveTo(26, y(n)); g.lineTo(w, y(n)); g.stroke(); g.setLineDash([]); g.fillStyle = '#6b6b72'; g.fillText(NOTES[n % 12], 6, y(n) + 3); }
    const N = 260, sung = [], seq = [[57.35, 0], [60.4, 0.27], [62.3, 0.52], [63.7, 0.76]]; let cur = seq[0][0];
    for (let i = 0; i < N; i++) { const t = i / N; let tg = seq[0][0]; for (const s of seq) if (t >= s[1]) tg = s[0]; cur += (tg - cur) * 0.09; sung.push(cur + 0.22 * Math.sin(t * 46)); }
    const allowed = (m) => notes.reduce((b, n) => (Math.abs(n - m) < Math.abs(b - m) ? n : b), notes[0]);
    const out = []; let shift = 0, center = sung[0], note = allowed(sung[0]); const tau = [0.16, 0.05, 0][mode], hum = [0.6, 0.25, 0][mode], hy = [0.22, 0.12, 0.03][mode];
    for (let i = 0; i < N; i++) {
      const m = sung[i], c = allowed(m); if (c !== note && Math.abs(m - note) - Math.abs(m - c) > hy) { note = c; center = m; }
      center += (m - center) * 0.08; const desired = note - (m + (center - m) * hum);
      shift = tau === 0 ? desired : shift + (desired - shift) * (1 - Math.exp(-((1 / N) * 3.2) / tau)); out.push(m + shift);
    }
    const x = (i) => 26 + (i / (N - 1)) * (w - 34);
    g.lineWidth = 1.5; g.strokeStyle = 'rgba(255,255,255,.22)'; g.beginPath(); sung.forEach((m, i) => (i ? g.lineTo(x(i), y(m)) : g.moveTo(x(i), y(m)))); g.stroke();
    g.lineWidth = 2.6; g.strokeStyle = '#fff'; g.beginPath(); out.forEach((m, i) => (i ? g.lineTo(x(i), y(m)) : g.moveTo(x(i), y(m)))); g.stroke();
    const pi = Math.floor(phase * (N - 1)); g.fillStyle = 'rgba(168,85,247,.45)'; g.beginPath(); g.arc(x(pi), y(out[pi]), 9, 0, TAU); g.fill();
    g.fillStyle = '#fff'; g.beginPath(); g.arc(x(pi), y(out[pi]), 3.5, 0, TAU); g.fill();
    g.fillStyle = '#6b6b72'; g.textAlign = 'right'; g.fillText('— gesungen   — UBTune', w - 8, 13);
  }

  window.UBV = { Orb, Keys, drawCurve, NOTES };
})();
