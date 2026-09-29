"""Write the Massif style family from the shared layer modules.

    python3 styles/massif/build.py              # every variant's MapLibre style, legend/, and family.json
    python3 styles/massif/build.py --convert    # and the SDK project, into carto/

Each variant is a standalone MapLibre style (<variant>.json) - what the reference pane draws and
what a MapLibre app loads. The SDK gets ONE CartoCSS project for all of them: family.json merges
the variants layer by layer, a value that differs between them becomes a match on the `variant`
config, which the converter keeps live as [param::variant], and carto/<variant>.json extends
project.json with that parameter set. Generated files are never edited by hand.
"""
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from layers import boundaries, buildings, imagery, labels, land, lowzoom, outdoor, pois, rail, road_labels, roads, shields, water  # noqa: E402
from palette import VARIANTS as PALETTES  # noqa: E402
from lib import FONTS  # noqa: E402
from params import PARAMS  # noqa: E402
import legend  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(HERE, '..', '..', 'tools', 'style-cli', 'dist', 'cli.js')
CONVERT = ['--fold-casings', '--tile-draw-size', '512', '--live-light']


class Variant:
    def __init__(self, name, title, parts, sources=(), params=None, **flags):
        self.name = name
        self.title = title
        self.parts = parts
        self.params = {k: (params or {}).get(k, spec['default']) for k, spec in PARAMS.items()}
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
    'satellite': {'type': 'raster', 'url': 'satellite.json', 'tileSize': 512},
    'dem': {'type': 'raster-dem', 'tiles': ['https://tiles.mapterhorn.com/{z}/{x}/{y}.webp'],
            'encoding': 'terrarium', 'tileSize': 512, 'maxzoom': 16},
}

STREETS = [land.background, lowzoom.landcover, land.layers, water.layers, lowzoom.depth, rail.tunnels,
           roads.tunnels, roads.ground, rail.ground, roads.bridges, rail.bridges, rail.overhead,
           boundaries.layers, buildings.layers, labels.low, road_labels.major, shields.layers, pois.mountain, pois.layers, road_labels.layers,
           labels.places]

# bottom to top; among the labels, the later a layer the higher its placement priority. The first
# variant is the SDK project's default.
OUTDOOR = [land.background, lowzoom.landcover, land.layers, water.layers, lowzoom.depth, outdoor.hillshade,
           outdoor.contours, rail.tunnels, roads.tunnels, outdoor.routes, roads.ground, rail.ground, roads.bridges,
           rail.bridges, rail.overhead, outdoor.cliffs, boundaries.layers, buildings.layers, outdoor.contour_labels,
           labels.low, road_labels.major, shields.layers, pois.mountain, pois.layers, outdoor.sac_labels,
           road_labels.layers, labels.places]

# e-ink carries everything outdoor does but the relief and the route bands, which grey into mud
EINK = [p for p in OUTDOOR if p not in (outdoor.hillshade, outdoor.routes)]

HYBRID = [land.background, imagery.layers, rail.tunnels, roads.tunnels, roads.ground, rail.ground, roads.bridges,
          rail.bridges, rail.overhead, boundaries.layers, labels.low, road_labels.major, shields.layers, pois.mountain, pois.layers, road_labels.layers,
          labels.places]

# a walker's map brings the campsites in with the huts
OUTDOOR_PARAMS = {'campsite_min_zoom': 13}

