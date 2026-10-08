from lib import from_zoom, gate, get, halo, in_class, layer, scaled, zoom_ramp
from layers import outdoor

CLASSES = ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'minor', 'service']

SORT_KEY = ['match', get('class'), 'motorway', 7, 'trunk', 6, 'primary', 5, 'secondary', 4,
            'tertiary', 3, 'minor', 2, 1]

# Mapbox Standard's `roads` widths on OMT classes: its street is our minor, its fallback our service.
# a class stays at 0 until Standard's own filter lets it in: primary at 6, secondary 8, tertiary 9
# `road_osm_low` takes OSM Carto's (Alpimaps') wider ones below z11 (OSM_CARTO_WIDTH)
def osm_low(osm, standard):
    return ['match', ['config', 'road_osm_low'], 1, osm, standard]


MAJOR = ('motorway', 'trunk', 'primary')
ROAD_CLASSES = ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'minor', 'other']
# per class at each stop; the last stops are both looks'
STANDARD_WIDTH = {
    3: {'motorway': 0.8, 'trunk': 0.8},
    6: {'motorway': 1, 'trunk': 1, 'primary': 0.4},
    8: {'motorway': 1.3, 'trunk': 1.3, 'primary': 1.1},
    9: {'motorway': 1.6, 'trunk': 1.6, 'primary': 1.4, 'secondary': 0.6},
    10: {'motorway': 1.94, 'trunk': 1.94, 'primary': 1.74, 'secondary': 0.94, 'tertiary': 0.46, 'minor': 0.11},
    11: {'motorway': 2.44, 'trunk': 2.44, 'primary': 2.24, 'secondary': 1.44, 'tertiary': 1.16, 'minor': 0.26},
}
SHARED_WIDTH = {
    12: {'motorway': 3.2, 'trunk': 3.2, 'primary': 3, 'secondary': 2.2, 'tertiary': 2.2, 'minor': 0.5},
    18: {'motorway': 30, 'trunk': 30, 'primary': 28, 'secondary': 26, 'tertiary': 26, 'minor': 20, 'other': 10},
    22: {'motorway': 300, 'trunk': 300, 'primary': 280, 'secondary': 260, 'tertiary': 260, 'minor': 200, 'other': 100},
}
# OSM Carto's (Alpimaps') at ITS zooms, one above Massif's (maplibre's 512-px count): shifted down a
# level, then Standard's curve from z11
OSM_CARTO_WIDTH = {
    6: {'motorway': 0.85, 'trunk': 0.85},
    8: {'motorway': 1.14, 'trunk': 1.14, 'primary': 1},
    9: {'motorway': 1.4, 'trunk': 1.4, 'primary': 1.5, 'secondary': 1},
    10: {'motorway': 1.82, 'trunk': 1.82, 'primary': 2, 'secondary': 2, 'tertiary': 0.35},
    11: {'motorway': 2.34, 'trunk': 2.34, 'primary': 2.33, 'secondary': 2.07, 'tertiary': 0.95},
}


def _at(stops, z, base=1.5):
    """a class table interpolated as maplibre's exponential curve does"""
    keys = sorted(stops)
    if z <= keys[0]:
        return stops[keys[0]]
    lo = max(k for k in keys if k <= z)
    hi = min((k for k in keys if k >= z), default=lo)
    if hi == lo:
        return stops[lo]
    t = (base ** (z - lo) - 1) / (base ** (hi - lo) - 1)
    return {cls: round(stops[lo].get(cls, 0) + t * (stops[hi].get(cls, 0) - stops[lo].get(cls, 0)), 2) for cls in ROAD_CLASSES}


def _match(table):
    out = ['match', get('class')]
    groups = {}
    for cls in ROAD_CLASSES[:-1]:
        groups.setdefault(table.get(cls, 0), []).append(cls)
    other = table.get('other', 0)
    for value, classes in groups.items():
        if value != other:
            out += [classes if len(classes) > 1 else classes[0], value]
    return out + [other]


def _width():
    standard = {**STANDARD_WIDTH, **SHARED_WIDTH}
    osm = {3: STANDARD_WIDTH[3], **{z - 1: v for z, v in OSM_CARTO_WIDTH.items()}, 11: STANDARD_WIDTH[11], **SHARED_WIDTH}
    stops = []
    for z in sorted(set(standard) | set(osm)):
        a, b = _match(_at(osm, z)), _match(_at(standard, z))
        stops += [z, b if a == b else osm_low(a, b)]
    return zoom_ramp(*stops, base=1.5)


