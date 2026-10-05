"""POIs: the palette tables below are the source of truth for every POI colour - the disc, the
label (two measure-light stops, read off mapbox/standard) and which classes get no disc at all.
Each POI layer carries the same flat match on `class`, because the converter needs a match it can
key on per layer to fold it into one project.json table - see the style's README.
"""
import json
import os
from collections import OrderedDict

from lib import boosted, by_hour, gate, get, layer, scaled, zoom_ramp

# Standard's poi-label text-color, read off mapbox/standard: night (brightness 0.25) and day (0.3).
# disc: Standard's day disc, a shade lighter than its text (day); night: both at night
CATEGORY = OrderedDict([
    ('food_and_drink', {'disc': 'hsl(30, 100%, 60%)', 'night': 'hsl(40, 95%, 70%)', 'day': 'hsl(30, 100%, 48%)'}),
    ('store_like', {'disc': 'hsl(210, 75%, 65%)', 'night': 'hsl(210, 70%, 75%)', 'day': 'hsl(210, 75%, 53%)'}),
    ('arts_and_entertainment', {'disc': 'hsl(320, 85%, 72%)', 'night': 'hsl(320, 70%, 75%)', 'day': 'hsl(320, 85%, 60%)'}),
    ('commercial_services', {'disc': 'hsl(250, 75%, 72%)', 'night': 'hsl(260, 70%, 75%)', 'day': 'hsl(250, 75%, 60%)'}),
    ('sport_and_leisure', {'disc': 'hsl(190, 75%, 50%)', 'night': 'hsl(190, 60%, 70%)', 'day': 'hsl(190, 75%, 38%)'}),
    ('park_like', {'disc': 'hsl(110, 70%, 40%)', 'night': 'hsl(110, 55%, 65%)', 'day': 'hsl(110, 70%, 28%)'}),
    ('medical', {'disc': 'hsl(0, 94%, 72%)', 'night': 'hsl(0, 70%, 70%)', 'day': 'hsl(0, 90%, 60%)'}),
    ('education', {'disc': 'hsl(30, 50%, 50%)', 'night': 'hsl(30, 50%, 70%)', 'day': 'hsl(30, 50%, 38%)'}),
    # ours: drinking water and springs in the water's own blue, so a walker reads "water" at a glance
    ('water', {'disc': 'hsl(200, 85%, 45%)', 'night': 'hsl(200, 80%, 72%)', 'day': 'hsl(200, 85%, 40%)'}),
    # Standard draws transit in its own layer and its own blue; this style keeps that.
    ('transit', {'disc': 'hsl(225, 60%, 58%)', 'night': 'hsl(225, 55%, 78%)', 'day': 'hsl(225, 60%, 48%)'}),
    # ours: the landcover barrier line's grey-brown, darkened to read as a glyph; its night is the line
    ('barrier', {'disc': 'hsl(20, 12%, 45%)', 'night': 'hsl(20, 10%, 70%)', 'day': 'hsl(20, 12%, 38%)'}),
    # ours: a landmark is a reference on the ground, not a place - a dark neutral, its night a light one
    ('landmark', {'disc': 'hsl(30, 8%, 35%)', 'night': 'hsl(30, 8%, 75%)', 'day': 'hsl(30, 8%, 30%)'}),
    ('default', {'disc': 'hsl(200, 20%, 55%)', 'night': 'hsl(210, 20%, 70%)', 'day': 'hsl(210, 20%, 43%)'}),
])

CLASSES = {
    # food shops with the food, as Standard's food_and_drink_stores share its orange
    'food_and_drink': ['alcohol_shop', 'bakery', 'bar', 'beer', 'butcher', 'cafe', 'fast_food', 'grocery',
                       'ice_cream', 'restaurant', 'sushi'],
    'store_like': ['clothing_store', 'florist', 'furniture', 'gift', 'hairdresser', 'laundry', 'shop'],
    'arts_and_entertainment': ['amusement_park', 'aquarium', 'archaeological_site', 'art_gallery', 'attraction',
                               'castle', 'cinema', 'fort', 'fountain', 'monument', 'museum', 'music', 'ruins',
                               'theatre', 'windmill', 'zoo'],
    'commercial_services': ['alpine_hut', 'atm', 'bank', 'bicycle', 'bicycle_rental', 'car',
                            'embassy', 'fire_station', 'fuel', 'lodging', 'parking',
                            'parking_garage', 'police', 'post', 'prison', 'town_hall', 'wilderness_hut'],
    'sport_and_leisure': ['american_football', 'baseball', 'basketball', 'cricket', 'golf',
                          'nightclub', 'pitch', 'skiing', 'soccer', 'stadium', 'swimming', 'tennis'],
    # adit, cave_entrance, fort...: Alpimaps' planetiler keeps OSM's value where OpenMapTiles has no class
    'park_like': ['adit', 'beach', 'bird_hide', 'campsite', 'cave_entrance', 'cemetery', 'dog_park', 'garden',
                  'mountain', 'park', 'playground', 'ranger_station', 'tree', 'viewpoint', 'volcano', 'wetland'],
    'water': ['drinking_water', 'spring', 'water', 'water_point', 'watering_place', 'waterfall'],
    'medical': ['dentist', 'doctors', 'hospital', 'pharmacy', 'veterinary'],
    'education': ['college', 'library', 'school'],
    # Standard has no religion category and neither does Liberty; both leave it the neutral grey.
    'default': ['place_of_worship'],
    'transit': ['aerialway', 'airfield', 'airport', 'bus', 'ferry', 'harbor', 'heliport',
                'lighthouse', 'railway', 'railway_light', 'railway_metro'],
    # every point barrier Alpimaps' planetiler emits (Poi.MULTIPOINT_CLASSES), and the shared glyph
    'barrier': ['barrier', 'bollard', 'border_control', 'cycle_barrier', 'gate', 'lift_gate', 'sally_port',
                'stile', 'toll_booth'],
    # Alpimaps' planetiler `poi_landmarks` and `poi_guideposts`, packed as the trees are
    'landmark': ['cairn', 'cross', 'guidepost', 'mast', 'power_tower', 'pylon', 'rock', 'stone', 'wayside_cross',
                 'wayside_shrine', 'wind_turbine'],
}

