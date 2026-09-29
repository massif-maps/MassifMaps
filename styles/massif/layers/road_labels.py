from lib import by_hour, get, layer, zoom_ramp

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
    c = v.palette
    return [
        layer(id, 'symbol', 'transportation_name', minzoom=minzoom,
              filter=['in', get('class'), ['literal', classes]],
              layout={'symbol-placement': 'line',
                      'text-field': ['coalesce', get('name'), get('name_int')],
                      'text-font': 'medium',
                      'text-size': zoom_ramp(10, size[0], 18, size[1]),
                      'text-transform': 'uppercase',
                      'text-letter-spacing': 0.15,
                      'text-max-angle': 30,
                      'text-padding': 1,
                      'text-pitch-alignment': 'viewport'},
              paint={'text-color': c['road-label'], 'text-halo-color': c['road-label-halo'], 'text-halo-width': 1},
              # Standard's night pair: light ink on a dark halo, not the day's grey glowing
              metadata={'massif:paint': {'text-color': by_hour(c['road-label-night'], c['road-label']),
                                         'text-halo-color': by_hour(c['road-label-halo-night'], c['road-label-halo'])}})
        for id, minzoom, classes, size in CLASSES
    ]
