"""Write the POI palette into style.json - run from this directory, `python3 poi-palette.py`.

The tables below are the source of truth for every POI colour: the disc, the label (two
measure-light stops, read off mapbox/standard) and which classes get no disc at all. style.json
carries the result twenty times over, once per POI layer, because the converter needs a match it
can key on per layer - see the README. Edit here, never there.
"""
import json
from collections import OrderedDict

STYLE = 'style.json'

# Standard's poi-label text-color, read off mapbox/standard: night (brightness 0.25) and day (0.3).
CATEGORY = OrderedDict([
    ('food_and_drink', {'disc': 'hsl(30, 100%, 48%)', 'night': 'hsl(40, 95%, 70%)', 'day': 'hsl(30, 100%, 48%)'}),
    ('store_like', {'disc': 'hsl(210, 75%, 53%)', 'night': 'hsl(210, 70%, 75%)', 'day': 'hsl(210, 75%, 53%)'}),
    ('arts_and_entertainment', {'disc': 'hsl(320, 85%, 60%)', 'night': 'hsl(320, 70%, 75%)', 'day': 'hsl(320, 85%, 60%)'}),
    ('commercial_services', {'disc': 'hsl(250, 75%, 60%)', 'night': 'hsl(260, 70%, 75%)', 'day': 'hsl(250, 75%, 60%)'}),
    ('sport_and_leisure', {'disc': 'hsl(190, 75%, 38%)', 'night': 'hsl(190, 60%, 70%)', 'day': 'hsl(190, 75%, 38%)'}),
    ('park_like', {'disc': 'hsl(110, 70%, 28%)', 'night': 'hsl(110, 55%, 65%)', 'day': 'hsl(110, 70%, 28%)'}),
    ('medical', {'disc': 'hsl(0, 90%, 60%)', 'night': 'hsl(0, 70%, 70%)', 'day': 'hsl(0, 90%, 60%)'}),
    ('education', {'disc': 'hsl(30, 50%, 38%)', 'night': 'hsl(30, 50%, 70%)', 'day': 'hsl(30, 50%, 38%)'}),
    # Standard draws transit in its own layer and its own blue; this style keeps that.
    ('transit', {'disc': 'hsl(225, 60%, 58%)', 'night': 'hsl(225, 55%, 78%)', 'day': 'hsl(225, 60%, 48%)'}),
    ('default', {'disc': 'hsl(210, 20%, 43%)', 'night': 'hsl(210, 20%, 70%)', 'day': 'hsl(210, 20%, 43%)'}),
])

CLASSES = {
    'food_and_drink': ['bar', 'cafe', 'fast_food', 'restaurant', 'ice_cream', 'sushi'],
    'store_like': ['alcohol_shop', 'bakery', 'beer', 'butcher', 'clothing_store', 'florist',
                   'furniture', 'gift', 'grocery', 'hairdresser', 'laundry', 'shop'],
    'arts_and_entertainment': ['amusement_park', 'aquarium', 'art_gallery', 'attraction', 'castle',
                               'cinema', 'monument', 'museum', 'music', 'ruins', 'theatre', 'zoo'],
    'commercial_services': ['alpine_hut', 'atm', 'bank', 'bicycle', 'bicycle_rental', 'car',
                            'embassy', 'fire_station', 'fuel', 'lodging', 'parking',
                            'parking_garage', 'police', 'post', 'prison', 'town_hall'],
    'sport_and_leisure': ['american_football', 'baseball', 'basketball', 'cricket', 'golf',
                          'nightclub', 'pitch', 'skiing', 'soccer', 'stadium', 'swimming', 'tennis'],
    'park_like': ['beach', 'campsite', 'cemetery', 'dog_park', 'garden', 'mountain', 'park',
                  'playground', 'ranger_station', 'viewpoint', 'volcano', 'water', 'waterfall',
                  'wetland'],
    'medical': ['dentist', 'doctors', 'hospital', 'pharmacy', 'veterinary'],
    'education': ['college', 'library', 'school'],
    # Standard has no religion category and neither does Liberty; both leave it the neutral grey.
    'default': ['place_of_worship'],
    'transit': ['aerialway', 'airfield', 'airport', 'bus', 'ferry', 'harbor', 'heliport',
                'lighthouse', 'railway', 'railway_light', 'railway_metro'],
}