# Street furniture, not a place: the glyph stands on the map with no disc under it, which is what
# Standard's backgroundPointOfInterestLabels=none does for the whole map. Matched on class AND
# subclass, because OpenMapTiles carries a bench or a tree as a subclass of something coarser.
NO_BACKGROUND = ['bench', 'drinking_water', 'garden', 'picnic_site', 'shelter', 'telephone',
                 'toilets', 'tree', 'viewpoint', 'waste_basket', *CLASSES['barrier'], *CLASSES['landmark']]

# Every sprite in sprite-src/poi is a glyph a POI can name: by subclass, else by class, else `default`.
# A class with a drawing (or an alias) is KNOWN; any other draws `default`, bare, from UNKNOWN_ZOOM.
ICONS = sorted(f[:-4] for f in os.listdir(os.path.join(os.path.dirname(__file__), '..', 'sprite-src', 'poi'))
               if f.endswith('.svg'))
DEFAULT_ICON = 'default'
UNKNOWN_ZOOM = 17
# a class drawn with another's glyph
ALIAS = {'border_control': 'barrier', 'sally_port': 'barrier', 'spring': 'water'}
KNOWN = sorted((set(ICONS) | set(ALIAS)) - {DEFAULT_ICON})

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


def day_color(fixed=None):
    """The same table, at day brightness: a literal colour maplibre can parse."""
    return flat_match(lambda cat: CATEGORY[cat]['day'], CATEGORY['default']['day'], fixed=fixed)


def night_color(fixed=None):
    return flat_match(lambda cat: CATEGORY[cat]['night'], CATEGORY['default']['night'], fixed=fixed)


def text_color(fixed=None):
    """Per category, and following the hour.

    The ramp is OUTSIDE and the class table inside, not the other way round: a stop whose value is a
    literal colour folds into a project.json table, while a match whose branches are ramps cannot,
    and becomes a per-feature ternary chain. Same picture, one lookup instead of ten comparisons.
    """
    def at(key):
        return flat_match(lambda cat: CATEGORY[cat][key], CATEGORY['default'][key], fixed=fixed)
    return ['interpolate', ['linear'], ['measure-light', 'brightness'],
            0.25, at('night'), 0.3, at('day')]


def flat_match(value_of, default, furniture=None, keep_furniture=False, fixed=None):
    """ONE match on `class`, which is the shape mapbox2css folds into a project.json table.

    Nesting a second match inside it - a subclass override, say - defeats the fold, and the rule
    becomes a fifteen-deep per-feature ternary instead of a lookup. Street furniture is therefore a
    branch of this same match rather than a wrapper around it.
    """
    if fixed:
        return value_of(fixed)
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
    return chained(lambda bare, cat: value_of(cat), out)


# The data decides when a POI appears: `rank`, ordinal in its z14 tile (1-390 over Grenoble), on
# Alpimaps' OSM ladder: 10 from z14, 30 from 15 (named from 16), 70 from 16, all from 17; eating,
# parking and schools a band later, shops two. Least important first: a lower rank wins a collision.
RANK = get('rank')
LATE = ['bar', 'college', 'parking', 'restaurant', 'school']
RANK_LADDER = [
    ('poi-rank-all', 17, ['>', RANK, 70], 'poi_rank_all_minzoom'),
    ('poi-rank-r70', 16, ['all', ['>', RANK, 30], ['<=', RANK, 70]], 'poi_rank70_minzoom'),
    ('poi-rank-r30-shop', 16, ['all', ['>', RANK, 10], ['<=', RANK, 30], ['==', get('class'), 'shop']], 'poi_rank70_minzoom'),
]
RANK30 = ['all', ['>', RANK, 10], ['<=', RANK, 30], ['!=', get('class'), 'shop']]
RANK10 = [
    ('poi-rank-r10-late', 15, ['all', ['<=', RANK, 10], ['in', get('class'), ['literal', LATE]]], 'poi_rank30_minzoom'),
    ('poi-rank-r10', 14, ['all', ['<=', RANK, 10], *[['!=', get('class'), c] for c in LATE]], 'poi_rank10_minzoom'),
]
# The exceptions, each its own layer: stations and airports before the ladder starts, as Standard
# draws them; the bus stop after it, as Standard does - icon at 17, name at 18.
STATION_LAYERS = [
    ('poi-transit', 13, ['aerialway', 'ferry', 'harbor', 'lighthouse', 'railway', 'railway_light', 'railway_metro']),
    ('poi-airport', 12, ['airfield', 'airport', 'heliport']),
]
BUS = ('bus', 17, 18)
# Mapbox's tiles carry a tram stop from z16, and Standard's transit-label names it as soon as it shows
TRAM = (16, 16)
TRAM_STOP = ['all', ['==', get('class'), 'railway'], ['==', get('subclass'), 'tram_stop']]
# a bus shelter is a bus stop's furniture: icon only, as late as the stop in a city, from z15 on a walker's map
PT_SHELTER = ['all', ['==', get('class'), 'shelter'], ['==', get('shelter_type'), 'public_transport']]
# the shelter_type values Alpimaps' tiles carry for a walker's shelter: named, from z13. Any other (none,
# sun_shelter, building...) is an icon from z17, as a public-transport one in a city
OUTDOOR_SHELTERS = ['basic_hut', 'lean_to', 'picnic_shelter', 'rock_shelter', 'weather_shelter', 'wilderness_hut']
HUT_SHELTERS = [t for t in OUTDOOR_SHELTERS if t != 'picnic_shelter']
# (category, bare) looked up before the class, so a child rule extending %poi draws a viewpoint (an
# attraction to OpenMapTiles) or a walker's shelter as Massif's own layers do
SUBCLASS_LOOK = {**{sub: (cat, False) for cat, subs in SUBCLASS.items() for sub in subs},
                 'kindergarten': ('park_like', False), 'viewpoint': ('park_like', True)}
