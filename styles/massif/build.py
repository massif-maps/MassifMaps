"""Write the Massif style family from the shared layer modules.

    python3 styles/massif/build.py              # every variant's MapLibre style and family.json
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
from lib import FONTS, fold_config, night_inverted, occlusion, on_roof, wider_halo  # noqa: E402
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
        out = [on_roof(occlusion(lay)) for part in self.parts for lay in part(self)]
        if self.flags.get('mono'):
            out = [wider_halo(night_inverted(lay), 1) for lay in out]
        # `lighting` 0 (e-ink, a flat OSM look): every colour as stated, lit by nothing, so white
        # stays white and a road never reads whiter than the ground it crosses
        for lay in out:
            paint = lay.get('metadata', {}).get('massif:paint')
            if paint:
                lay['metadata'] = {**lay['metadata'], 'massif:paint': {
                    # not a building's: unlit, its walls would read as its roof
                    k: ['match', ['config', 'lighting'], 0, 1, v]
                    if k.endswith('-emissive-strength') and lay['type'] != 'fill-extrusion' else v
                    for k, v in paint.items()}}
        return out



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

STREETS = [land.background, lowzoom.landcover, land.layers, water.layers, lowzoom.depth, outdoor.hillshade,
           outdoor.contours, rail.tunnels, roads.tunnels, roads.ground, rail.ground, roads.bridges, rail.bridges,
           rail.overhead, outdoor.cliffs, boundaries.layers, buildings.layers, outdoor.contour_labels, labels.low, road_labels.major, shields.layers, pois.trees, pois.landmarks, pois.barriers, pois.mountain, pois.layers, road_labels.layers,
           labels.peaks, labels.places]

# bottom to top; among the labels, the later a layer the higher its placement priority. The first
# variant is the SDK project's default.
OUTDOOR = [land.background, lowzoom.landcover, land.layers, water.layers, lowzoom.depth, outdoor.hillshade,
           outdoor.contours, rail.tunnels, roads.tunnels, outdoor.routes, roads.ground, rail.ground, roads.bridges,
           rail.bridges, rail.overhead, outdoor.cliffs, boundaries.layers, buildings.layers, outdoor.contour_labels,
           labels.low, road_labels.major, shields.layers, pois.trees, pois.landmarks, pois.barriers, pois.mountain, pois.layers, outdoor.sac_labels,
           road_labels.layers, labels.peaks, labels.places]

# e-ink carries everything outdoor does but the route bands, which grey into mud
EINK = [p for p in OUTDOOR if p is not outdoor.routes]

HYBRID = [land.background, imagery.layers, outdoor.hillshade, outdoor.contours, rail.tunnels, roads.tunnels,
          roads.ground, rail.ground, roads.bridges, rail.bridges, rail.overhead, outdoor.cliffs, boundaries.layers,
          outdoor.contour_labels, labels.low, road_labels.major, shields.layers, pois.trees, pois.landmarks, pois.barriers, pois.mountain, pois.layers, road_labels.layers,
          labels.peaks, labels.places]

# a walker's map brings the campsites in with the huts, and its water points early
OUTDOOR_PARAMS = {'campsite_min_zoom': 13, 'water_min_zoom': 16}

VARIANTS = {v.name: v for v in [
    Variant('streets', 'Massif Streets', STREETS, sources=('dem', 'contours')),
    Variant('outdoor', 'Massif Outdoor', OUTDOOR, sources=('dem', 'contours', 'routes'), params=OUTDOOR_PARAMS,
            trails=True, relief=True),
    Variant('topo', 'Massif Topo', OUTDOOR, sources=('dem', 'contours', 'routes'), params=OUTDOOR_PARAMS, trails=True,
            relief=True),
    Variant('hybrid', 'Massif Hybrid', HYBRID, sources=('satellite', 'dem', 'contours'), dark_ground=True),
    Variant('eink', 'Massif E-ink', EINK, sources=('dem', 'contours'), params={**OUTDOOR_PARAMS, 'polygons_border': 1, 'sac_scale_labels': 1, 'lighting': 0},
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


RELIEF_SOURCES = ('dem', 'contours')


def maplibre_style(v):
    layers = v.layers()
    for lay in layers:
        # maplibre has no parameter in a zoom test: the variant's default becomes the layer's minzoom,
        # floored by the layer's own as the SDK's two tests are
        param = lay.get('metadata', {}).get('massif:minzoom-param')
        if param:
            lay['minzoom'] = max(lay.get('minzoom', 0), v.params[param])
        for key in ('paint', 'layout', 'filter'):
            if key in lay:
                lay[key] = fold_config(lay[key], v.params)
        # every variant carries the relief and the contours; an app shows them where they are off
        if lay.get('source') in RELIEF_SOURCES and not v.flags.get('relief'):
            lay['layout'] = {**lay.get('layout', {}), 'visibility': 'none'}
    return document(v.title, layers, {'massif:variant': v.name, 'massif:live-config': list(PARAMS)},
                    {k: {**spec, 'default': v.params[k]} for k, spec in PARAMS.items()}, v.sources)


FIXED = ('id', 'type', 'source', 'source-layer', 'minzoom', 'maxzoom', 'filter')


def mergeable(column):
    """the same expression in every variant but for the objects in it, however deep"""
    if all(isinstance(v, dict) for v in column) or len({json.dumps(v) for v in column}) == 1:
        return True
    return all(isinstance(v, list) and len(v) == len(column[0]) and v[:1] == column[0][:1] for v in column) and \
        all(mergeable([v[i] for v in column]) for i in range(len(column[0])))


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
    if mergeable(list(values.values())):
        # the same expression with an object inside (["image", name, {params}]): merge the object,
        # so the variant match lands on each param and not on the image the converter unwraps
        return [by_variant({n: values[n][i] for n in names}) for i in range(len(values[names[0]]))]
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
        # e-ink's night inversion states a colour in massif:paint where the others leave it to the
        # plain paint: give those the plain value, so the key merges into a match on the variant
        sdk_keys = {k for t in trees.values() for k in t.get('metadata', {}).get('massif:paint', {})}
        for t in trees.values():
            for k in sdk_keys:
                if k.endswith('-color') and k in t.get('paint', {}) and k not in t.get('metadata', {}).get('massif:paint', {}):
                    t.setdefault('metadata', {})
                    t['metadata']['massif:paint'] = {**t['metadata'].get('massif:paint', {}), k: t['paint'][k]}
        # only hybrid draws its roads and paths once (roads.draw_once): the others merge in as no group
        if any('massif:draw-once' in t.get('metadata', {}) for t in trees.values()):
            for t in trees.values():
                t.setdefault('metadata', {}).setdefault('massif:draw-once', '')
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
    doc = document('Massif', layers, {'massif:live-config': [*PARAMS, 'variant'],
                                      'massif:palette-names': own},
                   {**PARAMS,
                    'variant': {'default': names[0], 'values': names}},
                   {k: s for v in VARIANTS.values() for k, s in v.sources.items()})
    # Standard's `day` lights, for the SDK project only (MapLibre has its own `light`): without them
    # the buildings kept the SDK's own lighting, whose walls read as bright as the roofs
    doc['lights'] = LIGHTS
    return doc


LIGHTS = [
    {'id': 'ambient', 'type': 'ambient', 'properties': {'color': 'hsl(0, 0%, 100%)', 'intensity': 0.8}},
    {'id': 'directional', 'type': 'directional',
     'properties': {'direction': ['literal', [180, 20]], 'color': 'hsl(0, 0%, 100%)', 'intensity': 0.2,
                    'cast-shadows': True, 'shadow-intensity': 1}},
]


def write(name, doc):
    open(os.path.join(HERE, name), 'w').write(json.dumps(doc, indent=2, ensure_ascii=False))


def convert(out='carto', extra=()):
    """The SDK project into `out` (relative to here): the converted family, one <variant>.json per
    variant, the override examples beside it and the legend spec. Returns the coverage line."""
    # generated whole: an icon a rule stopped naming must not linger in the committed carto/
    shutil.rmtree(os.path.join(HERE, out), ignore_errors=True)
    # the sprite URL resolves against the working directory, so the converter runs from here
    report = subprocess.run(['node', CLI, 'mapbox2css', 'family.json', out, *CONVERT, *extra], cwd=HERE,
                            capture_output=True, text=True, check=True).stdout
    # a when() is evaluated per feature and blocks rule pruning: the family is written to need none
    mss = open(os.path.join(HERE, out, 'style.mss')).read().splitlines()
    whens = [line.split(' {')[0] for line in mss if 'when(' in line]
    if whens:
        sys.exit('%d when() in %s/style.mss - rewrite the layer so it brackets:\n  %s' % (len(whens), out, '\n  '.join(whens)))
    spec = {'values': {n: n for n in VARIANTS}}
    for name, v in VARIANTS.items():
        params = {'variant': {**spec, 'default': name}}
        params.update({k: value for k, value in v.params.items() if value != PARAMS[k]['default']})
        project = {'extends': './project.json', 'styleparameters': params}
        open(os.path.join(HERE, out, name + '.json'), 'w').write(json.dumps(project, indent=2) + '\n')
    # the override examples are child projects of this one, so they sit beside it
    for example in sorted(os.listdir(os.path.join(HERE, 'examples'))):
        folder = os.path.join(HERE, 'examples', example)
        for f in sorted(os.listdir(folder)):
            if f.endswith(('.json', '.mss')):
                shutil.copy(os.path.join(folder, f), os.path.join(HERE, out, f))
    legend.write(os.path.join(HERE, out))
    return next(line for line in report.splitlines() if line.startswith('Coverage'))


def main(args):
    for name, v in VARIANTS.items():
        write(name + '.json', maplibre_style(v))
        print(name, '->', name + '.json')
    write('family.json', family_style())
    pois.write_sprite_palette(os.path.join(HERE, 'sprite-src', 'poi-palette.json'))
    if '--convert' in args:
        print(convert())
        print('carto/ + ' + ', '.join(n + '.json' for n in VARIANTS) + ' + legend.json + examples')


if __name__ == '__main__':
    main(sys.argv[1:])
