from lib import from_zoom, get, halo, in_class, layer, scaled, zoom_ramp
from layers import outdoor

CLASSES = ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'minor', 'service']

SORT_KEY = ['match', get('class'), 'motorway', 7, 'trunk', 6, 'primary', 5, 'secondary', 4,
            'tertiary', 3, 'minor', 2, 1]

# Mapbox Standard's `roads` widths on OMT classes: its street is our minor, its fallback our service.
# a class stays at 0 until Standard's own filter lets it in: primary at 6, secondary 8, tertiary 9
WIDTH = zoom_ramp(
    3, ['match', get('class'), ['motorway', 'trunk'], 0.8, 0],
    6, ['match', get('class'), ['motorway', 'trunk'], 1, 'primary', 0.4, 0],
    8, ['match', get('class'), ['motorway', 'trunk'], 1.3, 'primary', 1.1, 0],
    9, ['match', get('class'), ['motorway', 'trunk'], 1.6, 'primary', 1.4, 'secondary', 0.6, 0],
    12, ['match', get('class'), ['motorway', 'trunk'], 3.2, 'primary', 3, ['secondary', 'tertiary'], 2.2, 'minor', 0.5, 0],
    18, ['match', get('class'), ['motorway', 'trunk'], 30, 'primary', 28, ['secondary', 'tertiary'], 26, 'minor', 20, 10],
    22, ['match', get('class'), ['motorway', 'trunk'], 300, 'primary', 280, ['secondary', 'tertiary'], 260, 'minor', 200, 100],
    base=1.5)

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

# Standard's *_link widths; OMT marks a link with ramp=1 on the class it serves
LINK_WIDTH = zoom_ramp(12, ['match', get('class'), ['motorway', 'trunk'], 0.8, 0.4],
                       18, ['match', get('class'), ['motorway', 'trunk'], 20, 18],
                       22, ['match', get('class'), ['motorway', 'trunk'], 200, 180], base=1.5)
LINK_CASING = zoom_ramp(12, 0.5, 14, 0.8, 22, 2, base=1.5)

PATH_WIDTH = zoom_ramp(12, 0, 15, 1, 18, 6, 22, 80, base=1.5)
TRACK_WIDTH = zoom_ramp(12, 0.5, 15, 1.2, 18, 3, 22, 12, base=1.5)
# OSM Carto's tracktype ladder: the rougher the track, the shorter the dash
TRACK_GRADES = [('grade1', None), ('grade2', [5, 2]), ('grade3', [3, 2]), ('grade4', [2, 2]),
                ('grade5', [1, 2])]

EMISSIVE = ['match', get('class'), ['motorway', 'trunk'], 0.6, 0.4]

# OSM's loose surfaces, read off `surface_detail` (Alpimaps' planetiler keeps the raw tag there)
UNPAVED = ['unpaved', 'compacted', 'fine_gravel', 'gravel', 'pebblestone', 'ground', 'dirt', 'earth', 'grass',
           'grass_paver', 'mud', 'sand', 'rock', 'woodchips']


def fill_color(c):
    # below z14, uncased, Standard's one grey-blue for every road under a primary: a fill near the
    # ground's lightness would vanish without its casing
    low = ['match', get('class'), 'motorway', c['motorway'], 'trunk', c['trunk'], 'primary', c['primary'],
           'secondary', c['secondary-low'], c['road-low']]
    high = ['match', get('class'), 'motorway', c['motorway'], 'trunk', c['trunk'], 'primary', c['primary'],
            'secondary', c['secondary'], 'tertiary', c['tertiary'], c['road']]
    z = c.get('casing-from', 14)
    # e-ink fades the grey to white as the casing grows, rather than flipping a dark road at z14
    return ['interpolate', ['linear'], ['zoom'], z - 1, low, z, high] if c.get('casing-low') else ['step', ['zoom'], low, z, high]


def case_color(c, key='case'):
    per_class = [] if key != 'case' else ['primary', c['primary-case'], 'secondary', c['secondary-case'], 'tertiary', c['tertiary-case']]
    return ['match', get('class'), 'motorway', c['motorway-' + key], 'trunk', c['trunk-' + key], *per_class,
            c['road-' + key]]