SHELTER_LOOK = {t: ('park_like', False) for t in OUTDOOR_SHELTERS}
OTHER_SHELTER = ['all', ['==', get('class'), 'shelter'], ['!=', get('shelter_type'), 'public_transport'],
                 *[['!=', get('shelter_type'), t] for t in OUTDOOR_SHELTERS]]
# Drawn by a layer of their own at every zoom, so the ladder leaves them out. Excluded rather than
# listed: a class list is a when() per feature, an exclusion a few prunable selectors.
OWN_LAYER = sorted({'bus', 'campsite', 'drinking_water', 'shelter', 'spring', 'tree', 'wilderness_hut'} |
                   {c for _, _, cs in STATION_LAYERS for c in cs} | set(CLASSES['barrier']) - {'barrier'} |
                   set(CLASSES['landmark']))


def class_icon(alias=ALIAS):
    """the glyph a class names: its own, or the one it borrows"""
    by_icon = {}
    for cls, icon in sorted(alias.items()):
        by_icon.setdefault(icon, []).append(cls)
    out = ['match', get('class')]
    for icon, classes in by_icon.items():
        out += [classes if len(classes) > 1 else classes[0], icon]
    return out + [get('class')]


ICON = class_icon()


def icon_chain(cls, image, fallback=True):
    """subclass, else class, else DEFAULT_ICON: `coalesce` falls through a missing image on MapLibre,
    `??` through a missing parameter on the SDK"""
    return ['coalesce', image(get('subclass')), image(cls), *([image(DEFAULT_ICON)] if fallback else [])]


# MapLibre's test and the SDK's: the SDK reads the converter's `glyph` table, so a sprite or font
# glyph added under a class's name makes the class known with no other edit
KNOWN_MAPLIBRE = ['any', ['in', get('class'), ['literal', KNOWN]], ['in', get('subclass'), ['literal', KNOWN]]]
KNOWN_SDK = ['!=', ['coalesce', ['get', get('subclass'), ['config', 'glyph']],
                    ['get', get('class'), ['config', 'glyph']], ''], '']


def until_known(early, late, minzoom, maxzoom):
    """`early` below UNKNOWN_ZOOM, `late` from it"""
    if minzoom >= UNKNOWN_ZOOM:
        return late
    if maxzoom is not None and maxzoom <= UNKNOWN_ZOOM:
        return early
    return ['step', ['zoom'], early, UNKNOWN_ZOOM, late]


# Standard's night POI: the disc takes the category's night colour, and the ring and the glyph go
# dark instead of staying white - a white ring glares on a dark map
NIGHT_INK = 'hsl(0, 0%, 12%)'


def chained(value_of, inner):
    """`inner` (the class table) behind the subclass and shelter_type ones: mapbox2css folds each match
    into a project.json table and chains the lookups with `??`"""
    out = inner
    for field, look in (('shelter_type', SHELTER_LOOK), ('subclass', SUBCLASS_LOOK)):
        groups = {}
        for label, (cat, bare) in sorted(look.items()):
            groups.setdefault(json.dumps(value_of(bare, cat)), []).append(label)
        table = ['match', get(field)]
        for value, labels in groups.items():
            table += [labels if len(labels) > 1 else labels[0], json.loads(value)]
        out = table + [out]
    return out


def is_bare(cls):
    """no disc: street furniture, landmarks, and a class with no glyph of its own (drawn as `default`)"""
    return cls in NO_BACKGROUND or cls not in KNOWN


