"""The legend spec, `carto/legend.json`: synthetic features read off the tables the layers are built
from, which the SDK (MBVectorTileDecoder.getLegend) and `massif-style legend` resolve against the
compiled style with the live parameters. One spec for every variant: the variant is a parameter, and
an item a variant does not draw is dropped by the resolver. See docs/features/legends.md."""
import json
import os

from layers import land, outdoor, pois, roads

HERE = os.path.dirname(os.path.abspath(__file__))

SAC_LABELS = ['T1 hiking', 'T2 mountain hiking', 'T3 demanding mountain hiking', 'T4 alpine hiking',
              'T5 demanding alpine hiking', 'T6 difficult alpine hiking']
MTB_LABELS = {'mtb-easy': 'MTB 0-1 easy', 'mtb-medium': 'MTB 2 medium', 'mtb-hard': 'MTB 3 hard',
              'mtb-extreme': 'MTB 4-6 extreme'}
TRACK_LABELS = {'grade1': 'Track, paved or solid', 'grade2': 'Track, gravel', 'grade3': 'Track, mixed',
                'grade4': 'Track, mostly soft', 'grade5': 'Track, soft', 'unknown': 'Track, surface unknown'}
FILL_LABELS = {'wood': 'Forest', 'scrub': 'Scrub', 'grass': 'Grass, meadow', 'park': 'Park', 'wetland': 'Wetland',
               'rock': 'Rock, scree', 'sand': 'Sand', 'glacier': 'Glacier', 'farmland': 'Farmland',
               'cemetery': 'Cemetery', 'military': 'Military area', 'residential': 'Residential',
               'commercial': 'Commercial', 'industrial': 'Industrial'}
# (category, label, the class a POI of it carries)
POIS = [('food_and_drink', 'Food and drink', 'restaurant'), ('store_like', 'Shops', 'shop'),
        ('arts_and_entertainment', 'Culture', 'museum'), ('commercial_services', 'Services', 'bank'),
        ('sport_and_leisure', 'Sport', 'stadium'), ('park_like', 'Nature, parks', 'park'),
        ('medical', 'Health', 'pharmacy'), ('education', 'Education', 'school'),
        ('water', 'Drinking water', 'drinking_water'), ('transit', 'Transport', 'bus'),
        ('default', 'Other', 'place_of_worship')]
# (id, label, transportation_name fields): one per plate colour and sign
SHIELDS = [
    ('shield-red', 'Motorway, national road (FR, NL)', {'class': 'motorway', 'ref': 'A 7', 'iso_a2': 'FR'}),
    ('shield-yellow', 'Departmental road (FR), B road (DE)', {'class': 'secondary', 'ref': 'D 902', 'iso_a2': 'FR'}),
    ('shield-blue', 'Motorway (DE, GB, ES)', {'class': 'motorway', 'ref': 'A 9', 'iso_a2': 'DE'}),
    ('shield-e-road', 'European route', {'class': 'motorway', 'ref': 'E 15', 'network': 'e-road'}),
    ('shield-neutral', 'Other road number', {'class': 'primary', 'ref': '12'}),
    ('shield-us-interstate', 'Interstate (US)', {'class': 'motorway', 'ref': '95', 'network': 'us-interstate', 'ref_length': 2}),
    ('shield-us-highway', 'US highway', {'class': 'trunk', 'ref': '101', 'network': 'us-highway', 'ref_length': 3}),
    ('shield-exit', 'Motorway exit', {'class': 'motorway', 'subclass': 'junction', 'ref': '12', 'zoom': 15}),
]


def only(*attachments):
    """the item's own attachments, bands included: the same feature is drawn by other rules too"""
    return '^(%s)(_b[0-9]+)?$' % '|'.join(a.replace('-', '_') for a in attachments)


def item(id, label, layer, geometry, properties, attachment=None, zoom=None):
    return {'id': id, 'label': label, 'layer': layer, 'geometry': geometry, 'properties': properties,
            **({'attachment': attachment} if attachment else {}), **({'zoom': zoom} if zoom else {})}


def line(id, label, properties, attachment=None, layer='transportation'):
    return item(id, label, layer, 'line', properties, attachment)


