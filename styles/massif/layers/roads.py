from lib import get, in_class, layer, zoom_ramp

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

# kept zoomed out, as Liberty does, but a hair: at 1 px of fill a full casing is all you see
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


def fill_color(c):
    return ['match', get('class'), 'motorway', c['motorway'], 'trunk', c['trunk'], 'primary', c['primary'], c['road']]


def case_color(c, key='case'):
    return ['match', get('class'), 'motorway', c['motorway-' + key], 'trunk', c['trunk-' + key], c['road-' + key]]


def road_pair(c, id, filter, minzoom, width, casing, case_key='case', dash=None, layout=None, fill_opacity=None):
    layout = layout or {'line-cap': 'round', 'line-join': 'round', 'line-sort-key': SORT_KEY}
    case_paint = {'line-color': case_color(c, case_key), 'line-gap-width': width, 'line-width': casing}
    if dash:
        case_paint['line-dasharray'] = dash
    fill_paint = {'line-color': fill_color(c), 'line-width': width}
    if fill_opacity is not None:
        fill_paint['line-opacity'] = fill_opacity
    return [
        layer(id + '-casing', 'line', 'transportation', minzoom=minzoom, filter=filter, layout=layout,
              paint=case_paint, emissive=0),
        layer(id, 'line', 'transportation', minzoom=minzoom, filter=filter, layout=layout,
              paint=fill_paint, emissive=EMISSIVE),
    ]


def paths(c, brunnel_test, prefix='', minzoom=12):
    """footways, cycleways, bridleways and steps, Standard's white ribbon with a hairline case"""
    walk = ['all', ['==', get('class'), 'path'], ['!=', get('subclass'), 'steps'], brunnel_test]
    steps = ['all', ['==', get('class'), 'path'], ['==', get('subclass'), 'steps'], brunnel_test]
    # the zoom ramp outside: maplibre only takes a zoom expression at the top of a property
    color = zoom_ramp(*[x for z, key in ((15, 'path'), (16, 'path-z16')) for x in (
        z, ['match', get('subclass'), 'cycleway', c['cycleway'], 'bridleway', c['bridleway'], c[key]])])
    return [
        layer(prefix + 'path-casing', 'line', 'transportation', minzoom=15, filter=walk,
              layout={'line-join': 'round'},
              paint={'line-color': c['path-case'], 'line-gap-width': PATH_WIDTH,
                     'line-width': zoom_ramp(14, 0.5, 18, 1, 22, 2, base=1.5)}, emissive=0.15),
        layer(prefix + 'path', 'line', 'transportation', minzoom=minzoom, filter=walk,
              layout={'line-cap': 'round', 'line-join': 'round'},
              paint={'line-color': color, 'line-width': PATH_WIDTH}, emissive=0.25),
        layer(prefix + 'steps', 'line', 'transportation', minzoom=14, filter=steps,
              paint={'line-color': c['path-case'], 'line-width': PATH_WIDTH,
                     'line-dasharray': ['step', ['zoom'], ['literal', [1, 0]], 17, ['literal', [0.2, 0.2]],
                                        19, ['literal', [0.1, 0.1]]]}, emissive=0.25),
    ]


def tracks(c, brunnel_test):
    out = []
    for grade, dash in TRACK_GRADES + [('unknown', [3, 2])]:
        test = ['!', ['has', 'tracktype']] if grade == 'unknown' else ['==', get('tracktype'), grade]
        paint = {'line-color': c['track'], 'line-width': TRACK_WIDTH}
        if dash:
            paint['line-dasharray'] = dash
        out.append(layer('track-' + grade, 'line', 'transportation', minzoom=12,
                         filter=['all', ['==', get('class'), 'track'], test, brunnel_test],
                         paint=paint, emissive=0.25))
    return out


def tunnels(v):
    c = v.palette
    tunnel = ['==', get('brunnel'), 'tunnel']
    return (paths(c, tunnel, 'tunnel-', minzoom=16) +
            road_pair(c, 'road-tunnel', ['all', in_class(CLASSES), tunnel], 12, WIDTH, CASING_WIDTH,
                      dash=[3, 3], fill_opacity=0.5,
                      layout={'line-join': 'miter', 'line-cap': 'butt', 'line-sort-key': SORT_KEY}))


def ground(v):
    c = v.palette
    surface = ['!', ['in', get('brunnel'), ['literal', ['tunnel', 'bridge']]]]
    no_ramp = ['!=', get('ramp'), 1]
    return (paths(c, surface) + tracks(c, surface) + [
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
      + road_pair(c, 'road', ['all', in_class(CLASSES), surface, no_ramp], 3, WIDTH, CASING_WIDTH) + [
        # private and no-access ways: the casing's red dashes, the OSM convention MapTiler also draws
        layer('road-no-access', 'line', 'transportation', minzoom=15,
              filter=['all', ['!=', get('class'), 'path'], ['in', get('access'), ['literal', ['no', 'private']]]],
              paint={'line-color': c['no-access'], 'line-width': zoom_ramp(15, 1, 18, 4, 22, 20, base=1.5),
                     'line-dasharray': [1, 3]}, emissive=0.4),
    ] + oneway(c))


def oneway(c):
    base = ['all', in_class(CLASSES), ['!=', get('class'), 'motorway']]
    return [layer('oneway' + suffix, 'symbol', 'transportation', minzoom=16,
                  filter=base + [['==', get('oneway'), value]],
                  layout={'symbol-placement': 'line', 'symbol-spacing': 200, 'icon-image': icon,
                          'icon-size': zoom_ramp(16, 0.6, 18, 1), 'icon-rotation-alignment': 'map',
                          'icon-allow-overlap': True, 'icon-ignore-placement': True},
                  paint={'icon-opacity': 0.8}, emissive={'icon-emissive-strength': 1})
            for suffix, value, icon in (('', 1, 'oneway'), ('-reverse', -1, 'oneway-reverse'))]


def bridges(v):
    c = v.palette
    bridge = ['==', get('brunnel'), 'bridge']
    return (paths(c, bridge, 'bridge-') +
            road_pair(c, 'road-bridge', ['all', in_class(CLASSES), bridge], 12, WIDTH,
                      zoom_ramp(12, 0.8, 14, 1.2, 22, 3, base=1.5), case_key='bridge-case'))
