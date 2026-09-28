from lib import get, layer, zoom_ramp

# Standard's road-label, one layer per class instead of its zoom step inside the filter. Least
# important first, so the motorway's name is placed first and wins the collision.
CLASSES = [
    ('road-label-service', 15, ['service'], (6.5, 13)),
    ('road-label-minor', 13, ['minor'], (8, 14)),
    ('road-label-tertiary', 13, ['tertiary'], (9, 16)),
    ('road-label-primary', 12, ['primary', 'secondary'], (9, 16)),
    ('road-label-major', 10, ['motorway', 'trunk'], (9, 16)),
]


def layers(v):
    return [
        layer(id, 'symbol', 'transportation_name', minzoom=minzoom,
              filter=['in', get('class'), ['literal', classes]],
              layout={'symbol-placement': 'line',
                      'text-field': ['coalesce', get('name'), get('name_int')],
                      'text-font': ['Noto Sans Regular'],
                      'text-size': zoom_ramp(10, size[0], 18, size[1]),
                      'text-transform': 'uppercase',
                      'text-letter-spacing': 0.15,
                      'text-max-angle': 30,
                      'text-padding': 1,
                      'text-pitch-alignment': 'viewport'},
              paint={'text-color': 'hsl(0, 0%, 25%)',
                     'text-halo-color': 'hsl(0, 0%, 95%)',
                     'text-halo-width': 1})
        for id, minzoom, classes, size in CLASSES
    ]