def per_class(value_of, fixed=None, fixed_bare=False):
    """`value_of(bare, category)` per class, grouping the classes that share a value, behind the subclass
    and shelter_type tables (chained). `fixed`: one category's value."""
    if fixed:
        return value_of(fixed_bare, fixed)
    default = value_of(True, 'default')
    groups = {}
    for cls in sorted(set(CLASS_TO_CATEGORY) | set(NO_BACKGROUND) | set(KNOWN)):
        value = value_of(is_bare(cls), CLASS_TO_CATEGORY.get(cls, 'default'))
        if value != default:
            groups.setdefault(value, []).append(cls)
    out = ['match', get('class')]
    for value, classes in groups.items():
        out += [classes if len(classes) > 1 else classes[0], value]
    return chained(value_of, out + [default])


def icon_params(fixed=None, fixed_bare=False):
    # `transparent`, not `none`: the decoder's parseColor knows the CSS names and that one, and
    # throws on anything else - a bad colour kills the whole feature processor.
    table = lambda value_of: per_class(value_of, fixed, fixed_bare)
    disc = lambda key: table(lambda bare, cat: 'transparent' if bare else CATEGORY[cat][key])
    ring = lambda ink: table(lambda bare, cat: 'transparent' if bare else ink)
    # a glyph with no disc under it is drawn in the category colour, not white on it
    glyph = lambda ink, key: table(lambda bare, cat: CATEGORY[cat][key] if bare else ink)
    # poiStyle `plain`: OSM's look, every glyph bare in its category colour like the furniture
    plain = lambda badge, bare_value: ['match', ['config', 'poiStyle'], 'plain', bare_value, badge]
    tinted = lambda key: table(lambda bare, cat: CATEGORY[cat][key])
    return {'background': plain(by_hour(disc('night'), disc('disc')), 'transparent'),
            'background-stroke': plain(by_hour(ring(NIGHT_INK), ring('hsl(0, 0%, 100%)')), 'transparent'),
            'icon': plain(by_hour(glyph(NIGHT_INK, 'night'), glyph('hsl(0, 0%, 100%)', 'disc')),
                          by_hour(tinted('night'), tinted('disc'))),
            'radius': shape_match('radius'),
            'background-stroke-width': shape_match('border')}


MONO_INK = 'hsl(0, 0%, 0%)'


def mono_params(fixed_bare=None):
    """e-ink: a black glyph on a white disc with a black ring, the same for every category"""
    fixed = None if fixed_bare is None else 'default'
    return {'background': per_class(lambda bare, cat: 'transparent' if bare else 'hsl(0, 0%, 100%)', fixed, fixed_bare),
            'background-stroke': per_class(lambda bare, cat: 'transparent' if bare else MONO_INK, fixed, fixed_bare),
            'icon': MONO_INK,
            'radius': shape_match('radius'),
            'background-stroke-width': shape_match('border')}


NAME = ['coalesce', get('name'), get('name_int')]


def bare_scale(filter, bare):
    """`bare_icon_scale` for a glyph that is not a place (furniture, landmarks), 1 for a badge: tested
    only on the bare classes the filter lets through, so a ladder layer pays a few compares, not thirty"""
    scale = ['config', 'bare_icon_scale']
    flat = lambda f: [c for sub in f[1:] for c in flat(sub)] if f[0] == 'all' else [f]
    clauses = flat(filter) if filter else []
    reach, excluded, pinned, no_viewpoint = set(NO_BACKGROUND), set(), None, False
    for c in clauses:
        if c[1] == get('shelter_type') and c[0] in ('==', 'in'):
            return 1
        if c[0] == '!=' and c[1] == get('subclass') and c[2] == 'viewpoint':
            no_viewpoint = True
        if c[0] == '==' and c[1] == get('class'):
            pinned = {c[2]}
        elif c[0] == 'in' and c[1] == get('class'):
            pinned = set(c[2][1])
        elif c[0] == '!=' and c[1] == get('class'):
            excluded.add(c[2])
        elif c[0] == '==' and c[1] == get('subclass'):
            bare = bare or c[2] in NO_BACKGROUND
            reach = set()
    if pinned is not None:
        bare = bare or pinned <= set(NO_BACKGROUND)
        reach &= pinned
    reach -= excluded
    if bare:
        return scale
    expr = ['match', get('class'), sorted(reach), scale, 1] if reach else 1
    # a walker's shelter is a badge, a viewpoint (a subclass of attraction) is not: as SHELTER_LOOK and SUBCLASS_LOOK
    if 'shelter' in reach:
        expr = ['match', get('shelter_type'), OUTDOOR_SHELTERS, 1, expr]
    if not no_viewpoint and (pinned is None or 'attraction' in pinned):
        expr = ['match', get('subclass'), 'viewpoint', scale, expr]
    return expr


# a bare glyph's halo, a hair under a plain POI's
BARE_HALO = 1
POI_HALO = 1.5


def halo_width(mono, fixed, fixed_bare):
    """no halo round a badge (its disc is one), BARE_HALO round a bare glyph, POI_HALO round a plain POI"""
    badge = per_class(lambda bare, cat: BARE_HALO if bare else 0, fixed, fixed_bare)
    if mono:
        return badge
    return ['match', ['config', 'poiStyle'], 'plain', per_class(lambda bare, cat: BARE_HALO if bare else POI_HALO,
                                                                 fixed, fixed_bare), badge]


