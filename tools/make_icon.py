"""UBTune icon: lila particle ring + UB blackletter logo on a deep black rounded square."""
import math, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 2048
LOGO = sys.argv[1] if len(sys.argv) > 1 else "plugin/resources/ub-logo-full.png"
OUT = sys.argv[2] if len(sys.argv) > 2 else "branding"

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
DEEP, LILA, LIGHT, MAG = (109, 40, 217), (168, 85, 247), (214, 172, 255), (232, 121, 249)

BLUE, SKY = (59, 130, 246), (125, 211, 252)
def hue_col(h):
    if h < 0.60: return lerp(LILA, DEEP, abs(math.sin(h * 9)) * 0.35)
    if h < 0.78: return lerp(LILA, BLUE, (h - 0.60) / 0.18)
    if h < 0.90: return lerp(BLUE, SKY, (h - 0.78) / 0.12)
    return lerp(SKY, LILA, (h - 0.90) / 0.10)

def ring_layer(size, R, n=5200, seed=3):
    """abstract decaying particle ring + erosion streaks + shockwaves + grey halo"""
    rnd = random.Random(seed)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2
    # grey fuzzy outer disc (monochrome, MetaTune-ish)
    for _ in range(2600):
        a = rnd.random() * math.tau; r = R * (1.18 + rnd.random() ** 0.6 * 0.42)
        x, y = c + math.sin(a) * r, c - math.cos(a) * r; s = size / 1024 * (0.8 + rnd.random())
        d.rectangle([x, y, x + s, y + s], fill=(255, 255, 255, int(20 + 60 * rnd.random())))
    # shockwaves
    for k, (wr, al) in enumerate([(1.32, 70), (1.55, 40)]):
        for i in range(900):
            a = i / 900 * math.tau
            jag = 1 + 0.04 * math.sin(a * 13 + k) + 0.02 * (rnd.random() - .5)
            x, y = c + math.sin(a) * R * wr * jag, c - math.cos(a) * R * wr * jag
            s = size / 1024 * 1.6
            d.rectangle([x, y, x + s, y + s], fill=hue_col(0.82 + 0.1 * math.sin(a * 3)) + (al,))
    for _ in range(n):
        a = rnd.random() * math.tau
        free = rnd.random() < 0.22
        wob = 0.05 * math.sin(a * 4 + 1.3) + 0.035 * math.sin(a * 9 - 0.4)
        r = R * (1 + wob + (rnd.gauss(0, 0.02) if not free else (rnd.random() ** 2) * (0.5 if rnd.random() < .7 else -0.3)))
        x, y = c + math.sin(a) * r, c - math.cos(a) * r
        b = min(1, 0.35 + 0.65 * rnd.random() ** 1.4)
        col = hue_col((a / math.tau + 0.1 * math.sin(rnd.random() * 9)) % 1.0)
        if b > 0.8: col = lerp(col, (255, 255, 255), (b - 0.8) * 2.5)
        s = (0.8 + rnd.random() ** 3 * 4.0) * size / 1024
        if free:
            ln = s * (2 + rnd.random() * 5)
            d.line([x, y, x + math.sin(a) * ln, y - math.cos(a) * ln], fill=col + (int(70 + 120 * b),), width=max(1, int(s * 0.7)))
        else:
            d.ellipse([x - s, y - s, x + s, y + s], fill=col + (int(110 + 145 * b),))
    # inner nebula
    for _ in range(1400):
        a = rnd.random() * math.tau; r = R * math.sqrt(rnd.random()) * 0.62
        x, y = c + math.sin(a) * r, c - math.cos(a) * r; s = size / 1024 * (1 + rnd.random() * 2)
        d.rectangle([x, y, x + s, y + s], fill=hue_col(0.62 + rnd.random() * 0.38) + (int(25 + 40 * rnd.random()),))
    return img

def make(full_bleed):
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if full_bleed:
        box, rad = (0, 0, S, S), int(S * 0.2)
    else:
        m = int(S * 100 / 1024); box, rad = (m, m, S - m, S - m), int(S * 0.2)
        sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(sh).rounded_rectangle((box[0], box[1] + S * 0.012, box[2], box[3] + S * 0.012), rad, fill=(0, 0, 0, 150))
        base = Image.alpha_composite(base, sh.filter(ImageFilter.GaussianBlur(S * 0.018)))
    # body with subtle lila top light
    body = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    mask = Image.new("L", (S, S), 0); ImageDraw.Draw(mask).rounded_rectangle(box, rad, fill=255)
    yy, xx = np.mgrid[0:S, 0:S]
    cx, cy = S / 2, S / 2
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (S / 2)
    g = np.clip(1 - dist, 0, 1) ** 1.6
    arr = np.zeros((S, S, 4), np.uint8)
    arr[..., 0] = (5 + 22 * g).astype(np.uint8); arr[..., 1] = (5 + 10 * g).astype(np.uint8); arr[..., 2] = (7 + 34 * g).astype(np.uint8); arr[..., 3] = 255
    body = Image.fromarray(arr); body.putalpha(mask)
    base = Image.alpha_composite(base, body)

    R = S * 0.27
    ring = ring_layer(S, R)
    glow1 = ring.filter(ImageFilter.GaussianBlur(S * 0.025))
    glow2 = ring.filter(ImageFilter.GaussianBlur(S * 0.008))
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    for gl, k in ((glow1, 2.8), (glow2, 1.5)):
        a = np.array(gl).astype(float); a[..., 3] = np.clip(a[..., 3] * k, 0, 255)
        layer = Image.alpha_composite(layer, Image.fromarray(a.astype(np.uint8)))
    layer = Image.alpha_composite(layer, ring)
    clip = Image.new("RGBA", (S, S), (0, 0, 0, 0)); clip.paste(layer, (0, 0), mask)
    base = Image.alpha_composite(base, clip)

    # UB logo (white) with soft lila glow
    logo = Image.open(LOGO).convert("RGBA")
    lw = int(S * 0.40); logo = logo.resize((lw, int(logo.height * lw / logo.width)), Image.LANCZOS)
    L = Image.new("RGBA", (S, S), (0, 0, 0, 0)); L.paste(logo, ((S - lw) // 2, (S - logo.height) // 2 + int(S * 0.005)), logo)
    a = np.array(L).astype(float)
    gl = a.copy(); gl[..., 0], gl[..., 1], gl[..., 2] = LILA; gl[..., 3] *= 0.85
    glow = Image.fromarray(gl.astype(np.uint8)).filter(ImageFilter.GaussianBlur(S * 0.02))
    base = Image.alpha_composite(base, glow)
    base = Image.alpha_composite(base, L)
    # rim
    rim = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle(box, rad, outline=(255, 255, 255, 30), width=max(2, S // 512))
    return Image.alpha_composite(base, rim)

mac = make(False); win = make(True)
for sz in (1024, 512, 256, 128, 64, 48, 32, 24, 16):
    mac.resize((sz, sz), Image.LANCZOS).save(f"{OUT}/icon-{sz}.png", optimize=True)
win.resize((1024, 1024), Image.LANCZOS).save(f"{OUT}/icon-win-1024.png", optimize=True)
w256 = win.resize((256, 256), Image.LANCZOS)
w256.save(f"{OUT}/UBTune.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
mac.resize((1024, 1024), Image.LANCZOS).save(f"{OUT}/UBTune.icns")
print("icons written")
