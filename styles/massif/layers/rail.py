from lib import get, layer, zoom_ramp


def pair(id, cls):
    """Standard's two-layer rail: a line-gap-width pair for the rails, and a wide line worn away
    by a very short dash for the sleepers."""
    filter = ['==', get('class'), cls]
    return [
        layer(id, 'line', 'transportation', filter=filter,
              paint={'line-color': '#a6a6a6',
                     'line-gap-width': zoom_ramp(15, 0, 16, 1, 18, 2, 22, 20),
                     'line-width': zoom_ramp(14, 0.5, 22, 2, base=1.5)}),
        layer(id + '-tracks', 'line', 'transportation', minzoom=13, filter=filter,
              paint={'line-color': '#a6a6a6',
                     'line-width': zoom_ramp(16, 2, 18, 6, 20, 16, 22, 32, base=1.5),
                     'line-dasharray': ['step', ['zoom'], ['literal', [0.1, 15]], 16, ['literal', [0.1, 1]],
                                        18, ['literal', [0.05, 0.5]]],
                     'line-opacity': zoom_ramp(13.75, 0, 14, 1)}),
    ]


def layers(v):
    return pair('rail', 'rail') + pair('tram', 'transit')
