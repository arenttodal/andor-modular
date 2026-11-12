#!/usr/bin/env python3
"""
ANDOR MODULAR's materials: ebonized ash and black anodized aluminium.

Writes a skin pack in ANDOR OSP's skin format (manifest.json and one image per piece and
level) whose pieces replace the shared ones of the same name
(osp::plugin::design::Materials::pack):

  housing  ANDOR OSP's rendered housing (the family's bone ceramic body, untouched), with
           - its teak cheeks replaced by ebonized ash: an original, procedural ring-porous
             wood, stained near-black, its straight grain running down the cheek (latewood
             lines in very dark espresso, open earlywood pores in black), satin, lit by the
             shared render's own cheek shading so the silhouette and the light are the family's;
           - the cheeks' ends capped, and the body's top and bottom edges railed, in satin
             bead-blasted black anodized aluminium (a restrained bevel, a warm-neutral graphite
             reflection);
           - eight dark countersunk fasteners (one in each end cap, one near each end of each
             rail).
           Nothing is added inside the body: the rails sit on its top and bottom edges, where
           the shared render has its rim and shadow.
  badge-A  layer A's badge in the accent Modular already uses (the shared render's light and
           shadow kept: only its colour moves), so the first source's badge matches its arcs.

No photograph and no third-party texture is used: everything new is computed here, from a
fixed seed (the same bytes every run with the same numpy and Pillow).

Usage: tools/make_ebonized_ash_skin.py <ANDOR OSP's ceramic-skin.zip> <output zip>
       [--preview <dir>]   (also writes crops for review)

Build:  python3 tools/make_ebonized_ash_skin.py \\
            external/osp/shared/andor-ui/Resources/skin/ceramic-skin.zip \\
            product/Resources/ebonized-ash-skin.zip --preview docs/materials
"""

import io
import json
import sys
import zipfile

import numpy as np
from PIL import Image

SEED = 20261112

# ---- The material specification (docs/MATERIALS.md) -------------------------------------
# Ebonized ash: iron-stained, never a flat black: the pores are black, the dense latewood a
# very dark espresso that takes a little satin light.
ASH_PORE = np.array([0x07, 0x06, 0x05], dtype=np.float32)
ASH_SHADOW = np.array([0x10, 0x0D, 0x0B], dtype=np.float32)
ASH_BASE = np.array([0x21, 0x1B, 0x17], dtype=np.float32)
ASH_LIGHT = np.array([0x44, 0x36, 0x2B], dtype=np.float32)
ASH_SHEEN = np.array([26.0, 22.0, 18.0], dtype=np.float32)       # the satin highlight (added)

# Black anodized aluminium, bead-blasted: neutral graphite, a touch warm in its reflection.
ALU_TOP = np.array([0x2B, 0x2A, 0x29], dtype=np.float32)
ALU_BOTTOM = np.array([0x1A, 0x1A, 0x1A], dtype=np.float32)
ALU_BEVEL_LIGHT = np.array([0x6A, 0x66, 0x61], dtype=np.float32)
ALU_BEVEL_DARK = np.array([0x09, 0x09, 0x09], dtype=np.float32)
ALU_REFLECTION = np.array([15.0, 14.0, 12.0], dtype=np.float32)  # (added, a broad soft band)

# Layer A's badge: the accent Modular's interface already uses (Theme.cpp, applyMaterials).
BADGE_FACE = np.array([0xC8, 0x6A, 0x3C], dtype=np.float32)

# Geometry, in the housing render's own pixels (2x the 1448 x 1086 reference).
CAP = 34          # an end cap's length along the cheek
RAIL = 16         # a rail's height
SCREW_CAP = 13    # fastener diameters
SCREW_RAIL = 9
RAIL_SCREW_INSET = 76   # from the cheek, inside the nine-slice's fixed corner (60 ref px)


