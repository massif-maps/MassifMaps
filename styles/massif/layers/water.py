from lib import by_hour, get, in_class, layer, zoom_ramp

WIDTH = zoom_ramp(8, ['match', get('class'), ['river', 'canal'], 0.5, 0],
                  9, ['match', get('class'), ['river', 'canal'], 0.8, 0.1],
                  20, ['match', get('class'), ['river', 'canal'], 8, 3], base=1.3)


def layers(v):
    c = v.palette
    ground = ['!=', get('brunnel'), 'tunnel']
    line = {'line-cap': 'round', 'line-join': 'round'}
    return [
        layer('waterway', 'line', 'waterway', minzoom=8,
              filter=['all', ground, ['!=', get('intermittent'), 1]], layout=line,
              paint={'line-color': c['water'], 'line-width': WIDTH, 'line-opacity': zoom_ramp(8, 0, 8.5, 1)}),
        layer('waterway-intermittent', 'line', 'waterway', minzoom=12,
              filter=['all', ground, ['==', get('intermittent'), 1]],
              paint={'line-color': c['water'], 'line-width': WIDTH, 'line-dasharray': [3, 2]}),
    ] + ([
        # e-ink: ruled lines and a shoreline, since blue is not there to say "water"
        layer('water-pattern', 'fill', 'water', filter=['!=', get('brunnel'), 'tunnel'],
              paint={'fill-pattern': 'pattern-water'}),
        layer('water-outline', 'line', 'water', minzoom=8, filter=['!=', get('brunnel'), 'tunnel'],
              paint={'line-color': c['water'], 'line-width': zoom_ramp(8, 0.5, 16, 1.2)}),
    ] if v.flags.get('mono') else [
        layer('water', 'fill', 'water', filter=['!=', get('brunnel'), 'tunnel'],
              paint={'fill-color': c['water'], 'fill-antialias': False},
              metadata={'massif:paint': {'fill-color': by_hour(c['water-night'], c['water'])}},
              emissive=zoom_ramp(5, 0.1, 7, 0)),
    ]) + [
        layer('aeroway-area', 'fill', 'aeroway', minzoom=11, filter=['==', ['geometry-type'], 'Polygon'],
              paint={'fill-color': c['aeroway'], 'fill-opacity': zoom_ramp(10, 0, 11, 1), 'fill-antialias': False},
              emissive=0.15),
        layer('aeroway-line', 'line', 'aeroway', minzoom=9, filter=['==', ['geometry-type'], 'LineString'],
              paint={'line-color': c['aeroway'],
                     'line-width': zoom_ramp(9, ['match', get('class'), 'runway', 1, 0.5],
                                             18, ['match', get('class'), 'runway', 80, 20], base=1.5),
                     'line-opacity': zoom_ramp(10, 0, 11, 1)},
              emissive=0.5),
    ]