# Street furniture, not a place: the glyph stands on the map with no disc under it, which is what
# Standard's backgroundPointOfInterestLabels=none does for the whole map. Matched on class AND
# subclass, because OpenMapTiles carries a bench or a tree as a subclass of something coarser.
NO_BACKGROUND = ['bench', 'drinking_water', 'picnic_site', 'shelter', 'telephone',
                 'toilets', 'tree', 'waste_basket']

# A subclass that belongs to another category than its class. Liberty reads `subclass` for the ICON
# only (florist, furniture) and colours nothing by it, so there is no MapTiler palette to copy here -
# this table exists for the cases where OpenMapTiles' class is too coarse to carry the colour.
SUBCLASS = {
    'food_and_drink': ['biergarten', 'deli', 'food_court', 'pub'],
    'store_like': ['convenience', 'greengrocer', 'supermarket'],
    'park_like': ['allotments', 'nature_reserve', 'picnic_table'],
    'arts_and_entertainment': ['artwork', 'gallery', 'theme_park'],
}

# Standard's poi-label halo: near-black at night, white by day, width 1 and no blur.
HALO_NIGHT = 'hsl(0, 0%, 5%)'
HALO_DAY = 'hsl(0, 0%, 100%)'
HALO_WIDTH = 1.25

# The plate's corner radius, and the ring around it. The ring used to be measured off the artwork -
# one sheet of identical discs, so every shape got the same 3 - and a badge with square corners
# carries far more ring than a circle at the same width. Stated per shape instead.
SHAPE = {
    'railway': {'radius': 5, 'border': 1.5},
    'railway_light': {'radius': 11, 'border': 2},
    'airfield': {'radius': 8, 'border': 1.75},
    'airport': {'radius': 8, 'border': 1.75},
    'heliport': {'radius': 8, 'border': 1.75},
}
DEFAULT_SHAPE = {'radius': 21, 'border': 3}

CLASS_TO_CATEGORY = {c: cat for cat, cs in CLASSES.items() for c in cs}


def match_on_class(pairs, default):
    """['match', ['get','class'], [classes], value, ..., default] with the branches sorted."""
    out = ['match', ['get', 'class']]
    for classes, value in pairs:
        out.append(sorted(classes) if len(classes) > 1 else classes[0])
        out.append(value)
    out.append(default)
    return out


def stops(cat):
    """Standard's own ramp: two measure-light stops, night at 0.25 and day at 0.3."""
    return ['interpolate', ['linear'], ['measure-light', 'brightness'],
            0.25, CATEGORY[cat]['night'], 0.3, CATEGORY[cat]['day']]


def disc_match(furniture=None):
    return flat_match(lambda cat: CATEGORY[cat]['disc'], CATEGORY['default']['disc'], furniture)


def shape_match(key):
    """One flat match on class again, so both fold into a project.json table."""
    out = ['match', ['get', 'class']]
    for cls in sorted(SHAPE):
        out.append(cls)
        out.append(SHAPE[cls][key])
    out.append(DEFAULT_SHAPE[key])
    return out


def day_color():
    """The same table, at day brightness: a literal colour maplibre can parse."""
    return flat_match(lambda cat: CATEGORY[cat]['day'], CATEGORY['default']['day'])


