from lib import get, layer

US = ['us-interstate', 'us-highway']
BASE_FILTER = [['!=', get('subclass'), 'junction'], ['!=', get('class'), 'path'], ['has', 'ref']]


def first_letter(*pairs):
    """match on the ref's first letter: ('A', 'red'), ... -> shield-plate-<colour>."""
    out = ['match', ['upcase', ['slice', ['coalesce', get('ref'), ''], 0, 1]]]
    for letter, colour in pairs:
        out += [letter, 'shield-plate-' + colour]
    return out + ['shield-plate-neutral']


def by_class(*pairs):
    out = ['match', get('class')]
    for cls, colour in pairs:
        out += [cls, 'shield-plate-' + colour]
    return out + ['shield-plate-neutral']


# The plate colour per country. iso_a2 is read through a coalesce, so a tileset without it takes
# no country branch and every ref lands on the neutral plate.
PLATE = ['case', ['==', get('network'), 'e-road'], 'shield-plate-green',
         ['match', ['coalesce', get('iso_a2'), ''],
          'FR', first_letter(('A', 'red'), ('N', 'red'), ('D', 'yellow'), ('M', 'yellow')),
          'DE', first_letter(('A', 'blue'), ('B', 'yellow')),
          'GB', first_letter(('M', 'blue'), ('A', 'green')),
          'NL', first_letter(('A', 'red'), ('N', 'yellow')),
          'CH', by_class(('motorway', 'green'), ('trunk', 'blue')),
          'IT', first_letter(('A', 'green')),
          'ES', by_class(('motorway', 'blue'), ('trunk', 'blue')),
          'shield-plate-neutral']]

PLATE_TEXT = ['match', PLATE, ['shield-plate-yellow', 'shield-plate-neutral'], '#2b2400', '#ffffff']


def shield(id, minzoom, filter, image, text_color, spacing=350, text_size=10, padding=(1, 3, 1, 3)):
    return layer(id, 'symbol', 'transportation_name', minzoom=minzoom, filter=filter,
                 layout={'symbol-placement': 'line',
                         'symbol-spacing': spacing,
                         'symbol-avoid-edges': True,
                         'icon-image': image,
                         'icon-text-fit': 'both',
                         'icon-text-fit-padding': list(padding),
                         'text-field': get('ref'),
                         'text-font': ['Noto Sans Bold'],
                         'text-size': text_size,
                         'text-rotation-alignment': 'viewport',
                         'icon-rotation-alignment': 'viewport',
                         'text-padding': 2},
                 paint={'text-color': text_color})


def plate(id, minzoom, class_test):
    filter = ['all', *BASE_FILTER, ['!', ['in', get('network'), ['literal', US]]], class_test]
    return shield(id, minzoom, filter, PLATE, PLATE_TEXT)


def layers(v):
    def us(network, text_color):
        return shield('road-shield-' + network, 7, ['all', *BASE_FILTER, ['==', get('network'), network]],
                      'shield-' + network, text_color, padding=(0, 1, 0, 1))
    exit = shield('road-exit-shield', 15, ['all', ['has', 'ref'], ['==', get('subclass'), 'junction']],
                  'shield-plate-neutral', '#4a463d', spacing=250, text_size=9)
    # One layer per zoom band, each excluding what an earlier band drew: minzoom is decided per
    # tile, an `any` of zoom-and-class branches would be a when() every feature pays.
    return [
        us('us-interstate', '#ffffff'),
        us('us-highway', '#1a1a1a'),
        plate('road-shield-plate-major', 9, ['in', get('class'), ['literal', ['motorway', 'trunk']]]),
        plate('road-shield-plate-primary', 11, ['==', get('class'), 'primary']),
        plate('road-shield-plate', 13, ['!', ['in', get('class'), ['literal', ['motorway', 'trunk', 'primary']]]]),
        exit,
    ]