WIDTH = _width()

# Liberty's, kept zoomed out for e-ink, whose roads are white on white. Every other variant cases
# a road from z14 only, as Standard does (road_pair): below that the fill alone says the class.
CASING_WIDTH = zoom_ramp(
    3, ['match', get('class'), ['motorway', 'trunk'], 0.25, 0],
    6, ['match', get('class'), ['motorway', 'trunk'], 0.25, 'primary', 0.15, 0],
    8, ['match', get('class'), ['motorway', 'trunk', 'primary'], 0.3, 0],
    9, ['match', get('class'), ['motorway', 'trunk', 'primary'], 0.45, 'secondary', 0.3, 0],
    12, ['match', get('class'), ['motorway', 'trunk', 'primary', 'secondary', 'tertiary'], 0.8, 'minor', 0.5, 0],
    14, ['match', get('class'), ['motorway', 'trunk', 'primary'], 1, 0.8],
    22, 2,
    base=1.5)

# `road_osm_low`: the major roads outlined below z14 too, where OSM Carto (Alpimaps) brings each
# casing in, a level below its own numbers: motorway from z8, trunk from z9, the others at z10.
LOW_CASING_WIDTH = zoom_ramp(
    8, 0,
    9, ['match', get('class'), 'motorway', 0.5, 0],
    9.5, ['match', get('class'), 'motorway', 0.55, 'trunk', 0.3, 0],
    10, ['match', get('class'), ['motorway', 'trunk'], 0.6, ['primary', 'secondary', 'tertiary'], 0.5, 0],
    14, ['match', get('class'), ['motorway', 'trunk', 'primary'], 1, ['secondary', 'tertiary'], 0.8, 0])

# Standard's *_link widths; OMT marks a link with ramp=1 on the class it serves
LINK_WIDTH = zoom_ramp(12, ['match', get('class'), ['motorway', 'trunk'], 0.8, 0.4],
                       18, ['match', get('class'), ['motorway', 'trunk'], 20, 18],
                       22, ['match', get('class'), ['motorway', 'trunk'], 200, 180], base=1.5)
LINK_CASING = zoom_ramp(12, 0.5, 14, 0.8, 22, 2, base=1.5)

PATH_WIDTH = zoom_ramp(12, 0, 15, 1, 18, 6, 22, 80, base=1.5)
# a track is a double line, MapTiler's and IGN's, so it never reads as a single-line trail
TRACK_WIDTH = zoom_ramp(12, 1, 15, 2.2, 18, 5, 22, 20, base=1.5)
TRACK_CASING = zoom_ramp(12, 0.7, 15, 1.2, 18, 1.8, 22, 3, base=1.5)
# OSM Carto's tracktype ladder on the outline: the rougher the track, the shorter the dash. Never
# solid, or a track reads as a road
TRACK_GRADES = [('grade1', [8, 2]), ('grade2', [5, 2]), ('grade3', [3, 2]), ('grade4', [2, 2]),
                ('grade5', [1, 2])]

EMISSIVE = ['match', get('class'), ['motorway', 'trunk'], 0.6, 0.4]

# OSM's loose surfaces, read off `surface_detail` (Alpimaps' planetiler keeps the raw tag there)
UNPAVED = ['unpaved', 'compacted', 'fine_gravel', 'gravel', 'pebblestone', 'ground', 'dirt', 'earth', 'grass',
           'grass_paver', 'mud', 'sand', 'rock', 'woodchips']


def fill_color(c):
    # below z14, uncased, Standard's one grey-blue for every road under a primary: a fill near the
    # ground's lightness would vanish without its casing
    low = ['match', get('class'), 'motorway', c['motorway'], 'trunk', c['trunk'], 'primary', c['primary'],
           'secondary', c['secondary-low'], ['minor', 'service'], c['minor-low'], c['road-low']]
    high = ['match', get('class'), 'motorway', c['motorway'], 'trunk', c['trunk'], 'primary', c['primary'],
            'secondary', c['secondary'], 'tertiary', c['tertiary'], c['road']]
    # over the zoom the casing grows in (major_only_below, from_zoom)
    end = c.get('casing-from', 14)
    steps = []
    if c.get('casing-low'):
        # a road is the dark low colour until its outline comes in, then the street's white inside it
        for z, casing in zip(OUTLINE_STEPS[::2], OUTLINE_STEPS[1::2]):
            outlined = [cls for cls in MAJOR if class_value(casing, cls)]
            steps += [z, ['match', get('class'), *[x for cls in outlined for x in (cls, c[cls])], *low[2 + 2 * len(MAJOR):]]]
    return ['interpolate', ['linear'], ['zoom'], *steps, end - 1, low, end, high]