def major_only_below(expr, z):
    """A per-class width ramp whose stops below zoom z keep motorway, trunk and primary only."""
    def value(match, cls):
        if not isinstance(match, list):
            return match
        for i in range(2, len(match) - 1, 2):
            if match[i] == cls or (isinstance(match[i], list) and cls in match[i]):
                return match[i + 1]
        return match[-1]
    if not isinstance(expr, list) or expr[0] != 'interpolate':
        return expr
    out = expr[:3]
    for i in range(3, len(expr), 2):
        stop, match = expr[i], expr[i + 1]
        if stop < z:
            match = ['match', get('class'), *[x for cls in ('motorway', 'trunk', 'primary') for x in (cls, value(match, cls))], 0]
        elif out[-2] < z - 1:
            # the small roads' casing grows over z13-14, as from_zoom's does, not from the stop before
            out += [z - 1, out[-1]]
        out += [stop, match]
    return out


def road_pair(c, id, filter, minzoom, width, casing, case_key='case', dash=None, layout=None, fill_opacity=None,
              case_cap=None):
    layout = layout or {'line-cap': 'round', 'line-join': 'round', 'line-sort-key': SORT_KEY}
    case_layout = {**layout, 'line-cap': case_cap} if case_cap else layout
    # e-ink orders the major roads by the weight of their outline, having no colour to do it with; the
    # small ones stay uncased below z14 as elsewhere, a grey line rather than a heavy double one
    casing = scaled(major_only_below(casing, c.get('casing-from', 14)) if c.get('casing-low') else from_zoom(casing, 14),
                    c.get('casing-scale', 1))
    case_paint = {'line-color': case_color(c, case_key), 'line-gap-width': width, 'line-width': casing}
    if dash:
        case_paint['line-dasharray'] = dash
    fill_paint = {'line-color': fill_color(c), 'line-width': width}
    if fill_opacity is not None:
        fill_paint['line-opacity'] = fill_opacity
    return [
        layer(id + '-casing', 'line', 'transportation', minzoom=minzoom, filter=filter, layout=case_layout,
              paint=case_paint, emissive=0),
        layer(id, 'line', 'transportation', minzoom=minzoom, filter=filter, layout=layout,
              paint=fill_paint, emissive=EMISSIVE),
    ]


def paths(c, brunnel_test, prefix='', minzoom=12, trails=False):
    """footways, cycleways, bridleways and steps, Standard's white ribbon with a hairline case. With
    `trails` the paths and bridleways are left to outdoor.trails, which draws them by difficulty."""
    walk = ['all', ['==', get('class'), 'path'], ['!=', get('subclass'), 'steps'], brunnel_test]
    if trails:
        walk = ['all', ['==', get('class'), 'path'], ['!', in_class(['steps'] + outdoor.TRAILS, 'subclass')],
                brunnel_test]
    walk_prefix = prefix + ('urban-' if trails else '')
    steps = ['all', ['==', get('class'), 'path'], ['==', get('subclass'), 'steps'], brunnel_test]
    # the zoom ramp outside: maplibre only takes a zoom expression at the top of a property
    color = zoom_ramp(*[x for z, key in ((15, 'path'), (16, 'path-z16')) for x in (
        z, ['match', get('subclass'), 'cycleway', c['cycleway'], 'bridleway', c['bridleway'], c[key]])])
    return [
        layer(walk_prefix + 'path-casing', 'line', 'transportation', minzoom=15, filter=walk,
              layout={'line-join': 'round'},
              paint={'line-color': c['path-case'], 'line-gap-width': PATH_WIDTH,
                     'line-width': zoom_ramp(14, 0.5, 18, 1, 22, 2, base=1.5)}, emissive=0.15),
        layer(walk_prefix + 'path', 'line', 'transportation', minzoom=minzoom, filter=walk,
              metadata={'massif:minzoom-param': 'path_min_zoom'},
              layout={'line-cap': 'round', 'line-join': 'round'},
              paint={'line-color': color, 'line-width': PATH_WIDTH}, emissive=0.25),
        layer(prefix + 'steps', 'line', 'transportation', minzoom=14, filter=steps,
              paint={'line-color': c['path-case'], 'line-width': PATH_WIDTH,
                     'line-dasharray': ['step', ['zoom'], ['literal', [1, 0]], 17, ['literal', [0.2, 0.2]],
                                        19, ['literal', [0.1, 0.1]]]}, emissive=0.25),
    ]


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
    out = halo(c, 'track-halo', ['all', ['==', get('class'), 'track'], brunnel_test], width, 'track_min_zoom')
    for grade, dash in TRACK_GRADES + [('unknown', [3, 2])]:
        # Alpimaps' planetiler writes the grade's index in OSM's list (grade1 = 0), OpenMapTiles the name
        test = ['!', ['has', 'tracktype']] if grade == 'unknown' else \
            ['in', get('tracktype'), ['literal', [grade, int(grade[-1]) - 1]]]
        paint = {'line-color': c['track'], 'line-width': width}
        if dash:
            paint['line-dasharray'] = dash
        out.append(layer('track-' + grade, 'line', 'transportation', minzoom=12,
                         filter=['all', ['==', get('class'), 'track'], test, brunnel_test],
                         metadata={'massif:minzoom-param': 'track_min_zoom'},
                         paint=paint, emissive=0.25))
    return out


