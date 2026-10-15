#!/usr/bin/env python3 -I
"""Find labels that blink across a burst of frames from a SLOWLY turning camera (PIL only, no numpy).

  python3 -I analyze-label-flicker.py <frames-dir> [--drop 0.25] [--window 3] [--min-area 40] [--skip 12] [--out <dir>]

<frames-dir> holds frame_NNNN.png as written by `run-catalyst-demo.sh` with BURST=N and a small
STEP_DEG (0.1-0.25 deg: labels move only a few pixels between two frames, which the comparison
tolerates; a faster turn makes every label look like it blinked).

Each frame is reduced to a "label ink" map: pixels that stand out from their surroundings (text,
icons, shields), minus the paths' red and the trees' green. For two consecutive frames, ink that was
there and is now gone (`lost`, beyond a +-3 px search window) or that was not there and now is
(`gained`) is clustered. A BLINK is a loss answered by a gain at the same place within WINDOW frames
(a dip), or a gain answered by a loss (a flash): the label went away or faded and came back, or the
other way round. A control burst with a still camera is the noise floor; it should report none.

Writes <out>/events.txt and <out>/blink_<n>.png: the frames around the event, cropped, side by side.
"""
import argparse
import glob
import os
import sys
from PIL import Image, ImageChops, ImageFilter

REACH = 7          # px: a label may move this much between two frames and still count as the same ink
LEVEL = 110        # ink score above which a pixel is "ink"


def ink_map(path):
    img = Image.open(path).convert("RGB")
    contrast = ImageChops.difference(img, img.filter(ImageFilter.GaussianBlur(8)))
    red, green, blue = contrast.split()
    score = ImageChops.lighter(ImageChops.lighter(red, green), blue).point(lambda v: min(255, v * 3))
    hue = img.convert("HSV").split()[0]
    keep = hue.point(lambda v: 0 if (v <= 8 or v >= 246 or 50 <= v <= 115) else 255)
    return ImageChops.multiply(score, keep)


