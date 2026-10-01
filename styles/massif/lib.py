"""Expression helpers shared by every layer module."""
import colorsys
import re

SOURCE = 'openmaptiles'


def get(field):
    return ['get', field]


def zoom_ramp(*stops, base=None):
    """['interpolate', curve, ['zoom'], z0, v0, z1, v1, ...]."""
    curve = ['linear'] if base is None else ['exponential', base]
    return ['interpolate', curve, ['zoom'], *stops]


def in_class(classes, field='class'):
    return ['in', get(field), ['literal', list(classes)]]


def by_hour(night, day):
    """Standard's two measure-light stops. maplibre rejects measure-light, so this rides in
    `massif:paint` and the DAY value stays in the plain property for the reference pane."""
    return ['interpolate', ['linear'], ['measure-light', 'brightness'], 0.25, night, 0.3, day]


HSL = re.compile(r'^(hsla?\(\s*[\d.]+\s*,\s*[\d.]+%\s*,\s*)([\d.]+)(%.*)$')


def hex_color(hsl):
    """'hsl(h, s%, l%)' as '#rrggbb', the only colour spelling the SDK facade reads"""
    h, s, l = (float(x) for x in re.findall(r'[\d.]+', hsl)[:3])
    return '#%02x%02x%02x' % tuple(round(c * 255) for c in colorsys.hls_to_rgb(h / 360, l / 100, s / 100))


def inverted(expr):
    """every hsl colour in an expression with its lightness mirrored, for a page read at night"""
    if isinstance(expr, str):
        m = HSL.match(expr)
        return m.group(1) + ('%g' % (100 - float(m.group(2)))) + m.group(3) if m else expr
    if isinstance(expr, list):
        return [inverted(x) for x in expr]
    if isinstance(expr, dict):
        return {k: inverted(v) for k, v in expr.items()}
    return expr


def night_inverted(lay):
    """e-ink at night: the page turns black and the ink white, every colour's lightness mirrored
    past brightness 0.25-0.3 (SDK only, in `massif:paint`, as by_hour)"""
    meta = lay.setdefault('metadata', {})
    paint = {**lay.get('paint', {}), **meta.get('massif:paint', {})}
    def night(v):
        # a zoom ramp stays outside: the converter carries a brightness ramp inside one, not around it
        if isinstance(v, list) and v[:1] == ['interpolate'] and v[2] == ['zoom']:
            return v[:3] + [x if i % 2 == 0 else night(x) for i, x in enumerate(v[3:])]
        if isinstance(v, list) and v[:1] == ['step'] and v[1] == ['zoom']:
            return v[:2] + [night(v[2])] + [x if i % 2 == 0 else night(x) for i, x in enumerate(v[3:])]
        return by_hour(inverted(v), v)
    colours = {k: night(v) for k, v in paint.items() if k.endswith('-color') and v != inverted(v)}
    if colours:
        meta['massif:paint'] = {**meta.get('massif:paint', {}), **colours}
    image = meta.get('massif:layout', {}).get('icon-image')
    if isinstance(image, list) and len(image) == 3 and isinstance(image[2], dict) and 'params' in image[2]:
        params = {k: by_hour(inverted(v), v) if v != inverted(v) else v for k, v in image[2]['params'].items()}
        meta['massif:layout'] = {**meta['massif:layout'], 'icon-image': [image[0], image[1], {**image[2], 'params': params}]}
    return lay


# a role -> MapLibre's glyph-server face, and the SDK's system font list: iOS by name, Android's
# Roboto (its weight axis), the web build's preloaded Roboto behind the generic name. Nothing packaged.
FONTS = {
    'regular': ('Noto Sans Regular', 'ios:Helvetica Neue, Roboto, sans-serif'),
    'medium': ('Noto Sans Regular', 'ios:Helvetica Neue Medium, Roboto Medium, sans-serif Medium'),
    'bold': ('Noto Sans Bold', 'ios:Helvetica Neue Bold, Roboto Bold, sans-serif Bold'),
    'italic': ('Noto Sans Italic', 'ios:Helvetica Neue Italic, Roboto Italic, sans-serif Italic'),
}

EMISSIVE = {'background': 'background', 'fill': 'fill', 'line': 'line', 'fill-extrusion': 'fill-extrusion'}


def layer(id, type, source_layer=None, minzoom=None, maxzoom=None, filter=None, layout=None,
          paint=None, metadata=None, emissive=None, source=SOURCE):
    """A layer dict in the key order every module writes, so the JSON diffs stay readable.

    `emissive` is how much of the colour survives the night: Mapbox's *-emissive-strength, which
    maplibre does not know, so it rides in `massif:paint` (a dict for a symbol's text and icon).
    """
    out = {'id': id, 'type': type}
    if source_layer is not None:
        out['source'] = source
        out['source-layer'] = source_layer
    if minzoom is not None:
        out['minzoom'] = minzoom
    if maxzoom is not None:
        out['maxzoom'] = maxzoom
    if filter is not None:
        out['filter'] = filter
    if layout and isinstance(layout.get('text-font'), str):
        maplibre, sdk = FONTS[layout['text-font']]
        layout = {**layout, 'text-font': [maplibre]}
        metadata = dict(metadata or {})
        metadata['massif:layout'] = {**metadata.get('massif:layout', {}), 'text-font': [sdk]}
    if layout:
        out['layout'] = layout
    if paint is not None:
        out['paint'] = paint
    if emissive is not None:
        extra = dict(emissive) if isinstance(emissive, dict) else {EMISSIVE[type] + '-emissive-strength': emissive}
        metadata = dict(metadata or {})
        metadata['massif:paint'] = {**metadata.get('massif:paint', {}), **extra}
    if metadata:
        out['metadata'] = metadata
    return out


