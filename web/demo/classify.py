#!/usr/bin/env python3
"""Buckets bench captures by what they actually look like.

The peak finder's fault is run to run, so the question is never "is this frame right" but "how many
distinct frames does the same URL produce, and how often". Grouping by an exact pixel match answers
both: identical runs collapse into one bucket, and a bucket's size is the rate.

    python3 classify.py /tmp/run_*.png
"""

import sys
from PIL import Image, ImageChops, ImageStat


def contrast(image):
    """Shading energy. A tile that lost its normals renders flat, so its band's deviation collapses."""
    return ImageStat.Stat(image.convert('L')).stddev[0]


def main(paths):
    images = [(path, Image.open(path).convert('RGB')) for path in paths]
    buckets = []
    for path, image in images:
        for bucket in buckets:
            if ImageChops.difference(bucket['image'], image).getbbox() is None:
                bucket['members'].append(path)
                break
        else:
            buckets.append({'image': image, 'members': [path]})

    buckets.sort(key=lambda b: -len(b['members']))
    print(f'{len(images)} runs, {len(buckets)} distinct frames')
    for index, bucket in enumerate(buckets):
        print(f'  [{index}] x{len(bucket["members"])} contrast {contrast(bucket["image"]):6.2f}  '
              f'{", ".join(m.split("/")[-1] for m in bucket["members"])}')

    if len(buckets) > 1:
        base = buckets[0]['image']
        for index, bucket in enumerate(buckets[1:], start=1):
            difference = ImageChops.difference(base, bucket['image'])
            box = difference.getbbox()
            changed = sum(difference.convert('L').histogram()[1:])
            print(f'  [0] vs [{index}]: {changed} px differ, bbox {box}')
            difference.convert('L').point(lambda v: min(255, v * 4)).save(f'/tmp/classify-diff-{index}.png')


if __name__ == '__main__':
    main(sys.argv[1:])
