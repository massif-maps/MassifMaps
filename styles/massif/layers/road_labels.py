from lib import by_hour, get, layer, zoom_ramp

# Standard's road-label, one layer per class instead of its zoom step inside the filter. Least
# important first, so the primary's name is placed first and wins the collision.
CLASSES = [
    ('road-label-service', 15, ['service'], (6.5, 13)),
    ('road-label-minor', 13, ['minor'], (8, 14)),
    ('road-label-tertiary', 13, ['tertiary'], (9, 16)),
    ('road-label-primary', 12, ['primary', 'secondary'], (9, 16)),
]
# below the shields: a motorway is read by its ref, and its street name would take the A 480's place
MAJOR = [('road-label-major', 10, ['motorway', 'trunk'], (9, 16))]


def ways(v):
    """a track's or a path's name, else its ref (a greenway's V 64): in the way's own ink, in
    sentence case and smaller than a street's, since it names a walk and not an address"""
    c = v.palette
    return [layer(id, 'symbol', 'transportation_name', minzoom=minzoom, filter=['==', get('class'), cls],
                  layout={'symbol-placement': 'line',
                          'text-field': ['coalesce', get('name'), get('name_int'), get('ref')],
                          'text-font': 'italic',
                          'text-size': zoom_ramp(14, 10, 18, 12),
                          'text-max-angle': 30,
                          'text-padding': 1,
                          'text-pitch-alignment': 'viewport'},
                  paint={'text-color': c[key], 'text-halo-color': c['road-label-halo'], 'text-halo-width': 1},
                  metadata={'massif:paint': {'text-color': by_hour(c['road-label-night'], c[key]),
                                             'text-halo-color': by_hour(c['road-label-halo-night'], c['road-label-halo'])}})
            for id, minzoom, cls, key in (('path-label', 15, 'path', 'way-label'), ('track-label', 14, 'track', 'track-label'))]


def major(v):
    return names(v, MAJOR)


def layers(v):
    return ways(v) + names(v, CLASSES)


def names(v, classes):
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
        for id, minzoom, classes, size in classes
    ]
