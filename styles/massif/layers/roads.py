from lib import get, in_class, layer, zoom_ramp

CLASSES = ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'minor', 'service']

SORT_KEY = ['match', get('class'), 'motorway', 7, 'trunk', 6, 'primary', 5, 'secondary', 4,
            'tertiary', 3, 'minor', 2, 1]

# Mapbox Standard's `roads` widths on OMT classes: its street is our minor, its fallback our service.
WIDTH = zoom_ramp(
    3, ['match', get('class'), ['motorway', 'trunk', 'primary'], 0.8, 0],
    12, ['match', get('class'), ['motorway', 'trunk'], 3.2, 'primary', 3, ['secondary', 'tertiary'], 2.2, 'minor', 0.5, 0],
    18, ['match', get('class'), ['motorway', 'trunk'], 30, 'primary', 28, ['secondary', 'tertiary'], 26, 'minor', 20, 10],
    22, ['match', get('class'), ['motorway', 'trunk'], 300, 'primary', 280, ['secondary', 'tertiary'], 260, 'minor', 200, 100],
    base=1.5)

CASING_WIDTH = zoom_ramp(
    3, ['match', get('class'), ['motorway', 'trunk', 'primary'], 0.5, 0],
    12, ['match', get('class'), ['motorway', 'trunk', 'primary', 'secondary', 'tertiary'], 0.8, 'minor', 0.5, 0],
    14, ['match', get('class'), ['motorway', 'trunk', 'primary'], 1, 0.8],
    22, 2,
    base=1.5)


def layers(v):
    layout = {'line-cap': 'round', 'line-join': 'round', 'line-sort-key': SORT_KEY}
    return [
        layer('road-casing', 'line', 'transportation', minzoom=3, filter=in_class(CLASSES), layout=layout,
              paint={'line-color': ['match', get('class'), 'motorway', '#c08a3e', 'trunk', '#c9a24a', '#c3bfb4'],
                     'line-gap-width': WIDTH,
                     'line-width': CASING_WIDTH}),
        layer('road-fill', 'line', 'transportation', minzoom=3, filter=in_class(CLASSES), layout=layout,
              paint={'line-color': ['match', get('class'), 'motorway', '#f5c98a', 'trunk', '#fadfa4',
                                    'primary', '#fdf0c4', '#ffffff'],
                     'line-width': WIDTH}),
    ]
