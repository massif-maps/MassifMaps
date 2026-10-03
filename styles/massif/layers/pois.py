"""POIs: the palette tables below are the source of truth for every POI colour - the disc, the
label (two measure-light stops, read off mapbox/standard) and which classes get no disc at all.
Each POI layer carries the same flat match on `class`, because the converter needs a match it can
key on per layer to fold it into one project.json table - see the style's README.
"""
import json
from collections import OrderedDict

from lib import boosted, by_hour, gate, get, layer, zoom_ramp

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
                  'mountain', 'park', 'playground', 'ranger_station', 'viewpoint', 'volcano', 'waterfall', 'wetland'],
    'water': ['drinking_water', 'spring', 'water', 'water_point', 'watering_place'],
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

# every `poi-boost-<name>` an app may set (lib.boosted): the classes and subclasses a rule or a colour
# names, the mountain_peak classes, and the airport label
BOOST_NAMES = sorted(set(CLASS_TO_CATEGORY) | set(NO_BACKGROUND) | {s for ss in SUBCLASS.values() for s in ss} |
                     {'attraction', 'caravan_site', 'kindergarten', 'lodging', 'picnic_site', 'shelter',
                      'peak', 'saddle', 'volcano'})


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
    return out


# The data decides when a POI appears: OpenMapTiles' `rank`, on OpenFreeMap Liberty's ladder. Least
# important first, so a lower rank wins a collision.
RANK_LAYERS = [
    ('poi-rank-r20', 17, ['all', ['>=', get('rank'), 20]]),
    ('poi-rank-r7', 16, ['all', ['>=', get('rank'), 7], ['<', get('rank'), 20]]),
    ('poi-rank-r1', 15, ['all', ['>=', get('rank'), 1], ['<', get('rank'), 7]]),
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
OTHER_SHELTER = ['all', ['==', get('class'), 'shelter'], ['!=', get('shelter_type'), 'public_transport'],
                 *[['!=', get('shelter_type'), t] for t in OUTDOOR_SHELTERS]]
# Drawn by a layer of their own at every zoom, so the ladder leaves them out. Excluded rather than
# listed: a class list is a when() per feature, an exclusion a few prunable selectors.
OWN_LAYER = sorted({'bus', 'campsite', 'drinking_water', 'shelter', 'spring', 'wilderness_hut'} | {c for _, _, cs in STATION_LAYERS for c in cs})

# a viewpoint is an attraction to OpenMapTiles: its own glyph at every zoom (a ruin keeps the castle, as Standard)
ICON = ['match', get('subclass'), ['florist', 'furniture', 'viewpoint'], get('subclass'), get('class')]


# Standard's night POI: the disc takes the category's night colour, and the ring and the glyph go
# dark instead of staying white - a white ring glares on a dark map
NIGHT_INK = 'hsl(0, 0%, 12%)'


def per_class(value_of, default, fixed=None):
    """ONE flat match on class, grouping the classes that share a value: every branch a constant,
    which is the shape mapbox2css folds into a project.json table. `fixed`: one category's value."""
    if fixed:
        return value_of(None, fixed)
    groups = {}
    for cls in sorted(set(CLASS_TO_CATEGORY) | set(NO_BACKGROUND)):
        value = value_of(cls, CLASS_TO_CATEGORY.get(cls, 'default'))
        if value != default:
            groups.setdefault(value, []).append(cls)
    out = ['match', get('class')]
    for value, classes in groups.items():
        out += [classes if len(classes) > 1 else classes[0], value]
    return out + [default]


def icon_params(fixed=None):
    # `transparent`, not `none`: the decoder's parseColor knows the CSS names and that one, and
    # throws on anything else - a bad colour kills the whole feature processor.
    bare = lambda cls: cls in NO_BACKGROUND
    disc = lambda key: per_class(lambda cls, cat: 'transparent' if bare(cls) else CATEGORY[cat][key],
                                 CATEGORY['default'][key], fixed)
    ring = lambda ink: per_class(lambda cls, cat: 'transparent' if bare(cls) else ink, ink, fixed)
    # a glyph with no disc under it is drawn in the category colour, not white on it
    glyph = lambda ink, key: per_class(lambda cls, cat: CATEGORY[cat][key] if bare(cls) else ink, ink, fixed)
    # poiStyle `plain`: OSM's look, every glyph bare in its category colour like the furniture
    plain = lambda badge, bare_value: ['match', ['config', 'poiStyle'], 'plain', bare_value, badge]
    tinted = lambda key: per_class(lambda cls, cat: CATEGORY[cat][key], CATEGORY['default'][key], fixed)
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


def poi_layer(id, minzoom, filter, v, icon=ICON, maxzoom=None, text=NAME, overlap=False, scale=1,
              category=None):
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
        **({'icon-allow-overlap': True} if overlap else {}),
    }
    # on imagery the ground is dark by day as well, so the label keeps its night pair
    dark = v.flags.get('dark_ground', False)
    mono = v.flags.get('mono', False)
    massif_layout = {'icon-image': ['image', icon, {'params': mono_params() if mono else icon_params(category)}]}
    # a bare glyph fills the disc's box: at the badge's size it reads half OSM's 14 px icon
    massif_layout['icon-size'] = 0.4 * scale if mono else ['match', ['config', 'poiStyle'], 'plain', 0.6 * scale, 0.4 * scale]
    return boosted(layer(id, 'symbol', 'poi', minzoom=minzoom, maxzoom=maxzoom, filter=filter, layout=layout,
                 paint={'text-color': MONO_INK if mono else night_color(category) if dark else day_color(category),
                        'text-halo-color': HALO_NIGHT if dark else HALO_DAY, 'text-halo-width': HALO_WIDTH},
                 # ONE template and ONE attachment for every POI: a child project's rule extends the one
                 # and merges with the other, see docs/internals/cartocss-templates.md
                 metadata={'massif:params': ['icon-image', 'text-color'],
                           'massif:template': 'poi', 'massif:attachment': 'poi',
                           'massif:layout': massif_layout,
                           'massif:paint': {'text-color': MONO_INK if mono else night_color(category) if dark else text_color(category),
                                            'text-halo-color': HALO_NIGHT if dark else by_hour(HALO_NIGHT, HALO_DAY),
                                            # `plain`: a bare glyph needs the halo its disc gave it
                                            'icon-halo-color': HALO_NIGHT if dark else by_hour(HALO_NIGHT, HALO_DAY),
                                            'icon-halo-width': 0 if mono else ['match', ['config', 'poiStyle'], 'plain', 1.5, 0]}}),
                   'subclass', 'class')


