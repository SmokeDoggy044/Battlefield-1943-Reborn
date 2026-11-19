"""The pictures of Reborn's own achievements (bf/src/reborn_achievements.cpp): 64 x 64 RGBA PNGs, the size and format
of the game's own achievement pictures, as perforated postage stamps with a small printed scene of our own drawing.
Nothing here comes from the game: every shape is drawn below.

    python tools/make_reborn_achievements.py            writes bf/data/achievements/101.png, 102.png, 103.png
    python tools/make_reborn_achievements.py --sheet x.png   also a sheet of the three at 4x, for a look

Needs Pillow. Each picture is drawn at 8x and scaled down, so the edges are smooth.
"""

import argparse
import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZE = 64
SS = 8                      # supersampling
S = SIZE * SS
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'bf', 'data', 'achievements')

PAPER = (243, 240, 230, 255)
FRAME = (52, 50, 44, 255)


def px(v):
    return int(round(v * SS))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def vertical_gradient(w, h, top, bottom):
    g = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(g)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(top, bottom, y / max(1, h - 1)))
    return g


def radial_glow(w, h, cx, cy, r, colour):
    """A soft round light: colour at the centre, fading out by radius r."""
    mask = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(mask)
    steps = 40
    for i in range(steps, 0, -1):
        t = i / steps
        d.ellipse([cx - r * t, cy - r * t, cx + r * t, cy + r * t], fill=int(255 * (1 - t) ** 1.6))
    layer = Image.new('RGBA', (w, h), colour)
    layer.putalpha(mask)
    return layer


def stamp_mask():
    """The stamp's outline: a square with round bites out of its edges (the perforation)."""
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    inset = px(1)
    d.rectangle([inset, inset, S - inset - 1, S - inset - 1], fill=255)
    holes = 9
    r = px(1.75)
    span = S - 2 * inset
    for i in range(holes + 1):
        c = inset + span * i / holes
        for x, y in ((c, inset), (c, S - inset - 1), (inset, c), (S - inset - 1, c)):
            d.ellipse([x - r, y - r, x + r, y + r], fill=0)
    return m


