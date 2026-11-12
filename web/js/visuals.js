/* UBTune – shared canvas visuals: particle ring, piano, mode curves. by unique & Mbezza */
(function () {
  const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const TAU = Math.PI * 2;
  const C = { lila: [168, 85, 247], light: [210, 168, 255], deep: [109, 40, 217], mag: [232, 121, 249], white: [255, 255, 255] };
  const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  const rgba = (c, a) => `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${a})`;

  function fit(cv) {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = cv.clientWidth, h = cv.clientHeight;
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
    return { w, h, dpr };
  }

  class Ring {
    constructor(cv, o = {}) {
      this.cv = cv; this.ctx = cv.getContext('2d');
      this.o = Object.assign({ n: 700, rFrac: 0.34, wheel: true, text: true, mouse: false }, o);
      this.s = { level: 0, inputMidi: -1, targetNote: -1, correction: 0, key: 9, scaleName: 'PENTATONIC MINOR', mask: 0x4a9 | 0, mode: 1, detect: -1 };
      this.t = 0; this.energy = 0; this.voiced = 0; this.comet = 0; this.glow = new Float32Array(12); this.mx = 0; this.my = 0;
      let seed = 7; const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
      const gauss = () => { let u = 0, v = 0; while (!u) u = rnd(); v = rnd(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(TAU * v); };
      this.ps = Array.from({ length: this.o.n }, () => ({ a: rnd() * TAU, r: gauss() * 0.045, spd: (0.05 + rnd() * 0.16) * (rnd() < 0.12 ? -1 : 1), size: 0.6 + Math.pow(rnd(), 3) * 2.4, ph: rnd() * TAU, tw: 0.5 + rnd() * 2.5 }));
      if (this.o.mouse) window.addEventListener('pointermove', (e) => { this.mx = (e.clientX / innerWidth - 0.5); this.my = (e.clientY / innerHeight - 0.5); }, { passive: true });
    }
    set(st) { Object.assign(this.s, st); }
    step(dt) {
      const s = this.s; this.t += dt;
      const tE = Math.min(1, s.level * 9);
      this.energy += (tE - this.energy) * (1 - Math.exp(-dt * (tE > this.energy ? 18 : 4)));
      const v = s.inputMidi > 0 ? 1 : 0; this.voiced += (v - this.voiced) * (1 - Math.exp(-dt * 10));
      if (v) {
        const pc = ((s.inputMidi % 12) + 12) % 12, tgt = pc / 12 * TAU; let d = tgt - this.comet;
        while (d > Math.PI) d -= TAU; while (d < -Math.PI) d += TAU;
        this.comet += d * (1 - Math.exp(-dt * (s.mode === 2 ? 40 : 14)));
      }
      for (let i = 0; i < 12; i++) { const on = s.targetNote >= 0 && s.targetNote % 12 === i ? 1 : 0; this.glow[i] += (on - this.glow[i]) * (1 - Math.exp(-dt * (on ? 25 : 5))); }
      const mul = (s.mode === 0 ? 0.6 : s.mode === 1 ? 1 : 1.5) * (0.35 + this.energy * 2.2);
      for (const p of this.ps) { p.a += p.spd * dt * mul; if (p.a > TAU) p.a -= TAU; if (p.a < 0) p.a += TAU; }
    }
    draw() {
      const { w, h, dpr } = fit(this.cv), g = this.ctx, s = this.s, E = Math.max(0, this.energy);
      g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
      const cx = w / 2 + this.mx * 24, cy = h / 2 + this.my * 18, R = Math.min(w, h) * this.o.rFrac;
      // ambient
      let gr = g.createRadialGradient(cx, cy, R * 0.2, cx, cy, R * 1.7);
      gr.addColorStop(0, rgba(C.deep, 0.0)); gr.addColorStop(0.55, rgba(C.lila, 0.13 + 0.12 * E)); gr.addColorStop(1, rgba(C.deep, 0));
      g.fillStyle = gr; g.fillRect(0, 0, w, h);
      for (let i = 0; i < 4; i++) { g.strokeStyle = rgba(C.lila, 0.04 + 0.03 * E); g.lineWidth = 6 + i * 7; g.beginPath(); g.arc(cx, cy, R, 0, TAU); g.stroke(); }
      // particles (additive)
      g.globalCompositeOperation = 'lighter';
      const robot = s.mode === 2, t = this.t;
      for (const p of this.ps) {
        const wob = Math.sin(p.a * 5 + t * 1.3 + p.ph) * 0.022 * (0.4 + E) + Math.sin(p.a * 3 - t * 0.7) * 0.014;
        let da = Math.abs(p.a - this.comet); da = Math.min(da, TAU - da);
        const near = Math.exp(-(da * da) / 0.09) * this.voiced;
        let rr = R * (1 + p.r * (1 + E * 1.6 + Math.abs(s.correction) * 0.6) + wob + near * 0.07 * (0.4 + E));
        if (robot) rr = R + Math.round((rr - R) / (R * 0.02)) * R * 0.02;
        const x = cx + Math.sin(p.a) * rr, y = cy - Math.cos(p.a) * rr;
        const tw = 0.55 + 0.45 * Math.sin(t * p.tw + p.ph);
        const b = Math.min(1, 0.25 + 0.45 * tw + near * 0.8 + E * 0.25);
        const sz = p.size * (1 + near * 0.9 + E * 0.35) * (w > 700 ? 1.15 : 1);
        let col = mix(C.deep, C.lila, Math.min(1, b * 1.4)); col = mix(col, b > 0.8 ? C.white : C.mag, Math.max(0, b - 0.65) * 1.6);
        if (sz > 2.2) { g.fillStyle = rgba(col, 0.1 * b); g.beginPath(); g.arc(x, y, sz * 2.2, 0, TAU); g.fill(); }
        g.fillStyle = rgba(col, 0.35 + 0.65 * b);
        if (robot) g.fillRect(x - sz / 2, y - sz / 2, sz, sz); else { g.beginPath(); g.arc(x, y, sz / 2, 0, TAU); g.fill(); }
      }
      // comet
      if (this.voiced > 0.02) {
        const rc = R * 1.13;
        for (let k = 14; k >= 0; k--) {
          const a = this.comet - k * 0.022, x = cx + Math.sin(a) * rc, y = cy - Math.cos(a) * rc, al = this.voiced * (1 - k / 15), sz = 7 * (1 - k / 18);
          g.fillStyle = rgba(C.mag, 0.16 * al); g.beginPath(); g.arc(x, y, sz * 1.8, 0, TAU); g.fill();
          g.fillStyle = rgba(C.white, 0.85 * al); g.beginPath(); g.arc(x, y, sz * 0.4, 0, TAU); g.fill();
        }
      }
      g.globalCompositeOperation = 'source-over';
      // wheel
      if (this.o.wheel) {
        const Rn = R * 1.27;
        g.textAlign = 'center'; g.textBaseline = 'middle';
        for (let i = 0; i < 12; i++) {
          const a = i / 12 * TAU, inS = (s.mask >> i) & 1, root = i === s.key, x = cx + Math.sin(a) * Rn, y = cy - Math.cos(a) * Rn, gl = this.glow[i];
          if (gl > 0.02) { g.fillStyle = rgba(C.lila, 0.35 * gl); g.beginPath(); g.arc(x, y, 17, 0, TAU); g.fill(); }
          g.fillStyle = gl > 0.3 ? '#fff' : inS ? (root ? rgba(C.light, 1) : 'rgba(245,241,251,.85)') : 'rgba(117,109,130,.7)';
          g.font = `${inS || gl > 0.3 ? 600 : 400} 12px "JetBrains Mono", monospace`;
          g.fillText(NOTES[i], x, y);
          const tx = cx + Math.sin(a) * R * 1.13, ty = cy - Math.cos(a) * R * 1.13;
          g.fillStyle = inS ? rgba(C.lila, 0.75) : 'rgba(74,68,86,.6)'; g.beginPath(); g.arc(tx, ty, inS ? 2 : 1.2, 0, TAU); g.fill();
        }
      }
      if (s.detect >= 0) {
        g.strokeStyle = rgba(C.mag, 0.9); g.lineWidth = 2.5; g.lineCap = 'round';
        g.beginPath(); g.arc(cx, cy, R * 0.8, -Math.PI / 2, -Math.PI / 2 + TAU * Math.max(0.01, s.detect)); g.stroke();
      }
      if (this.o.text) {
        g.textAlign = 'center'; g.textBaseline = 'middle';
        g.fillStyle = '#8e879b'; g.font = '600 11px "JetBrains Mono", monospace';
        g.fillText(s.detect >= 0 ? 'LISTENING …' : 'KEY', cx, cy - R * 0.5);
        const custom = s.scaleName === 'CUSTOM';
        g.fillStyle = '#f5f1fb'; g.font = `600 ${custom ? R * 0.26 : R * 0.6}px "Instrument Sans", sans-serif`;
        g.fillText(custom ? 'CUSTOM' : NOTES[s.key], cx, cy - R * 0.06);
        g.fillStyle = rgba(C.light, 1); g.font = '600 12px "JetBrains Mono", monospace';
        g.fillText(s.scaleName, cx, cy + R * 0.36);
      }
    }
  }

  class Piano {
    constructor(cv, lo = 36, hi = 84, o = {}) {
      this.cv = cv; this.ctx = cv.getContext('2d'); this.lo = lo; this.hi = hi; this.o = Object.assign({ rail: true, labels: true }, o);
      this.s = { inputMidi: -1, targetNote: -1, mask: 0xfff, key: 0 };
      this.glow = new Float32Array(128); this.tglow = new Float32Array(128); this.puck = -1; this.pa = 0; this.onToggle = null;
      cv.addEventListener('pointerdown', (e) => { if (!this.onToggle) return; const r = cv.getBoundingClientRect(); const n = this.noteAt(e.clientX - r.left, e.clientY - r.top); if (n >= 0) this.onToggle(n % 12); });
    }
    static black(n) { const p = n % 12; return p === 1 || p === 3 || p === 6 || p === 8 || p === 10; }
    static wi(n) { return Math.floor(n / 12) * 7 + [0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6][n % 12]; }
    area() { const top = this.o.rail ? 20 : 0; return { x: 0, y: top, w: this.cv.clientWidth, h: this.cv.clientHeight - top }; }
    rect(n) {
      const a = this.area(), nw = Piano.wi(this.hi) - Piano.wi(this.lo) + 1, ww = a.w / nw, x = a.x + (Piano.wi(n) - Piano.wi(this.lo)) * ww;
      if (!Piano.black(n)) return { x, y: a.y, w: ww, h: a.h };
      const bw = ww * 0.62; return { x: x + ww - bw / 2, y: a.y, w: bw, h: a.h * 0.62 };
    }
    noteAt(x, y) {
      const inR = (r) => x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
      for (let n = this.lo; n <= this.hi; n++) if (Piano.black(n) && inR(this.rect(n))) return n;
      for (let n = this.lo; n <= this.hi; n++) if (!Piano.black(n) && inR(this.rect(n))) return n;
      return -1;
    }
    xFor(m) { m = Math.max(this.lo, Math.min(this.hi, m)); const n0 = Math.floor(m), f = m - n0, r0 = this.rect(n0), r1 = this.rect(Math.min(this.hi, n0 + 1)); return r0.x + r0.w / 2 + (r1.x + r1.w / 2 - r0.x - r0.w / 2) * f; }
    set(st) { Object.assign(this.s, st); }
    step(dt) {
      const sung = this.s.inputMidi > 0 ? Math.round(this.s.inputMidi) : -1;
      for (let n = 0; n < 128; n++) {
        const a = n === sung ? 1 : 0; this.glow[n] += (a - this.glow[n]) * (1 - Math.exp(-dt * (a > this.glow[n] ? 30 : 5)));
        const b = n === this.s.targetNote ? 1 : 0; this.tglow[n] += (b - this.tglow[n]) * (1 - Math.exp(-dt * (b > this.tglow[n] ? 30 : 6)));
      }
      if (this.s.inputMidi > 0) { const x = this.xFor(this.s.inputMidi); this.puck = this.puck < 0 ? x : this.puck + (x - this.puck) * (1 - Math.exp(-dt * 22)); }
      this.pa += ((this.s.inputMidi > 0 ? 1 : 0) - this.pa) * (1 - Math.exp(-dt * 10));
    }
    draw() {
      const { w, h, dpr } = fit(this.cv), g = this.ctx, s = this.s; g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
      const rr = (r, rad) => { g.beginPath(); g.roundRect ? g.roundRect(r.x, r.y, r.w, r.h, rad) : g.rect(r.x, r.y, r.w, r.h); };
      if (this.o.rail) {
        g.fillStyle = 'rgba(255,255,255,.06)'; g.fillRect(0, 8, w, 2);
        if (this.pa > 0.02 && this.puck >= 0) {
          if (s.targetNote >= this.lo && s.targetNote <= this.hi) {
            const r = this.rect(s.targetNote), tx = r.x + r.w / 2;
            g.strokeStyle = rgba(C.mag, 0.55 * this.pa); g.lineWidth = 2; g.beginPath(); g.moveTo(Math.min(tx, this.puck), 9); g.lineTo(Math.max(tx, this.puck), 9); g.stroke();
            g.fillStyle = rgba(C.white, 0.8 * this.pa); g.fillRect(tx - 1, 4, 2, 10);
          }
          g.fillStyle = rgba(C.lila, 0.35 * this.pa); g.beginPath(); g.arc(this.puck, 9, 9, 0, TAU); g.fill();
          g.fillStyle = rgba(C.light, this.pa); g.beginPath(); g.arc(this.puck, 9, 4, 0, TAU); g.fill();
        }
      }
      const key = (n) => {
        const r0 = this.rect(n), bl = Piano.black(n), inS = (s.mask >> (n % 12)) & 1, gl = this.glow[n], tg = this.tglow[n];
        const r = bl ? r0 : { x: r0.x + 1, y: r0.y, w: r0.w - 2, h: r0.h }, rad = bl ? 3 : 5;
        const lg = g.createLinearGradient(0, r.y, 0, r.y + r.h);
        const base = bl ? [9, 7, 13] : inS ? [34, 29, 44] : [21, 18, 27];
        lg.addColorStop(0, rgba(base.map((v) => v + 8), 1)); lg.addColorStop(1, rgba(base.map((v) => v * 0.75), 1));
        g.fillStyle = lg; rr(r, rad); g.fill();
        if (tg > 0.01) { g.fillStyle = rgba(C.mag, 0.25 * tg); rr(r, rad); g.fill(); }
        if (gl > 0.01) {
          const gg = g.createLinearGradient(0, r.y + r.h, 0, r.y); gg.addColorStop(0, rgba(C.light, 0.95 * gl)); gg.addColorStop(1, rgba(C.lila, 0.15 * gl));
          g.fillStyle = gg; rr(r, rad); g.fill();
          g.shadowColor = rgba(C.lila, 0.9 * gl); g.shadowBlur = 18 * gl; g.strokeStyle = rgba(C.lila, 0.5 * gl); rr(r, rad); g.stroke(); g.shadowBlur = 0;
        }
        g.strokeStyle = bl ? 'rgba(255,255,255,.1)' : 'rgba(255,255,255,.06)'; g.lineWidth = 1; rr(r, rad); g.stroke();
        if (inS) { const d = bl ? 4.5 : 5.5; g.fillStyle = gl > 0.4 ? '#fff' : (n % 12 === s.key ? rgba(C.mag, 1) : rgba(C.lila, 1)); g.beginPath(); g.arc(r.x + r.w / 2, r.y + r.h - (bl ? 8 : (this.o.labels ? 22 : 10)), d / 2, 0, TAU); g.fill(); }
        if (!bl && n % 12 === 0 && this.o.labels && r.h > 50) { g.fillStyle = gl > 0.4 ? '#07050b' : '#8e879b'; g.font = '9.5px "JetBrains Mono", monospace'; g.textAlign = 'center'; g.fillText('C' + (Math.floor(n / 12) - 1), r.x + r.w / 2, r.y + r.h - 6); }
      };
      for (let n = this.lo; n <= this.hi; n++) if (!Piano.black(n)) key(n);
      for (let n = this.lo; n <= this.hi; n++) if (Piano.black(n)) key(n);
    }
  }

  /* pitch-curve illustration for the three modes */
  function drawCurve(cv, mode, phase) {
    const { w, h, dpr } = fit(cv), g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
    const notes = [57, 60, 62, 64]; const lo = 55.5, hi = 65.5, y = (m) => h - (m - lo) / (hi - lo) * h;
    g.font = '9px "JetBrains Mono", monospace'; g.textAlign = 'left';
    for (const n of notes) { g.strokeStyle = 'rgba(168,85,247,.16)'; g.setLineDash([3, 5]); g.beginPath(); g.moveTo(26, y(n)); g.lineTo(w, y(n)); g.stroke(); g.setLineDash([]); g.fillStyle = '#756d82'; g.fillText(NOTES[n % 12], 6, y(n) + 3); }
    const N = 260, sung = [], seq = [[57.35, 0], [60.4, 0.27], [62.3, 0.52], [63.7, 0.76]];
    let cur = seq[0][0];
    for (let i = 0; i < N; i++) {
      const t = i / N; let tg = seq[0][0]; for (const s of seq) if (t >= s[1]) tg = s[0];
      cur += (tg - cur) * 0.09; sung.push(cur + 0.22 * Math.sin(t * 46));
    }
    const allowed = (m) => notes.reduce((b, n) => Math.abs(n - m) < Math.abs(b - m) ? n : b, notes[0]);
    const out = []; let shift = 0, center = sung[0], note = allowed(sung[0]);
    const tau = [0.16, 0.05, 0][mode], hum = [0.6, 0.25, 0][mode], hy = [0.22, 0.12, 0.03][mode];
    for (let i = 0; i < N; i++) {
      const m = sung[i], c = allowed(m);
      if (c !== note && Math.abs(m - note) - Math.abs(m - c) > hy) { note = c; center = m; }
      center += (m - center) * 0.08; const desired = note - (m + (center - m) * hum);
      shift = tau === 0 ? desired : shift + (desired - shift) * (1 - Math.exp(-(1 / N) * 3.2 / tau));
      out.push(m + shift);
    }
    const x = (i) => 26 + i / (N - 1) * (w - 34);
    g.lineWidth = 1.5; g.strokeStyle = 'rgba(179,171,192,.35)'; g.beginPath(); sung.forEach((m, i) => (i ? g.lineTo(x(i), y(m)) : g.moveTo(x(i), y(m)))); g.stroke();
    const grd = g.createLinearGradient(0, 0, w, 0); grd.addColorStop(0, '#6d28d9'); grd.addColorStop(0.6, '#a855f7'); grd.addColorStop(1, mode === 2 ? '#e879f9' : '#d2a8ff');
    g.lineWidth = 2.6; g.strokeStyle = grd; g.shadowColor = 'rgba(168,85,247,.8)'; g.shadowBlur = 10; g.beginPath();
    out.forEach((m, i) => (i ? g.lineTo(x(i), y(m)) : g.moveTo(x(i), y(m)))); g.stroke(); g.shadowBlur = 0;
    const pi = Math.floor(phase * (N - 1)); g.fillStyle = '#fff'; g.beginPath(); g.arc(x(pi), y(out[pi]), 3.5, 0, TAU); g.fill();
    g.fillStyle = 'rgba(232,121,249,.3)'; g.beginPath(); g.arc(x(pi), y(out[pi]), 9, 0, TAU); g.fill();
    g.fillStyle = '#756d82'; g.textAlign = 'right'; g.fillText('— gesungen   — UBTune', w - 8, 13);
  }

  window.UBV = { Ring, Piano, drawCurve, NOTES };
})();