def scaled(expr, k):
    """A width ramp with every output multiplied by k - the stops and match labels left alone."""
    if k == 1 or not isinstance(expr, list):
        return expr * k if isinstance(expr, (int, float)) and not isinstance(expr, bool) else expr
    if expr[0] == 'interpolate':
        return expr[:3] + [x if i % 2 == 0 else scaled(x, k) for i, x in enumerate(expr[3:])]
    if expr[0] == 'match':
        body = expr[2:-1]
        return expr[:2] + [x if i % 2 == 0 else scaled(x, k) for i, x in enumerate(body)] + [scaled(expr[-1], k)]
    return expr


def from_zoom(expr, z):
    """A width ramp that is 0 below zoom z and follows expr's own stops from z on."""
    stops = [(expr[i], expr[i + 1]) for i in range(3, len(expr), 2) if expr[i] >= z]
    return expr[:3] + [z - 1, 0] + [x for stop in stops for x in stop]


# Standard's text-occlusion-opacity: what a label keeps while a 3D building hides its anchor. Line, natural
# and (ours) POI labels go (0), numbers on a road stay faint (0.1); places keep the layer default.
OCCLUSION = [(('road-label', 'path-label', 'track-label', 'waterway-label', 'stream-label', 'water-name',
               'peak', 'contour-label', 'landcover-label', 'park-label', 'housenumber', 'poi-'), 0),
             (('road-shield', 'road-exit-shield', 'trail-t'), 0.1)]


def occlusion(lay):
    """the OCCLUSION opacity of a symbol layer, SDK-only (maplibre has no such property)"""
    if lay['type'] != 'symbol':
        return lay
    for prefixes, value in OCCLUSION:
        if lay['id'].startswith(prefixes):
            meta = lay.setdefault('metadata', {})
            # `label_occlusion` 0: nothing is hidden, and no ray is cast
            value = ['match', ['config', 'label_occlusion'], 0, 1, value]
            meta['massif:paint'] = {**meta.get('massif:paint', {}), 'text-occlusion-opacity': value}
            if 'icon-image' in lay.get('layout', {}):
                meta['massif:paint']['icon-occlusion-opacity'] = value
            break
    return lay


def on_roof(lay):
    """`poi_on_roof` 1: a POI inside a 3D building stands on its roof (mapbox symbol-z-elevate), SDK-only"""
    if lay['type'] == 'symbol' and lay['id'].startswith('poi-'):
        meta = lay.setdefault('metadata', {})
        meta['massif:layout'] = {**meta.get('massif:layout', {}),
                                 'symbol-z-elevate': ['==', ['config', 'poi_on_roof'], 1]}
    return lay


def gate(lay, v, name, value=1):
    """Draw a layer only while parameter `name` is `value`. The SDK tests it per tile; MapLibre,
    which has no config in a filter, draws the layer if the variant's default is that value."""
    meta = lay.setdefault('metadata', {})
    test = ['==', ['config', name], value]
    meta['massif:filter'] = ['all', meta['massif:filter'], test] if 'massif:filter' in meta else test
    meta['massif:layout'] = {**meta.get('massif:layout', {}), 'visibility': 'visible'}
    lay['layout'] = {**lay.get('layout', {}), 'visibility': 'visible' if v.params[name] == value else 'none'}
    return lay


def fold_config(expr, params):
    """`['config', name]` replaced by the parameter's value, and a match on a constant by its branch:
    MapLibre has no config expression, so its styles carry the variant's defaults."""
    if not isinstance(expr, list):
        return {k: fold_config(v, params) for k, v in expr.items()} if isinstance(expr, dict) else expr
    if len(expr) == 2 and expr[0] == 'config':
        return params[expr[1]]
    expr = [fold_config(x, params) for x in expr]
    if expr[0] == 'match' and not isinstance(expr[1], (list, dict)):
        for i in range(2, len(expr) - 1, 2):
            labels = expr[i] if isinstance(expr[i], list) else [expr[i]]
            if expr[1] in labels:
                return expr[i + 1]
        return expr[-1]
    return expr


def padded(expr, px):
    """A width ramp with px added to every output, for a margin drawn under the line."""
    if isinstance(expr, (int, float)):
        return expr + px
    if expr[0] == 'interpolate':
        return expr[:3] + [x if i % 2 == 0 else padded(x, px) for i, x in enumerate(expr[3:])]
    if expr[0] == 'match':
        body = expr[2:-1]
        return expr[:2] + [x if i % 2 == 0 else padded(x, px) for i, x in enumerate(body)] + [padded(expr[-1], px)]
    raise ValueError('padded: %s' % expr[0])


def wider_halo(lay, px):
    """a label's halo px wider, for e-ink: a black word on a thin halo is lost over a dark pattern"""
    paint = lay.get('paint', {})
    if 'text-halo-width' in paint:
        lay['paint'] = {**paint, 'text-halo-width': padded(paint['text-halo-width'], px)}
    return lay


def halo(c, id, filter, width, param):
    """e-ink's white margin under a thin dashed way, so it still reads across a patterned wood"""
    if 'line-halo' not in c:
        return []
    return [layer(id, 'line', 'transportation', minzoom=12, filter=filter, layout={'line-join': 'round'},
                  paint={'line-color': c['line-halo'], 'line-width': padded(width, 2)},
                  metadata={'massif:minzoom-param': param})]