def case_color(c, key='case'):
    per_class = [] if key != 'case' else ['primary', c['primary-case'], 'secondary', c['secondary-case'], 'tertiary', c['tertiary-case']]
    return ['match', get('class'), 'motorway', c['motorway-' + key], 'trunk', c['trunk-' + key], *per_class,
            c['road-' + key]]


def class_value(match, cls):
    if not isinstance(match, list):
        return match
    for i in range(2, len(match) - 1, 2):
        if match[i] == cls or (isinstance(match[i], list) and cls in match[i]):
            return match[i + 1]
    return match[-1]


def major_only_below(expr, z):
    """A per-class width ramp whose stops below zoom z keep motorway, trunk and primary only."""
    if not isinstance(expr, list) or expr[0] != 'interpolate':
        return expr
    out = expr[:3]
    for i in range(3, len(expr), 2):
        stop, match = expr[i], expr[i + 1]
        if stop < z:
            match = ['match', get('class'), *[x for cls in MAJOR for x in (cls, class_value(match, cls))], 0]
        elif out[-2] < z - 1:
            # the small roads' casing grows over z13-14, as from_zoom's does, not from the stop before
            out += [z - 1, out[-1]]
        out += [stop, match]
    return out


# e-ink's outlined roads: OSM Carto's steps (LOW_CASING_WIDTH's up to z10) for the major classes
OUTLINE_STEPS = major_only_below(LOW_CASING_WIDTH, 11)[3:11]


def osm_low_steps(expr):
    """a casing ramp whose stops below z12 are OSM Carto's: no outline on a road until its own step"""
    first = next(i for i in range(3, len(expr), 2) if expr[i] >= 12)
    return expr[:3] + OUTLINE_STEPS + expr[first:] if first > 3 else expr


def draw_once(c, id, metadata=None):
    """Hybrid's translucent roads and paths: each pixel drawn once per group, or a cap stacks over its
    neighbour. A casing shares its fill's group, so it does not show through the fill either."""
    return {**(metadata or {}), 'massif:draw-once': id} if c.get('draw-once') else metadata


def road_pair(c, id, filter, minzoom, width, casing, case_key='case', dash=None, layout=None, fill_opacity=None,
              case_cap=None, maxzoom=None, minzoom_param=None):
    layout = layout or {'line-cap': 'round', 'line-join': 'round', 'line-sort-key': SORT_KEY}
    case_layout = {**layout, 'line-cap': case_cap} if case_cap else layout
    # e-ink orders the major roads by the weight of their outline, having no colour to do it with; the
    # small ones stay uncased below z14 as elsewhere, a grey line rather than a heavy double one
    casing = scaled(osm_low_steps(major_only_below(casing, c.get('casing-from', 14))) if c.get('casing-low') else from_zoom(casing, 14),
                    c.get('casing-scale', 1))
    case_paint = {'line-color': case_color(c, case_key), 'line-gap-width': width, 'line-width': casing}
    if dash:
        case_paint['line-dasharray'] = dash
    fill_paint = {'line-color': fill_color(c), 'line-width': width}
    if fill_opacity is not None:
        fill_paint['line-opacity'] = fill_opacity
    metadata = {'massif:minzoom-param': minzoom_param} if minzoom_param else None
    return [
        layer(id + '-casing', 'line', 'transportation', minzoom=minzoom, maxzoom=maxzoom, filter=filter,
              layout=case_layout, paint=case_paint, emissive=0, metadata=draw_once(c, id, metadata)),
        layer(id, 'line', 'transportation', minzoom=minzoom, maxzoom=maxzoom, filter=filter, layout=layout,
              paint=fill_paint, emissive=EMISSIVE, metadata=draw_once(c, id, metadata)),
    ]


def low_casing(v, filter, id='road-casing-low', maxzoom=14):
    c = v.palette
    if c.get('casing-low'):
        return []
    return [gate(layer(id, 'line', 'transportation', minzoom=8, maxzoom=maxzoom, filter=filter,
                       layout={'line-cap': 'round', 'line-join': 'round', 'line-sort-key': SORT_KEY},
                       paint={'line-color': case_color(c), 'line-gap-width': WIDTH, 'line-width': LOW_CASING_WIDTH},
                       metadata=draw_once(c, id), emissive=0), v, 'road_osm_low')]


