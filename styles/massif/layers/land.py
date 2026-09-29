from lib import gate, get, in_class, layer, zoom_ramp
from params import PARAMS

# e-ink's textures (sprite-src/pattern/) over the flat grey, from the zoom each says something at
# (Alpimaps' e-ink zooms): a pattern is a textured fill per tile, and at z8 a wood's trees are noise
# the four an app may move, as Alpimaps' style lets it
PATTERN_PARAMS = {'wood': 'forest_pattern_zoom', 'scrub': 'scrub_pattern_zoom', 'rock': 'rock_pattern_zoom',
                  'wetland': 'wetland_pattern_zoom'}
PATTERNS = {'sand': 12, 'glacier': 11, 'military': 13, 'grass': 14, 'park': 14, 'cemetery': 14,
            **{key: PARAMS[name]['default'] for key, name in PATTERN_PARAMS.items()}}

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
    # Alpimaps' planetiler files leisure=park here, stock OpenMapTiles as a grass subclass
    ('park', ['park', 'garden', 'stadium', 'theme_park', 'zoo']),
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
    mono = v.flags.get('mono', False)
    fill = lambda key: {'fill-color': c[key], 'fill-antialias': False}

    def patterned(id, key, source, minzoom, filter):
        if not (mono and key in PATTERNS):
            return []
        moved = key in PATTERN_PARAMS
        return [layer(id + '-pattern', 'fill', source, minzoom=minzoom if moved else max(minzoom, PATTERNS[key]),
                      filter=filter, metadata={'massif:minzoom-param': PATTERN_PARAMS[key]} if moved else None,
                      paint={'fill-pattern': 'pattern-' + key})]
    out = [
        layer('landuse-residential', 'fill', 'landuse', minzoom=6, filter=in_class(RESIDENTIAL),
              paint={'fill-color': c['residential'], 'fill-opacity': zoom_ramp(6, 0, 9, 1)}, emissive=0.25),
    ]
    # below z8 the bathymap archive's generalised landcover draws instead (lowzoom.py), and the two
    # crossfade; Standard's woods are a paler green that deepens as the forests resolve into stands
    fade = zoom_ramp(8, 0, 9, 1)
    wood = {'fill-color': zoom_ramp(8, c['wood-low'], 11, c['wood']), 'fill-antialias': False}
    def opacity(key, ramp):
        # e-ink hands the flat grey over to the pattern: ground stays white where there is texture
        if not (mono and key in PATTERNS):
            return {'fill-opacity': ramp or 1}
        stops = ramp[3:] if ramp else [PATTERNS[key] - 2, 1]
        return {'fill-opacity': zoom_ramp(*stops, PATTERNS[key] - 1, stops[-1], PATTERNS[key], 0)}

    for key, classes in LANDCOVER:
        out.append(layer('landcover-' + key, 'fill', 'landcover', minzoom=8, filter=in_class(classes),
                         paint={**(wood if key == 'wood' else fill(key)), **opacity(key, fade)}, emissive=0.2))
        out += patterned('landcover-' + key, key, 'landcover', 8, in_class(classes))
    for key, subclasses in LANDCOVER_SUBCLASS:
        out.append(layer('landcover-' + key + '-sub', 'fill', 'landcover', minzoom=10,
                         filter=in_class(subclasses, 'subclass'),
                         paint={**fill(key), **opacity(key, None)}, emissive=0.2))
        out += patterned('landcover-' + key + '-sub', key, 'landcover', 10, in_class(subclasses, 'subclass'))
    # `polygons_border`, on for e-ink: a texture alone does not say where a wood or its clearing
    # stops; dotted, as Swisstopo and IGN edge a wood, so it is not read as a contour
    out.append(gate(layer('landcover-outline', 'line', 'landcover', minzoom=12,
                          filter=['==', ['geometry-type'], 'Polygon'],
                          paint={'line-color': c['landcover-outline'], 'line-width': zoom_ramp(12, 0.6, 16, 1),
                                 'line-dasharray': [1, 2]}), v, 'polygons_border'))
    out += [
        layer('park', 'fill', 'park', minzoom=5,
              paint={'fill-color': c['national-park'], 'fill-opacity': zoom_ramp(5, 0, 6, 0.5, 13, 0.3)},
              emissive=0.2),
        layer('park-outline', 'line', 'park', minzoom=9,
              paint={'line-color': c['national-park-line'], 'line-width': zoom_ramp(9, 0.5, 14, 1.5),
                     'line-dasharray': [3, 2], 'line-opacity': 0.6},
              emissive=0.2),
    ]
    for key, classes in LANDUSE:
        out.append(layer('landuse-' + key, 'fill', 'landuse', minzoom=9, filter=in_class(classes),
                         paint={**fill(key), **opacity(key, zoom_ramp(9, 0, 10, 1))}, emissive=0.25))
        out += patterned('landuse-' + key, key, 'landuse', 9, in_class(classes))
    out.append(gate(layer('landuse-outline', 'line', 'landuse', minzoom=13,
                          filter=['all', ['==', ['geometry-type'], 'Polygon'], ['!', in_class(RESIDENTIAL)]],
                          paint={'line-color': c['landcover-outline'], 'line-width': zoom_ramp(13, 0.4, 16, 0.8)}),
                    v, 'polygons_border'))
    out += [
        layer('landuse-military-line', 'line', 'landuse', minzoom=12, filter=['==', get('class'), 'military'],
              paint={'line-color': c['military-line'], 'line-width': 1, 'line-dasharray': [4, 2]}, emissive=0.25),
        layer('pitch-outline', 'line', 'landuse', minzoom=15, filter=in_class(['pitch', 'playground']),
              paint={'line-color': c['pitch-line']}, emissive=0.15),
        layer('barrier', 'line', 'landcover', minzoom=15, filter=['==', get('class'), 'barrier'],
              paint={'line-color': c['barrier'], 'line-width': zoom_ramp(15, 0.5, 18, 1.5)}, emissive=0.15),
    ]
    return out