def plain_size(scale, bare_size):
    """`poiStyle` plain: a place's glyph at 0.6, a bare one (furniture, landmarks) as badge mode draws it"""
    def size(e):
        if e == ['config', 'bare_icon_scale']:
            return ['*', 0.4, e]
        if e == 1:
            return 0.6
        if isinstance(e, list) and e[0] == 'match':
            return e[:2] + [x if i % 2 == 0 else size(x) for i, x in enumerate(e[2:-1])] + [size(e[-1])]
        raise ValueError(f'bare_scale shape: {e}')
    return times(scale, size(bare_size)) if scale != 1 else size(bare_size)


def times(size, factor):
    """`size` (a number or a zoom ramp) times a per-feature `factor`: inside the ramp's outputs, where
    MapLibre wants the zoom on top"""
    if factor == 1:
        return size
    if isinstance(size, list) and size[0] == 'interpolate':
        return size[:3] + [x if i % 2 == 0 else ['*', x, factor] for i, x in enumerate(size[3:])]
    return ['*', size, factor]


def poi_layer(id, minzoom, filter, v, icon=ICON, maxzoom=None, text=NAME, overlap=False, scale=1,
              category=None, extra=None, bare=False, unknown=False):
    """`scale` may be a zoom ramp; `extra` overrides the label's layout; `bare`: no disc whatever the
    class; `unknown`: the filter lets any class through, so one with no glyph waits for UNKNOWN_ZOOM"""
    mono = v.flags.get('mono', False)
    # The reference pane names the BAKED sprite, the SDK the neutral one it splits and
    # recolours: a sprite with the colour already in it has no plate mapbox2css can measure.
    suffix = '-mono' if mono else '-poi'
    baked = lambda name: ['image', name + suffix if isinstance(name, str) else ['concat', name, suffix]]
    params = {'params': mono_params(True if bare else None) if mono else icon_params(category, bare)}
    neutral = lambda name: ['image', name]
    chain = lambda image: until_known(icon_chain(icon, image, fallback=False), icon_chain(icon, image),
                                      minzoom, maxzoom) if unknown else icon_chain(icon, image)
    bare_size = bare_scale(filter, bare)
    gated = lambda known: until_known(['case', known, text, ''], text, minzoom, maxzoom) if unknown and text else text
    maplibre_icon = chain(baked)
    if any(c[0] in ('==', 'in') and c[1] == get('shelter_type') for c in (filter[1:] if filter[0] == 'all' else [filter])):
        # a walker's shelter is a badge (SHELTER_LOOK): the sprite bakes it as `shelter-<shelter_type>`
        maplibre_icon = ['coalesce', baked(['concat', 'shelter-', get('shelter_type')]), *maplibre_icon[1:]]
    layout = {
        'icon-image': maplibre_icon,
        'icon-size': times(scaled(scale, 0.4), bare_size),
        'text-field': gated(KNOWN_MAPLIBRE),
        # named, not dropped: without it maplibre falls back to a stack the glyph server lacks
        'text-font': 'medium',
        'text-size': 12,
        'text-max-width': 9,
        'text-padding': 2,
        'text-variable-anchor': ['top', 'left', 'right'],
        'text-radial-offset': 1.0,
        'text-justify': 'auto',
        'text-optional': True,
        **({'icon-allow-overlap': True} if overlap else {}),
        **(extra or {}),
    }
    # on imagery the ground is dark by day as well, so the label keeps its night pair
    dark = v.flags.get('dark_ground', False)
    massif_icon = chain(neutral)
    # the converter takes the FIRST image's params for the whole layer: stated once, not per image and band
    (massif_icon[2] if massif_icon[0] == 'step' else massif_icon)[1].append(params)
    massif_layout = {'icon-image': massif_icon, **({'text-field': gated(KNOWN_SDK)} if unknown else {})}
    # a bare glyph fills the disc's box: at the badge's size it reads half OSM's 14 px icon
    massif_layout['icon-size'] = times(scaled(scale, 0.4), bare_size) if mono else \
        ['match', ['config', 'poiStyle'], 'plain', plain_size(scale, bare_size), times(scaled(scale, 0.4), bare_size)]
    return boosted(layer(id, 'symbol', 'poi', minzoom=minzoom, maxzoom=maxzoom, filter=filter, layout=layout,
                 paint={'text-color': MONO_INK if mono else night_color(category) if dark else day_color(category),
                        'text-halo-color': HALO_NIGHT if dark else HALO_DAY, 'text-halo-width': HALO_WIDTH},
                 # ONE template and ONE attachment for every POI: a child project's rule extends the one
                 # and merges with the other, see docs/internals/cartocss-templates.md
                 metadata={'massif:params': ['icon-image', 'text-color', 'icon-halo-width'],
                           'massif:template': 'poi', 'massif:attachment': 'poi',
                           'massif:layout': massif_layout,
                           'massif:paint': {'text-color': MONO_INK if mono else night_color(category) if dark else text_color(category),
                                            'text-halo-color': HALO_NIGHT if dark else by_hour(HALO_NIGHT, HALO_DAY),
                                            # `plain`: a bare glyph needs the halo its disc gave it
                                            'icon-halo-color': HALO_NIGHT if dark else by_hour(HALO_NIGHT, HALO_DAY),
                                            'icon-halo-width': halo_width(mono, category, bare)}}),
                   'subclass', 'class')


