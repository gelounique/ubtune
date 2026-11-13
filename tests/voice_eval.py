"""Real-voice evaluation for UBTune.
Runs the engine (tests/voice_run) on real recordings and measures against librosa pYIN:
 - detector errors (gross >50c, octave), voicing errors
 - note flips per voiced second vs. musically necessary changes
 - artifacts: HF click frames/s (output HF bursts that are not in the input)
 - output quality (robotic): % voiced output frames within +-20c of an allowed note
usage: voice_eval.py /path/voice_run  wav [wav...]"""
import sys, subprocess, os, numpy as np, librosa, soundfile as sf
RUN = sys.argv[1]; files = sys.argv[2:]
CACHE = '/tmp/voice/ref_cache'; os.makedirs(CACHE, exist_ok=True)
FS = 48000; HOP = 256

def ref_f0(path, y):
    c = os.path.join(CACHE, os.path.basename(path) + '.npy')
    if os.path.exists(c): return np.load(c)
    f0, vflag, vprob = librosa.pyin(y, fmin=65, fmax=1000, sr=FS, frame_length=2048, hop_length=HOP)
    m = np.where(vflag, 69 + 12 * np.log2(np.nan_to_num(f0, nan=1) / 440), np.nan)
    np.save(c, m); return m

def tel(path):
    a = np.loadtxt(path, delimiter=',')
    return a[:, 0], a[:, 1], a[:, 2].astype(int), a[:, 4]

def eval_file(wav, mode, key, scale, mask):
    y, _ = sf.read(wav, dtype='float32')
    out = '/tmp/voice/out.wav'; t = '/tmp/voice/tel.csv'
    subprocess.run([RUN, wav, out, t, str(mode), str(key), str(scale)], check=True)
    yo, _ = sf.read(out, dtype='float32')
    ref = ref_f0(wav, y); tref = (np.arange(len(ref)) * HOP) / FS
    s, im, tn, dl = tel(t); ts = s / FS
    # align: engine telemetry describes the window ending at ts -> centre ~ ts - 0.013
    est = np.interp(tref + 0.013, ts, np.where(im > 0, im, np.nan), left=np.nan, right=np.nan)
    both = ~np.isnan(ref) & ~np.isnan(est)
    d = est[both] - ref[both]
    gross = np.mean(np.abs(d) > 0.5) * 100
    octv = np.mean(np.abs(np.abs(d) - 12) < 1.0) * 100 + np.mean(np.abs(np.abs(d) - 24) < 1.0) * 100
    false_v = np.mean(np.isnan(ref) & ~np.isnan(est)) * 100
    voiced_sec = np.sum(tn >= 0) * 64 / FS
    flips = int(np.sum((tn[1:] != tn[:-1]) & (tn[1:] >= 0) & (tn[:-1] >= 0)))
    # reference "necessary" changes: nearest allowed note of 60ms-smoothed ref pitch, debounced 60 ms
    allowed = [n for n in range(30, 100) if (mask >> (n % 12)) & 1]
    near = lambda m: allowed[int(np.argmin([abs(m - a) for a in allowed]))]
    r = ref.copy(); k = max(1, int(0.06 * FS / HOP)); need = 0; cur = None; pend = None; pc = 0
    for i in range(len(r)):
        if np.isnan(r[i]): cur = None; continue
        w = r[max(0, i - k):i + 1]; m = np.nanmean(w); n = near(m)
        if cur is None: cur = n; continue
        if n != cur:
            pc = pc + 1 if pend == n else 1; pend = n
            if pc * HOP / FS >= 0.06: cur = n; need += 1; pc = 0
        else: pc = 0
    # artifacts: HF bursts in output not present in input (align by xcorr)
    lag = int(np.argmax([np.dot(y[48000:48000*4], yo[48000+l:48000*4+l]) for l in range(0, 400, 4)]) * 4)
    from scipy.signal import butter, sosfilt
    sos = butter(4, 5000, 'hp', fs=FS, output='sos')
    hi, ho = sosfilt(sos, y), sosfilt(sos, yo)
    ho = ho[lag:]; n = min(len(hi), len(ho)); fr = 96
    ei = np.array([np.mean(hi[i:i+fr]**2) for i in range(0, n - fr, fr)]); eo = np.array([np.mean(ho[i:i+fr]**2) for i in range(0, n - fr, fr)])
    floor = np.percentile(eo, 60) + 1e-9
    clicks = int(np.sum((eo > 8 * (ei + floor * 0.25)) & (eo > floor * 4)))
    # output quantisation (robotic): pyin on output
    q = None
    if mode == 2:
        fo, vf, _ = librosa.pyin(yo, fmin=65, fmax=1000, sr=FS, frame_length=2048, hop_length=HOP)
        mo = 69 + 12 * np.log2(fo[vf] / 440)
        dev = np.array([min(abs(m - a) for a in allowed) for m in mo])
        q = np.mean(dev < 0.2) * 100
    return dict(gross=gross, octv=octv, false_v=false_v, flips_s=flips / max(voiced_sec, 1e-3), need_s=need / max(voiced_sec, 1e-3), clicks_s=clicks / (len(y) / FS), q=q, delay=np.median(dl[tn >= 0]) if np.any(tn >= 0) else 0)

if __name__ == '__main__':
    agg = {}
    for mode, key, scale, mask in [(1, 0, 0, 0xfff), (2, 9, 4, sum(1 << ((9 + i) % 12) for i in [0, 3, 5, 7, 10])), (0, 0, 1, sum(1 << i for i in [0, 2, 4, 5, 7, 9, 11]))]:
        rows = [eval_file(f, mode, key, scale, mask) for f in files]
        avg = {k: np.mean([r[k] for r in rows if r[k] is not None]) if any(r[k] is not None for r in rows) else None for k in rows[0]}
        name = ['SMOOTH/C-major', 'PRO/chromatic', 'ROBOTIC/A-pentMin'][mode]
        print(f"{name:18s} detector gross {avg['gross']:5.1f}%  octave {avg['octv']:4.1f}%  falseVoiced {avg['false_v']:4.1f}% | flips/s {avg['flips_s']:5.2f} (needed {avg['need_s']:4.2f}) | clicks/s {avg['clicks_s']:5.2f}"
              + (f" | on-note {avg['q']:5.1f}%" if avg['q'] is not None else '') + f" | delay {avg['delay']:.1f}ms")
