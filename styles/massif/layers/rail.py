from lib import by_hour, gate, get, in_class, layer, zoom_ramp

SLEEPERS = ['step', ['zoom'], ['literal', [0.1, 15]], 16, ['literal', [0.1, 1]], 18, ['literal', [0.05, 0.5]]]


def sleepers(id, filter, color, metadata=None):
    return layer(id, 'line', 'transportation', minzoom=13, filter=filter,
                 paint={'line-color': color, 'line-width': zoom_ramp(16, 2, 18, 6, 20, 16, 22, 32, base=1.5),
                        'line-dasharray': SLEEPERS, 'line-opacity': zoom_ramp(13.75, 0, 14, 1)}, metadata=metadata)


def pair(c, id, filter, prefix=''):
    """Standard's two-layer rail: a line-gap-width pair for the rails, and a wide line worn away
    by a very short dash for the sleepers."""
    night = {'massif:paint': {'line-color': by_hour(c['rail-night'], c['rail'])}}
    return [
        layer(prefix + id, 'line', 'transportation', filter=filter,
              paint={'line-color': c['rail'], 'line-gap-width': zoom_ramp(15, 0, 16, 1, 18, 2, 22, 20),
                     'line-width': zoom_ramp(14, 0.5, 22, 2, base=1.5)}, metadata=night),
        sleepers(prefix + id + '-tracks', filter, c['rail'], night),
    ]


def emphasis(v, filter):
    """`emphasis_rails`, Alpimaps' switch, for a map read along the railway: a main line (no `service`)
    in a solid dark line from its first zoom, handing over to dark sleepers. Hidden in MapLibre (no config in a filter)."""
    c = v.palette
    main = ['all', filter, ['!', ['has', 'service']]]
    line = layer('rail-emphasis', 'line', 'transportation', filter=main,
                 paint={'line-color': c['rail-emphasis'], 'line-width': zoom_ramp(6, 1, 12, 2, 16, 3.5, 20, 7, base=1.3),
                        'line-opacity': zoom_ramp(15, 1, 16.5, 0)}, emissive=0.5)
    ties = sleepers('rail-tracks-emphasis', main, c['rail-emphasis'])
    return [gate(line, v, 'emphasis_rails'), gate(ties, v, 'emphasis_rails')]


def ground(v):
    c = v.palette
    surface = ['!', ['in', get('brunnel'), ['literal', ['tunnel', 'bridge']]]]
    rail = ['all', ['==', get('class'), 'rail'], surface]
    line, ties = emphasis(v, rail)
    return ([line] + pair(c, 'rail', rail) + [ties] +
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
        # a hairline where it first shows, so a ski area's lifts do not net the whole slope
        layer('aerialway', 'line', 'transportation', minzoom=12, filter=['==', get('class'), 'aerialway'],
              paint={'line-color': ['match', get('subclass'), 'zip_line', c['aerialway'], c['aerialway-lift']],
                     'line-width': zoom_ramp(12, 0.4, 15, 1, 20, 2, base=1.5)}, emissive=1),
        # the tiles carry power lines from z14 only: drawn from 13, an app on the SDK's 256-px tiles (its
        # zoom one above maplibre's) shows them from its 14, from the z14 tiles it already reads there
        layer('power-line', 'line', 'power', minzoom=13,
              paint={'line-color': c['power'], 'line-width': zoom_ramp(13, 0.8, 16, 1, 20, 1.5, base=1.5)}, emissive=0.5),
    ]