# A walker's POIs, each until its category layer takes over, water from `water_min_zoom`.
# data-driven even for the hut layer: a constant icon-image is not one mapbox2css recolours
MOUNTAIN_ICON = class_icon({**ALIAS, 'lodging': 'alpine_hut'})
MOUNTAIN_LAYERS = [
    ('poi-mountain-sight', 14, 16, ['in', get('class'), ['literal', ['adit', 'archaeological_site', 'castle',
                                                                      'cave_entrance', 'fort', 'waterfall']]], ICON, None),
    # a named park early and over the sights, as Standard gives park_like a wider filterrank
    ('poi-mountain-park', 14, 16, ['all', ['in', get('class'), ['literal', ['park', 'garden']]], ['has', 'name']], ICON, None),
    ('poi-mountain-picnic', 13, 15, ['==', get('class'), 'picnic_site'], MOUNTAIN_ICON, None),
    # a picnic shelter is a park bench's roof: a walker's shelter's look, but late and under the huts
    ('poi-mountain-picnic-shelter', 16, None, ['all', ['==', get('class'), 'shelter'],
                                               ['==', get('shelter_type'), 'picnic_shelter']], MOUNTAIN_ICON, None),
    ('poi-mountain-shelter', 13, None, ['all', ['==', get('class'), 'shelter'],
                                        ['in', get('shelter_type'), ['literal', HUT_SHELTERS]]], MOUNTAIN_ICON, None),
    ('poi-mountain-water', 12, None, ['==', get('class'), 'drinking_water'], MOUNTAIN_ICON, 'water_min_zoom'),
    ('poi-mountain', 12, None, ['==', get('class'), 'wilderness_hut'], MOUNTAIN_ICON, None),
    # OpenMapTiles files a hut under lodging, whose glyph is a bed
    ('poi-mountain-hut', 12, 15, ['==', get('subclass'), 'alpine_hut'], MOUNTAIN_ICON, None),
]


def springs(v):
    """a spring as Alpimaps' OSM style draws it: a water-blue dot in a white ring, growing over z12-16,
    never hidden by another label; named from z17"""
    mono = v.flags.get('mono', False)
    spring = ['==', get('class'), 'spring']
    dot = layer('poi-spring', 'circle', 'poi', minzoom=12, filter=spring,
                paint={'circle-color': MONO_INK if mono else CATEGORY['water']['disc'],
                       'circle-radius': zoom_ramp(12, 1.5, 14, 2, 16, 5),
                       'circle-stroke-color': HALO_DAY, 'circle-stroke-width': zoom_ramp(13.5, 0, 14, 1)},
                metadata={'massif:minzoom-param': 'water_min_zoom'})
    name = layer('poi-spring-label', 'symbol', 'poi', minzoom=17, filter=spring,
                 layout={'text-field': NAME, 'text-font': 'medium', 'text-size': 12, 'text-max-width': 9,
                         'text-anchor': 'top', 'text-offset': [0, 0.6], 'text-optional': True},
                 paint={'text-color': MONO_INK if mono else CATEGORY['water']['day'],
                        'text-halo-color': HALO_DAY, 'text-halo-width': HALO_WIDTH})
    # `highlight_drinking_water` draws springs in water_highlight() instead
    return [gate(dot, v, 'highlight_drinking_water', 0), gate(boosted(name, 'class'), v, 'highlight_drinking_water', 0)]


# a cave's name waits for z15: the entrance alone says where it is
CAVE_NAME = ['step', ['zoom'], ['match', get('class'), 'cave_entrance', '', NAME], 15, NAME]


def mountain(v):
    out = []
    for id, minzoom, maxzoom, filter, icon, param in MOUNTAIN_LAYERS:
        lay = poi_layer(id, minzoom, filter, v, icon=icon, maxzoom=maxzoom,
                        category='park_like' if id == 'poi-mountain-shelter' else None,
                        text=CAVE_NAME if id == 'poi-mountain-sight' else NAME)
        if param:
            lay['metadata']['massif:minzoom-param'] = param
        if param == 'water_min_zoom':
            # `highlight_drinking_water` draws these in water_highlight() instead
            gate(lay, v, 'highlight_drinking_water', 0)
        out.append(lay)
        if id == 'poi-mountain-water':
            out += springs(v)
    return out


def campsites(v):
    """From `campsite_min_zoom`, a caravan site only with `show_caravan_site`; each twice, placed
    with and without `campsite_allow_overlap`, since overlap is decided per layer."""
    camp = ['==', get('class'), 'campsite']
    out = []
    for id, filter, switch in (('poi-campsite', ['all', camp, ['!=', get('subclass'), 'caravan_site']], None),
                               ('poi-caravan-site', ['all', camp, ['==', get('subclass'), 'caravan_site']],
                                'show_caravan_site')):
        for overlap in (0, 1):
            # z10, where the tiles start carrying campsites: the floor an app may lower `campsite_min_zoom` to
            lay = poi_layer(id + ('-overlap' if overlap else ''), 10, filter, v, overlap=bool(overlap))
            lay['metadata']['massif:minzoom-param'] = 'campsite_min_zoom'
            gate(lay, v, 'campsite_allow_overlap', overlap)
            if switch:
                gate(lay, v, switch)
            out.append(lay)
    return out


