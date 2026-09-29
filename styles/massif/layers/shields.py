from lib import gate, get, layer

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
# OpenMapTiles names the UK and Irish networks itself, so those two need no iso_a2.
PLATE = ['match', ['coalesce', get('network'), ''],
         ['gb-motorway', 'ie-motorway'], 'shield-plate-blue',
         ['gb-trunk', 'gb-primary', 'ie-national'], 'shield-plate-green',
         ['match', ['coalesce', get('iso_a2'), ''],
          'FR', first_letter(('A', 'red'), ('N', 'red'), ('D', 'yellow'), ('M', 'yellow')),
          'DE', first_letter(('A', 'blue'), ('B', 'yellow')),
          'GB', first_letter(('M', 'blue'), ('A', 'green')),
          'NL', first_letter(('A', 'red'), ('N', 'yellow')),
          'CH', by_class(('motorway', 'green'), ('trunk', 'blue')),
          'IT', first_letter(('A', 'green')),
          'ES', by_class(('motorway', 'blue'), ('trunk', 'blue')),
          'shield-plate-neutral']]

PLATE_TEXT = ['match', PLATE, ['shield-plate-yellow', 'shield-plate-neutral'], '#1b1d27', '#ffffff']


# Standard's size: a 9 px ref on a plate barely taller than it, so a shield leaves the road its room
def shield(id, minzoom, filter, image, text_color, spacing=400, text_size=9, padding=(0.5, 2.5, 0.5, 2.5), fit=True):
    lay = layer(id, 'symbol', 'transportation_name', minzoom=minzoom, filter=filter,
                 layout={'symbol-placement': 'line',
                         'symbol-spacing': spacing,
                         'symbol-avoid-edges': True,
                         'icon-image': image,
                         'icon-text-fit': 'both',
                         'icon-text-fit-padding': list(padding),
                         'text-field': get('ref'),
                         'text-font': 'bold',
                         'text-size': text_size,
                         'text-letter-spacing': 0.05,
                         'text-rotation-alignment': 'viewport',
                         'icon-rotation-alignment': 'viewport',
                         'text-padding': 2},
                 paint={'text-color': text_color})
    if not fit:
        del lay['layout']['icon-text-fit'], lay['layout']['icon-text-fit-padding']
    return lay


def plate(id, minzoom, class_test, mono):
    filter = ['all', *BASE_FILTER, ['!', ['in', get('network'), ['literal', US]]], ['!=', get('network'), 'e-road'],
              class_test]
    if mono:
        return shield(id + '-mono', minzoom, filter, 'shield-plate-mono', '#000000')
    return shield(id, minzoom, filter, PLATE, PLATE_TEXT)


def layers(v):
    mono = v.flags.get('mono', False)

    def us(network, text_color):
        # a sprite per ref length, as Standard has: the shape is the sign, and stretched it is not.
        # A tileset without ref_length (ours) draws the wide one.
        base = ['all', *BASE_FILTER, ['==', get('network'), network]]
        if mono:
            return [shield('road-shield-' + network + '-mono', 7, base, 'shield-plate-mono', '#000000')]
        tests = (('', ['>', get('ref_length'), 2], '3'), ('-short', ['<=', get('ref_length'), 2], '2'),
                 ('-any', ['!', ['has', 'ref_length']], '3'))
        return [shield('road-shield-' + network + suffix, 7, base + [test], 'shield-' + network + '-' + n,
                       text_color, fit=False) for suffix, test, n in tests]
    exit = shield('road-exit-shield' + ('-mono' if mono else ''), 14, ['all', ['has', 'ref'], ['==', get('subclass'), 'junction']],
                  'shield-plate-mono' if mono else 'shield-exit', '#000000' if mono else '#ffffff',
                  spacing=250, text_size=8, padding=(0, 1, 0, 1))
    # below the national plates: a ramp in the E-road's relation carries its ref and would take the
    # place of the motorway's own A 480 beside it
    e_road = shield('road-shield-e-road' + ('-mono' if mono else ''), 9,
                    ['all', *BASE_FILTER, ['==', get('network'), 'e-road']],
                    'shield-plate-mono' if mono else 'shield-plate-green', '#000000' if mono else '#ffffff')
    # One layer per zoom band, each excluding what an earlier band drew: minzoom is decided per
    # tile, an `any` of zoom-and-class branches would be a when() every feature pays. Least
    # important first, so the motorway's plate is placed first and an exit number takes what is left.
    return [gate(lay, v, 'road_shields') for lay in [
        exit,
        e_road,
        plate('road-shield-plate', 13, ['!', ['in', get('class'), ['literal', ['motorway', 'trunk', 'primary']]]], mono),
        plate('road-shield-plate-primary', 11, ['==', get('class'), 'primary'], mono),
        *us('us-highway', '#1b1d27'),
        *us('us-interstate', '#ffffff'),
        plate('road-shield-plate-major', 9, ['in', get('class'), ['literal', ['motorway', 'trunk']]], mono),
    ]]