def blur(a, sigma):
    """Separable Gaussian blur of a 2-D float array (edges held)."""
    if sigma <= 0:
        return a
    radius = int(np.ceil(3 * sigma))
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    k /= k.sum()

    def along(arr, axis):
        pad = [(0, 0)] * arr.ndim
        pad[axis] = (radius, radius)
        padded = np.pad(arr, pad, mode="edge")
        out = np.zeros_like(arr, dtype=np.float32)
        n = arr.shape[axis]
        for i, w in enumerate(k):
            out += w * np.take(padded, np.arange(i, i + n), axis=axis)
        return out

    return along(along(a.astype(np.float32), 0), 1)


def smooth_noise_1d(n, scale, rng):
    """Value noise along one axis: random values every `scale` samples, cosine-interpolated."""
    count = int(np.ceil(n / scale)) + 2
    values = rng.standard_normal(count)
    x = np.arange(n) / scale
    i = np.floor(x).astype(int)
    f = x - i
    f = (1 - np.cos(np.pi * f)) / 2
    return values[i] * (1 - f) + values[i + 1] * f


def ash_tone(width, height, rng):
    """Rift-sawn ash, grain running down the cheek. Ash is ring-porous: each growth ring is a
    band of large open pores (earlywood) against dense, smooth latewood, so its grain reads as
    straight, strongly drawn lines even under a black stain. Returns (tone 0..1, pores 0..1):
    tone 0 the stained earlywood, 1 the latewood where it takes the light; pores the open
    vessels (cut along their length: short dashes), which stay black and matte."""
    x = np.arange(width, dtype=np.float32)[None, :]
    # The grain is nearly straight: a slow lean over the whole length, a slight waver.
    sway = smooth_noise_1d(height, 1300.0, rng)[:, None] * 4.0 + smooth_noise_1d(height, 240.0, rng)[:, None] * 0.9
    u = x + sway
    # Growth rings of uneven width (from hairlines to broad bands): the ring index along u.
    widths = 1.8 + 13.0 * rng.beta(1.1, 3.2, width + 160)
    edges = np.cumsum(widths)
    ring = np.interp(u, np.arange(-80, width + 80) * 1.0, np.interp(np.arange(-80, width + 80) + 80.0, edges, np.arange(edges.size)))
    phase = ring - np.floor(ring)                      # 0 at the ring's earlywood, 1 at its end
    # Earlywood: the first third of the ring, darker and full of pores; then latewood, which
    # brightens toward the ring's end.
    early = np.clip(1.0 - phase / 0.34, 0.0, 1.0)
    late = np.clip((phase - 0.34) / 0.66, 0.0, 1.0) ** 0.8
    # Every ring has its own density (some lines are bold, some barely there).
    ring_id = np.floor(ring).astype(int) % 997
    weight = (0.12 + 0.88 * rng.random(997) ** 1.3)[ring_id]
    tone = 0.42 + 0.34 * late * weight - 0.20 * early
    # Broad figure: slow drifts of lighter and darker wood, along and across.
    tone += 0.07 * smooth_noise_1d(height, 700.0, rng)[:, None]
    tone += 0.06 * smooth_noise_1d(width + 64, 11.0, rng)[None, 16:16 + width] * (0.6 + 0.4 * smooth_noise_1d(height, 380.0, rng)[:, None])
    # Fibre: fine streaks stretched along the grain.
    fibre = rng.standard_normal((height // 10 + 2, width)).astype(np.float32)
    fibre = np.asarray(Image.fromarray(fibre).resize((width, height), Image.BICUBIC))
    tone += 0.030 * fibre
    # Pores: dashes along the grain, far more of them in the earlywood.
    pores = np.zeros((height, width), dtype=np.float32)
    count = int(width * height / 150)
    px = rng.integers(0, width, count)
    py = rng.integers(0, height, count)
    length = rng.integers(6, 34, count)
    depth = rng.uniform(0.35, 1.0, count)
    keep = rng.random(count) < (0.12 + 0.88 * early[np.minimum(py, height - 1), px])
    for i in np.where(keep)[0]:
        pores[py[i]:py[i] + length[i], px[i]] += depth[i]
    pores = np.clip(blur(pores, 0.55), 0.0, 1.0)
    return np.clip(tone, 0.0, 1.0), pores


def ash_colour(tone, pores):
    t = tone[..., None]
    low = ASH_SHADOW + (ASH_BASE - ASH_SHADOW) * np.clip(t / 0.5, 0, 1)
    high = ASH_BASE + (ASH_LIGHT - ASH_BASE) * np.clip((t - 0.5) / 0.5, 0, 1)
    wood = np.where(t < 0.5, low, high)
    p = (0.85 * pores)[..., None]
    return wood * (1.0 - p) + ASH_PORE * p


def aluminium(height, width, rng, vertical):
    """A satin, bead-blasted black anodized bar: `vertical` False: a rail (lit across its
    height); True: an end cap (the caller shades it with the cheek's own roundness)."""
    t = (np.arange(height, dtype=np.float32) / max(1, height - 1))[:, None]
    base = ALU_TOP[None, None, :] + (ALU_BOTTOM - ALU_TOP)[None, None, :] * (t[..., None] if not vertical else 0.45)
    out = np.broadcast_to(base, (height, width, 3)).copy()
    if not vertical:
        # the graphite reflection: one broad soft band a third of the way down
        band = np.exp(-0.5 * ((t - 0.34) / 0.2) ** 2)
        out += band[..., None] * ALU_REFLECTION
    # bead blast: a fine, even grain (no direction)
    grain = blur(rng.standard_normal((height, width)).astype(np.float32), 0.6)
    out += (grain * 3.2)[..., None]
    return out


def screw(diameter, scale=6):
    """A dark countersunk hex-socket fastener, lit from the top left: returns (rgb, alpha)
    of diameter x diameter."""
    n = diameter * scale
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    cx = cy = (n - 1) / 2.0
    dx, dy = (x - cx) / (n / 2.0), (y - cy) / (n / 2.0)
    r = np.sqrt(dx * dx + dy * dy)
    alpha = np.clip((1.0 - r) * n / 2.0, 0.0, 1.0)
    # The countersink's wall faces inward: its lower right catches the light.
    facing = np.clip((dx + dy) / (np.maximum(r, 1e-3) * 1.414), -1.0, 1.0)
    wall = 30.0 + 26.0 * facing
    head = 23.0 - 7.0 * (dx + dy) * 0.5            # the head's flat top, a touch lighter top left
    value = np.where(r > 0.70, wall, head)
    # the hex socket
    angle = np.arctan2(dy, dx)
    hexagon = r * np.cos((angle + np.pi / 6.0) % (np.pi / 3.0) - np.pi / 6.0)
    socket = hexagon < 0.36
    value = np.where(socket, 6.0 + 9.0 * np.clip(facing, 0, 1) * (hexagon > 0.27), value)
    # a hairline where the head meets the countersink
    value = np.where(np.abs(r - 0.70) < 0.035, 10.0, value)
    rgb = value[..., None] * np.array([1.0, 0.985, 0.96], dtype=np.float32)

    def down(a):
        return a.reshape(diameter, scale, diameter, scale, -1).mean(axis=(1, 3))

    return down(rgb), down(alpha[..., None])[..., 0]


def paste(canvas, rgb, alpha, cx, cy):
    h, w = alpha.shape
    x0, y0 = int(round(cx - w / 2.0)), int(round(cy - h / 2.0))
    region = canvas[y0:y0 + h, x0:x0 + w]
    region[:] = region * (1.0 - alpha[..., None]) + rgb * alpha[..., None]


def ebonized_housing(housing):
    """The shared housing with ebonized ash cheeks, aluminium end caps and rails."""
    rgb = np.asarray(housing.convert("RGB")).astype(np.float32)
    h, w, _ = rgb.shape
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    luminance = 0.299 * r + 0.587 * g + 0.114 * b
    # The teak: warm, saturated pixels in the outer bands only (the cheeks).
    warmth = np.clip((r - b - 30.0) / 30.0, 0.0, 1.0)
    edge = int(0.05 * w)
    band = np.zeros((h, w), dtype=np.float32)
    band[:, :edge] = 1.0
    band[:, w - edge:] = 1.0
    mask = np.clip(blur(warmth * band, 0.7) * 1.15, 0.0, 1.0)

    rows = slice(int(0.1 * h), int(0.9 * h))
    sides = []
    for side, inner_on_right in ((slice(0, edge), True), (slice(w - edge, w), False)):
        weight = mask[:, side]
        cols = np.where(weight[rows].mean(axis=0) > 0.5)[0]
        ys = np.where(weight[:, cols].mean(axis=1) > 0.5)[0]
        sides.append((side, inner_on_right, side.start + cols.min(), side.start + cols.max(), ys.min(), ys.max()))

    # The render's own light on each cheek along its length (the rounded ends, the recess).
    lit = np.ones((h, w), dtype=np.float32)
    form = np.ones((h, w), dtype=np.float32)
    for side, inner_on_right, left, right, top, bottom in sides:
        cheek = luminance[:, side]
        weight = mask[:, side]
        row_mean = np.nansum(cheek * weight, axis=1) / np.maximum(1e-3, weight.sum(axis=1))
        along = row_mean / np.median(row_mean[rows])
        along = blur(np.repeat(along[:, None], 3, axis=1), 3.0)[:, 1]
        lit[:, side] = np.clip(along[:, None], 0.45, 1.2)
        # A rounded post: lit from the front, leaning to its inner edge.
        x = np.arange(side.stop - side.start, dtype=np.float32) + side.start
        t = np.clip((x - left) / max(1.0, right - left), 0.0, 1.0) * 2.0 - 1.0
        if not inner_on_right:
            t = -t
        normal_z = np.sqrt(np.clip(1.0 - (0.92 * t) ** 2, 0.0, 1.0))
        form[:, side] = (0.50 + 0.50 * normal_z + 0.14 * t)[None, :]

    rng = np.random.default_rng(SEED)
    out = rgb.copy()
    for side, inner_on_right, left, right, top, bottom in sides:
        width = side.stop - side.start
        tone, pores = ash_tone(width, h, rng)
        wood = ash_colour(tone, pores)
        shade = (np.clip(lit[:, side], 0.5, 1.2)[..., None] ** 0.7) * (form[:, side][..., None] ** 1.5) * 1.12
        shaded = wood * shade
        # Satin: a soft, broad highlight down the face, held back in the pores (they are matte),
        # and a hairline of light where the inner edge turns.
        sheen = np.clip((form[:, side] - 0.96) / 0.18, 0.0, 1.0) ** 2.0
        shaded += (sheen * (1.0 - 0.8 * pores) * (0.55 + 0.9 * tone))[..., None] * ASH_SHEEN
        x = np.arange(width, dtype=np.float32) + side.start
        inner = right if inner_on_right else left
        hairline = np.exp(-0.5 * ((x - (inner + (-2.0 if inner_on_right else 2.0))) / 0.9) ** 2)
        shaded += hairline[None, :, None] * np.array([20.0, 17.0, 14.0], dtype=np.float32)

        # End caps: aluminium over the cheek's two ends, rounded with it.
        cap = aluminium(h, width, rng, vertical=True) * (form[:, side][..., None] ** 0.9) * 1.04
        y = np.arange(h, dtype=np.float32)[:, None]
        in_cap = ((y < top + CAP) | (y > bottom - CAP)).astype(np.float32)
        # the seam between metal and wood: a dark line, then the cap's lit edge
        for seam, direction in ((top + CAP, -1.0), (bottom - CAP, 1.0)):
            line = np.exp(-0.5 * ((y - seam) / 0.8) ** 2)
            lip = np.exp(-0.5 * ((y - (seam + direction * 2.0)) / 0.8) ** 2)
            cap = cap * (1.0 - 0.75 * line[..., None]) + lip[..., None] * (ALU_BEVEL_LIGHT - ALU_BOTTOM) * 0.42
            shaded = shaded * (1.0 - 0.65 * np.exp(-0.5 * ((y - (seam - direction * 1.5)) / 1.6) ** 2)[..., None])
        # the caps' outer ends keep the render's roll-off
        cap *= np.clip(lit[:, side], 0.55, 1.1)[..., None] ** 0.5
        material = shaded * (1.0 - in_cap[..., None]) + cap * in_cap[..., None]
        m = mask[:, side][..., None]
        out[:, side] = out[:, side] * (1.0 - m) + material * m

    # The recess beside each cheek: the teak's warm tint on its dark rim goes with it.
    near = np.clip(blur(mask, 3.0) * 6.0, 0.0, 1.0)
    rim = (1.0 - mask) * near * np.clip((r - b - 24.0) / 24.0, 0.0, 1.0)
    grey = luminance[..., None] * np.array([0.96, 0.94, 0.92], dtype=np.float32)
    out = out * (1.0 - rim[..., None] * 0.8) + (grey * 0.74) * (rim[..., None] * 0.8)

    # The rails: along the body's top edge (between the cheeks' caps) and its bottom edge.
    (_, _, l_left, l_right, top, bottom), (_, _, r_left, r_right, _, _) = sides
    x0, x1 = l_right - 1, r_left + 2   # (tucked a little under the caps: no light between them)
    centre = w // 2
    body_top = int(np.argmax(np.abs(np.diff(luminance[:120, centre])) > 6.0)) + 1          # the body's lit top edge
    foot = luminance[h - 80:, centre]
    body_bottom = h - 80 + int(np.argmin(foot))                                              # its dark foot line
    rails = ((body_top - RAIL, body_top), (body_bottom - RAIL + 2, body_bottom + 2))
    for y0, y1 in rails:
        bar = aluminium(y1 - y0, x1 - x0, rng, vertical=False)
        bar[0] = bar[0] * 0.35 + ALU_BEVEL_LIGHT * 0.65          # the lit top bevel
        bar[1] = bar[1] * 0.7 + ALU_BEVEL_LIGHT * 0.3
        bar[-1] = ALU_BEVEL_DARK
        bar[-2] = bar[-2] * 0.6 + ALU_BEVEL_DARK * 0.4
        # where a rail meets a cap: a fine joint
        bar[:, 0] = bar[:, 0] * 0.45
        bar[:, -1] = bar[:, -1] * 0.45
        out[y0:y1, x0:x1] = bar
        # its soft shadow on what lies under it
        for i in range(1, 9):
            if y1 - 1 + i < h:
                out[y1 - 1 + i, x0:x1] *= 1.0 - 0.26 * np.exp(-i / 2.6)
    # a rail stands a little proud of the surround above it too
    ty0 = rails[0][0]
    for i in range(1, 5):
        out[ty0 - i, x0:x1] *= 1.0 - 0.10 * np.exp(-i / 1.6)

    # Fasteners: one in each end cap, one near each end of each rail.
    cap_screw = screw(SCREW_CAP)
    rail_screw = screw(SCREW_RAIL)
    for _, _, left, right, top_, bottom_ in ((None, None, l_left, l_right, top, bottom), (None, None, r_left, r_right, top, bottom)):
        cx = (left + right) / 2.0 + 0.5
        paste(out, *cap_screw, cx, top_ + CAP / 2.0 + 2.0)
        paste(out, *cap_screw, cx, bottom_ - CAP / 2.0 - 1.0)
    for y0, y1 in rails:
        cy = (y0 + y1) / 2.0
        paste(out, *rail_screw, x0 + RAIL_SCREW_INSET, cy)
        paste(out, *rail_screw, x1 - RAIL_SCREW_INSET, cy)

    geometry = {"cheekLeft": [int(l_left), int(l_right)], "cheekRight": [int(r_left), int(r_right)], "cheekY": [int(top), int(bottom)],
                "railTop": [int(rails[0][0]), int(rails[0][1])], "railBottom": [int(rails[1][0]), int(rails[1][1])], "railX": [int(x0), int(x1)]}
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGB"), geometry


def tinted_badge(badge, face_hex):
    """The badge in Modular's accent: each saturated pixel's colour scaled from the rendered
    face (light and shading kept); the neutral shadow as it was."""
    rgba = np.asarray(badge.convert("RGBA")).astype(np.float32)
    rgb, alpha = rgba[..., :3], rgba[..., 3:]
    face = np.array([int(face_hex[i:i + 2], 16) for i in (1, 3, 5)], dtype=np.float32)
    saturation = (rgb.max(axis=2) - rgb.min(axis=2)) / np.maximum(1.0, rgb.max(axis=2))
    weight = np.clip((saturation - 0.12) / 0.25, 0.0, 1.0)[..., None]
    scaled = rgb * (BADGE_FACE / face)[None, None, :]
    out = rgb * (1.0 - weight) + scaled * weight
    return Image.fromarray(np.concatenate([np.clip(out, 0, 255), alpha], axis=2).astype(np.uint8), "RGBA")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    source, target = sys.argv[1], sys.argv[2]
    preview = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    with zipfile.ZipFile(source) as z:
        manifest = json.loads(z.read("manifest.json"))
        pieces = {p["name"]: p for p in manifest["pieces"]}
        out_pieces = []
        files = {}
        housing = dict(pieces["housing"])
        level = housing["levels"][0]
        image, geometry = ebonized_housing(Image.open(io.BytesIO(z.read(level["file"]))))
        buffer = io.BytesIO()
        image.save(buffer, "JPEG", quality=94, subsampling=0, optimize=True)
        files[level["file"]] = buffer.getvalue()
        out_pieces.append(housing)
        if preview:
            s = level["scale"]
            image.crop((0, 0, 360, 420)).resize((720, 840), Image.LANCZOS).save(f"{preview}/ebonized-ash-corner-top-left.png")
            image.crop((image.width - 360, image.height - 420, image.width, image.height)).resize((720, 840), Image.LANCZOS).save(f"{preview}/ebonized-ash-corner-bottom-right.png")
            image.crop((0, 700, 120, 1500)).resize((360, 2400), Image.LANCZOS).save(f"{preview}/ebonized-ash-cheek-grain.png")
            image.resize((image.width // s, image.height // s), Image.LANCZOS).save(f"{preview}/ebonized-ash-housing.png")
        badge = dict(pieces["badge-A"])
        for level in badge["levels"]:
            tinted = tinted_badge(Image.open(io.BytesIO(z.read(level["file"]))), badge["face"])
            buffer = io.BytesIO()
            tinted.save(buffer, "PNG", optimize=True)
            files[level["file"]] = buffer.getvalue()
        badge["face"] = "#%02x%02x%02x" % tuple(int(v) for v in BADGE_FACE)
        out_pieces.append(badge)

    out_manifest = {"schemaVersion": manifest.get("schemaVersion", 1), "name": "modular-ebonized-ash",
                    "direction": "andor-modular-ebonized-ash (derived from " + manifest.get("direction", "") + ")",
                    "pieces": out_pieces}
    # (fixed timestamps: the same bytes every run)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_STORED) as z:
        def write(name, data):
            z.writestr(zipfile.ZipInfo(name, date_time=(2026, 11, 12, 0, 0, 0)), data)
        write("manifest.json", json.dumps(out_manifest, indent=1))
        for name, data in sorted(files.items()):
            write(name, data)
    print("wrote", target, sum(len(d) for d in files.values()), "bytes of images")
    print("geometry (housing pixels):", json.dumps(geometry))


if __name__ == "__main__":
    main()