def kindergarten(v):
    """a kindergarten in the parks' green rather than a school's brown; OpenMapTiles files it under
    school, so it is its own layer the way a viewpoint is"""
    return [poi_layer('poi-kindergarten', 15, ['==', get('subclass'), 'kindergarten'], v,
                      category='park_like')]


def water_highlight(v):
    """`highlight_drinking_water`, Alpimaps': water points larger, never hidden by another label, and
    from z12 whatever `water_min_zoom` says"""
    lay = poi_layer('poi-water-highlight', 12, ['in', get('class'), ['literal', ['drinking_water', 'spring']]], v,
                    icon=MOUNTAIN_ICON, overlap=True, scale=1.4)
    return [gate(lay, v, 'highlight_drinking_water')]


TREE = ['==', get('class'), 'tree']
TREE_DOT = 'hsl(100, 45%, 60%)'
# a 0.4 badge's glyph at z17, growing with the zoom
TREE_SCALE = zoom_ramp(17, 1, 22, 2)
# under every label on the SDK: the layers must sit with the other POIs, `poi` cannot be split (README)
TREE_SINK = 10000000


def sunk(lay):
    """the layer's placement priority TREE_SINK lower, an app's poi-boost still added (SDK only)"""
    key = lay['metadata']['massif:layout']['symbol-sort-key']
    lay['metadata']['massif:layout']['symbol-sort-key'] = ['-', ['-', key[1], TREE_SINK]]
    return lay


def trees(v):
    """MapTiler's ladder: an unnamed tree (one MultiPoint per tile, `class` only) is a dot from z16 and
    the glyph from z18, a named one the glyph and its name from z17, every other label winning over
    them. Hybrid draws only the named ones: the imagery shows the rest."""
    named = sunk(poi_layer('poi-tree-named', 17, ['all', TREE, ['has', 'name']], v, scale=TREE_SCALE,
                           extra={'text-font': 'italic', 'text-size': 11, 'text-variable-anchor': ['top']}))
    if v.flags.get('dark_ground'):
        return [named]
    unnamed = ['all', TREE, ['!', ['has', 'name']]]
    dot = layer('poi-tree-dot', 'circle', 'poi', minzoom=16, maxzoom=18, filter=unnamed,
                paint={'circle-color': MONO_INK if v.flags.get('mono') else TREE_DOT,
                       'circle-radius': zoom_ramp(16, 1.5, 18, 2.5),
                       'circle-stroke-color': HALO_DAY, 'circle-stroke-width': 1})
    return [dot, sunk(poi_layer('poi-tree', 18, unnamed, v, text='', scale=TREE_SCALE)), named]


# least important first, a layer each: also keeps any one priority off most %poi rules (the template's)
BARRIER_TIERS = [('bollard', ['bollard']),
                 ('passage', ['cycle_barrier', 'gate', 'lift_gate', 'sally_port', 'stile']),
                 ('control', ['border_control', 'toll_booth'])]


def barriers(v):
    """a point barrier (gate, bollard, stile...: unnamed ones one MultiPoint per tile, `class` only) is
    a small bare glyph from z17, named if it is, just over the trees and under every other label. A
    halo round the glyph keeps it on imagery and patterns; hybrid draws it in its night colour."""
    out = []
    for tier, classes in BARRIER_TIERS:
        cls = ['==', get('class'), classes[0]] if len(classes) == 1 else ['in', get('class'), ['literal', classes]]
        for suffix, named, text in (('', ['!', ['has', 'name']], ''), ('-named', ['has', 'name'], NAME)):
            out.append(reference(poi_layer('poi-barrier-%s%s' % (tier, suffix), 17, ['all', cls, named], v,
                                           text=text,
                                           extra={'text-size': 11, 'text-variable-anchor': ['top']}), v))
    return out


def reference(lay, v, category=None):
    """a bare glyph under every POI (sunk); on hybrid in its night colour"""
    lay = sunk(lay)
    if v.flags.get('dark_ground'):
        # one params object, shared by every image of the chain
        params = lay['metadata']['massif:layout']['icon-image'][1][2]['params']
        params['icon'] = per_class(lambda bare, cat: CATEGORY[cat]['night'], category)
    return lay


# by the size of the thing, so by how far off it reads: a pylon line or a turbine from the valley
LANDMARK_TIERS = [(17, ['guidepost']),
                  (16, ['cairn', 'rock', 'stone', 'wayside_cross', 'wayside_shrine']),
                  (15, ['cross', 'mast', 'pylon']),
                  (13, ['power_tower', 'wind_turbine'])]


def landmarks(v):
    """a viewpoint, then what marks a spot on the ground rather than names one (Alpimaps' `poi_landmarks`,
    `poi_guideposts`): bare glyphs over the trees and under the barriers, so they never hide a POI"""
    out = [reference(poi_layer('poi-viewpoint', 14, ['==', get('subclass'), 'viewpoint'], v, category='park_like',
                               bare=True), v, 'park_like')]
    for minzoom, classes in LANDMARK_TIERS:
        cls = ['==', get('class'), classes[0]] if len(classes) == 1 else ['in', get('class'), ['literal', classes]]
        out.append(reference(poi_layer('poi-landmark-z%d' % minzoom, minzoom, cls, v, scale=TREE_SCALE,
                                       extra={'text-size': 11, 'text-variable-anchor': ['top']}), v))
    return out


