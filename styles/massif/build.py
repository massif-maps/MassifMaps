"""Write the Massif style family from the shared layer modules.

    python3 styles/massif/build.py              # every variant's MapLibre style, and family.json
    python3 styles/massif/build.py --convert    # and the SDK project, into carto/

Each variant is a standalone MapLibre style (<variant>.json) - what the reference pane draws and
what a MapLibre app loads. The SDK gets ONE CartoCSS project for all of them: family.json merges
the variants layer by layer, a value that differs between them becomes a match on the `variant`
config, which the converter keeps live as [param::variant], and carto/<variant>.json extends
project.json with that parameter set. Generated files are never edited by hand.
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from layers import boundaries, buildings, labels, land, lowzoom, outdoor, pois, rail, road_labels, roads, shields, water  # noqa: E402
from palette import VARIANTS as PALETTES  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(HERE, '..', '..', 'tools', 'style-cli', 'dist', 'cli.js')
CONVERT = ['--fold-casings', '--tile-draw-size', '512', '--fonts', 'fonts', '--live-light']


class Variant:
    def __init__(self, name, title, parts, sources=(), **flags):
        self.name = name
        self.title = title
        self.parts = parts
        self.palette = PALETTES[name]
        self.sources = {k: SOURCES[k] for k in ('openmaptiles', 'bathymap', *sources)}
        self.flags = flags

    def layers(self):
        return [lay for part in self.parts for lay in part(self)]


SOURCES = {
    'openmaptiles': {'type': 'vector', 'url': 'https://tiles.openfreemap.org/planet'},
    # optional, no public host: the app supplies the archive, the preview serves it by name
    'bathymap': {'type': 'vector', 'url': 'bathymap.json', 'maxzoom': 6},
    'contours': {'type': 'vector', 'url': 'contours.json', 'minzoom': 11, 'maxzoom': 14},
    'routes': {'type': 'vector', 'url': 'routes.json', 'maxzoom': 14},
    'dem': {'type': 'raster-dem', 'tiles': ['https://tiles.mapterhorn.com/{z}/{x}/{y}.webp'],
            'encoding': 'terrarium', 'tileSize': 512, 'maxzoom': 16},
}

STREETS = [land.background, lowzoom.landcover, land.layers, water.layers, lowzoom.depth, rail.tunnels,
           roads.tunnels, roads.ground, rail.ground, roads.bridges, rail.bridges, rail.overhead,
           boundaries.layers, buildings.layers, labels.low, shields.layers, pois.layers, road_labels.layers,
           labels.places]

# bottom to top; among the labels, the later a layer the higher its placement priority. The first
# variant is the SDK project's default.
OUTDOOR = [land.background, lowzoom.landcover, land.layers, water.layers, lowzoom.depth, outdoor.hillshade,
           outdoor.contours, rail.tunnels, roads.tunnels, outdoor.routes, roads.ground, rail.ground, roads.bridges,
           rail.bridges, rail.overhead, outdoor.cliffs, boundaries.layers, buildings.layers, outdoor.contour_labels,
           labels.low, shields.layers, pois.layers, road_labels.layers, labels.places]

VARIANTS = {v.name: v for v in [
    Variant('streets', 'Massif Streets', STREETS),
    Variant('outdoor', 'Massif Outdoor', OUTDOOR, sources=('dem', 'contours', 'routes'), trails=True),
]}

POI_RANKING = {'default': 'category', 'values': ['category', 'rank']}


def document(name, layers, metadata, schema, sources):
    return {
        'version': 8,
        'name': name,
        'metadata': {'massif:schema': 'openmaptiles', **metadata},
        'sources': sources,
        'sprite': 'sprite/sprite',
        'glyphs': 'https://tiles.openfreemap.org/fonts/{fontstack}/{range}.pbf',
        'layers': layers,
        'schema': schema,
    }


def maplibre_style(v):
    return document(v.title, v.layers(), {'massif:variant': v.name, 'massif:live-config': ['poiRanking']},
                    {'poiRanking': POI_RANKING}, v.sources)


FIXED = ('id', 'type', 'source', 'source-layer', 'minzoom', 'maxzoom', 'filter')


def by_variant(values):
    """One value per variant name -> that value, or a match on the variant config."""
    names = list(values)
    if all(values[n] == values[names[0]] for n in names):
        return values[names[0]]
    if all(isinstance(values[n], dict) for n in names):
        keys = []
        for n in names:
            keys += [k for k in values[n] if k not in keys]
        missing = [(k, n) for k in keys for n in names if k not in values[n]]
        if missing:
            raise ValueError('a key only some variants state: %s' % missing)
        return {k: by_variant({n: values[n][k] for n in names}) for k in keys}
    default = values[names[0]]
    out = ['match', ['config', 'variant']]
    groups = []
    for n in names[1:]:
        if values[n] == default:
            continue
        group = next((g for g in groups if g[1] == values[n]), None)
        if group:
            group[0].append(n)
        else:
            groups.append([[n], values[n]])
    for members, value in groups:
        out += [members[0] if len(members) == 1 else members, value]
    return out + [default]


def ordered_union(lists):
    out = []
    for ids in lists:
        at = 0
        for id in ids:
            if id in out:
                if out.index(id) < at:
                    raise ValueError('variants disagree on the order of %s' % id)
                at = out.index(id) + 1
            else:
                out.insert(at, id)
                at += 1
    return out


def family_style():
    per = {name: {lay['id']: lay for lay in v.layers()} for name, v in VARIANTS.items()}
    names = list(VARIANTS)
    layers = []
    for id in ordered_union([list(per[n]) for n in names]):
        present = [n for n in names if id in per[n]]
        trees = {n: per[n][id] for n in present}
        for key in FIXED:
            if len({json.dumps(t.get(key)) for t in trees.values()}) > 1:
                raise ValueError('%s: %s differs between variants - give each its own layer' % (id, key))
        merged = by_variant(trees)
        if len(present) < len(names):
            # pruned per tile at decode: a layer the selected variant does not draw costs nothing
            only = ['==', ['config', 'variant'], present[0]] if len(present) == 1 else \
                ['in', ['config', 'variant'], ['literal', present]]
            meta = merged.setdefault('metadata', {})
            meta['massif:filter'] = ['all', meta['massif:filter'], only] if 'massif:filter' in meta else only
        layers.append(merged)
    return document('Massif', layers, {'massif:live-config': ['poiRanking', 'variant']},
                    {'poiRanking': POI_RANKING, 'variant': {'default': names[0], 'values': names}},
                    {k: s for v in VARIANTS.values() for k, s in v.sources.items()})


def write(name, doc):
    open(os.path.join(HERE, name), 'w').write(json.dumps(doc, indent=2, ensure_ascii=False))


def main(args):
    for name, v in VARIANTS.items():
        write(name + '.json', maplibre_style(v))
        print(name, '->', name + '.json')
    write('family.json', family_style())
    pois.write_sprite_palette(os.path.join(HERE, 'sprite-src', 'poi-palette.json'))
    if '--convert' in args:
        # the sprite URL resolves against the working directory, so the converter runs from here
        report = subprocess.run(['node', CLI, 'mapbox2css', 'family.json', 'carto', *CONVERT], cwd=HERE,
                                capture_output=True, text=True, check=True).stdout
        print(next(line for line in report.splitlines() if line.startswith('Coverage')))
        spec = {'values': {n: n for n in VARIANTS}}
        for name in VARIANTS:
            project = {'extends': './project.json', 'styleparameters': {'variant': {**spec, 'default': name}}}
            open(os.path.join(HERE, 'carto', name + '.json'), 'w').write(json.dumps(project, indent=2) + '\n')
        print('carto/ + ' + ', '.join(n + '.json' for n in VARIANTS))


if __name__ == '__main__':
    main(sys.argv[1:])