def spec():
    assert all(cls in pois.CLASSES[cat] for cat, _, cls in POIS)
    road = lambda cls, **extra: {'class': cls, **extra}
    sections = [
        {'id': 'roads', 'label': 'Roads', 'zoom': 15, 'items': [
            line('motorway', 'Motorway', road('motorway')),
            line('trunk', 'Trunk road', road('trunk')),
            line('primary', 'Primary road', road('primary')),
            line('secondary', 'Secondary road', road('secondary')),
            line('minor', 'Minor road', road('minor')),
            line('unpaved', 'Unpaved road', road('minor', surface='unpaved', surface_detail='unpaved')),
            line('no-access', 'Private or no access', road('service', access='no')),
            line('cycleway', 'Cycleway', road('path', subclass='cycleway')),
            line('path', 'Path, footway', road('path', subclass='footway')),
        ]},
        {'id': 'tracks', 'label': 'Tracks', 'zoom': 15, 'items': [
            line('track-' + grade, TRACK_LABELS[grade],
                 road('track', **({'tracktype': grade} if grade != 'unknown' else {})),
                 only('track-halo', 'track-' + grade))
            for grade, _ in roads.TRACK_GRADES + [('unknown', None)]]},
        {'id': 'trails', 'label': 'Hiking difficulty (SAC scale)', 'zoom': 15, 'items': [
            line(id.replace('trail-', 'sac-'), SAC_LABELS[i], road('path', subclass='path', sac_scale=values[0]),
                 only('trail-halo', id))
            for i, (id, values, _, _) in enumerate(outdoor.SAC)]},
        {'id': 'mtb', 'label': 'Mountain bike difficulty (beside the path)', 'zoom': 15, 'items': [
            line(id, MTB_LABELS[id], road('path', subclass='path', mtb_scale=values[1]), only(id))
            for id, values, _, _ in outdoor.MTB]},
        {'id': 'transport', 'label': 'Transport', 'zoom': 15, 'items': [
            line('rail', 'Railway', road('rail', subclass='rail')),
            line('aerialway', 'Cable car, ski lift', road('aerialway', subclass='cable_car')),
            line('ferry', 'Ferry', road('ferry')),
            line('via-ferrata', 'Via ferrata', road('via_ferrata')),
        ]},
        {'id': 'water', 'label': 'Water', 'zoom': 14, 'items': [
            item('water', 'Lake, river', 'water', 'polygon', {'class': 'lake'}),
            line('stream', 'Stream', {'class': 'stream'}, layer='waterway'),
            line('stream-intermittent', 'Intermittent stream', {'class': 'stream', 'intermittent': 1},
                 layer='waterway'),
        ]},
        {'id': 'land', 'label': 'Land', 'zoom': 14, 'items': [
            *[item(key, FILL_LABELS[key], 'landcover', 'polygon', {'class': classes[0]})
              for key, classes in land.LANDCOVER],
            *[item(key, FILL_LABELS[key], 'landcover', 'polygon', {'class': 'grass', 'subclass': subclasses[0]})
              for key, subclasses in land.LANDCOVER_SUBCLASS if key in FILL_LABELS],
            *[item(key, FILL_LABELS[key], 'landuse', 'polygon', {'class': cls})
              for key, cls in [('residential', 'residential'), ('commercial', 'commercial'),
                               ('industrial', 'industrial'), ('cemetery', 'cemetery'), ('military', 'military')]],
        ]},
        {'id': 'outdoor', 'label': 'Mountain', 'zoom': 14, 'items': [
            item('peak', 'Summit', 'mountain_peak', 'point', {'class': 'peak', 'name': 'Grand Veymont', 'ele': 2341, 'rank': 1}),
            item('alpine-hut', 'Alpine hut', 'poi', 'point', {'class': 'lodging', 'subclass': 'alpine_hut', 'name': 'Refuge'}),
            item('shelter', 'Shelter', 'poi', 'point', {'class': 'shelter', 'subclass': 'shelter', 'name': 'Cabane'}),
            item('viewpoint', 'Viewpoint', 'poi', 'point', {'class': 'attraction', 'subclass': 'viewpoint', 'name': 'Belvédère'}),
            item('cave', 'Cave', 'poi', 'point', {'class': 'cave_entrance', 'name': 'Grotte'}),
            item('ruins', 'Ruins', 'poi', 'point', {'class': 'castle', 'subclass': 'ruins', 'name': 'Ruines'}),
            item('spring', 'Spring', 'poi', 'point', {'class': 'spring', 'subclass': 'spring'}),
        ]},
        {'id': 'pois', 'label': 'Places of interest', 'zoom': 17, 'items': [
            item('poi-' + cat, label, 'poi', 'point', {'class': cls, 'subclass': cls, 'name': label, 'rank': 1})
            for cat, label, cls in POIS]},
        {'id': 'shields', 'label': 'Road numbers', 'zoom': 13, 'items': [
            item(id, label, 'transportation_name', 'line', {k: v for k, v in fields.items() if k != 'zoom'},
                 zoom=fields.get('zoom'))
            for id, label, fields in SHIELDS]},
    ]
    return {'title': 'Massif', 'sections': sections}


def write():
    open(os.path.join(HERE, 'carto', 'legend.json'), 'w').write(json.dumps(spec(), indent=2, ensure_ascii=False) + '\n')