def text_color():
    """Per category, and following the hour.

    The ramp is OUTSIDE and the class table inside, not the other way round: a stop whose value is a
    literal colour folds into a project.json table, while a match whose branches are ramps cannot,
    and becomes a per-feature ternary chain. Same picture, one lookup instead of ten comparisons.
    """
    def at(key):
        return flat_match(lambda cat: CATEGORY[cat][key], CATEGORY['default'][key])
    return ['interpolate', ['linear'], ['measure-light', 'brightness'],
            0.25, at('night'), 0.3, at('day')]


def flat_match(value_of, default, furniture=None):
    """ONE match on `class`, which is the shape mapbox2css folds into a project.json table.

    Nesting a second match inside it - a subclass override, say - defeats the fold, and the rule
    becomes a fifteen-deep per-feature ternary instead of a lookup. Street furniture is therefore a
    branch of this same match rather than a wrapper around it.
    """
    out = ['match', ['get', 'class']]
    if furniture is not None:
        out.append(sorted(NO_BACKGROUND))
        out.append(furniture)
    for cat in CATEGORY:
        classes = [c for c in CLASSES.get(cat, []) if c not in NO_BACKGROUND]
        if not classes:
            continue
        out.append(sorted(classes) if len(classes) > 1 else classes[0])
        out.append(value_of(cat))
    out.append(default)
    return out


def main():
    style = json.load(open(STYLE))
    touched = 0
    for lay in style['layers']:
        if not lay['id'].startswith('poi-'):
            continue
        layout = lay.setdefault('layout', {})
        paint = lay.setdefault('paint', {})

        # 1. No italic - Standard's poi-label takes the map's own face. NAMED, not dropped:
        # without text-font maplibre falls back to its own default stack, which the style's glyph
        # server does not carry, and the 404 stalls every symbol layer on the reference pane.
        layout['text-font'] = ['Noto Sans Regular']

        # 2. The label follows the icon's category, and the hour. maplibre rejects
        # ["measure-light", …] outright - it is GL v3, like ["image", …, {params}] - so the ramp
        # rides in metadata and the DAY colour stays behind as what the reference pane draws.
        paint['text-color'] = day_color()
        extra = lay.setdefault('metadata', {}).setdefault('massif:paint', {})
        extra['text-color'] = text_color()

        # The halo follows the hour too, or a white ring survives into the night around a label that
        # has gone pale. Standard's own two colours; its WIDTH is 1 and this is the one number here
        # deliberately off it - a hair more separation over a busy roof.
        paint['text-halo-color'] = HALO_DAY
        paint['text-halo-width'] = HALO_WIDTH
        extra['text-halo-color'] = ['interpolate', ['linear'], ['measure-light', 'brightness'],
                                    0.25, HALO_NIGHT, 0.3, HALO_DAY]

        # 3. The disc colour, and the classes that get none.
        img = lay.get('metadata', {}).get('massif:layout', {}).get('icon-image')
        if isinstance(img, list) and len(img) > 2 and isinstance(img[2], dict):
            params = img[2].setdefault('params', {})
            # `transparent`, not `none`: the decoder's parseColor knows the CSS names and that one,
            # and throws on anything else - a bad colour kills the whole feature processor.
            params['background'] = disc_match(furniture='transparent')
            params['background-stroke'] = ['match', ['get', 'class'], sorted(NO_BACKGROUND),
                                           'transparent', 'hsl(0, 0%, 100%)']
            # A glyph with no disc under it is drawn in the category colour, not white on it.
            params['icon'] = flat_match(lambda cat: 'hsl(0, 0%, 100%)', 'hsl(0, 0%, 100%)',
                                        furniture=disc_match())
            params['radius'] = shape_match('radius')
            params['background-stroke-width'] = shape_match('border')
        touched += 1

    out = json.dumps(style, indent=2, ensure_ascii=False)
    open(STYLE, 'w').write(out)  # the file carries no trailing newline
    print('rewrote', touched, 'poi layers')
    print('classes mapped:', len(CLASS_TO_CATEGORY), '| no background:', len(NO_BACKGROUND))


if __name__ == '__main__':
    main()
