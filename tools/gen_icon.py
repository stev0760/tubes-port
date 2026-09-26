#!/usr/bin/env python3
"""Draw the port's icon and emit it as a Windows .ico and a C++ pixel table.

The icon is a tilted test tube holding a red, a yellow and a blue atom. It is
original art, drawn here from shapes, and deliberately NOT taken from the
game: it is built into the executable, and this repository carries no game
data. Nothing in it is traced from a sprite in TUBES.RES.

Every size is its own drawing rather than one image scaled down. Each shape is
a signed distance function in unit space (0..1 across the icon); a pixel is
inside a shape when the function is <= 0 at the pixel's centre, and each
shape's outline is its inside eroded by one pixel (two from 64 up). The atoms
are shaded spheres lit from the upper left in a five-step ramp. The same
drawing at 16 px and at 64 px therefore gets a 1 px outline at both, which is
what keeps the small sizes legible.

The 256 px entry is the 64 px drawing scaled up four times with nearest
neighbour, so the large icon keeps the pixel-art look of the rest.

Outputs, both GENERATED - regenerate rather than edit:
    packaging/windows/tubes-port.ico   every size Windows asks for
    src/icon_pixels.cpp                64x64 RGBA for SDL_SetWindowIcon

Usage, from the repository root:
    tools/gen_icon.py                      # write both outputs
    tools/gen_icon.py --preview DIR        # also write one PNG per size

Standard library only: the PNGs inside the .ico are encoded here with zlib.
"""
import argparse
import math
import os
import struct
import sys
import zlib

# ---- palette -------------------------------------------------------------

# Five-step ramps: outline, shadow, body, light, highlight. VGA-style colours
# chosen for the icon, not sampled from the game's palette.
RAMPS = {
    "red":    ["#3a0710", "#8c1322", "#d52c3f", "#ff7383", "#ffe0e3"],
    "yellow": ["#4a3200", "#a87800", "#f2c21c", "#ffe477", "#fffbe0"],
    "blue":   ["#0a1552", "#1a37b8", "#3f72ff", "#9db8ff", "#eef2ff"],
}
ATOMS = ["red", "yellow", "blue"]  # bottom to top
GLASS_OUTLINE = "#1b3a46"
GLASS_FILL = (188, 228, 240, 0.34)
GLASS_SHINE = (255, 255, 255, 0.62)

SIZES = [16, 20, 24, 32, 40, 48, 64]
WINDOW_ICON_SIZE = 64


def rgba(h, a=1.0):
    return (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16), a)


# ---- geometry, unit space ------------------------------------------------

def sd_circle(cx, cy, r):
    return lambda u, v: math.hypot(u - cx, v - cy) - r


def sd_capsule(ax, ay, bx, by, r):
    def f(u, v):
        pax, pay, bax, bay = u - ax, v - ay, bx - ax, by - ay
        h = max(0.0, min(1.0, (pax * bax + pay * bay) / (bax * bax + bay * bay)))
        return math.hypot(pax - bax * h, pay - bay * h) - r
    return f


def union(*fs):
    return lambda u, v: min(f(u, v) for f in fs)


def intersect(*fs):
    return lambda u, v: max(f(u, v) for f in fs)


class Layer:
    def __init__(self, sdf, fill, outline=None):
        self.sdf = sdf
        self.fill = fill          # (u, v) -> (r, g, b, a)
        self.outline = outline    # (r, g, b, a), or None for no outline


def sphere(cx, cy, r, ramp_name):
    ramp = [rgba(h) for h in RAMPS[ramp_name]]
    light = (-0.55, -0.62, 0.56)
    ln = math.sqrt(sum(c * c for c in light))

    def fill(u, v):
        dx, dy = (u - cx) / r, (v - cy) / r
        q = min(1.0, dx * dx + dy * dy)
        nz = math.sqrt(1.0 - q)
        i = (dx * light[0] + dy * light[1] + nz * light[2]) / ln
        if i > 0.94:
            return ramp[4]
        if i > 0.66:
            return ramp[3]
        if i > 0.22:
            return ramp[2]
        return ramp[1]

    return Layer(sd_circle(cx, cy, r), fill, ramp[0])


def test_tube():
    """The glass tube, mouth to the upper right, three atoms stacked inside."""
    a = (0.74, 0.17)            # centre of the mouth
    b = (0.32, 0.74)            # centre of the rounded bottom
    r = 0.23                    # tube radius
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length   # down the tube
    nx, ny = -uy, ux                    # across it, toward the light

    body = intersect(sd_capsule(a[0], a[1], b[0], b[1], r),
                     lambda u, v: -((u - a[0]) * ux + (v - a[1]) * uy))
    lip = sd_capsule(a[0] + nx * (r + 0.012), a[1] + ny * (r + 0.012),
                     a[0] - nx * (r + 0.012), a[1] - ny * (r + 0.012), 0.03)
    glass = union(body, lip)

    layers = [Layer(glass, lambda u, v: GLASS_FILL, rgba(GLASS_OUTLINE))]
    for k, name in enumerate(ATOMS):
        t = 0.27 * k
        layers.append(sphere(b[0] - ux * t, b[1] - uy * t, 0.145, name))

    def shine_band(u, v):
        px, py = u - a[0], v - a[1]
        across = (px * nx + py * ny) / r
        along = (px * ux + py * uy) / length
        return max(abs(across - 0.62) - 0.17, abs(along - 0.5) - 0.4)

    layers.append(Layer(intersect(body, shine_band), lambda u, v: GLASS_SHINE))
    return layers


