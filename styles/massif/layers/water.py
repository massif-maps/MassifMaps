from lib import by_hour, get, in_class, layer, padded, zoom_ramp

WIDTH = zoom_ramp(8, ['match', get('class'), ['river', 'canal'], 0.5, 0],
                  9, ['match', get('class'), ['river', 'canal'], 0.8, 0.1],
                  20, ['match', get('class'), ['river', 'canal'], 8, 3], base=1.3)


def layers(v):
    c = v.palette
    ground = ['!=', get('brunnel'), 'tunnel']
    line = {'line-cap': 'round', 'line-join': 'round'}
    mono = v.flags.get('mono', False)
    stream = c.get('waterway', c['water'])
    # e-ink: a river is a dashed grey line and a lake ruled lines inside a light shore, so neither
    # reads as a road's white ribbon between two dark casings
    suffix, dash, dash_seasonal = ('-mono', [5, 2], [1, 2]) if mono else ('', None, [3, 2])
    running = {'line-color': stream, 'line-width': WIDTH, 'line-opacity': zoom_ramp(8, 0, 8.5, 1)}
    if dash:
        running['line-dasharray'] = dash
    # e-ink: a stream's dashes run on a pale bed, where a track's run on nothing, so the two dashed
    # lines are told apart without colour
    bed = [layer('waterway-mono-bed', 'line', 'waterway', minzoom=12, filter=ground, layout=line,
                 paint={'line-color': c['waterway-bed'], 'line-width': padded(WIDTH, 2.5)})] if mono else []
    return bed + [
        layer('waterway' + suffix, 'line', 'waterway', minzoom=8,
              filter=['all', ground, ['!=', get('intermittent'), 1]], layout=line, paint=running),
        layer('waterway-intermittent' + suffix, 'line', 'waterway', minzoom=12,
              filter=['all', ground, ['==', get('intermittent'), 1]],
              paint={'line-color': stream, 'line-width': WIDTH, 'line-dasharray': dash_seasonal}),
        layer('water', 'fill', 'water', filter=['!=', get('brunnel'), 'tunnel'],
              paint={'fill-color': c['water'], 'fill-antialias': False},
              metadata={'massif:paint': {'fill-color': by_hour(c['water-night'], c['water'])}},
              emissive=zoom_ramp(5, 0.1, 7, 0)),
    ] + ([
        # the ruled lines at every zoom: water is the one texture still worth its cost zoomed out
        layer('water-pattern', 'fill', 'water', filter=['!=', get('brunnel'), 'tunnel'],
              paint={'fill-pattern': 'pattern-water'}),
        layer('water-outline', 'line', 'water', minzoom=8, filter=['!=', get('brunnel'), 'tunnel'],
              paint={'line-color': c['shoreline'], 'line-width': zoom_ramp(8, 0.3, 16, 0.8)}),
    ] if mono else []) + [
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
