/* UBTune 1.1 – website logic: loader, hero orb, mode curves, screenshots, live demo. by unique & Mbezza */
(function () {
  const { Orb, Keys, drawCurve, NOTES } = window.UBV;
  const $ = (s, r = document) => r.querySelector(s), $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const SCALE_SHORT = ['CHROMATIC', 'MAJOR', 'MINOR', 'PENT. MAJOR', 'PENT. MINOR', 'BLUES', 'HARM. MINOR', 'DORIAN', 'CUSTOM'];
  const SCALE_NAMES = ['CHROMATIC', 'MAJOR', 'MINOR', 'PENTATONIC MAJOR', 'PENTATONIC MINOR', 'BLUES', 'HARMONIC MINOR', 'DORIAN', 'CUSTOM'];
  const IV = [null, [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 4, 7, 9], [0, 3, 5, 7, 10], [0, 3, 5, 6, 7, 10], [0, 2, 3, 5, 7, 8, 11], [0, 2, 3, 5, 7, 9, 10]];
  const maskOf = (root, s) => { if (!IV[s]) return 0xfff; let m = 0; for (const i of IV[s]) m |= 1 << ((root + i) % 12); return m; };
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const small = innerWidth < 700;

  /* loader */
  const ld = $('#ld'), ldc = $('#ldRing'), ldPc = $('#ldPc'); let ldP = 0, ldDone = false;
  (function tick() {
    if (ldDone) return;
    ldP = Math.min(100, ldP + (document.readyState === 'complete' ? 6 : 1.4));
    const g = ldc.getContext('2d'), c = 132, r = 118; g.clearRect(0, 0, 264, 264);
    g.lineWidth = 5; g.strokeStyle = '#1c1c20'; g.beginPath(); g.arc(c, c, r, 0, Math.PI * 2); g.stroke();
    const grd = g.createLinearGradient(0, 0, 264, 264); grd.addColorStop(0, '#a855f7'); grd.addColorStop(0.6, '#3b82f6'); grd.addColorStop(1, '#7dd3fc');
    g.strokeStyle = grd; g.lineCap = 'round'; g.beginPath(); g.arc(c, c, r, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * ldP / 100); g.stroke();
    ldPc.textContent = Math.round(ldP);
    if (ldP >= 100) { ldDone = true; setTimeout(() => ld.classList.add('out'), 120); } else requestAnimationFrame(tick);
  })();

  /* nav + reveal + visibility */
  const nav = $('#nav'); addEventListener('scroll', () => nav.classList.toggle('solid', scrollY > 40), { passive: true });
  const io = new IntersectionObserver((es) => es.forEach((e) => { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } }), { threshold: 0.12 });
  $$('.reveal').forEach((el) => io.observe(el));
  const vis = new Map(); const vio = new IntersectionObserver((es) => es.forEach((e) => vis.set(e.target, e.isIntersecting)), { rootMargin: '120px' });
  const isVis = (el) => vis.get(el) !== false;

  /* hero: orb + keys driven by a fake sung melody */
  const heroOrb = new Orb($('#heroOrb'), { n: small ? 900 : 1600, rFrac: 0.30 });
  const heroKeys = new Keys($('#heroKeys'));
  vio.observe($('#heroOrb'));
  const heroMask = maskOf(9, 4);
  heroOrb.set({ key: 9, scaleName: 'PENTATONIC MINOR', mode: 1 }); heroKeys.set({ mask: heroMask, key: 9 });
  const melody = [[57, 0.5], [60, 0.5], [62, 0.75], [64, 0.5], [67, 1], [64, 0.5], [62, 0.5], [60, 0.75], [57, 1.2], [-1, 0.6], [64, 0.5], [67, 0.5], [69, 1], [67, 0.5], [64, 0.5], [62, 1.2], [-1, 0.8]];
  let mi = 0, mt = 0, sungCur = 57;
  function heroStep(dt) {
    mt += dt; if (mt > melody[mi][1]) { mt = 0; mi = (mi + 1) % melody.length; }
    const n = melody[mi][0], on = n > 0;
    if (on) sungCur += (n + 0.3 - sungCur) * (1 - Math.exp(-dt * 9));
    const sung = sungCur + 0.18 * Math.sin(performance.now() / 160);
    const st = { inputMidi: on ? sung : -1, targetNote: on ? n : -1, level: on ? 0.06 + 0.03 * Math.sin(mt * 6) : 0.004, correction: on ? n - sung : 0 };
    heroOrb.set(st); heroKeys.set(st); heroOrb.step(dt); heroKeys.step(dt);
  }

  /* mode curves + screenshots */
  const curves = $$('.curve'); curves.forEach((c) => vio.observe(c));
  $$('.shot-tabs button').forEach((b) => b.addEventListener('click', () => {
    $$('.shot-tabs button').forEach((x) => x.classList.toggle('on', x === b));
    const img = $('#shotImg'); img.style.opacity = 0;
    setTimeout(() => { img.src = `assets/shot-${b.dataset.shot}.jpg`; img.onload = () => (img.style.opacity = 1); }, 180);
  }));

  /* ---------------- demo ---------------- */
  const st = { mode: 1, key: 9, scale: 4, retune: 20, humanize: 25, formant: 0, mix: 100, custom: maskOf(9, 4), bypass: false };
  const demoOrb = new Orb($('#demoOrb'), { n: small ? 900 : 1300, rFrac: 0.33 });
  const demoKeys = new Keys($('#demoKeys'));
  vio.observe($('#demoOrb'));
  const mask = () => (st.scale === 8 ? st.custom : maskOf(st.key, st.scale));
  demoKeys.onToggle = (pc) => { const m = mask() ^ (1 << pc); if (!m) return; st.custom = m; st.scale = 8; sync(); };

  function selector(el, items, get, setv) {
    $$('button', el).forEach((b) => b.addEventListener('click', () => { setv(((get() + +b.dataset.d) % items.length + items.length) % items.length); sync(); }));
    const sel = document.createElement('select'); sel.setAttribute('aria-label', $('i', el).textContent);
    items.forEach((t, i) => { const o = document.createElement('option'); o.value = i; o.textContent = t; sel.appendChild(o); });
    Object.assign(sel.style, { position: 'absolute', inset: '0', opacity: '0', cursor: 'pointer', width: '100%' });
    const v = $('.v', el); v.style.position = 'relative'; v.appendChild(sel);
    sel.addEventListener('change', () => { setv(+sel.value); sync(); });
    return sel;
  }
  const keySelect = selector($('#keySel'), NOTES, () => st.key, (v) => { st.key = v; if (st.scale === 8) st.scale = 4; });
  const scaleSelect = selector($('#scaleSel'), SCALE_SHORT.slice(0, 8), () => (st.scale === 8 ? 0 : st.scale), (v) => (st.scale = v));

  const sliders = { retune: (v) => `${v} ms`, humanize: (v) => `${v} %`, formant: (v) => `${v > 0 ? '+' : ''}${(+v).toFixed(1)} st`, mix: (v) => `${v} %` };
  const setFill = (inp) => inp.style.setProperty('--v', `${((inp.value - inp.min) / (inp.max - inp.min)) * 100}%`);
  Object.keys(sliders).forEach((k) => { const inp = $('#s-' + k); inp.addEventListener('input', () => { st[k] = +inp.value; sync(); }); });
  $$('#modeSeg button').forEach((b) => b.addEventListener('click', () => {
    const m = +b.dataset.mode; st.mode = m;
    if (m === 0) { st.retune = 70; st.humanize = 60; }
    else if (m === 1) { st.retune = 20; st.humanize = 25; }
    else { st.retune = 0; st.humanize = 0; if (st.scale !== 8) st.scale = [1, 3].includes(st.scale) ? 3 : 4; }
    sync();
  }));
  $$('.ab button').forEach((b) => b.addEventListener('click', () => { st.bypass = b.dataset.ab === '0'; sync(); }));

  let ctx = null, node = null, micStream = null, micSrc = null, voxSrc = null, tel = null, lastSerial = 0, latency = 0, lastDetected = '';
  function sync() {
    $$('#modeSeg button').forEach((b) => b.classList.toggle('on', +b.dataset.mode === st.mode));
    $$('.ab button').forEach((b) => b.classList.toggle('on', (b.dataset.ab === '0') === st.bypass));
    $('#keySel b').textContent = NOTES[st.key]; keySelect.value = st.key;
    $('#scaleSel b').textContent = SCALE_SHORT[st.scale]; if (st.scale !== 8) scaleSelect.value = st.scale;
    Object.keys(sliders).forEach((k) => { const inp = $('#s-' + k); if (+inp.value !== st[k]) inp.value = st[k]; setFill(inp); $('#o-' + k).textContent = sliders[k](st[k]); });
    const m = mask();
    demoOrb.set({ key: st.key, scaleName: SCALE_NAMES[st.scale], mode: st.mode }); demoKeys.set({ mask: m, key: st.key });
    if (node) node.port.postMessage({ type: 'params', p: { mode: st.mode, mask: m, retune: st.retune, humanize: st.humanize, formant: st.formant, transpose: 0, mix: st.mix, output: 0, bypass: st.bypass } });
  }
  sync();

  async function ensureAudio() {
    if (ctx) { if (ctx.state === 'suspended') await ctx.resume(); return; }
    ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
    await ctx.audioWorklet.addModule('js/ubtune-worklet.js?v=112');
    node = new AudioWorkletNode(ctx, 'ubtune', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
    node.port.onmessage = (e) => {
      if (e.data.type === 'ready') latency = (e.data.latency / e.data.fs) * 1000;
      if (e.data.type === 'tel') {
        tel = e.data.t;
        if (tel.detectSerial !== lastSerial) {
          lastSerial = tel.detectSerial; st.key = tel.detectedKey;
          st.scale = [3, 4].includes(st.scale) ? (tel.detectedMinor ? 4 : 3) : tel.detectedMinor ? 2 : 1;
          lastDetected = `${NOTES[st.key]} ${tel.detectedMinor ? 'MINOR' : 'MAJOR'}`; sync();
        }
      }
    };
    node.connect(ctx.destination); sync();
  }

  const micBtn = $('#micBtn'), voxBtn = $('#voxBtn');
  function stopMic() { if (micSrc) micSrc.disconnect(); micSrc = null; if (micStream) micStream.getTracks().forEach((t) => t.stop()); micStream = null; micBtn.classList.remove('live'); $('span', micBtn).textContent = 'Mikro starten'; }
  function stopVox() { if (voxSrc) { try { voxSrc.stop(); } catch (_) {} voxSrc.disconnect(); } voxSrc = null; $('span', voxBtn).textContent = 'Beispiel-Stimme'; }
  micBtn.addEventListener('click', async () => {
    if (micStream) { stopMic(); return; }
    try {
      await ensureAudio(); stopVox();
      micStream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, latency: 0 } });
      micSrc = ctx.createMediaStreamSource(micStream); micSrc.connect(node);
      micBtn.classList.add('live'); $('span', micBtn).textContent = 'Live – stoppen';
      const base = ((ctx.baseLatency || 0) + (ctx.outputLatency || 0)) * 1000;
      $('#demoNote').textContent = `Live · UBTune ≈ 3 ms${base ? ` + Browser/Audio ≈ ${base.toFixed(0)} ms` : ''}. Im Plugin fällt der Browser-Anteil weg. Kopfhörer nutzen!`;
    } catch (err) { $('#demoNote').textContent = 'Kein Mikro-Zugriff – erlaube das Mikrofon im Browser oder nimm die Beispiel-Stimme.'; }
  });
  voxBtn.addEventListener('click', async () => {
    if (voxSrc) { stopVox(); return; }
    await ensureAudio(); stopMic();
    voxSrc = ctx.createBufferSource(); voxSrc.buffer = makeVoice(ctx.sampleRate); voxSrc.loop = true; voxSrc.connect(node); voxSrc.start();
    $('span', voxBtn).textContent = 'Stopp';
    $('#demoNote').textContent = 'Beispiel: leicht schiefe Melodie mit Slides in A-Moll. Schalte ORIGINAL / UBTUNE und probier ROBOTIC.';
  });
  $('#detectBtn').addEventListener('click', async () => { await ensureAudio(); node.port.postMessage({ type: 'detect' }); });

  function makeVoice(fs) {
    const notes = [[57, 0.55], [60, 0.45], [62, 0.5], [64, 0.9], [-1, 0.15], [67, 0.5], [64, 0.4], [62, 0.45], [60, 0.5], [57, 1.1], [-1, 0.3], [64, 0.5], [67, 0.45], [69, 0.9], [67, 0.45], [64, 0.5], [62, 1.2], [-1, 0.5]];
    const off = [0.32, -0.28, 0.4, -0.35, 0, 0.45, 0.3, -0.4, 0.35, -0.3, 0, 0.38, -0.33, 0.42, -0.3, 0.36, 0.3, 0];
    const total = notes.reduce((a, n) => a + n[1], 0), N = Math.floor(total * fs), buf = new AudioBuffer({ length: N, sampleRate: fs, numberOfChannels: 1 }), x = buf.getChannelData(0);
    const reso = (f, bw) => { const r = Math.exp((-Math.PI * bw) / fs); return { a1: 2 * r * Math.cos((2 * Math.PI * f) / fs), a2: -r * r, b0: 1 - r, y1: 0, y2: 0 }; };
    const tick = (q, v) => { const y = q.b0 * v + q.a1 * q.y1 + q.a2 * q.y2; q.y2 = q.y1; q.y1 = y; return y; };
    const F = [reso(720, 90), reso(1150, 110), reso(2600, 170), reso(3300, 250)];
    let ph = 0, i = 0, cur = 57, t = 0, seed = 3; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647 - 0.5;
    notes.forEach(([n, d], k) => {
      const L = Math.floor(d * fs); if (n > 0) cur = n + off[k] - 0.9;
      for (let j = 0; j < L && i < N; j++, i++, t += 1 / fs) {
        const env = n > 0 ? Math.min(1, j / (0.04 * fs)) * Math.min(1, (L - j) / (0.07 * fs)) : 0;
        if (n > 0) cur += (n + off[k] - cur) * (1 - Math.exp(-1 / (0.06 * fs)));
        const m = cur + 0.2 * Math.sin(2 * Math.PI * 5.3 * t) * Math.min(1, j / (0.25 * fs)), f = 440 * Math.pow(2, (m - 69) / 12);
        ph += f / fs; if (ph >= 1) ph -= 1;
        const src = (ph < 0.6 ? Math.pow(Math.sin((Math.PI * ph) / 0.6), 2) : 0) - 0.3 + 0.03 * rnd();
        x[i] = (tick(F[0], src) + 0.6 * tick(F[1], src) + 0.3 * tick(F[2], src) + 0.15 * tick(F[3], src)) * 0.28 * env;
      }
    });
    return buf;
  }

  /* main loop */
  let last = performance.now(), curvePh = 0;
  /* adaptive Qualität: volle Auflösung, solange es flüssig läuft – erst bei Rucklern stufenweise runter (und später wieder hoch) */
  const Q = (window.UBQ = { s: 1 }), QL = [1, 0.85, 0.72, 0.6]; let qi = 0, qAcc = 0, qN = 0, qGood = 0;
  function quality(ms) {
    if (document.hidden || ms > 250) return;
    qAcc += ms; if (++qN < 40) return;
    const avg = qAcc / qN; qAcc = qN = 0;
    if (avg > 21 && qi < QL.length - 1) { Q.s = QL[++qi]; qGood = 0; }
    else if (avg < 17.6 && qi > 0 && ++qGood >= 8) { Q.s = QL[--qi]; qGood = 0; }
  }
  const txtCache = new WeakMap();
  const setTxt = (el, v) => { if (txtCache.get(el) !== v) { txtCache.set(el, v); el.textContent = v; } };
  function frame(now) {
    const dt = Math.max(0, Math.min(0.05, (now - last) / 1000)); quality(now - last); last = now;
    if (isVis($('#heroOrb')) && !reduced) { heroStep(dt); heroOrb.draw(); heroKeys.draw(); }
    if (isVis($('#demoOrb'))) {
      const live = tel && (micStream || voxSrc);
      const t = live ? tel : { inputMidi: -1, targetNote: -1, level: 0.004, correction: 0, detect: tel ? tel.detect : -1, delayMs: 0 };
      const s = { inputMidi: t.inputMidi, targetNote: t.targetNote, level: t.level, correction: t.correction, detect: t.detect };
      demoOrb.set(s); demoKeys.set(s); demoOrb.step(dt); demoKeys.step(dt); demoOrb.draw(); demoKeys.draw();
      let txt = live ? '— singen —' : '— bereit —';
      if (t.inputMidi > 0) { const n = Math.round(t.inputMidi), c = Math.round((t.inputMidi - n) * 100); txt = `IN ${NOTES[((n % 12) + 12) % 12]}${Math.floor(n / 12) - 1} ${c >= 0 ? '+' : ''}${c}¢` + (t.targetNote >= 0 ? `  →  ${NOTES[t.targetNote % 12]}${Math.floor(t.targetNote / 12) - 1}` : ''); }
      if (live && t.delayMs) txt += `  ·  ${t.delayMs.toFixed(1)} MS`;
      setTxt($('#demoRead'), st.bypass ? txt + '  ·  ORIGINAL' : txt);
      const det = t.detect >= 0, btn = $('#detectBtn');
      if (btn.classList.contains('on') !== det) btn.classList.toggle('on', det); const bw = det ? `${Math.max(2, t.detect * 100).toFixed(1)}%` : '0'; if (txtCache.get(btn) !== bw) { txtCache.set(btn, bw); $('#detectBar').style.width = bw; }
      setTxt($('#detectTitle'), det ? `LISTENING ${Math.round(t.detect * 100)} %` : 'DETECT KEY');
      setTxt($('#detectSub'), det ? 'SING WEITER · KLICK = STOPP' : lastDetected ? `ERKANNT: ${lastDetected}` : '8 SEK SINGEN · SETZT KEY + SCALE');
    }
    curvePh = (curvePh + dt / 4) % 1;
    curves.forEach((c) => { if (isVis(c)) drawCurve(c, +c.dataset.mode, curvePh); });
    requestAnimationFrame(frame);
  }
  if (reduced) { for (let i = 0; i < 30; i++) heroStep(1 / 30); heroOrb.draw(); heroKeys.draw(); }
  requestAnimationFrame(frame);
})();