def paths(v, brunnel_test, prefix='', minzoom=12, trails=False):
    """footways, cycleways, bridleways and steps, Standard's white ribbon with a hairline case. With
    `trails` the paths and bridleways are left to outdoor.trails, which draws them by difficulty."""
    c = v.palette
    walk = ['all', ['==', get('class'), 'path'], ['!=', get('subclass'), 'steps'], brunnel_test]
    if trails:
        walk = ['all', ['==', get('class'), 'path'], ['!', in_class(['steps'] + outdoor.TRAILS, 'subclass')],
                brunnel_test]
    walk_prefix = prefix + ('urban-' if trails else '')
    steps = ['all', ['==', get('class'), 'path'], ['==', get('subclass'), 'steps'], brunnel_test]
    # the zoom ramp outside: maplibre only takes a zoom expression at the top of a property
    # a cycleway's ribbon is a path's, Standard's: in green it was a solid band wherever no road covered it
    color = zoom_ramp(*[x for z, key in ((15, 'path'), (16, 'path-z16')) for x in (
        z, ['match', get('subclass'), 'bridleway', c['bridleway'], c[key]])])
    return [
        gate(layer(walk_prefix + 'path-casing', 'line', 'transportation', minzoom=15, filter=walk,
                   metadata=draw_once(c, walk_prefix + 'path'), layout={'line-join': 'round'},
                   paint={'line-color': c['path-case'], 'line-gap-width': PATH_WIDTH,
                          'line-width': zoom_ramp(14, 0.5, 18, 1, 22, 2, base=1.5)}, emissive=0.15), v, 'path_osm', 0),
        gate(layer(walk_prefix + 'path', 'line', 'transportation', minzoom=minzoom, filter=walk,
                   metadata=draw_once(c, walk_prefix + 'path', {'massif:minzoom-param': 'path_min_zoom'}),
                   layout={'line-cap': 'round', 'line-join': 'round'},
                   paint={'line-color': color, 'line-width': PATH_WIDTH}, emissive=0.25), v, 'path_osm', 0),
        *osm_footway(c, walk_prefix + 'path-osm', walk, minzoom, v, ['!=', get('subclass'), 'cycleway']),
        layer(prefix + 'steps', 'line', 'transportation', minzoom=14, filter=steps,
              paint={'line-color': c['path-case'], 'line-width': PATH_WIDTH,
                     'line-dasharray': ['step', ['zoom'], ['literal', [1, 0]], 17, ['literal', [0.2, 0.2]],
                                        19, ['literal', [0.1, 0.1]]]}, emissive=0.25),
    ]


def osm_footway(c, id, filter, minzoom, v, dashed=None):
    """`path_osm`: OSM Carto's footway, dashes on a translucent white casing, growing in as a path's
    ribbon does; `dashed` narrows the dashes, a cycleway keeping its own blue ones over the casing"""
    meta = {'massif:minzoom-param': 'path_min_zoom'}
    return [gate(layer(id + '-casing', 'line', 'transportation', minzoom=minzoom, filter=filter,
                       layout={'line-join': 'round'}, metadata=dict(meta),
                       paint={'line-color': c['path-osm-case'], 'line-width': zoom_ramp(12, 0, 14, 1.5, 15, 2.5, 18, 5)},
                       emissive=0.25), v, 'path_osm'),
            gate(layer(id, 'line', 'transportation', minzoom=minzoom, filter=['all', filter, dashed] if dashed else filter,
                       layout={'line-join': 'round'}, metadata=dict(meta),
                       paint={'line-color': c['path-osm'], 'line-width': zoom_ramp(12, 0, 14, 0.8, 15, 1.2, 18, 2),
                              'line-dasharray': [1.3, 2.3]}, emissive=0.4), v, 'path_osm')]


def cycleway(c, brunnel_test, prefix=''):
    """Standard's cycleway: a green dash laid over the roads, since a cycle track so often runs
    beside one and would otherwise vanish under its casing."""
    # from z13, where a city's cycle network starts to matter (Standard waits for z15)
    return [layer(prefix + 'cycleway', 'line', 'transportation', minzoom=13,
                  filter=['all', ['==', get('class'), 'path'], ['==', get('subclass'), 'cycleway'], brunnel_test],
                  paint={'line-color': c['cycleway'], 'line-width': zoom_ramp(13, 0.8, 18, 2, 22, 20),
                         'line-opacity': zoom_ramp(13, 0, 13.5, 1),
                         'line-dasharray': ['step', ['zoom'], ['literal', [1, 0]], 16, ['literal', [1, 1]]]},
                  emissive=0.6)]