# A walker's POIs, each until its category layer takes over, water from `water_min_zoom`.
# data-driven even for the hut layer: a constant icon-image is not one mapbox2css recolours
MOUNTAIN_ICON = ['match', get('class'), ['lodging', 'wilderness_hut'], 'alpine_hut', 'spring', 'water', get('class')]
# the viewpoint first, so a cave or a ruin beside it wins the collision; at every zoom, in nature's
# green as the sprite bakes it, where its class (attraction) would colour it pink
MOUNTAIN_LAYERS = [
    ('poi-mountain-viewpoint', 14, None, ['==', get('subclass'), 'viewpoint'], ICON, None),
    ('poi-mountain-sight', 14, 16, ['in', get('class'), ['literal', ['adit', 'archaeological_site', 'castle',
                                                                      'cave_entrance', 'fort', 'waterfall']]], ICON, None),
    # a named park early and over the sights, as Standard gives park_like a wider filterrank
    ('poi-mountain-park', 14, 16, ['all', ['in', get('class'), ['literal', ['park', 'garden']]], ['has', 'name']], ICON, None),
    ('poi-mountain-picnic', 13, 15, ['==', get('class'), 'picnic_site'], MOUNTAIN_ICON, None),
    ('poi-mountain-shelter', 13, None, ['all', ['==', get('class'), 'shelter'],
                                        ['in', get('shelter_type'), ['literal', OUTDOOR_SHELTERS]]], MOUNTAIN_ICON, None),
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


def mountain(v):
    out = []
    for id, minzoom, maxzoom, filter, icon, param in MOUNTAIN_LAYERS:
        # a water point's glyph a size down: they are many, and a walker needs the dot, not the badge
        lay = poi_layer(id, minzoom, filter, v, icon=icon, maxzoom=maxzoom,
                        category='park_like' if id in ('poi-mountain-viewpoint', 'poi-mountain-shelter') else None,
                        scale=0.75 if id == 'poi-mountain-water' else 1)
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
            lay = poi_layer(id + ('-overlap' if overlap else ''), 12, filter, v, overlap=bool(overlap))
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


def stop(id, filter, zooms, const, v):
    """The icon from `poi_<const>_minzoom`, the name from `poi_<const>_label_minzoom`: project constants,
    so a child project moves either in its `constants`"""
    icon_zoom, name_zoom = zooms
    icon = poi_layer(id + '-icon', icon_zoom, filter, v, maxzoom=name_zoom, text='')
    icon['metadata'].update({'massif:minzoom-const': 'poi_%s_minzoom' % const,
                             'massif:maxzoom-const': 'poi_%s_label_minzoom' % const})
    named = poi_layer(id, name_zoom, filter, v)
    named['metadata']['massif:minzoom-const'] = 'poi_%s_label_minzoom' % const
    return [icon, named]


def pt_shelter(v):
    """`poi_pt_shelter_minzoom` in a city (streets, hybrid), z15 on the walker's variants"""
    if v.flags.get('trails'):
        return [poi_layer('poi-pt-shelter-outdoor', 15, PT_SHELTER, v, text='')]
    lay = poi_layer('poi-pt-shelter', 17, PT_SHELTER, v, text='')
    lay['metadata']['massif:minzoom-const'] = 'poi_pt_shelter_minzoom'
    return [lay]


def layers(v):
    ladder = ['all', *[['!=', get('class'), c] for c in OWN_LAYER],
              ['!=', get('subclass'), 'kindergarten'], ['!=', get('subclass'), 'viewpoint']]
    not_tram = ['!=', get('subclass'), 'tram_stop']
    return (pt_shelter(v) + [poi_layer('poi-shelter', 17, OTHER_SHELTER, v, text='')] + stop('poi-bus', ['==', get('class'), BUS[0]], BUS[1:], 'bus', v) +
            [poi_layer(id, minzoom, ['all', ladder, filter], v) for id, minzoom, filter in RANK_LAYERS] + campsites(v) +
            [poi_layer(id, minzoom, ['all', ['in', get('class'), ['literal', classes]], not_tram], v)
             for id, minzoom, classes in STATION_LAYERS] + stop('poi-tram', TRAM_STOP, TRAM, 'tram', v) +
            kindergarten(v) + water_highlight(v))


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

