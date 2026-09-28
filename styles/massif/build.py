"""Write every Massif style variant's MapLibre style.json from the shared layer modules.

    python3 styles/massif/build.py                      # all variants
    python3 styles/massif/build.py streets              # one
    python3 styles/massif/build.py --convert streets    # and its CartoCSS, into <variant>/carto

The generated style.json is the source the converter reads and the reference pane draws; it is
never edited by hand - edit the modules here.
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from layers import boundaries, buildings, labels, land, lowzoom, pois, rail, road_labels, roads, shields, water  # noqa: E402
from palette import VARIANTS as PALETTES  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
STYLES = os.path.dirname(HERE)


class Variant:
    def __init__(self, name, title, parts):
        self.name = name
        self.title = title
        self.parts = parts
        self.palette = PALETTES[name]

    @property
    def folder(self):
        return os.path.join(STYLES, 'massif-' + self.name)


VARIANTS = {
    # bottom to top; among the labels, the later a layer the higher its placement priority
    'streets': Variant('streets', 'Massif Streets', [
        land.background, lowzoom.landcover, land.layers, water.layers, lowzoom.depth, rail.tunnels, roads.tunnels, roads.ground, rail.ground,
        roads.bridges, rail.bridges, rail.overhead, boundaries.layers, buildings.layers,
        labels.low, shields.layers, pois.layers, road_labels.layers, labels.places]),
}


def style(v):
    return {
        'version': 8,
        'name': v.title,
        'metadata': {
            'massif:schema': 'openmaptiles',
            'massif:stage': 'base map: land, water, roads, rail, boundaries, buildings and labels',
            'massif:live-config': ['poiRanking'],
        },
        'sources': {
            'openmaptiles': {'type': 'vector', 'url': 'https://tiles.openfreemap.org/planet'},
            # optional, no public host: the app supplies the archive, the preview serves it by name
            'bathymap': {'type': 'vector', 'url': 'bathymap.json', 'maxzoom': 6},
        },
        'sprite': 'sprite/sprite',
        'glyphs': 'https://tiles.openfreemap.org/fonts/{fontstack}/{range}.pbf',
        'layers': [lay for part in v.parts for lay in part(v)],
        'schema': {'poiRanking': {'default': 'category', 'values': ['category', 'rank']}},
    }


CLI = os.path.join(STYLES, '..', 'tools', 'style-cli', 'dist', 'cli.js')
CONVERT = ['--fold-casings', '--tile-draw-size', '512', '--fonts', 'fonts', '--live-light']


def main(args):
    convert = '--convert' in args
    for name in [a for a in args if not a.startswith('--')] or VARIANTS:
        v = VARIANTS[name]
        out = json.dumps(style(v), indent=2, ensure_ascii=False)
        open(os.path.join(v.folder, 'style.json'), 'w').write(out)
        pois.write_sprite_palette(os.path.join(v.folder, 'sprite-src', 'poi-palette.json'))
        print(name, '->', os.path.relpath(v.folder, STYLES))
        if convert:
            # the sprite URL resolves against the working directory, so the converter runs from the style
            report = subprocess.run(['node', CLI, 'mapbox2css', 'style.json', 'carto', *CONVERT], cwd=v.folder,
                                    capture_output=True, text=True, check=True).stdout
            print(next(line for line in report.splitlines() if line.startswith('Coverage')))


if __name__ == '__main__':
    main(sys.argv[1:])
