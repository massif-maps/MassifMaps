#!/usr/bin/env python3
"""
Captures one screenshot per example, into docs/examples/screenshots/<id>.png.

That directory is the ONE home for them: the gradle build ships it as an APK asset so the gallery
grid has thumbnails offline, and the website reads the same files. Run this after adding an
example, then re-run gen-examples.py so the manifest records the capture.

  python3 scripts/capture-examples.py                 # every example
  python3 scripts/capture-examples.py markers fly-to  # only these
  python3 scripts/capture-examples.py --settle 40     # slow network / cold caches

Tiles need time to settle before a screenshot means anything - network, then cache, then label
placement. The default is deliberately generous; a capture that looks empty needs MORE, not a
retry.
"""

import argparse
import json
import os
import subprocess
import sys

try:
  from PIL import Image
except ImportError:
  Image = None

PACKAGE = 'com.massifmaps.MassifDemo'
ACTIVITY = PACKAGE + '/.ExampleActivity'


def adb(device, *args):
  command = ['adb'] + (['-s', device] if device else []) + list(args)
  return subprocess.run(command, capture_output=True, text=True)


def landscape(device, on):
  """
  Captures are taken in LANDSCAPE.

  The vignette is a wide rectangle, and cropping one out of a portrait phone screenshot keeps
  about a quarter of the height - nothing composes into that. Turned back afterwards.
  """
  adb(device, 'shell', 'settings', 'put', 'system', 'accelerometer_rotation', '0')
  adb(device, 'shell', 'settings', 'put', 'system', 'user_rotation', '1' if on else '0')
  # And no status bar: the clock and the signal meter are not part of the map.
  adb(device, 'shell', 'settings', 'put', 'global', 'policy_control',
      'immersive.full=*' if on else 'null')


def capture(device, identifier, outPath, settle, width, aspect):
  adb(device, 'shell', 'am', 'force-stop', PACKAGE)
  # 'ui false' strips the back button, the controls and the caption: the vignette is the map.
  started = adb(device, 'shell', 'am', 'start', '-n', ACTIVITY,
                '--es', 'example', identifier, '--es', 'ui', 'false')
  if started.returncode != 0 or 'Error' in started.stderr:
    print('  %-18s FAILED to start: %s' % (identifier, started.stderr.strip()))
    return False
  subprocess.run(['sleep', str(settle)])
  remote = '/sdcard/example-%s.png' % identifier
  if adb(device, 'shell', 'screencap', '-p', remote).returncode != 0:
    print('  %-18s FAILED to screencap' % identifier)
    return False
  os.makedirs(os.path.dirname(outPath), exist_ok=True)
  pulled = adb(device, 'pull', remote, outPath)
  adb(device, 'shell', 'rm', '-f', remote)
  if pulled.returncode != 0:
    print('  %-18s FAILED to pull' % identifier)
    return False
  size = shrink(outPath, width, aspect)
  print('  %-18s %s (%d KB)' % (identifier, os.path.basename(outPath), size // 1024))
  return True


def shrink(path, width, aspect):
  """
  A centred horizontal band, then a thumbnail.

  Both surfaces show this as a wide vignette in a grid, so the stored file IS the vignette: the
  full portrait screenshot is cropped to the middle band, which is why an example has to compose
  its subject in the VERTICAL CENTRE of the screen. It also drops the title pill and the caption,
  which are app chrome rather than the map.
  """
  if Image is None:
    return os.path.getsize(path)
  image = Image.open(path)
  band = round(image.width / aspect)
  if band < image.height:
    top = (image.height - band) // 2
    image = image.crop((0, top, image.width, top + band))
  if image.width > width:
    image = image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)
  # A map thumbnail quantises well and a PNG of one does not: 8-bit cuts it by about 5x with no
  # visible loss at this size.
  image.convert('RGB').quantize(colors=192, method=Image.MEDIANCUT).save(path, optimize=True)
  return os.path.getsize(path)


here = os.path.dirname(os.path.abspath(__file__))
parser = argparse.ArgumentParser()
parser.add_argument('ids', nargs='*', help='example ids; every one in the manifest by default')
parser.add_argument('--device', default='', help='adb device id, when more than one is attached')
parser.add_argument('--settle', type=int, default=30,
                    help='seconds to let tiles and labels settle before capturing')
parser.add_argument('--width', type=int, default=800, help='thumbnail width in pixels')
# 2.0 keeps a LANDSCAPE capture whole (2400x1080 is already 2.22) while still cropping a portrait
# one to a band. A narrower value here cuts the sky off a 3D view.
parser.add_argument('--aspect', type=float, default=2.0,
                    help='width/height of the centred band kept from the screenshot')
parser.add_argument('--manifest', default=os.path.join(here, '../docs/examples/examples.json'))
args = parser.parse_args()

if Image is None:
  print('Pillow is not installed - screenshots will be kept at full size')

with open(args.manifest) as f:
  manifest = json.load(f)
wanted = [example
          for section in manifest['sections']
          for example in section['examples']
          if not args.ids or example['id'] in args.ids]
if not wanted:
  print('No examples matched %s' % (args.ids or '<all>'))
  sys.exit(1)

shotDir = os.path.join(os.path.dirname(args.manifest), 'screenshots')
print('Capturing %d example(s), %d s settle each:' % (len(wanted), args.settle))
landscape(args.device, True)
failed = [example['id'] for example in wanted
          if not capture(args.device, example['id'],
                         os.path.join(shotDir, example['id'] + '.png'), args.settle,
                         args.width, args.aspect)]
adb(args.device, 'shell', 'am', 'force-stop', PACKAGE)
landscape(args.device, False)

if failed:
  print('%d failed: %s' % (len(failed), ', '.join(failed)))
  sys.exit(1)
print('Done. Re-run gen-examples.py so the manifest records them.')
