from lib import by_hour, gate, get, in_class, layer, zoom_ramp

SLEEPERS = ['step', ['zoom'], ['literal', [0.1, 15]], 16, ['literal', [0.1, 1]], 18, ['literal', [0.05, 0.5]]]


def pair(c, id, filter, prefix=''):
    """Standard's two-layer rail: a line-gap-width pair for the rails, and a wide line worn away
    by a very short dash for the sleepers."""
    color = {'line-color': c['rail']}
    night = {'massif:paint': {'line-color': by_hour(c['rail-night'], c['rail'])}}
    return [
        layer(prefix + id, 'line', 'transportation', filter=filter,
              paint={**color, 'line-gap-width': zoom_ramp(15, 0, 16, 1, 18, 2, 22, 20),
                     'line-width': zoom_ramp(14, 0.5, 22, 2, base=1.5)}, metadata=night),
        layer(prefix + id + '-tracks', 'line', 'transportation', minzoom=13, filter=filter,
              paint={**color, 'line-width': zoom_ramp(16, 2, 18, 6, 20, 16, 22, 32, base=1.5),
                     'line-dasharray': SLEEPERS, 'line-opacity': zoom_ramp(13.75, 0, 14, 1)}, metadata=night),
    ]


def emphasis(c, filter):
    """`emphasis_rails`, Alpimaps' switch: a main line (no `service`, so no siding or yard) drawn dark
    and wide, for a map read along the railway. Hidden in MapLibre, which has no config in a filter."""
    return [layer('rail-emphasis', 'line', 'transportation', minzoom=6,
                  filter=['all', filter, ['!', ['has', 'service']]],
                  layout={'visibility': 'none'},
                  paint={'line-color': c['rail-emphasis'], 'line-width': zoom_ramp(6, 1, 12, 2, 16, 3.5, 20, 7, base=1.3)},
                  metadata={'massif:layout': {'visibility': 'visible'},
                            'massif:filter': ['==', ['config', 'emphasis_rails'], 1]}, emissive=0.5)]


def ground(v):
    c = v.palette
    surface = ['!', ['in', get('brunnel'), ['literal', ['tunnel', 'bridge']]]]
    rail = ['all', ['==', get('class'), 'rail'], surface]
    return (emphasis(c, rail) + pair(c, 'rail', rail) +
            [gate(lay, v, 'show_tram') for lay in pair(c, 'tram', ['all', ['==', get('class'), 'transit'], surface])])


def tunnels(v):
    c = v.palette
    return [gate(layer('rail-tunnel', 'line', 'transportation', minzoom=13,
                       filter=['all', in_class(['rail', 'transit']), ['==', get('brunnel'), 'tunnel']],
                       paint={'line-color': c['rail'], 'line-width': zoom_ramp(13, 0.5, 18, 2, base=1.5),
                              'line-dasharray': [2, 2], 'line-opacity': 0.5}), v, 'show_underground')]


def bridges(v):
    c = v.palette
    bridge = ['==', get('brunnel'), 'bridge']
    return [layer('rail-bridge-casing', 'line', 'transportation', minzoom=13,
                  filter=['all', in_class(['rail', 'transit']), bridge],
                  paint={'line-color': c['road-bridge-case'], 'line-gap-width': zoom_ramp(13, 2, 18, 8, 22, 40, base=1.5),
                         'line-width': zoom_ramp(13, 0.5, 18, 1.5)}),
            layer('rail-bridge-deck', 'line', 'transportation', minzoom=13,
                  filter=['all', in_class(['rail', 'transit']), bridge],
                  paint={'line-color': c['land'], 'line-width': zoom_ramp(13, 2, 18, 8, 22, 40, base=1.5)})] + \
        pair(c, 'rail', ['all', ['==', get('class'), 'rail'], bridge], 'bridge-') + \
        [gate(lay, v, 'show_tram') for lay in pair(c, 'tram', ['all', ['==', get('class'), 'transit'], bridge], 'bridge-')]


def overhead(v):
    """ferries and lifts, over everything on the ground"""
    c = v.palette
    return [
        layer('ferry', 'line', 'transportation', minzoom=8, filter=['==', get('class'), 'ferry'],
              paint={'line-color': c['ferry'], 'line-width': zoom_ramp(14, 0.5, 20, 1, base=1.5),
                     'line-opacity': zoom_ramp(8, 0, 10, 1),
                     'line-dasharray': ['step', ['zoom'], ['literal', [1, 0]], 13, ['literal', [12, 4]]]},
              emissive=0.5),
        layer('aerialway', 'line', 'transportation', minzoom=12, filter=['==', get('class'), 'aerialway'],
              paint={'line-color': c['aerialway'], 'line-width': zoom_ramp(14, 1, 20, 2, base=1.5)}, emissive=1),
    ]