def stop(id, filter, zooms, const, v):
    """The icon from `poi_<const>_minzoom`, the name from `poi_<const>_label_minzoom`: project constants,
    so a child project moves either in its `constants`"""
    icon_zoom, name_zoom = zooms
    unknown = const == 'rank30'
    icon = poi_layer(id + '-icon', icon_zoom, filter, v, maxzoom=name_zoom, text='', unknown=unknown)
    icon['metadata'].update({'massif:minzoom-const': 'poi_%s_minzoom' % const,
                             'massif:maxzoom-const': 'poi_%s_label_minzoom' % const})
    named = poi_layer(id, name_zoom, filter, v, unknown=unknown)
    named['metadata']['massif:minzoom-const'] = 'poi_%s_label_minzoom' % const
    return [icon, named]


def ranked(rows, ladder, v):
    out = []
    for id, minzoom, filter, const in rows:
        clauses = filter[1:] if filter[0] == 'all' else [filter]
        pinned = any(c[0] in ('==', 'in') and c[1] == get('class') for c in clauses)
        lay = poi_layer(id, minzoom, ['all', ladder, filter], v, unknown=not pinned)
        lay['metadata']['massif:minzoom-const'] = const
        out.append(lay)
    return out


def pt_shelter(v):
    """`poi_pt_shelter_minzoom`, never before the bus stop it stands at"""
    lay = poi_layer('poi-pt-shelter', 17, PT_SHELTER, v, text='')
    lay['metadata']['massif:minzoom-const'] = 'poi_pt_shelter_minzoom'
    return [lay]


def layers(v):
    ladder = ['all', *[['!=', get('class'), c] for c in OWN_LAYER],
              ['!=', get('subclass'), 'kindergarten'], ['!=', get('subclass'), 'viewpoint']]
    not_tram = ['!=', get('subclass'), 'tram_stop']
    return (pt_shelter(v) + [poi_layer('poi-shelter', 17, OTHER_SHELTER, v, text='')] + stop('poi-bus', ['==', get('class'), BUS[0]], BUS[1:], 'bus', v) +
            ranked(RANK_LADDER, ladder, v) + stop('poi-rank-r30', ['all', ladder, RANK30], (15, 16), 'rank30', v) +
            ranked(RANK10, ladder, v) + campsites(v) +
            [poi_layer(id, minzoom, ['all', ['in', get('class'), ['literal', classes]], not_tram], v)
             for id, minzoom, classes in STATION_LAYERS] + stop('poi-tram', TRAM_STOP, TRAM, 'tram', v) +
            kindergarten(v) + water_highlight(v))


def write_sprite_palette(path):
    """The same table, for the sprite build to bake with.

    MapLibre cannot read the image params that colour the disc on the SDK, so the sprite carries it
    already drawn. Two consumers of one table, and this file is the one that is edited - the JSON is
    output, like style.json.
    """
    from params import PARAMS
    # MapLibre's sprites are not SDF, so a bare glyph's halo is drawn into it, BARE_HALO screen pixels
    # at the size the layers draw a bare glyph (0.4 * bare_icon_scale), in the sprite's 48-unit box
    halo = round(BARE_HALO / (0.4 * PARAMS['bare_icon_scale']['default']), 2)

    def entry(name, cat, bare, mono):
        shape = SHAPE.get(name, DEFAULT_SHAPE)
        if mono:
            return {'disc': None if bare else 'hsl(0, 0%, 100%)', 'glyph': MONO_INK, 'radius': shape['radius'],
                    'border': 0 if bare else shape['border'], 'halo': halo if bare else 0}
        return {'disc': None if bare else CATEGORY[cat]['disc'],
                'glyph': CATEGORY[cat]['disc'] if bare else 'hsl(0, 0%, 100%)',
                'radius': shape['radius'], 'border': 0 if bare else shape['border'], 'halo': halo if bare else 0}

    def section(mono):
        # Street furniture is in no category list, so walk both: those classes still need a glyph
        # colour, and without them the sheet would draw a bin with a disc under it.
        classes = {cls: entry(cls, CLASS_TO_CATEGORY.get(cls, 'default'), is_bare(cls), mono)
                   for cls in sorted(set(CLASS_TO_CATEGORY) | set(NO_BACKGROUND) | set(ICONS))}
        # a subclass's own glyph takes the subclass's look, as the SDK's subclass table gives it
        for sub, (cat, bare) in SUBCLASS_LOOK.items():
            if sub in ICONS and sub not in CLASS_TO_CATEGORY:
                classes[sub] = entry(sub, cat, bare, mono)
        return {'ring': MONO_INK if mono else 'hsl(0, 0%, 100%)', 'halo': HALO_DAY,
                'default': entry('default', 'default', False, mono), 'classes': classes,
                # `shelter-<shelter_type>`: the shelter glyph in a walker's shelter's look (SHELTER_LOOK)
                'aliases': {f'shelter-{t}': {'from': 'shelter', **entry('shelter', cat, bare, mono)}
                            for t, (cat, bare) in sorted(SHELTER_LOOK.items())}}

    payload = {'comment': 'Generated by styles/massif/build.py from layers/pois.py - edit that, never this.',
               **section(False), 'mono': section(True)}
    open(path, 'w').write(json.dumps(payload, indent=2) + '\n')

