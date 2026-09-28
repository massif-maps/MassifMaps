"""Write every Massif style variant's MapLibre style.json from the shared layer modules.

    python3 styles/massif/build.py            # all variants
    python3 styles/massif/build.py streets    # one

The generated style.json is the source the converter reads and the reference pane draws; it is
never edited by hand - edit the modules here.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from layers import base, buildings, pois, rail, road_labels, roads, shields  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
STYLES = os.path.dirname(HERE)


class Variant:
    def __init__(self, name, title, modules):
        self.name = name
        self.title = title
        self.modules = modules

    @property
    def folder(self):
        return os.path.join(STYLES, 'massif-' + self.name)


VARIANTS = {
    'streets': Variant('streets', 'Massif Streets',
                       [base, roads, rail, buildings, shields, pois, road_labels]),
}


def style(v):
    return {
        'version': 8,
        'name': v.title,
        'metadata': {
            'massif:schema': 'openmaptiles',
            'massif:stage': 'roads, buildings and labels - drawn as context, not as the final design',
            'massif:live-config': ['poiRanking'],
        },
        'sources': {'openmaptiles': {'type': 'vector', 'url': 'https://tiles.openfreemap.org/planet'}},
        'sprite': 'sprite/sprite',
        'glyphs': 'https://tiles.openfreemap.org/fonts/{fontstack}/{range}.pbf',
        'layers': [lay for module in v.modules for lay in module.layers(v)],
        'schema': {'poiRanking': {'default': 'category', 'values': ['category', 'rank']}},
    }


def main(names):
    for name in names or VARIANTS:
        v = VARIANTS[name]
        out = json.dumps(style(v), indent=2, ensure_ascii=False)
        open(os.path.join(v.folder, 'style.json'), 'w').write(out)
        pois.write_sprite_palette(os.path.join(v.folder, 'sprite-src', 'poi-palette.json'))
        print(name, '->', os.path.relpath(v.folder, STYLES))


if __name__ == '__main__':
    main(sys.argv[1:])
