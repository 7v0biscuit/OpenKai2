#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draw the two-star Dragon Ball application icon used by the launcher."""
import argparse
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
CANVAS = 1024


def disc(cx, cy, r):
    return [cx - r, cy - r, cx + r, cy + r]


def layer(shape, colour, blur=0):
    """A flat colour masked by shape, optionally softened."""
    mask = Image.new('L', (CANVAS, CANVAS), 0)
    shape(ImageDraw.Draw(mask))
    if blur:
        mask = mask.filter(ImageFilter.GaussianBlur(blur))
    tile = Image.new('RGBA', (CANVAS, CANVAS), colour)
    tile.putalpha(mask)
    return tile


def star(draw, cx, cy, radius, rotation, light, dark):
    """A five-point star split into facets: each point has a lit and a shaded half."""
    tips, valleys = [], []
    for i in range(5):
        angle = rotation - math.pi / 2 + i * 2 * math.pi / 5
        tips.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius))
        angle += math.pi / 5
        valleys.append((cx + math.cos(angle) * radius * 0.43, cy + math.sin(angle) * radius * 0.43))
    centre = (cx, cy)
    for i in range(5):
        before = valleys[i - 1]
        draw.polygon([centre, before, tips[i]], fill=light)
        draw.polygon([centre, tips[i], valleys[i]], fill=dark)


def ball():
    c, r = CANVAS / 2, CANVAS * 0.45
    image = Image.new('RGBA', (CANVAS, CANVAS), (0, 0, 0, 0))
    inside = Image.new('L', (CANVAS, CANVAS), 0)
    ImageDraw.Draw(inside).ellipse(disc(c, c, r), fill=255)

    image.alpha_composite(layer(lambda d: d.ellipse(disc(c, c, r), fill=255), (255, 236, 196, 255)))
    image.alpha_composite(layer(lambda d: d.ellipse(disc(c + r * 0.03, c + r * 0.03, r * 0.95), fill=255),
                                (255, 150, 20, 255)))
    crescent = layer(lambda d: d.ellipse(disc(c + r * 0.07, c + r * 0.07, r * 0.86), fill=255),
                     (246, 92, 10, 255), blur=3)
    cut = Image.new('L', (CANVAS, CANVAS), 255)
    ImageDraw.Draw(cut).ellipse(disc(c - r * 0.06, c - r * 0.1, r * 0.78), fill=0)
    crescent.putalpha(ImageChops.multiply(crescent.getchannel('A'), cut.filter(ImageFilter.GaussianBlur(10))))
    image.alpha_composite(crescent)
    image.alpha_composite(layer(lambda d: d.ellipse(disc(c - r * 0.1, c - r * 0.12, r * 0.66), fill=255),
                                (255, 178, 24, 255), blur=4))
    image.alpha_composite(layer(lambda d: d.ellipse(disc(c - r * 0.2, c - r * 0.24, r * 0.38), fill=255),
                                (255, 214, 90, 150), blur=40))

    stars = Image.new('RGBA', (CANVAS, CANVAS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(stars)
    for sx, sy, rotation in ((-0.1, -0.1, 0.0), (0.3, 0.3, 0.12)):
        star(draw, c + r * sx, c + r * sy, r * 0.235, rotation, (236, 46, 34, 255), (160, 16, 20, 255))
    image.alpha_composite(stars)

    gloss = layer(lambda d: d.ellipse([c - r * 0.84, c - r * 0.9, c - r * 0.14, c - r * 0.3], fill=255),
                  (255, 255, 248, 225), blur=5)
    hollow = Image.new('L', (CANVAS, CANVAS), 255)
    ImageDraw.Draw(hollow).ellipse(disc(c - r * 0.02, c + r * 0.06, r * 0.7), fill=0)
    gloss.putalpha(ImageChops.multiply(gloss.getchannel('A'), hollow.filter(ImageFilter.GaussianBlur(6))))
    image.alpha_composite(gloss)
    image.alpha_composite(layer(lambda d: d.ellipse(disc(c - r * 0.6, c - r * 0.2, r * 0.05), fill=255),
                                (255, 255, 255, 230), blur=2))
    image.putalpha(ImageChops.multiply(image.getchannel('A'), inside))
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args()
    source = ball()
    images = [source.resize((size, size), Image.LANCZOS) for size in SIZES]
    images[-1].save(args.output, format='ICO', sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    if args.preview:
        sheet = Image.new('RGBA', (sum(SIZES[i] for i in (0, 3, 5, 8)) + 40, 256), (14, 20, 31, 255))
        x = 0
        for size in (16, 32, 48, 256):
            frame = source.resize((size, size), Image.LANCZOS)
            sheet.alpha_composite(frame, (x, 256 - size))
            x += size + 10
        sheet.save(args.preview)


if __name__ == '__main__':
    main()