def unpaved(c, brunnel_test):
    """OSM Carto's unpaved road: the casing broken into dashes, drawn by covering every other stretch
    of it in the road's own fill. One layer per class: the class and the surface are two sets."""
    casing = scaled(CASING_WIDTH, c.get('casing-scale', 1))
    return [layer('road-unpaved-' + cls, 'line', 'transportation', minzoom=14,
                  filter=['all', ['==', get('class'), cls], brunnel_test, ['in', get('surface_detail'), ['literal', UNPAVED]]],
                  layout={'line-join': 'round'},
                  paint={'line-color': c['road'], 'line-gap-width': WIDTH, 'line-width': casing,
                         'line-dasharray': [2, 2]}, emissive=0.4)
            for cls in ('minor', 'service')]


def tracks(c, brunnel_test):
    width = scaled(TRACK_WIDTH, c.get('track-scale', 1))
    casing = scaled(TRACK_CASING, c.get('track-scale', 1))
    outer = width[:3] + [x if i % 2 == 0 else x + 2 * y for i, (x, y) in enumerate(zip(width[3:], casing[3:]))]
    track = ['all', ['==', get('class'), 'track'], brunnel_test]
    out = halo(c, 'track-halo', track, outer, 'track_min_zoom')
    for grade, dash in TRACK_GRADES + [('unknown', [3, 2])]:
        # Alpimaps' planetiler writes the grade's index in OSM's list (grade1 = 0), OpenMapTiles the name
        test = ['!', ['has', 'tracktype']] if grade == 'unknown' else \
            ['in', get('tracktype'), ['literal', [grade, int(grade[-1]) - 1]]]
        out.append(layer('track-' + grade + '-casing', 'line', 'transportation', minzoom=12,
                         filter=['all', ['==', get('class'), 'track'], test, brunnel_test],
                         layout={'line-join': 'round'}, metadata={'massif:minzoom-param': 'track_min_zoom'},
                         paint={'line-color': c['track'], 'line-gap-width': width, 'line-width': casing,
                                'line-dasharray': dash}, emissive=0.25))
    return out + [layer('track-fill', 'line', 'transportation', minzoom=12, filter=track,
                        layout={'line-join': 'round'}, metadata={'massif:minzoom-param': 'track_min_zoom'},
                        paint={'line-color': c['track-fill'], 'line-width': width}, emissive=0.25)]


def tunnels(v):
    c = v.palette
    tunnel = ['==', get('brunnel'), 'tunnel']
    return (paths(v, tunnel, 'tunnel-') + cycleway(c, tunnel, 'tunnel-') +
            road_pair(c, 'road-tunnel', ['all', in_class(CLASSES), tunnel], 12, WIDTH, CASING_WIDTH,
                      dash=[3, 3], fill_opacity=0.5, minzoom_param='tunnel_min_zoom',
                      layout={'line-join': 'miter', 'line-cap': 'butt', 'line-sort-key': SORT_KEY}))


def plain_brunnels(v, no_ramp):
    """Tunnels and bridges below the zoom they take their own look, drawn as the road they carry:
    left out, a motorway broke off at every tunnel."""
    c = v.palette
    brunnel = ['all', in_class(CLASSES), ['in', get('brunnel'), ['literal', ['tunnel', 'bridge']]], no_ramp]
    return road_pair(c, 'road-brunnel-low', brunnel, 3, WIDTH, CASING_WIDTH, maxzoom=12)


# a trail's weight from afar, the full chain from z15
VIA_FERRATA_CASING = zoom_ramp(13, 1.6, 15, 4, 18, 8)
VIA_FERRATA_WIDTH = zoom_ramp(13, 0.8, 15, 2.4, 18, 5)


