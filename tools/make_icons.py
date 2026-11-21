#!/usr/bin/env python3
"""Draws the Monolith menu icons (assets/monolith-uns.png, assets/monolith-sel.png).

fx-CG50 menu icons are 92x64. Like Casio's own, this one is a flat rounded
tile with a white pictogram; the top-right corner stays free for the menu
letter and the bottom for the name, both drawn by the OS. The selected icon
sits on the cyan highlight the OS menu uses."""
import os
from PIL import Image, ImageDraw

W, H, S = 92, 64, 4                       # icon size, supersampling factor
TILE = (124, 92, 196)                     # Catppuccin mauve, darkened to sit with Casio's tiles
TILE_EDGE = (98, 70, 160)                 # darker base edge, as on Casio's tiles
TILE_LIGHT = (146, 116, 214)              # light top edge
SIDE = (225, 214, 245)                    # side face of the slab
CYAN_LEFT, CYAN_RIGHT = (99, 207, 255), (41, 190, 255)   # menu highlight
WHITE = (255, 255, 255, 255)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets')

def tile_mask():
    m = Image.new('L', (W * S, H * S), 0)
    ImageDraw.Draw(m).rounded_rectangle([3 * S, 5 * S, 89 * S - 1, 43 * S - 1], radius=4 * S, fill=255)
    return m.resize((W, H), Image.LANCZOS)

def tile():
    big = Image.new('RGBA', (W * S, H * S), TILE + (255,))
    d = ImageDraw.Draw(big)
    d.rectangle([0, 41 * S, W * S, 43 * S], fill=TILE_EDGE + (255,))
    d.rectangle([0, 5 * S, W * S, 6 * S - 1], fill=TILE_LIGHT + (255,))
    gy = 36 * S                                                    # horizon
    d.line([(14 * S, gy), (72 * S, gy)], fill=WHITE, width=2 * S)
    x0, x1, top, side = 40 * S, 49 * S, 8 * S, 4 * S               # the monolith
    d.polygon([(x1, top), (x1 + side, top - 2 * S), (x1 + side, gy - 2 * S), (x1, gy)], fill=SIDE + (255,))
    d.rectangle([x0, top, x1, gy], fill=WHITE)
    d.polygon([(x0, gy + S), (x1, gy + S), (x1 - 10 * S, gy + 4 * S), (x0 - 16 * S, gy + 4 * S)], fill=(255, 255, 255, 80))
    cx, cy, r = 24 * S, 17 * S, 6 * S                              # star rising behind it
    q = r // 4
    d.polygon([(cx, cy - r), (cx + q, cy - q), (cx + r, cy), (cx + q, cy + q),
               (cx, cy + r), (cx - q, cy + q), (cx - r, cy), (cx - q, cy - q)], fill=WHITE)
    d.ellipse([cx + 13 * S, cy - 9 * S, cx + 16 * S, cy - 6 * S], fill=WHITE)
    return big.resize((W, H), Image.LANCZOS).convert('RGB')

def highlight():
    big = Image.new('RGB', (W * S, H * S), (255, 255, 255))
    grad = Image.new('RGB', (W * S, H * S))
    for x in range(W * S):                                         # light on the left, deeper on the right
        t = x / (W * S - 1)
        c = tuple(round(a + (b - a) * t) for a, b in zip(CYAN_LEFT, CYAN_RIGHT))
        ImageDraw.Draw(grad).line([(x, 0), (x, H * S)], fill=c)
    m = Image.new('L', (W * S, H * S), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, W * S - 1, H * S - 1], radius=2 * S, fill=255)
    big.paste(grad, (0, 0), m)
    return big.resize((W, H), Image.LANCZOS)

mask, art = tile_mask(), tile()
for name, bg in (('monolith-uns.png', Image.new('RGB', (W, H), (255, 255, 255))), ('monolith-sel.png', highlight())):
    bg.paste(art, (0, 0), mask)
    bg.save(os.path.join(OUT, name))
    print('wrote assets/' + name)