def grain(img, amount, seed):
    """Printed paper: a fine, faint noise over the scene."""
    rnd = random.Random(seed)
    w, h = img.size
    n = Image.new('L', (w // SS, h // SS))
    n.putdata([128 + int(rnd.gauss(0, amount)) for _ in range(n.size[0] * n.size[1])])
    n = n.resize((w, h), Image.BILINEAR)
    rgb = img.convert('RGB')
    noisy = ImageChops.add(rgb, Image.merge('RGB', (n, n, n)), 1.0, -128)
    out = noisy.convert('RGBA')
    out.putalpha(img.getchannel('A'))
    return out


def tone(img, sat):
    """The muted, slightly warm print of a wartime stamp: colour pulled toward a sepia grey."""
    rgb = img.convert('RGB')
    grey = rgb.convert('L')
    sepia = Image.merge('RGB', (grey.point(lambda v: min(255, int(v * 1.04 + 6))),
                                grey.point(lambda v: int(v * 0.98 + 2)),
                                grey.point(lambda v: int(v * 0.86))))
    out = Image.blend(sepia, rgb, sat).convert('RGBA')
    out.putalpha(img.getchannel('A'))
    return out


def vignette(img, strength):
    w, h = img.size
    mask = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(mask)
    steps = 30
    for i in range(steps):
        t = i / steps
        a = int(255 * strength * (1 - t) ** 2)
        d.rectangle([w * t / 2.6, h * t / 2.6, w - w * t / 2.6, h - h * t / 2.6], outline=a, width=max(1, int(w / 2.6 / steps) + 1))
    mask = mask.filter(ImageFilter.GaussianBlur(w / 30))
    dark = Image.new('RGBA', (w, h), (20, 18, 12, 255))
    dark.putalpha(mask)
    return Image.alpha_composite(img, dark)


# ---------------------------------------------------------------- the three scenes (drawn on the inner picture)

def feather(d, root, angle, length, width, fill):
    """One long feather: a pointed leaf from `root` along `angle` (radians)."""
    x, y = root
    ca, sa = math.cos(angle), math.sin(angle)
    nx, ny = -sa, ca
    pts = []
    for k in range(13):
        t = k / 12
        half = width * math.sin(math.pi * t) ** 0.8 * (1 - 0.35 * t)
        pts.append((x + ca * length * t + nx * half, y + sa * length * t + ny * half))
    for k in range(12, -1, -1):
        t = k / 12
        half = width * math.sin(math.pi * t) ** 0.8 * (1 - 0.35 * t)
        pts.append((x + ca * length * t - nx * half * 0.4, y + sa * length * t - ny * half * 0.4))
    d.polygon(pts, fill=fill)


def scene_reborn(w, h):
    """I am reborn: a phoenix rising out of its flames, wings up, on a deep red field."""
    img = vertical_gradient(w, h, (70, 34, 30, 255), (150, 64, 36, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.5, h * 0.78, w * 0.62, (246, 160, 72, 255)))
    d = ImageDraw.Draw(img)
    cx = w * 0.5
    # the flames at the foot: tongues of fire, dark to light
    for colour, scale in (((196, 74, 34, 255), 1.0), ((232, 136, 54, 255), 0.72), ((250, 206, 120, 255), 0.42)):
        pts = [(0, h)]
        tongues = 7
        for i in range(tongues * 2 + 1):
            t = i / (tongues * 2)
            x = w * t
            peak = (i % 2 == 1)
            hgt = h * (0.30 if peak else 0.12) * scale * (1.2 - abs(t - 0.5) * 1.2)
            pts.append((x, h - hgt))
        pts.append((w, h))
        d.polygon(pts, fill=colour)
    # the wings: feathers fanning up and out from the shoulders
    gold, light, deep = (238, 196, 110, 255), (252, 232, 170, 255), (204, 142, 66, 255)
    sy = h * 0.47
    for side in (-1, 1):
        root = (cx + side * w * 0.05, sy)
        for i, (deg, ln) in enumerate(((20, 0.40), (38, 0.44), (56, 0.43), (74, 0.38), (90, 0.30))):
            a = math.radians(-90 + side * (90 - deg))
            feather(d, root, a, w * ln, w * 0.075, deep if i % 2 else gold)
        for i, (deg, ln) in enumerate(((30, 0.26), (52, 0.28), (74, 0.24))):
            a = math.radians(-90 + side * (90 - deg))
            feather(d, root, a, w * ln, w * 0.05, light)
    # the body and the head, rising
    d.ellipse([cx - w * 0.07, sy - h * 0.08, cx + w * 0.07, sy + h * 0.16], fill=gold)
    d.ellipse([cx - w * 0.055, sy - h * 0.20, cx + w * 0.055, sy - h * 0.09], fill=light)
    d.polygon([(cx - w * 0.02, sy - h * 0.165), (cx + w * 0.02, sy - h * 0.165), (cx, sy - h * 0.235)], fill=light)  # the crest
    d.polygon([(cx + w * 0.045, sy - h * 0.15), (cx + w * 0.10, sy - h * 0.135), (cx + w * 0.045, sy - h * 0.12)], fill=deep)  # the beak
    # the tail, three long plumes falling into the fire
    for k, dx in enumerate((-0.07, 0.0, 0.07)):
        feather(d, (cx, sy + h * 0.12), math.radians(90 + dx * 260), h * (0.30 if k == 1 else 0.24), w * 0.05, deep if k == 1 else gold)
    return img


def scene_forever(w, h):
    """I am forever: an endless loop in gold between two laurel branches, on a deep green field."""
    img = vertical_gradient(w, h, (58, 82, 64, 255), (30, 46, 36, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.5, h * 0.45, w * 0.6, (120, 150, 104, 255)))
    d = ImageDraw.Draw(img)
    cx, cy = w * 0.5, h * 0.47
    # laurel: two arcs of leaves meeting at the foot
    leaf = (176, 182, 120, 255)
    leaf_dark = (120, 130, 82, 255)
    for side in (-1, 1):
        for i in range(7):
            t = i / 6
            a = math.radians(115 + 120 * t) if side < 0 else math.radians(65 - 120 * t)
            R = w * 0.40
            x, y = cx + R * math.cos(a), cy + h * 0.05 + R * math.sin(a) * 0.95
            ang = a + (math.pi / 2) * side
            lw, lh = w * 0.085, w * 0.036
            pts = []
            for k in range(16):
                u = 2 * math.pi * k / 16
                ex, ey = lw * math.cos(u), lh * math.sin(u)
                pts.append((x + ex * math.cos(ang) - ey * math.sin(ang), y + ex * math.sin(ang) + ey * math.cos(ang)))
            d.polygon(pts, fill=leaf if i % 2 else leaf_dark)
    # the loop: a lemniscate drawn as a thick stroke, a darker shadow under it
    def loop(scale, dx, dy):
        pts = []
        for k in range(240):
            t = 2 * math.pi * k / 240
            den = 1 + math.sin(t) ** 2
            x = math.cos(t) / den
            y = math.sin(t) * math.cos(t) / den
            pts.append((cx + dx + x * w * scale, cy + dy + y * w * scale * 1.15))
        return pts + pts[:1]
    d.line(loop(0.33, w * 0.012, h * 0.018), fill=(24, 30, 22, 255), width=int(w * 0.095), joint='curve')
    d.line(loop(0.33, 0, 0), fill=(214, 172, 84, 255), width=int(w * 0.085), joint='curve')
    d.line(loop(0.33, -w * 0.006, -h * 0.010), fill=(246, 214, 132, 255), width=int(w * 0.035), joint='curve')
    # three small stars over it: always on
    for i, sx in enumerate((0.34, 0.5, 0.66)):
        star(d, w * sx, h * (0.15 if i == 1 else 0.19), w * (0.045 if i == 1 else 0.035), (238, 226, 184, 255))
    return img


def star(d, x, y, r, fill, inner=0.42, rot=-math.pi / 2):
    pts = []
    for k in range(10):
        rr = r if k % 2 == 0 else r * inner
        a = rot + math.pi * k / 5
        pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
    d.polygon(pts, fill=fill)


def scene_inevitable(w, h):
    """I am inevitable: the host's own command flag, a white star on red, planted on a hill over the sea at dusk."""
    img = vertical_gradient(w, h, (104, 62, 58, 255), (214, 150, 108, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.30, h * 0.62, w * 0.5, (250, 196, 140, 255)))
    d = ImageDraw.Draw(img)
    # the sea
    d.rectangle([0, h * 0.70, w, h], fill=(70, 78, 86, 255))
    for i in range(5):
        y = h * (0.74 + 0.05 * i)
        d.line([(w * (0.05 + 0.13 * (i % 2)), y), (w * (0.32 + 0.1 * (i % 3)), y)], fill=(150, 140, 128, 255), width=max(1, int(h * 0.012)))
    # the hill
    d.polygon([(w * 0.30, h), (w * 0.52, h * 0.66), (w * 0.62, h * 0.62), (w * 0.74, h * 0.64), (w, h * 0.74), (w, h)], fill=(52, 50, 36, 255))
    # the pole
    px0, top, foot = w * 0.62, h * 0.12, h * 0.64
    d.rectangle([px0 - w * 0.014, top, px0 + w * 0.014, foot], fill=(36, 32, 26, 255))
    d.ellipse([px0 - w * 0.03, top - w * 0.03, px0 + w * 0.03, top + w * 0.03], fill=(206, 176, 96, 255))
    # the flag, waving: a strip whose edge follows a sine
    fw, fh = w * 0.34, h * 0.24
    x0, y0 = px0 + w * 0.012, top + h * 0.03
    upper, lower = [], []
    for k in range(25):
        t = k / 24
        wave = math.sin(t * math.pi * 1.6) * h * 0.03 * t
        upper.append((x0 + fw * t, y0 + wave))
        lower.append((x0 + fw * t, y0 + fh + wave * 0.8 - fh * 0.06 * t))
    d.polygon(upper + lower[::-1], fill=(168, 44, 38, 255))
    d.line(upper, fill=(204, 80, 64, 255), width=max(1, int(h * 0.015)))
    star(d, x0 + fw * 0.46, y0 + fh * 0.50 + math.sin(0.46 * math.pi * 1.6) * h * 0.03 * 0.46, fh * 0.34, (244, 236, 214, 255))
    return img


# ---------------------------------------------------------------- the vehicles (104 to 108)

def poly(d, pts, w, h, fill):
    """A polygon given in fractions of the picture."""
    d.polygon([(x * w, y * h) for x, y in pts], fill=fill)


def disc(d, x, y, r, fill, w):
    """A circle at (x, y) of radius r, all in fractions of the picture's width."""
    d.ellipse([(x - r) * w, (y - r) * w, (x + r) * w, (y + r) * w], fill=fill)


def dust(img, w, h, puffs, colour, seed):
    """Soft clouds of dust or smoke: blurred discs (x, y, r, alpha), laid over the picture."""
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rnd = random.Random(seed)
    for x, y, r, a in puffs:
        jx, jy = rnd.uniform(-0.01, 0.01), rnd.uniform(-0.01, 0.01)
        d.ellipse([(x + jx - r) * w, (y + jy - r * 0.8) * h, (x + jx + r) * w, (y + jy + r * 0.8) * h],
                  fill=tuple(colour[:3]) + (a,))
    layer = layer.filter(ImageFilter.GaussianBlur(w / 70))
    return Image.alpha_composite(img, layer)


def rotated(pts, cx, cy, angle, scale):
    ca, sa = math.cos(angle), math.sin(angle)
    return [(cx + (x * ca - y * sa) * scale, cy + (x * sa + y * ca) * scale) for x, y in pts]


def scene_tank(w, h):
    """Steel Behemoth: a heavy tank side on, its long gun forward, rolling over dry ground in its own dust."""
    img = vertical_gradient(w, h, (150, 120, 84, 255), (226, 192, 136, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.72, h * 0.30, w * 0.5, (250, 220, 160, 255)))
    d = ImageDraw.Draw(img)
    # far hills, then the ground
    poly(d, [(0, 0.62), (0.18, 0.55), (0.36, 0.59), (0.58, 0.52), (0.80, 0.57), (1, 0.53), (1, 0.75), (0, 0.75)], w, h,
         (150, 128, 92, 255))
    poly(d, [(0, 0.74), (1, 0.72), (1, 1), (0, 1)], w, h, (112, 92, 60, 255))
    for i in range(4):
        y = 0.80 + 0.05 * i
        d.line([(w * (0.08 + 0.21 * (i % 2)), h * y), (w * (0.45 + 0.17 * (i % 3)), h * y)], fill=(136, 114, 78, 255),
               width=max(1, int(h * 0.012)))
    img = dust(img, w, h, [(0.08, 0.66, 0.12, 190), (0.18, 0.62, 0.10, 170), (0.04, 0.56, 0.08, 120),
                           (0.26, 0.69, 0.08, 150)], (214, 190, 146), 104)
    d = ImageDraw.Draw(img)
    steel, steel_hi, steel_lo, track = (86, 92, 62, 255), (128, 132, 92, 255), (58, 62, 42, 255), (40, 38, 32, 255)
    # the tracks: a long rounded band, and the road wheels in it
    d.rounded_rectangle([w * 0.13, h * 0.585, w * 0.83, h * 0.745], radius=h * 0.08, fill=track)
    for i in range(6):
        disc(d, 0.21 + 0.112 * i, 0.666 * h / w, 0.046, (96, 96, 76, 255), w)
        disc(d, 0.21 + 0.112 * i, 0.666 * h / w, 0.020, (54, 54, 44, 255), w)
    # the hull: the sloped front plate on the right, the fenders over the tracks
    poly(d, [(0.11, 0.60), (0.16, 0.50), (0.68, 0.49), (0.86, 0.565), (0.86, 0.60)], w, h, steel)
    poly(d, [(0.16, 0.50), (0.68, 0.49), (0.70, 0.515), (0.17, 0.525)], w, h, steel_hi)
    poly(d, [(0.11, 0.60), (0.86, 0.60), (0.86, 0.62), (0.11, 0.62)], w, h, steel_lo)
    # the turret, the gun's mantlet and the long gun
    poly(d, [(0.30, 0.50), (0.35, 0.37), (0.56, 0.355), (0.64, 0.41), (0.65, 0.50)], w, h, steel)
    poly(d, [(0.35, 0.37), (0.56, 0.355), (0.60, 0.385), (0.36, 0.40)], w, h, steel_hi)
    d.rectangle([w * 0.41, h * 0.32, w * 0.50, h * 0.365], fill=steel_lo)  # the commander's hatch
    d.rectangle([w * 0.62, h * 0.405, w * 0.68, h * 0.465], fill=steel_lo)
    d.rectangle([w * 0.66, h * 0.418, w * 0.97, h * 0.448], fill=steel)
    d.rectangle([w * 0.66, h * 0.418, w * 0.97, h * 0.426], fill=steel_hi)
    d.rectangle([w * 0.92, h * 0.408, w * 0.985, h * 0.458], fill=steel_lo)  # the muzzle
    star(d, w * 0.25, h * 0.555, w * 0.032, (226, 218, 190, 255))  # a white star on the hull side
    return img


def scene_jeep(w, h):
    """Road Warrior: an open jeep at speed on a dirt road past a palm, a trail of dust behind it."""
    img = vertical_gradient(w, h, (112, 150, 160, 255), (232, 210, 150, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.22, h * 0.36, w * 0.45, (252, 230, 170, 255)))
    d = ImageDraw.Draw(img)
    # a palm on the right, far
    poly(d, [(0.86, 0.66), (0.875, 0.40), (0.865, 0.24), (0.855, 0.24), (0.86, 0.40), (0.845, 0.66)], w, h, (70, 66, 46, 255))
    for a, ln in ((-160, 0.17), (-130, 0.15), (-60, 0.16), (-20, 0.15), (-95, 0.10)):
        feather(d, (w * 0.86, h * 0.24), math.radians(a), w * ln, w * 0.035, (64, 92, 50, 255))
    # the ground and the road
    poly(d, [(0, 0.66), (1, 0.64), (1, 1), (0, 1)], w, h, (128, 120, 72, 255))
    poly(d, [(0, 0.72), (1, 0.70), (1, 0.86), (0, 0.90)], w, h, (184, 158, 112, 255))
    for i in range(3):
        d.line([(w * (0.05 + 0.3 * i), h * (0.79 - 0.01 * i)), (w * (0.17 + 0.3 * i), h * (0.785 - 0.01 * i))],
               fill=(158, 134, 94, 255), width=max(1, int(h * 0.012)))
    img = dust(img, w, h, [(0.06, 0.66, 0.11, 200), (0.14, 0.70, 0.09, 190), (0.02, 0.58, 0.07, 130),
                           (0.20, 0.64, 0.06, 120)], (226, 204, 160), 105)
    d = ImageDraw.Draw(img)
    body, body_hi, body_lo, tyre = (90, 98, 58, 255), (134, 140, 90, 255), (60, 66, 40, 255), (34, 32, 28, 255)
    # the body: the tub at the back, the bonnet forward, the grille at the front
    poly(d, [(0.18, 0.655), (0.19, 0.53), (0.50, 0.53), (0.54, 0.555), (0.85, 0.555), (0.87, 0.575), (0.87, 0.66)], w, h, body)
    poly(d, [(0.54, 0.555), (0.85, 0.555), (0.86, 0.570), (0.54, 0.570)], w, h, body_hi)
    poly(d, [(0.19, 0.53), (0.50, 0.53), (0.505, 0.545), (0.19, 0.545)], w, h, body_hi)
    d.rectangle([w * 0.855, h * 0.575, w * 0.875, h * 0.65], fill=body_lo)
    # the windscreen, up, leaning back
    d.line([(w * 0.53, h * 0.555), (w * 0.49, h * 0.39), (w * 0.50, h * 0.385), (w * 0.545, h * 0.555)], fill=body_lo,
           width=int(w * 0.018), joint='curve')
    # the driver: a helmet and shoulders over the tub's side, an arm to the wheel
    d.ellipse([w * 0.36, h * 0.37, w * 0.45, h * 0.45], fill=(70, 76, 48, 255))
    d.rectangle([w * 0.34, h * 0.43, w * 0.46, h * 0.535], fill=(76, 80, 54, 255))
    d.line([(w * 0.44, h * 0.47), (w * 0.52, h * 0.50)], fill=(60, 62, 44, 255), width=int(w * 0.022))
    # the spare wheel on the back, then the wheels
    d.ellipse([w * 0.12, h * 0.52, w * 0.20, h * 0.65], fill=tyre)
    for cx in (0.31, 0.72):
        disc(d, cx, 0.68 * h / w, 0.095, tyre, w)
        disc(d, cx, 0.68 * h / w, 0.045, (120, 120, 96, 255), w)
        disc(d, cx, 0.68 * h / w, 0.015, tyre, w)
    star(d, w * 0.68, h * 0.605, w * 0.03, (226, 218, 190, 255))
    return img


def scene_boat(w, h):
    """Naval Warfare: a landing boat driving for the beach, spray off its bow, an island on the horizon at dawn."""
    img = vertical_gradient(w, h, (128, 148, 162, 255), (226, 204, 168, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.80, h * 0.50, w * 0.45, (252, 226, 176, 255)))
    d = ImageDraw.Draw(img)
    # the island far off, a palm on it
    poly(d, [(0.52, 0.56), (0.62, 0.50), (0.76, 0.49), (0.90, 0.53), (1, 0.55), (1, 0.57), (0.52, 0.57)], w, h, (92, 100, 86, 255))
    d.line([(w * 0.70, h * 0.50), (w * 0.715, h * 0.40)], fill=(84, 90, 74, 255), width=max(1, int(w * 0.012)))
    for a in (-160, -120, -60, -20):
        feather(d, (w * 0.715, h * 0.40), math.radians(a), w * 0.07, w * 0.018, (84, 96, 76, 255))
    # the sea
    sea = vertical_gradient(w, h - int(h * 0.56), (96, 118, 128, 255), (48, 66, 78, 255))
    img.alpha_composite(sea, (0, int(h * 0.56)))
    d = ImageDraw.Draw(img)
    for i in range(6):
        y = 0.62 + 0.06 * i
        x0 = 0.02 + 0.17 * (i % 3)
        d.line([(w * x0, h * y), (w * (x0 + 0.22), h * y)], fill=(140, 158, 160, 255), width=max(1, int(h * 0.011)))
    poly(d, [(0.02, 0.70), (0.16, 0.655), (0.18, 0.69), (0.04, 0.76)], w, h, (196, 208, 204, 255))  # the wake
    hull, hull_hi, hull_lo = (96, 106, 104, 255), (140, 150, 144, 255), (62, 70, 70, 255)
    # the hull, stern on the left, riding a little bow up
    poly(d, [(0.10, 0.535), (0.72, 0.495), (0.78, 0.50), (0.80, 0.685), (0.14, 0.70)], w, h, hull)
    poly(d, [(0.10, 0.535), (0.72, 0.495), (0.72, 0.515), (0.11, 0.555)], w, h, hull_hi)
    # the bow ramp, a square face toward us
    poly(d, [(0.72, 0.475), (0.88, 0.445), (0.91, 0.655), (0.80, 0.685), (0.78, 0.50)], w, h, hull_lo)
    poly(d, [(0.72, 0.475), (0.88, 0.445), (0.885, 0.465), (0.73, 0.495)], w, h, hull_hi)
    # the helmsman's post and the two machine guns at the stern
    d.rectangle([w * 0.15, h * 0.44, w * 0.24, h * 0.53], fill=hull_lo)
    d.ellipse([w * 0.170, h * 0.395, w * 0.225, h * 0.45], fill=(66, 72, 52, 255))
    for x in (0.28, 0.36):
        d.line([(w * x, h * 0.525), (w * x, h * 0.44)], fill=(40, 42, 40, 255), width=max(1, int(w * 0.012)))
        d.line([(w * x, h * 0.45), (w * (x + 0.07), h * 0.43)], fill=(40, 42, 40, 255), width=max(1, int(w * 0.016)))
    # the spray off the bow
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    for x, y, r in ((0.90, 0.64, 0.07), (0.95, 0.58, 0.05), (0.84, 0.70, 0.07), (0.98, 0.66, 0.05), (0.88, 0.55, 0.04)):
        ld.ellipse([(x - r) * w, (y - r) * h, (x + r) * w, (y + r) * h], fill=(236, 240, 232, 220))
    layer = layer.filter(ImageFilter.GaussianBlur(w / 90))
    img = Image.alpha_composite(img, layer)
    d = ImageDraw.Draw(img)
    d.line([(w * 0.14, h * 0.70), (w * 0.80, h * 0.685)], fill=(214, 222, 218, 255), width=max(1, int(h * 0.016)))
    return img


# A fighter's outline, half of it (the right side), in units of the span: x right, y down, the nose up.
FIGHTER = [(0.000, -0.300), (0.040, -0.270), (0.050, -0.080), (0.470, -0.030), (0.490, 0.010), (0.050, 0.060),
           (0.035, 0.250), (0.170, 0.270), (0.175, 0.300), (0.020, 0.315), (0.000, 0.350)]


def plane_shape(cx, cy, angle, scale, half=FIGHTER):
    full = half + [(-x, y) for x, y in reversed(half)]
    return rotated(full, cx, cy, angle, scale)


def scene_fighter(w, h):
    """Air Superiority: a fighter climbing hard through a clear sky, another far off, clouds below."""
    img = vertical_gradient(w, h, (70, 112, 150, 255), (176, 200, 210, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.78, h * 0.18, w * 0.35, (250, 244, 214, 255)))
    img = dust(img, w, h, [(0.10, 0.88, 0.16, 235), (0.32, 0.92, 0.14, 230), (0.56, 0.90, 0.15, 235), (0.82, 0.93, 0.16, 230),
                           (0.22, 0.80, 0.09, 200), (0.70, 0.82, 0.10, 210), (0.95, 0.80, 0.07, 180)], (240, 240, 232), 107)
    d = ImageDraw.Draw(img)
    d.polygon(plane_shape(w * 0.24, h * 0.30, math.radians(-30), w * 0.20), fill=(70, 80, 92, 255))  # another, far off
    # the fighter: a dark silhouette, the light on its wings' leading edges, the canopy, the propeller's blur
    a, cx, cy, sc = math.radians(32), w * 0.56, h * 0.54, w * 0.92
    d.polygon(plane_shape(cx, cy, a, sc), fill=(54, 62, 70, 255))
    d.polygon(rotated([(-0.47, -0.035), (0.47, -0.035), (0.47, -0.022), (-0.47, -0.022)], cx, cy, a, sc), fill=(122, 134, 140, 255))
    d.polygon(rotated([(-0.022, -0.17), (0.022, -0.17), (0.022, -0.10), (-0.022, -0.10)], cx, cy, a, sc), fill=(146, 168, 180, 255))
    hub = rotated([(0, -0.31)], cx, cy, a, sc)[0]
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse([hub[0] - w * 0.13, hub[1] - w * 0.13, hub[0] + w * 0.13, hub[1] + w * 0.13],
                                  fill=(220, 226, 226, 90))
    img = Image.alpha_composite(img, layer)
    d = ImageDraw.Draw(img)
    for x in (-0.30, 0.30):  # a pale star under each wing
        c = rotated([(x, 0.01)], cx, cy, a, sc)[0]
        d.ellipse([c[0] - w * 0.035, c[1] - w * 0.035, c[0] + w * 0.035, c[1] + w * 0.035], fill=(214, 206, 180, 255))
        star(d, c[0], c[1], w * 0.03, (54, 62, 70, 255), rot=a - math.pi / 2)
    return img


def bomb(d, x, y, angle, size, fill):
    body = [(0, -0.5), (0.18, -0.38), (0.2, 0.1), (0.12, 0.34), (0.26, 0.5), (-0.26, 0.5), (-0.12, 0.34), (-0.2, 0.1),
            (-0.18, -0.38)]
    d.polygon(rotated(body, x, y, angle, size), fill=fill)


def side_bomber(img, d, cx, cy, length, fill):
    """A two engine bomber side on, flying right: the body, the glazed nose, the tail fin, the near engine and its
    propeller's blur. In units of its length."""
    def p(pts):
        return [(cx + x * length, cy + y * length) for x, y in pts]
    d.polygon(p([(-0.50, -0.02), (-0.42, -0.055), (0.30, -0.06), (0.44, -0.035), (0.50, 0.0), (0.44, 0.035),
                 (0.30, 0.045), (-0.40, 0.025)]), fill=fill)
    d.polygon(p([(-0.50, -0.02), (-0.47, -0.20), (-0.39, -0.20), (-0.33, -0.05)]), fill=fill)  # the fin
    d.polygon(p([(-0.52, -0.012), (-0.30, -0.012), (-0.30, 0.012), (-0.52, 0.012)]), fill=fill)  # the tailplane
    d.polygon(p([(-0.06, 0.0), (0.20, 0.0), (0.16, 0.03), (-0.08, 0.03)]), fill=fill)  # the wing's root
    d.ellipse(p([(0.06, 0.005), (0.30, 0.075)]), fill=fill)  # the engine
    d.polygon(p([(0.36, -0.03), (0.44, -0.035), (0.495, -0.005), (0.40, -0.005)]), fill=(150, 120, 100, 255))  # the nose glass
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse(p([(0.295, -0.06), (0.325, 0.14)]), fill=(230, 200, 170, 110))
    img.alpha_composite(layer)


def scene_bombers(w, h):
    """Death From Above: bombers over an island at dusk, their bombs falling, the first hits bursting below."""
    img = vertical_gradient(w, h, (150, 88, 72, 255), (226, 150, 96, 255))
    img = Image.alpha_composite(img, radial_glow(w, h, w * 0.5, h * 0.85, w * 0.6, (250, 186, 110, 255)))
    d = ImageDraw.Draw(img)
    poly(d, [(0, 0.86), (0.18, 0.80), (0.42, 0.82), (0.66, 0.78), (0.86, 0.81), (1, 0.79), (1, 1), (0, 1)], w, h, (58, 48, 38, 255))
    img = dust(img, w, h, [(0.30, 0.74, 0.10, 200), (0.40, 0.70, 0.08, 170), (0.66, 0.72, 0.11, 200), (0.74, 0.66, 0.07, 160)],
               (70, 56, 50), 108)
    # the bursts
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    for x, y, r in ((0.32, 0.80, 0.09), (0.66, 0.79, 0.11)):
        pts = []
        for k in range(16):
            rr = r if k % 2 == 0 else r * 0.5
            ang = math.pi * k / 8 - math.pi / 2
            pts.append((x * w + rr * w * math.cos(ang), y * h + rr * w * math.sin(ang) * 0.8))
        ld.polygon(pts, fill=(250, 196, 96, 235))
        ld.ellipse([(x - r * 0.45) * w, (y - r * 0.35) * h, (x + r * 0.45) * w, (y + r * 0.35) * h], fill=(255, 240, 190, 255))
    layer = layer.filter(ImageFilter.GaussianBlur(w / 160))
    img = Image.alpha_composite(img, layer)
    d = ImageDraw.Draw(img)
    dark = (44, 34, 36, 255)
    for x, y, s in ((0.42, 0.30, 0.78), (0.84, 0.17, 0.40)):  # two bombers side on, flying right
        side_bomber(img, d, w * x, h * y, w * s, dark)
    for x, y, s in ((0.40, 0.44, 0.10), (0.36, 0.56, 0.11), (0.33, 0.68, 0.11), (0.78, 0.32, 0.08), (0.74, 0.44, 0.085),
                    (0.70, 0.56, 0.09)):
        bomb(d, w * x, h * y, math.radians(180 + 14), w * s, dark)  # nose down, a little forward
    return img


SCENES = [
    (101, scene_reborn, 0.80),
    (102, scene_forever, 0.78),
    (103, scene_inevitable, 0.80),
    (104, scene_tank, 0.78),
    (105, scene_jeep, 0.80),
    (106, scene_boat, 0.80),
    (107, scene_fighter, 0.80),
    (108, scene_bombers, 0.80),
]


def make(scene, sat, seed):
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    paper = Image.new('RGBA', (S, S), PAPER)
    out.paste(paper, (0, 0), stamp_mask())
    # the picture inside a thin dark frame, as the game's stamps have
    m0, m1 = px(7), S - px(7)
    d = ImageDraw.Draw(out)
    d.rectangle([m0 - px(0.9), m0 - px(0.9), m1 + px(0.9) - 1, m1 + px(0.9) - 1], fill=FRAME)
    pic = scene(m1 - m0, m1 - m0)
    pic = vignette(pic, 0.45)
    pic = tone(pic, sat)
    out.paste(pic, (m0, m0))
    out = grain(out, 6, seed)
    # a faint shade at the paper's edge, so the stamp reads on light and dark rows alike
    small = out.resize((SIZE, SIZE), Image.LANCZOS)
    return small


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=OUT, help='where the PNGs go (bf/data/achievements)')
    ap.add_argument('--sheet', default='', help='also write a sheet of the three at 4x here')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    made = []
    for ident, scene, sat in SCENES:
        img = make(scene, sat, ident)
        path = os.path.join(args.out, '%d.png' % ident)
        img.save(path, optimize=True)
        made.append(img)
        print('wrote %s (%dx%d RGBA)' % (os.path.normpath(path), img.size[0], img.size[1]))
    if args.sheet:
        sheet = Image.new('RGBA', (len(made) * 4 * SIZE + (len(made) + 1) * 24, 4 * SIZE + 48), (60, 60, 56, 255))
        for i, img in enumerate(made):
            sheet.alpha_composite(img.resize((4 * SIZE, 4 * SIZE), Image.NEAREST), (24 + i * (4 * SIZE + 24), 24))
        sheet.save(args.sheet)
        print('wrote', args.sheet)


if __name__ == '__main__':
    main()