def clusters(mask, min_area):
    """8-connected clusters of a 0/255 mask, found at 1/4 resolution: [(x0, y0, x1, y1, pixels)]."""
    step = 4
    small = mask.resize((mask.size[0] // step, mask.size[1] // step), Image.BOX).point(lambda v: 255 if v > 0 else 0)
    small = small.filter(ImageFilter.MaxFilter(3))
    width, height = small.size
    data = small.tobytes()
    full = mask.tobytes()
    seen = bytearray(width * height)
    found = []
    for start in range(width * height):
        if not data[start] or seen[start]:
            continue
        stack, cells = [start], []
        seen[start] = 1
        while stack:
            cell = stack.pop()
            cells.append(cell)
            cx, cy = cell % width, cell // width
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < width and 0 <= ny < height:
                        neighbour = ny * width + nx
                        if data[neighbour] and not seen[neighbour]:
                            seen[neighbour] = 1
                            stack.append(neighbour)
        xs = [c % width for c in cells]
        ys = [c // width for c in cells]
        box = (min(xs) * step, min(ys) * step, (max(xs) + 1) * step, (max(ys) + 1) * step)
        pixels = sum(1 for y in range(box[1], min(box[3], mask.size[1])) for x in range(box[0], min(box[2], mask.size[0]))
                     if full[y * mask.size[0] + x])
        if pixels >= min_area:
            found.append(box + (pixels,))
    return found


def near(a, b, margin=14):
    return not (a[2] + margin < b[0] or b[2] + margin < a[0] or a[3] + margin < b[1] or b[3] + margin < a[1])


def crop_row(paths, frames, box, out_path, margin=16):
    crops = []
    for index in frames:
        if 0 <= index < len(paths):
            crops.append(Image.open(paths[index]).convert("RGB").crop(
                (max(0, box[0] - margin), max(0, box[1] - margin), box[2] + margin, box[3] + margin)))
    width = sum(c.size[0] + 4 for c in crops)
    sheet = Image.new("RGB", (width, max(c.size[1] for c in crops)), (255, 0, 255))
    x = 0
    for crop in crops:
        sheet.paste(crop, (x, 0))
        x += crop.size[0] + 4
    sheet.save(out_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("frames")
    parser.add_argument("--drop", type=float, default=0.25, help="ink change (fraction of full scale) that counts")
    parser.add_argument("--window", type=int, default=3, help="frames a blink may last")
    parser.add_argument("--min-area", type=int, default=40, help="ink pixels a cluster needs")
    parser.add_argument("--skip", type=int, default=0, help="ignore events starting before this frame (the first moves re-place every label)")
    parser.add_argument("--out", default=None)
    parser.add_argument("--max-images", type=int, default=15)
    args = parser.parse_args()

    paths = sorted(glob.glob(os.path.join(args.frames, "frame_*.png")))
    if len(paths) < 4:
        sys.exit("need at least 4 frames in %s" % args.frames)
    out = args.out or os.path.join(args.frames, "analysis")
    os.makedirs(out, exist_ok=True)

    cut = int(args.drop * 255)
    lost_by_pair, gained_by_pair = {}, {}
    previous = ink_map(paths[0])
    for index in range(1, len(paths)):
        current = ink_map(paths[index])
        reach = ImageFilter.MaxFilter(REACH)
        lost = ImageChops.subtract(previous, current.filter(reach)).point(lambda v: 255 if v > cut else 0)
        gained = ImageChops.subtract(current, previous.filter(reach)).point(lambda v: 255 if v > cut else 0)
        lost_by_pair[index] = clusters(lost, args.min_area)
        gained_by_pair[index] = clusters(gained, args.min_area)
        previous = current

    events = []
    for index, lost in lost_by_pair.items():
        for box in lost:
            for later in range(index + 1, min(index + 1 + args.window, len(paths))):
                match = next((g for g in gained_by_pair.get(later, []) if near(box, g)), None)
                if match:
                    events.append((index, later, "dip", box, match))
                    break
    for index, gained in gained_by_pair.items():
        for box in gained:
            for later in range(index + 1, min(index + 1 + args.window, len(paths))):
                match = next((g for g in lost_by_pair.get(later, []) if near(box, g)), None)
                if match:
                    events.append((index, later, "flash", box, match))
                    break

    events = [e for e in events if e[0] >= args.skip]
    lost_total = sum(len(v) for v in lost_by_pair.values())
    gained_total = sum(len(v) for v in gained_by_pair.values())
    pairs = len(paths) - 1
    with open(os.path.join(out, "events.txt"), "w") as handle:
        for first, second, kind, box, match in sorted(events):
            handle.write("%s frames %d->%d at (%d,%d)-(%d,%d) ink %d/%d\n" % (kind, first, second, box[0], box[1], box[2], box[3], box[4], match[4]))
    for number, (first, second, kind, box, match) in enumerate(sorted(events, key=lambda e: -(e[3][4] + e[4][4]))[:args.max_images]):
        crop_row(paths, range(first - 2, second + 2), box, os.path.join(out, "blink_%02d.png" % number))

    print("frames %d, pairs %d: lost clusters %d (%.1f/pair), gained clusters %d (%.1f/pair)" % (
        len(paths), pairs, lost_total, lost_total / pairs, gained_total, gained_total / pairs))
    print("blink events: %d (%d dips, %d flashes), %.2f per frame pair" % (
        len(events), sum(1 for e in events if e[2] == "dip"), sum(1 for e in events if e[2] == "flash"), len(events) / pairs))
    by_frame = {}
    for first, _, kind, box, _ in events:
        by_frame.setdefault(first, []).append(kind)
    print("frames with an event:", ", ".join("%d(%d)" % (k, len(v)) for k, v in sorted(by_frame.items())[:25]) or "none")
    print("written to", out)


main()