def ground(v):
    c = v.palette
    surface = ['!', ['in', get('brunnel'), ['literal', ['tunnel', 'bridge']]]]
    # the thin ways have no bridge look of their own: a track bridge is the track
    not_tunnel = ['!=', get('brunnel'), 'tunnel']
    no_ramp = ['!=', get('ramp'), 1]
    via_ferrata = ['all', ['==', get('class'), 'via_ferrata'], not_tunnel]
    trails = v.flags.get('trails', False)
    return (paths(v, surface, trails=trails) + (outdoor.trails(v, not_tunnel) if trails else []) + tracks(c, not_tunnel)
            + (outdoor.mtb(v, not_tunnel) if trails else []) + [
        # a chain, MapTiler's beads on a yellow core: no other way is drawn with a dot
        layer('via-ferrata-casing', 'line', 'transportation', minzoom=13, filter=via_ferrata,
              paint={'line-color': c['via-ferrata-case'], 'line-width': VIA_FERRATA_CASING}, emissive=0.25),
        layer('via-ferrata', 'line', 'transportation', minzoom=13, filter=via_ferrata,
              paint={'line-color': c['via-ferrata'], 'line-width': VIA_FERRATA_WIDTH}, emissive=0.25),
        layer('via-ferrata-dots', 'line', 'transportation', minzoom=13, filter=via_ferrata,
              paint={'line-color': c['via-ferrata-case'], 'line-width': VIA_FERRATA_WIDTH,
                     'line-dasharray': [0.5, 1.5]}, emissive=0.25),
    ] + [
        # our fork flags construction on the way, stock OMT suffixes the class
        layer(id, 'line', 'transportation', minzoom=14, filter=test,
              paint={'line-color': c['construction'], 'line-width': zoom_ramp(14, 2, 18, 20, 22, 200, base=1.5),
                     'line-dasharray': ['step', ['zoom'], ['literal', [0.3, 0.3]], 17, ['literal', [0.2, 0.2]],
                                        19, ['literal', [0.1, 0.1]]]}, emissive=0.4)
        for id, test in (('road-construction', ['==', get('construction'), 1]),
                         ('road-construction-omt', in_class([k + '_construction' for k in CLASSES])))
    ] + road_pair(c, 'road-link', ['all', in_class(CLASSES), surface, ['==', get('ramp'), 1]], 12,
                  LINK_WIDTH, LINK_CASING)
      + low_casing(v, ['all', in_class(CLASSES), not_tunnel, no_ramp])
      + low_casing(v, ['all', in_class(CLASSES), ['==', get('brunnel'), 'tunnel'], no_ramp], 'road-tunnel-casing-low', 13)
      + plain_brunnels(v, no_ramp)
      + road_pair(c, 'road', ['all', in_class(CLASSES), surface, no_ramp], 3, WIDTH, CASING_WIDTH)
      + unpaved(c, surface) + cycleway(c, surface) + [
        # private and no-access ways: the casing's red dashes, the OSM convention MapTiler also draws
        layer('road-no-access', 'line', 'transportation', minzoom=14,
              filter=['all', ['!=', get('class'), 'path'], ['in', get('access'), ['literal', ['no', 'private']]]],
              paint={'line-color': c['no-access'], 'line-width': zoom_ramp(14, 0.8, 15, 1, 18, 4, 22, 20, base=1.5),
                     'line-dasharray': [1, 3]}, emissive=0.4),
    ] + oneway(c))


def oneway(c):
    base = ['all', in_class(CLASSES), ['!=', get('class'), 'motorway']]
    # its own layer per arrow colour: an icon that differs by variant is data-driven, and a data-driven
    # icon converts to a shield, which drops the line placement. A dark arrow in a white edge, which
    # reads on a white street, a coloured major and a night road alike
    arrow = c.get('oneway-arrow', 'ink')
    return [layer('oneway' + ('' if arrow == 'ink' else '-' + arrow) + suffix, 'symbol', 'transportation', minzoom=16,
                  filter=base + [['==', get('oneway'), value]],
                  layout={'symbol-placement': 'line', 'symbol-spacing': 200, 'icon-image': icon,
                          'icon-size': zoom_ramp(16, 0.6, 18, 1), 'icon-rotation-alignment': 'map',
                          'icon-allow-overlap': True, 'icon-ignore-placement': True},
                  paint={'icon-opacity': 0.8}, emissive={'icon-emissive-strength': 1})
            for suffix, value, icon in (('', 1, 'oneway-' + arrow), ('-reverse', -1, 'oneway-reverse-' + arrow))]


def bridges(v):
    c = v.palette
    bridge = ['==', get('brunnel'), 'bridge']
    return (paths(v, bridge, 'bridge-') +
            road_pair(c, 'road-bridge', ['all', in_class(CLASSES), bridge], 12, WIDTH,
                      zoom_ramp(12, 0.8, 14, 1.2, 22, 3, base=1.5), case_key='bridge-case',
                      # Standard's: a round casing rings the bridge's end over the road it lands on
                      case_cap='butt') + cycleway(c, bridge, 'bridge-'))