VARIANTS = {v.name: v for v in [
    Variant('streets', 'Massif Streets', STREETS),
    Variant('outdoor', 'Massif Outdoor', OUTDOOR, sources=('dem', 'contours', 'routes'), params=OUTDOOR_PARAMS,
            trails=True),
    Variant('topo', 'Massif Topo', OUTDOOR, sources=('dem', 'contours', 'routes'), params=OUTDOOR_PARAMS, trails=True),
    Variant('hybrid', 'Massif Hybrid', HYBRID, sources=('satellite',), dark_ground=True),
    Variant('eink', 'Massif E-ink', EINK, sources=('contours',), params={**OUTDOOR_PARAMS, 'polygons_border': 1, 'sac_scale_labels': 1},
            mono=True, trails=True),
]}



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
    layers = v.layers()
    for lay in layers:
        # maplibre has no parameter in a zoom test: the variant's default becomes the layer's minzoom,
        # floored by the layer's own as the SDK's two tests are
        param = lay.get('metadata', {}).get('massif:minzoom-param')
        if param:
            lay['minzoom'] = max(lay.get('minzoom', 0), v.params[param])
    return document(v.title, layers, {'massif:variant': v.name, 'massif:live-config': list(PARAMS)},
                    {k: {**spec, 'default': v.params[k]} for k, spec in PARAMS.items()}, v.sources)


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
    lists = [values[n] for n in names]
    if all(isinstance(v, list) and len(v) == len(lists[0]) and v[:1] == lists[0][:1] for v in lists):
        # the same expression with an object inside (["image", name, {params}]): merge the object,
        # so the variant match lands on each param and not on the image the converter unwraps
        column = lambda i: {n: values[n][i] for n in names}
        if all(all(isinstance(v, dict) for v in column(i).values()) or len({json.dumps(v) for v in column(i).values()}) == 1
               for i in range(len(lists[0]))):
            return [by_variant(column(i)) for i in range(len(lists[0]))]
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
            # only == and != : each converts to a bracketed test the decoder prunes per tile, where an
            # `in` over several variants would become a when() evaluated per feature
            absent = [n for n in names if n not in present]
            only = ['==', ['config', 'variant'], present[0]] if len(present) == 1 else \
                ['all', *[['!=', ['config', 'variant'], n] for n in absent]]
            meta = merged.setdefault('metadata', {})
            meta['massif:filter'] = ['all', meta['massif:filter'], only] if 'massif:filter' in meta else only
        layers.append(merged)
    # the default palette's own names for its colours and fonts, so variables.mss says @motorway
    own = {k: v for k, v in VARIANTS[names[0]].palette.items() if isinstance(v, str)}
    own.update({'font-' + role: sdk for role, (_, sdk) in FONTS.items()})
    return document('Massif', layers, {'massif:live-config': [*PARAMS, 'variant'],
                                       'massif:palette-names': own},
                    {**PARAMS,
                     'variant': {'default': names[0], 'values': names}},
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
        # a when() is evaluated per feature and blocks rule pruning: the family is written to need none
        mss = open(os.path.join(HERE, 'carto', 'style.mss')).read().splitlines()
        whens = [line.split(' {')[0] for line in mss if 'when(' in line]
        if whens:
            sys.exit('%d when() in carto/style.mss - rewrite the layer so it brackets:\n  %s' % (len(whens), '\n  '.join(whens)))
        spec = {'values': {n: n for n in VARIANTS}}
        for name, v in VARIANTS.items():
            params = {'variant': {**spec, 'default': name}}
            params.update({k: value for k, value in v.params.items() if value != PARAMS[k]['default']})
            project = {'extends': './project.json', 'styleparameters': params}
            open(os.path.join(HERE, 'carto', name + '.json'), 'w').write(json.dumps(project, indent=2) + '\n')
        # the override examples are child projects of this one, so they sit beside it
        for example in sorted(os.listdir(os.path.join(HERE, 'examples'))):
            folder = os.path.join(HERE, 'examples', example)
            for f in sorted(os.listdir(folder)):
                if f.endswith(('.json', '.mss')):
                    shutil.copy(os.path.join(folder, f), os.path.join(HERE, 'carto', f))
        legend.write()
        print('carto/ + ' + ', '.join(n + '.json' for n in VARIANTS) + ' + legend.json + examples')


if __name__ == '__main__':
    main(sys.argv[1:])