# ---- rasteriser ----------------------------------------------------------

def render(size, layers):
    """RGBA bytes, straight (not premultiplied) alpha, rows top to bottom."""
    px = [[0.0, 0.0, 0.0, 0.0] for _ in range(size * size)]
    erode = 2 if size >= 64 else 1
    for layer in layers:
        mask = [layer.sdf((x + 0.5) / size, (y + 0.5) / size) <= 0
                for y in range(size) for x in range(size)]
        inner = mask
        if layer.outline is not None:
            for _ in range(erode):
                nxt = [False] * (size * size)
                for y in range(size):
                    for x in range(size):
                        i = y * size + x
                        if not inner[i]:
                            continue
                        if (y > 0 and inner[i - size] and y < size - 1 and inner[i + size]
                                and x > 0 and inner[i - 1] and x < size - 1 and inner[i + 1]):
                            nxt[i] = True
                inner = nxt
        for y in range(size):
            for x in range(size):
                i = y * size + x
                if not mask[i]:
                    continue
                c = (layer.fill((x + 0.5) / size, (y + 0.5) / size)
                     if inner[i] else layer.outline)
                a, dst = c[3], px[i]
                da = dst[3]
                out_a = a + da * (1 - a)
                for ch in range(3):
                    dst[ch] = ((c[ch] * a + dst[ch] * da * (1 - a)) / out_a) if out_a else 0.0
                dst[3] = out_a
    out = bytearray()
    for r_, g, b_, a in px:
        out += bytes((round(r_), round(g), round(b_), round(a * 255)))
    return bytes(out)


def upscale(pixels, size, factor):
    out = bytearray()
    for y in range(size * factor):
        row = pixels[(y // factor) * size * 4:((y // factor) + 1) * size * 4]
        for x in range(size):
            out += row[x * 4:x * 4 + 4] * factor
    return bytes(out)


# ---- encoders ------------------------------------------------------------

def png(pixels, size):
    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = b"".join(b"\x00" + pixels[y * size * 4:(y + 1) * size * 4] for y in range(size))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def ico(images):
    """images: list of (size, png_bytes). PNG-in-ICO, which Windows has read
    since Vista, for every entry."""
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for size, data in images:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        blobs += data
        offset += len(data)
    return header + entries + blobs


def cpp_table(pixels, size):
    lines = [
        "// GENERATED by tools/gen_icon.py - do not edit, regenerate.",
        "//",
        "// The window icon: the same test tube as the .exe's, as straight RGBA,",
        "// rows top to bottom. Original art, drawn from shapes by the generator;",
        "// nothing here comes from the game's data.",
        "",
        '#include "icon.h"',
        "",
        "namespace tubes {",
        "",
        f"const int kIconSize = {size};",
        "",
        f"const unsigned char kIconRgba[{size * size * 4}] = {{",
    ]
    for y in range(size):
        row = pixels[y * size * 4:(y + 1) * size * 4]
        for start in range(0, len(row), 16):
            lines.append("    " + ", ".join(str(b) for b in row[start:start + 16]) + ",")
    lines += ["};", "", "}  // namespace tubes", ""]
    return "\n".join(lines)


# ---- main ----------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", default=".", help="repository root (default: .)")
    ap.add_argument("--preview", help="also write one PNG per size into DIR")
    args = ap.parse_args()

    layers = test_tube()
    rendered = {s: render(s, layers) for s in SIZES}
    rendered[256] = upscale(rendered[64], 64, 4)

    ico_path = os.path.join(args.repo, "packaging", "windows", "tubes-port.ico")
    os.makedirs(os.path.dirname(ico_path), exist_ok=True)
    with open(ico_path, "wb") as f:
        f.write(ico([(s, png(rendered[s], s)) for s in sorted(rendered)]))

    cpp_path = os.path.join(args.repo, "src", "icon_pixels.cpp")
    with open(cpp_path, "w", newline="\n") as f:
        f.write(cpp_table(rendered[WINDOW_ICON_SIZE], WINDOW_ICON_SIZE))

    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        for s, pixels in rendered.items():
            with open(os.path.join(args.preview, f"icon-{s}.png"), "wb") as f:
                f.write(png(pixels, s))

    print(f"wrote {ico_path} ({len(rendered)} sizes) and {cpp_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
