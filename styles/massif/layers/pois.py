"""POIs: the palette tables below are the source of truth for every POI colour - the disc, the
label (two measure-light stops, read off mapbox/standard) and which classes get no disc at all.
Each POI layer carries the same flat match on `class`, because the converter needs a match it can
key on per layer to fold it into one project.json table - see the style's README.
"""
import json
from collections import OrderedDict

from lib import by_hour, get, layer

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
                            'parking_garage', 'police', 'post', 'prison', 'town_hall', 'wilderness_hut'],
    'sport_and_leisure': ['american_football', 'baseball', 'basketball', 'cricket', 'golf',
                          'nightclub', 'pitch', 'skiing', 'soccer', 'stadium', 'swimming', 'tennis'],
    'park_like': ['beach', 'campsite', 'cemetery', 'dog_park', 'garden', 'mountain', 'park',
                  'playground', 'ranger_station', 'spring', 'viewpoint', 'volcano', 'water', 'waterfall',
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
NO_BACKGROUND = ['bench', 'drinking_water', 'garden', 'picnic_site', 'shelter', 'telephone',
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


def disc_match(furniture=None, keep_furniture=False):
    return flat_match(lambda cat: CATEGORY[cat]['disc'], CATEGORY['default']['disc'],
                      furniture, keep_furniture)


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


def night_color():
    return flat_match(lambda cat: CATEGORY[cat]['night'], CATEGORY['default']['night'])


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


def flat_match(value_of, default, furniture=None, keep_furniture=False):
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
        classes = CLASSES.get(cat, []) if keep_furniture else [
            c for c in CLASSES.get(cat, []) if c not in NO_BACKGROUND]
        if not classes:
            continue
        out.append(sorted(classes) if len(classes) > 1 else classes[0])
        out.append(value_of(cat))
    out.append(default)
    return out


# Least important first, so a station still wins a collision. Gated by CATEGORY the way Standard
# draws them early; the rank layers are the `poiRanking: rank` switch, Liberty's own ladder.
RANK_LAYERS = [
    ('poi-rank-r20', 17, ['all', ['>=', get('rank'), 20]]),
    ('poi-rank-r7', 16, ['all', ['>=', get('rank'), 7], ['<', get('rank'), 20]]),
    ('poi-rank-r1', 15, ['all', ['>=', get('rank'), 1], ['<', get('rank'), 7]]),
]
CATEGORY_LAYERS = [
    ('poi-waste', 18, ['drinking_water', 'toilets', 'waste_basket']),
    ('poi-shop', 17, ['alcohol_shop', 'bakery', 'beer', 'butcher', 'clothing_store', 'florist', 'furniture',
                      'gift', 'grocery', 'hairdresser', 'ice_cream', 'laundry', 'nightclub', 'shop', 'sushi',
                      'telephone']),
    ('poi-amenity', 17, ['bicycle', 'bicycle_rental', 'car', 'fuel', 'parking', 'parking_garage']),
    ('poi-bus', 16, ['bus']),
    ('poi-attraction', 16, ['amusement_park', 'aquarium', 'attraction']),
    ('poi-cultural', 16, ['art_gallery', 'castle', 'monument', 'museum', 'ruins']),
    ('poi-sport', 16, ['american_football', 'baseball', 'basketball', 'cricket', 'golf', 'pitch', 'skiing',
                       'soccer', 'stadium', 'swimming', 'tennis']),
    ('poi-outdoor', 16, ['beach', 'dog_park', 'garden', 'mountain', 'park', 'playground', 'ranger_station',
                         'viewpoint', 'volcano', 'water', 'waterfall', 'wetland', 'zoo']),
    ('poi-food', 16, ['bar', 'cafe', 'fast_food', 'restaurant']),
    ('poi-cemetery', 15, ['cemetery']),
    ('poi-lodging', 15, ['alpine_hut', 'campsite', 'lodging', 'picnic_site', 'shelter']),
    ('poi-public', 15, ['atm', 'bank', 'cinema', 'embassy', 'fire_station', 'information', 'library', 'music',
                        'police', 'post', 'prison', 'theatre', 'town_hall']),
    ('poi-worship', 15, ['place_of_worship']),
    ('poi-education', 15, ['college', 'school']),
    ('poi-health', 14, ['dentist', 'doctors', 'hospital', 'pharmacy', 'veterinary']),
    ('poi-transit', 13, ['aerialway', 'ferry', 'harbor', 'lighthouse', 'railway', 'railway_light', 'railway_metro']),
    ('poi-airport', 12, ['airfield', 'airport', 'heliport']),
]

ICON = ['match', get('subclass'), ['florist', 'furniture'], get('subclass'), get('class')]


# Standard's night POI: the disc takes the category's night colour, and the ring and the glyph go
# dark instead of staying white - a white ring glares on a dark map
NIGHT_INK = 'hsl(0, 0%, 12%)'


def per_class(value_of, default):
    """ONE flat match on class, grouping the classes that share a value: every branch a constant,
    which is the shape mapbox2css folds into a project.json table."""
    groups = {}
    for cls in sorted(set(CLASS_TO_CATEGORY) | set(NO_BACKGROUND)):
        value = value_of(cls, CLASS_TO_CATEGORY.get(cls, 'default'))
        if value != default:
            groups.setdefault(value, []).append(cls)
    out = ['match', get('class')]
    for value, classes in groups.items():
        out += [classes if len(classes) > 1 else classes[0], value]
    return out + [default]


def icon_params():
    # `transparent`, not `none`: the decoder's parseColor knows the CSS names and that one, and
    # throws on anything else - a bad colour kills the whole feature processor.
    bare = lambda cls: cls in NO_BACKGROUND
    disc = lambda key: per_class(lambda cls, cat: 'transparent' if bare(cls) else CATEGORY[cat][key],
                                 CATEGORY['default'][key])
    ring = lambda ink: per_class(lambda cls, cat: 'transparent' if bare(cls) else ink, ink)
    # a glyph with no disc under it is drawn in the category colour, not white on it
    glyph = lambda ink, key: per_class(lambda cls, cat: CATEGORY[cat][key] if bare(cls) else ink, ink)
    # poiStyle `plain`: OSM's look, every glyph bare in its category colour like the furniture
    plain = lambda badge, bare_value: ['match', ['config', 'poiStyle'], 'plain', bare_value, badge]
    tinted = lambda key: per_class(lambda cls, cat: CATEGORY[cat][key], CATEGORY['default'][key])
    return {'background': plain(by_hour(disc('night'), disc('disc')), 'transparent'),
            'background-stroke': plain(by_hour(ring(NIGHT_INK), ring('hsl(0, 0%, 100%)')), 'transparent'),
            'icon': plain(by_hour(glyph(NIGHT_INK, 'night'), glyph('hsl(0, 0%, 100%)', 'disc')),
                          by_hour(tinted('night'), tinted('disc'))),
            'radius': shape_match('radius'),
            'background-stroke-width': shape_match('border')}


MONO_INK = 'hsl(0, 0%, 0%)'


def mono_params():
    """e-ink: a black glyph on a white disc with a black ring, the same for every category"""
    bare = lambda cls: cls in NO_BACKGROUND
    return {'background': per_class(lambda cls, cat: 'transparent' if bare(cls) else 'hsl(0, 0%, 100%)',
                                    'hsl(0, 0%, 100%)'),
            'background-stroke': per_class(lambda cls, cat: 'transparent' if bare(cls) else MONO_INK, MONO_INK),
            'icon': MONO_INK,
            'radius': shape_match('radius'),
            'background-stroke-width': shape_match('border')}


NAME = ['coalesce', get('name'), get('name_int')]
# a bus shelter is named after its stop, which the stop's own POI already says; a hut's shelter is not
SHELTER_NAME = ['case', ['==', get('shelter_type'), 'public_transport'], '', NAME]


def poi_layer(id, minzoom, filter, ranking, v, icon=ICON, maxzoom=None, text=NAME):
    layout = {
        # The reference pane names the BAKED sprite, the SDK the neutral one it splits and
        # recolours: a sprite with the colour already in it has no plate mapbox2css can measure.
        'icon-image': icon if v.flags.get('mono') else ['concat', icon, '-poi'],
        'icon-size': 0.4,
        'text-field': text,
        # named, not dropped: without it maplibre falls back to a stack the glyph server lacks
        'text-font': 'medium',
        'text-size': 12,
        'text-max-width': 9,
        'text-padding': 2,
        'text-variable-anchor': ['top', 'left', 'right'],
        'text-radial-offset': 1.0,
        'text-justify': 'auto',
        'text-optional': True,
    }
    # on imagery the ground is dark by day as well, so the label keeps its night pair
    dark = v.flags.get('dark_ground', False)
    mono = v.flags.get('mono', False)
    massif_layout = {'icon-image': ['image', icon, {'params': mono_params() if mono else icon_params()}]}
    # a bare glyph fills the disc's box: at the badge's size it reads half OSM's 14 px icon
    massif_layout['icon-size'] = 0.4 if mono else ['match', ['config', 'poiStyle'], 'plain', 0.6, 0.4]
    if ranking == 'rank':
        # maplibre draws the default mode; the SDK turns these back on through massif:layout
        layout['visibility'] = 'none'
        massif_layout['visibility'] = 'visible'
    return layer(id, 'symbol', 'poi', minzoom=minzoom, maxzoom=maxzoom, filter=filter, layout=layout,
                 paint={'text-color': MONO_INK if mono else night_color() if dark else day_color(),
                        'text-halo-color': HALO_NIGHT if dark else HALO_DAY, 'text-halo-width': HALO_WIDTH},
                 metadata={'massif:params': ['icon-image', 'text-color'],
                           'massif:layout': massif_layout,
                           # maplibre rejects ["config", ...] in a filter, so the switch rides here
                           **({'massif:filter': ['==', ['config', 'poiRanking'], ranking]} if ranking else {}),
                           'massif:paint': {'text-color': MONO_INK if mono else night_color() if dark else text_color(),
                                            'text-halo-color': HALO_NIGHT if dark else by_hour(HALO_NIGHT, HALO_DAY)}})


# A walker's POIs, from the zoom a hike is planned at. Each stops where its category layer takes
# over; a bivouac and a spring have none, so they carry on.
# data-driven even for the hut layer: a constant icon-image is not one mapbox2css recolours
MOUNTAIN_ICON = ['match', get('class'), ['lodging', 'wilderness_hut'], 'alpine_hut', 'spring', 'water', get('class')]
MOUNTAIN_LAYERS = [
    ('poi-mountain-water', 14, 18, ['==', get('class'), 'drinking_water'], MOUNTAIN_ICON),
    ('poi-mountain-shelter', 13, 15, ['==', get('class'), 'shelter'], MOUNTAIN_ICON),
    ('poi-mountain', 12, None, ['in', get('class'), ['literal', ['spring', 'wilderness_hut']]], MOUNTAIN_ICON),
    # OpenMapTiles files a hut under lodging, whose glyph is a bed
    ('poi-mountain-hut', 12, 15, ['==', get('subclass'), 'alpine_hut'], MOUNTAIN_ICON),
]


def shelter_text(id):
    return SHELTER_NAME if id in ('poi-lodging', 'poi-mountain-shelter') else NAME


def mountain(v):
    return [poi_layer(id, minzoom, filter, None, v, icon=icon, maxzoom=maxzoom, text=shelter_text(id))
            for id, minzoom, maxzoom, filter, icon in MOUNTAIN_LAYERS]


def layers(v):
    return ([poi_layer(id, minzoom, filter, 'rank', v) for id, minzoom, filter in RANK_LAYERS] +
            [poi_layer(id, minzoom, ['in', get('class'), ['literal', classes]], 'category', v, text=shelter_text(id))
             for id, minzoom, classes in CATEGORY_LAYERS])


def write_sprite_palette(path):
    """The same table, for the sprite build to bake with.

    MapLibre cannot read the image params that colour the disc on the SDK, so the sprite carries it
    already drawn. Two consumers of one table, and this file is the one that is edited - the JSON is
    output, like style.json.
    """
    # Street furniture is in no category list, so walk both: those classes still need a glyph
    # colour, and without them the sheet would draw a bin with a disc under it.
    per_class = {}
    for cls in sorted(set(CLASS_TO_CATEGORY) | set(NO_BACKGROUND)):
        cat = CLASS_TO_CATEGORY.get(cls, 'default')
        shape = SHAPE.get(cls, DEFAULT_SHAPE)
        per_class[cls] = {
            'disc': None if cls in NO_BACKGROUND else CATEGORY[cat]['disc'],
            'glyph': CATEGORY[cat]['disc'] if cls in NO_BACKGROUND else 'hsl(0, 0%, 100%)',
            'radius': shape['radius'],
            'border': 0 if cls in NO_BACKGROUND else shape['border'],
        }
    default = {'disc': CATEGORY['default']['disc'], 'glyph': 'hsl(0, 0%, 100%)',
               'radius': DEFAULT_SHAPE['radius'], 'border': DEFAULT_SHAPE['border']}
    payload = {'comment': 'Generated by styles/massif/build.py from layers/pois.py - edit that, never this.',
               'ring': 'hsl(0, 0%, 100%)', 'default': default, 'classes': per_class}
    open(path, 'w').write(json.dumps(payload, indent=2) + '\n')