def tunnels(v):
    c = v.palette
    tunnel = ['==', get('brunnel'), 'tunnel']
    return (paths(c, tunnel, 'tunnel-') +
            road_pair(c, 'road-tunnel', ['all', in_class(CLASSES), tunnel], 12, WIDTH, CASING_WIDTH,
                      dash=[3, 3], fill_opacity=0.5,
                      layout={'line-join': 'miter', 'line-cap': 'butt', 'line-sort-key': SORT_KEY}))


def ground(v):
    c = v.palette
    surface = ['!', ['in', get('brunnel'), ['literal', ['tunnel', 'bridge']]]]
    no_ramp = ['!=', get('ramp'), 1]
    trails = v.flags.get('trails', False)
    return (paths(c, surface, trails=trails) + (outdoor.trails(c, surface) if trails else []) + tracks(c, surface)
            + (outdoor.mtb(c, surface) if trails else []) + [
        layer('via-ferrata', 'line', 'transportation', minzoom=13,
              filter=['all', ['==', get('class'), 'via_ferrata'], ['!=', get('brunnel'), 'tunnel']],
              paint={'line-color': c['via-ferrata'], 'line-width': zoom_ramp(13, 1, 18, 2.5),
                     'line-dasharray': [1, 1.5]}, emissive=0.25),
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
      + road_pair(c, 'road', ['all', in_class(CLASSES), surface, no_ramp], 3, WIDTH, CASING_WIDTH)
      + unpaved(c, surface) + cycleway(c, surface) + [
        # private and no-access ways: the casing's red dashes, the OSM convention MapTiler also draws
        layer('road-no-access', 'line', 'transportation', minzoom=15,
              filter=['all', ['!=', get('class'), 'path'], ['in', get('access'), ['literal', ['no', 'private']]]],
              paint={'line-color': c['no-access'], 'line-width': zoom_ramp(15, 1, 18, 4, 22, 20, base=1.5),
                     'line-dasharray': [1, 3]}, emissive=0.4),
    ] + oneway(c))


def oneway(c):
    base = ['all', in_class(CLASSES), ['!=', get('class'), 'motorway']]
    # its own layer per arrow colour: an icon that differs by variant is data-driven, and a data-driven
    # icon converts to a shield, which drops the line placement
    arrow = c.get('oneway-arrow', 'white')
    return [layer('oneway' + ('' if arrow == 'white' else '-' + arrow) + suffix, 'symbol', 'transportation', minzoom=16,
                  filter=base + [['==', get('oneway'), value]],
                  layout={'symbol-placement': 'line', 'symbol-spacing': 200, 'icon-image': icon,
                          'icon-size': zoom_ramp(16, 0.6, 18, 1), 'icon-rotation-alignment': 'map',
                          'icon-allow-overlap': True, 'icon-ignore-placement': True},
                  paint={'icon-opacity': 0.8}, emissive={'icon-emissive-strength': 1})
            for suffix, value, icon in (('', 1, 'oneway-' + arrow), ('-reverse', -1, 'oneway-reverse-' + arrow))]


def bridges(v):
    c = v.palette
    bridge = ['==', get('brunnel'), 'bridge']
    return (paths(c, bridge, 'bridge-') +
            road_pair(c, 'road-bridge', ['all', in_class(CLASSES), bridge], 12, WIDTH,
                      zoom_ramp(12, 0.8, 14, 1.2, 22, 3, base=1.5), case_key='bridge-case',
                      # Standard's: a round casing rings the bridge's end over the road it lands on
                      case_cap='butt') + cycleway(c, bridge, 'bridge-'))
