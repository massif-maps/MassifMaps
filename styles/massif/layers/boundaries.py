from lib import get, layer, zoom_ramp


def layers(v):
    c = v.palette
    land = ['!=', get('maritime'), 1]
    level = lambda n: ['==', get('admin_level'), n]
    return [
        layer('boundary-country-halo', 'line', 'boundary', minzoom=3,
              filter=['all', level(2), land],
              paint={'line-color': c['boundary-halo'], 'line-width': zoom_ramp(3, 4, 12, 8),
                     'line-opacity': zoom_ramp(3, 0, 4, 0.5), 'line-blur': zoom_ramp(3, 0, 12, 2)}),
        layer('boundary-minor', 'line', 'boundary', minzoom=10,
              filter=['all', ['in', get('admin_level'), ['literal', [6, 8]]], land],
              paint={'line-color': c['boundary-minor'],
                     'line-width': zoom_ramp(10, ['match', get('admin_level'), 6, 0.6, 0.4],
                                             16, ['match', get('admin_level'), 6, 1.5, 1]),
                     'line-dasharray': [3, 2], 'line-opacity': zoom_ramp(10, 0, 11, 0.7)},
              emissive=0.5),
        layer('boundary-state', 'line', 'boundary', minzoom=3,
              filter=['all', level(4), land],
              paint={'line-color': c['boundary-state'], 'line-width': zoom_ramp(3, 0.3, 12, 1.5),
                     'line-dasharray': ['step', ['zoom'], ['literal', [2, 0]], 7, ['literal', [2, 2, 6, 2]]],
                     'line-opacity': zoom_ramp(3, 0, 4, 1)},
              emissive=0.5),
        layer('boundary-country', 'line', 'boundary', minzoom=1,
              filter=['all', level(2), land, ['!=', get('disputed'), 1]],
              paint={'line-color': c['boundary-country'], 'line-width': zoom_ramp(3, 0.5, 12, 2)},
              emissive=1),
        layer('boundary-disputed', 'line', 'boundary', minzoom=1,
              filter=['all', level(2), land, ['==', get('disputed'), 1]],
              paint={'line-color': c['boundary-country'], 'line-width': zoom_ramp(3, 0.5, 12, 2),
                     'line-dasharray': ['step', ['zoom'], ['literal', [3, 2, 5]], 7, ['literal', [2, 1.5]]]},
              emissive=0.8),
    ]
