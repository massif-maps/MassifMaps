from lib import get, in_class, layer, zoom_ramp

# (palette key, OMT landcover classes); drawn in this order, so a later group paints over an earlier one
LANDCOVER = [
    ('farmland', ['farmland']), ('grass', ['grass']), ('wood', ['wood']), ('wetland', ['wetland']),
    ('sand', ['sand']), ('rock', ['rock']), ('glacier', ['ice']),
]
# grass subclasses that read as something else; subclass, because OMT folds them all into `grass`
LANDCOVER_SUBCLASS = [('scrub', ['scrub', 'heath']),
                      ('park', ['park', 'garden', 'village_green', 'recreation_ground', 'allotments']),
                      ('pitch', ['golf_course'])]

LANDUSE = [
    ('commercial', ['commercial', 'retail']),
    ('industrial', ['industrial', 'garages', 'railway', 'quarry', 'dam', 'bus_station']),
    ('education', ['school', 'university', 'kindergarten', 'college', 'library', 'education']),
    ('hospital', ['hospital']),
    ('cemetery', ['cemetery']),
    ('park', ['stadium', 'theme_park', 'zoo']),
    ('pitch', ['pitch', 'track', 'playground']),
    ('parking', ['parking', 'pedestrian', 'square']),
    ('military', ['military']),
]
RESIDENTIAL = ['residential', 'suburb', 'quarter', 'neighbourhood']


def background(v):
    return [layer('background', 'background', paint={'background-color': v.palette['land']},
                  emissive=zoom_ramp(13, 0.1, 14, 0.25))]


def layers(v):
    c = v.palette
    fill = lambda key: {'fill-color': c[key], 'fill-antialias': False}
    out = [
        layer('landuse-residential', 'fill', 'landuse', minzoom=6, filter=in_class(RESIDENTIAL),
              paint={'fill-color': c['residential'], 'fill-opacity': zoom_ramp(6, 0, 9, 1)}, emissive=0.25),
    ]
    # below z7 the bathymap archive's generalised landcover draws instead (lowzoom.py), and the two
    # crossfade; Standard's woods are a paler green that deepens as the forests resolve into stands
    fade = {'fill-opacity': zoom_ramp(7, 0, 9, 1)}
    wood = {'fill-color': zoom_ramp(8, c['wood-low'], 11, c['wood']), 'fill-antialias': False}
    out += [layer('landcover-' + key, 'fill', 'landcover', minzoom=7, filter=in_class(classes),
                  paint={**(wood if key == 'wood' else fill(key)), **fade}, emissive=0.2)
            for key, classes in LANDCOVER]
    out += [layer('landcover-' + key + '-sub', 'fill', 'landcover', minzoom=10,
                  filter=in_class(subclasses, 'subclass'), paint=fill(key), emissive=0.2)
            for key, subclasses in LANDCOVER_SUBCLASS]
    out += [
        layer('park', 'fill', 'park', minzoom=5,
              paint={'fill-color': c['national-park'], 'fill-opacity': zoom_ramp(5, 0, 6, 0.5, 13, 0.3)},
              emissive=0.2),
        layer('park-outline', 'line', 'park', minzoom=9,
              paint={'line-color': c['national-park-line'], 'line-width': zoom_ramp(9, 0.5, 14, 1.5),
                     'line-dasharray': [3, 2], 'line-opacity': 0.6},
              emissive=0.2),
    ]
    out += [layer('landuse-' + key, 'fill', 'landuse', minzoom=9, filter=in_class(classes),
                  paint={**fill(key), 'fill-opacity': zoom_ramp(9, 0, 10, 1)}, emissive=0.25)
            for key, classes in LANDUSE]
    out += [
        layer('landuse-military-line', 'line', 'landuse', minzoom=12, filter=['==', get('class'), 'military'],
              paint={'line-color': c['military-line'], 'line-width': 1, 'line-dasharray': [4, 2]}, emissive=0.25),
        layer('pitch-outline', 'line', 'landuse', minzoom=15, filter=in_class(['pitch', 'playground']),
              paint={'line-color': c['pitch-line']}, emissive=0.15),
        layer('barrier', 'line', 'landcover', minzoom=15, filter=['==', get('class'), 'barrier'],
              paint={'line-color': c['barrier'], 'line-width': zoom_ramp(15, 0.5, 18, 1.5)}, emissive=0.15),
    ]
    return out
