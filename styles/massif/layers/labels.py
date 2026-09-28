from lib import by_hour, get, in_class, layer, zoom_ramp

NAME = ['coalesce', get('name'), get('name_int')]
REGULAR, BOLD, ITALIC = ['Noto Sans Regular'], ['Noto Sans Bold'], ['Noto Sans Italic']


def text(c, color='label', halo='halo', halo_width=1, night_color='label-night'):
    """paint + the night: a dark label on a white halo by day inverts at night, like Standard's"""
    return dict(
        paint={'text-color': c[color], 'text-halo-color': c[halo], 'text-halo-width': halo_width},
        metadata={'massif:paint': {'text-color': by_hour(c[night_color], c[color]),
                                   'text-halo-color': by_hour(c['halo-night'], c[halo])}},
        emissive={'text-emissive-strength': 1, 'icon-emissive-strength': 1})


def low(v):
    """labels that give way to everything: house numbers, water, parks, peaks"""
    c = v.palette
    water = text(c, 'water-label', night_color='water')
    return [
        layer('housenumber', 'symbol', 'housenumber', minzoom=17,
              layout={'text-field': get('housenumber'), 'text-font': REGULAR,
                      'text-size': zoom_ramp(17, 10, 20, 13), 'text-padding': 3},
              **text(c, 'housenumber')),
        layer('waterway-label', 'symbol', 'waterway', minzoom=13,
              filter=['all', ['has', 'name'], in_class(['river', 'canal'])],
              layout={'symbol-placement': 'line', 'text-field': NAME, 'text-font': ITALIC,
                      'text-size': zoom_ramp(13, 12, 18, 16), 'text-letter-spacing': 0.05,
                      'text-max-angle': 30},
              **water),
        layer('stream-label', 'symbol', 'waterway', minzoom=15,
              filter=['all', ['has', 'name'], ['!', in_class(['river', 'canal'])]],
              layout={'symbol-placement': 'line', 'text-field': NAME, 'text-font': ITALIC,
                      'text-size': zoom_ramp(15, 10, 18, 13), 'text-max-angle': 30},
              **water),
        layer('water-name-line', 'symbol', 'water_name', minzoom=9,
              filter=['==', ['geometry-type'], 'LineString'],
              layout={'symbol-placement': 'line-center', 'text-field': NAME, 'text-font': ITALIC,
                      'text-size': zoom_ramp(9, 12, 18, 18), 'text-letter-spacing': 0.05},
              **water),
        layer('water-name', 'symbol', 'water_name', minzoom=3,
              filter=['==', ['geometry-type'], 'Point'],
              layout={'text-field': NAME, 'text-font': ITALIC,
                      'text-size': zoom_ramp(3, ['match', get('class'), ['ocean', 'sea'], 12, 10],
                                             14, ['match', get('class'), ['ocean', 'sea'], 20, 16]),
                      'text-letter-spacing': ['match', get('class'), 'ocean', 0.25, 'sea', 0.15, 0.01],
                      'text-max-width': 7},
              **water),
        layer('park-label', 'symbol', 'park', minzoom=9, filter=['has', 'name'],
              layout={'text-field': NAME, 'text-font': ITALIC, 'text-size': zoom_ramp(9, 11, 16, 14),
                      'text-max-width': 8, 'text-padding': 4},
              **text(c, 'label-park')),
        layer('landcover-label', 'symbol', 'landcover_name', minzoom=15,
              layout={'text-field': NAME, 'text-font': ITALIC, 'text-size': 11, 'text-max-width': 8,
                      'text-padding': 4},
              **text(c, 'label-park')),
        layer('peak', 'symbol', 'mountain_peak', minzoom=11,
              filter=['all', ['!=', get('class'), 'cliff'], ['has', 'name']],
              layout={'icon-image': 'peak', 'icon-size': zoom_ramp(11, 0.7, 15, 1),
                      'text-field': ['concat', NAME, '\n', ['to-string', get('ele')], ' m'],
                      'text-font': REGULAR, 'text-size': zoom_ramp(11, 10, 16, 12),
                      'text-anchor': 'top', 'text-offset': [0, 0.5], 'text-max-width': 8,
                      'text-optional': True},
              **text(c, 'label-natural')),
        layer('airport-label', 'symbol', 'aerodrome_label', minzoom=10,
              layout={'icon-image': 'airport', 'icon-size': 0.4,
                      'text-field': ['coalesce', get('iata'), NAME], 'text-font': BOLD, 'text-size': 12,
                      'text-anchor': 'top', 'text-offset': [0, 0.9], 'text-optional': True},
              **text(c, 'label-airport')),
        layer('path-label', 'symbol', 'transportation_name', minzoom=15,
              filter=['all', ['==', get('class'), 'path'], ['has', 'name']],
              layout={'symbol-placement': 'line', 'text-field': NAME, 'text-font': REGULAR,
                      'text-size': zoom_ramp(15, 10, 18, 12), 'text-max-angle': 30},
              **text(c, 'label-natural')),
    ]


def place(id, classes, minzoom, maxzoom, size, c, font=REGULAR, color='label', extra=None, filter=None):
    layout = {'text-field': NAME, 'text-font': font, 'text-size': size, 'text-max-width': 7}
    layout.update(extra or {})
    return layer(id, 'symbol', 'place', minzoom=minzoom, maxzoom=maxzoom,
                 filter=filter or in_class(classes), layout=layout, **text(c, color, halo_width=1.25))


def places(v):
    """settlements, last so they are placed first: a town's name outranks anything in it"""
    c = v.palette
    soft = {'text-transform': 'uppercase', 'text-letter-spacing': 0.1}
    dot = {'icon-image': ['match', get('capital'), 2, 'dot-capital', 'dot'],
           'text-variable-anchor': ['top', 'bottom', 'left', 'right'], 'text-radial-offset': 0.5,
           'text-justify': 'auto'}
    return [
        place('place-hamlet', ['hamlet', 'locality', 'isolated_dwelling', 'farm'], 13, 18,
              zoom_ramp(13, 10, 16, 13), c, color='label-soft'),
        place('place-neighbourhood', ['neighbourhood', 'quarter'], 13, 17,
              zoom_ramp(13, 10, 16, 13), c, color='label-soft', extra={**soft, 'text-letter-spacing': 0.05}),
        place('place-suburb', ['suburb'], 11, 15, zoom_ramp(11, 11, 15, 15), c, color='label-soft', extra=soft),
        place('place-island', ['island', 'islet'], 10, None, zoom_ramp(10, 11, 16, 15), c, font=ITALIC),
        place('place-village', ['village'], 10, 16, zoom_ramp(10, 10, 14, 14, 16, 16), c),
        place('place-town', ['town'], 8, 16, zoom_ramp(8, 11, 12, 16, 16, 20), c),
        place('place-town-dot', ['town'], 6, 8, zoom_ramp(6, 10, 8, 11), c, extra=dot),
        place('place-city', ['city'], 8, 15, zoom_ramp(8, 16, 12, 20, 15, 24), c),
        place('place-city-dot', ['city'], 3, 8, zoom_ramp(3, 11, 6, 14, 8, 16), c, extra=dot),
        place('place-state', ['state', 'province'], 4, 9, zoom_ramp(4, 9, 9, 16), c, font=BOLD,
              color='label-soft', extra={**soft, 'text-letter-spacing': 0.15, 'text-max-width': 6}),
        place('place-country', ['country'], 1, 10, zoom_ramp(1, 11, 5, 16, 9, 22), c,
              extra={'text-max-width': 6}),
    ]
