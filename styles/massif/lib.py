"""Expression helpers shared by every layer module."""

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
