"""Named colours, the day values. A variant overrides entries; the rules only ever name them.

Taken from Mapbox Standard's day theme unless noted - its lightness steps are what make a busy
city read calmly, and the night comes from the scene light (emissive), not from a second palette.
"""

STREETS = {
    'land': 'hsl(20, 20%, 95%)',
    'residential': 'hsl(20, 7%, 97%)',
    'commercial': 'hsla(24, 100%, 94%, 1)',
    'industrial': 'hsl(230, 15%, 92%)',
    'education': 'hsl(40, 50%, 88%)',
    'hospital': 'hsl(0, 50%, 92%)',
    'airport': 'hsl(225, 60%, 92%)',
    'parking': 'hsl(20, 3%, 93%)',
    'military': 'hsl(0, 40%, 92%)',
    'military-line': 'hsl(0, 50%, 70%)',
    'pitch': 'hsl(103, 70%, 86%)',
    'pitch-line': 'hsl(115, 60%, 74%)',
    'park': 'hsl(115, 60%, 80%)',
    'national-park': 'hsl(115, 30%, 84%)',
    'national-park-line': 'hsl(115, 35%, 60%)',
    'cemetery': 'hsl(115, 50%, 84%)',
    'wood': 'hsla(115, 60%, 74%, 0.8)',
    'wood-low': 'hsl(115, 60%, 84%)',
    'grass': 'hsla(115, 60%, 87%, 0.6)',
    'scrub': 'hsla(115, 60%, 84%, 0.6)',
    'farmland': 'hsla(102, 60%, 86%, 0.6)',
    'wetland': 'hsl(115, 60%, 86%)',
    'sand': 'hsl(52, 65%, 86%)',
    # Standard's bare is near-white; in the Alps that erases every ridge, so rock stays a stone grey
    'rock': 'hsl(30, 10%, 88%)',
    'glacier': 'hsl(200, 70%, 95%)',
    'barrier': 'hsl(20, 10%, 70%)',
    'landcover-outline': 'hsla(0, 0%, 0%, 0.3)',
    # low zoom, from Standard's landcover and water-depth
    'lowzoom-built': 'hsl(20, 12%, 91%)',
    'glacier-low': 'hsl(0, 0%, 100%)',
    'wetland-low': 'hsl(115, 60%, 86%)',
    'crop-low': 'hsl(102, 60%, 86%)',
    'scrub-low': 'hsl(115, 60%, 92%)',
    'heath-low': 'hsl(90, 50%, 90%)',
    'grass-low': 'hsl(90, 60%, 89%)',
    'sand-low': 'hsl(45, 55%, 91%)',
    'depth-200': 'hsl(200, 100%, 72%)',
    'depth-7000': 'hsl(200, 100%, 64%)',
    'water': 'hsl(200, 100%, 80%)',
    # a stream is a line a pixel or two wide: in the water's own tint it vanishes
    'waterway': 'hsl(205, 75%, 66%)',
    'water-night': 'hsl(200, 100%, 62%)',
    'water-label': 'hsl(205, 60%, 30%)',
    'water-halo': 'hsla(0, 0%, 100%, 0.55)',
    'aeroway': 'hsl(225, 52%, 87%)',
    'boundary-country': 'hsl(345, 100%, 70%)',
    'boundary-state': 'hsl(345, 100%, 75%)',
    'boundary-minor': 'hsl(345, 25%, 70%)',
    'boundary-halo': 'hsl(345, 100%, 100%)',
    'road-label': 'hsl(0, 0%, 25%)', 'road-label-halo': 'hsl(0, 0%, 95%)',
    'road-label-night': 'hsl(0, 0%, 90%)', 'road-label-halo-night': 'hsl(0, 0%, 30%)',
    'label': 'hsl(0, 0%, 15%)',
    'label-soft': 'hsla(0, 0%, 0%, 0.6)',
    'label-natural': 'hsl(210, 20%, 40%)',
    'label-park': 'hsl(115, 45%, 28%)',
    'label-airport': 'hsl(225, 60%, 50%)',
    'halo': 'hsl(0, 0%, 100%)',
    # MapTiler outdoor's: a contour number reads over the relief without a white smear round it
    'contour-halo': 'hsla(0, 0%, 100%, 0.8)',
    'halo-night': 'hsl(0, 0%, 5%)',
    'cliff': 'hsl(15, 10%, 45%)',
    'label-night': 'hsl(0, 0%, 92%)',
    'housenumber': 'hsl(20, 0%, 50%)',
    # Standard's grey-blue, stepped by class the way MapTiler steps its yellows: the busier the road,
    # the deeper its fill; a casing 10 points darker, a bridge's 10 more
    'motorway': 'hsl(214, 23%, 70%)', 'motorway-case': 'hsl(214, 23%, 60%)', 'motorway-bridge-case': 'hsl(214, 23%, 50%)',
    'trunk': 'hsl(235, 20%, 70%)', 'trunk-case': 'hsl(235, 20%, 60%)', 'trunk-bridge-case': 'hsl(235, 20%, 50%)',
    'primary': 'hsl(224, 26%, 74%)', 'secondary': 'hsl(224, 25%, 82%)',
    # Standard fills secondary and tertiary alike; a key apart (a hair apart) lets an extending style
    # draw a tertiary white under OSM's yellow secondary
    'tertiary': 'hsl(224, 25%, 82.5%)',
    'road': 'hsl(224, 20%, 90%)', 'road-case': 'hsl(224, 22%, 72%)', 'road-bridge-case': 'hsl(224, 25%, 60%)',
    # Standard cases every road alike; its own key per class (a hair apart, so each is its own palette
    # name) is what lets an extending style case a primary as OSM does
    'primary-case': 'hsl(224, 22%, 71%)', 'secondary-case': 'hsl(224, 22%, 71.5%)',
    'tertiary-case': 'hsl(224, 22%, 72.5%)',
    'road-low': 'hsl(224, 25%, 80%)',
    # minor and service roads below z14: a hairline at road-low vanished over the countryside
    'minor-low': 'hsl(224, 12%, 62%)',
    # secondary and tertiary below z14: the road-low grey, its own key (a hair apart) so an extending
    # style can keep OSM's yellow at every zoom
    'secondary-low': 'hsl(224, 25%, 79.5%)',
    'path': 'hsl(295, 10%, 97%)',
    'path-z16': 'hsl(295, 10%, 93%)',
    'path-case': 'hsl(0, 10%, 80%)',
    # OSM Carto's footway, for `path_osm`
    'path-osm': 'hsl(6, 93%, 71%)',
    'path-osm-case': 'hsla(0, 0%, 100%, 0.5)',
    'cycleway': 'hsl(125, 50%, 55%)',
    'bridleway': 'hsl(30, 40%, 55%)',
    'track': 'hsl(30, 55%, 35%)', 'track-fill': 'hsl(0, 0%, 100%)',
    'via-ferrata': 'hsl(52, 100%, 55%)', 'via-ferrata-case': 'hsl(25, 60%, 22%)',
    'no-access': 'hsla(0, 70%, 65%, 0.7)',
    'construction': 'hsl(224, 25%, 80%)',
    'ferry': 'hsl(209, 80%, 60%)',
    'aerialway': 'hsl(225, 60%, 58%)',
    # a cable car or a ski lift in rail's ink; a zip line keeps the aerialway blue
    'aerialway-lift': 'hsl(0, 0%, 15%)',
    'power': 'hsl(0, 0%, 48%)',
    'rail': 'hsl(0, 0%, 65%)',
    'rail-night': 'hsl(0, 0%, 30%)',
    'rail-emphasis': 'hsl(0, 0%, 35%)',
    'bridge-case': 'hsl(224, 25%, 70%)',
    'bridge-shadow': 'hsl(224, 25%, 60%)',
    'tunnel-case': 'hsl(224, 25%, 70%)',
    'oneway': 'hsl(224, 20%, 45%)',
    'way-label': 'hsl(0, 0%, 35%)',
    'track-label': 'hsl(30, 50%, 30%)',
}

# Outdoor: MapTiler outdoor's warmer ground and deeper woods, so relief and trails read on top
OUTDOOR = {
    **STREETS,
    'land': 'hsl(40, 25%, 94%)',
    'residential': 'hsl(35, 15%, 91%)',
    'wood': 'hsla(105, 42%, 64%, 0.75)',
    'wood-low': 'hsl(105, 42%, 78%)',
    'grass': 'hsla(90, 45%, 80%, 0.6)',
    'scrub': 'hsla(95, 40%, 74%, 0.6)',
    'rock': 'hsl(30, 8%, 84%)',
    'glacier': 'hsl(200, 60%, 96%)',
    'relief': 0.35,
    'hillshade-shadow': 'hsl(30, 10%, 30%)',
    'hillshade-highlight': 'hsl(40, 30%, 97%)',
    'hillshade-accent': 'hsl(30, 15%, 45%)',
    'contour': 'hsl(35, 35%, 52%)',
    'contour-index': 'hsl(35, 38%, 42%)',
    'contour-label': 'hsl(35, 38%, 30%)',
    'cliff': 'hsl(15, 10%, 45%)',
    'trail': 'hsl(5, 72%, 45%)',
    'trail-alpine': 'hsl(215, 70%, 42%)',
    'route-hiking': 'hsl(330, 70%, 55%)',
    'route-bicycle': 'hsl(215, 80%, 55%)',
    'mtb-easy': 'hsl(130, 60%, 35%)',
    'mtb-medium': 'hsl(215, 80%, 45%)',
    'mtb-hard': 'hsl(0, 75%, 45%)',
    'mtb-extreme': 'hsl(0, 0%, 10%)',
}

# every variant carries the relief and the contours, off by default but outdoor and topo's: an app
# turns them on
STREETS.update({k: OUTDOOR[k] for k in ('relief', 'hillshade-shadow', 'hillshade-highlight', 'hillshade-accent',
                                         'contour', 'contour-index', 'contour-label')})

# Topo: MapTiler topo's printed-map look - grey ground, olive woods, brown contours, a deeper water
# and a stronger relief - over the outdoor layers, so on the SDK it is colours and no new rule
TOPO = {
    **OUTDOOR,
    'land': 'hsl(0, 0%, 94%)',
    'residential': 'hsl(45, 10%, 86%)',
    'commercial': 'hsl(30, 20%, 88%)',
    'industrial': 'hsl(230, 8%, 87%)',
    'wood': 'hsl(74, 31%, 72%)',
    'wood-low': 'hsl(74, 31%, 80%)',
    'grass': 'hsl(79, 35%, 83%)',
    'scrub': 'hsl(75, 37%, 78%)',
    'farmland': 'hsl(51, 34%, 88%)',
    'park': 'hsl(79, 35%, 78%)',
    'sand': 'hsl(54, 70%, 85%)',
    'rock': 'hsl(20, 6%, 82%)',
    'glacier': 'hsl(0, 0%, 100%)',
    'water': 'hsl(199, 44%, 62%)',
    'waterway': 'hsl(199, 50%, 50%)',
    'water-label': 'hsl(205, 60%, 18%)',
    'water-halo': 'hsla(0, 0%, 100%, 0.7)',
    'relief': 0.5,
    'hillshade-shadow': 'hsl(0, 0%, 35%)',
    'hillshade-highlight': 'hsl(51, 12%, 90%)',
    'hillshade-accent': 'hsl(40, 30%, 45%)',
    'contour': 'hsl(22, 35%, 58%)',
    'contour-index': 'hsl(22, 35%, 48%)',
    'contour-label': 'hsl(20, 28%, 32%)',
}

# Hybrid: imagery is the ground, so the roads are translucent lines over it and every label is pale
# ink on a dark halo, by day as by night - Standard satellite's and MapTiler hybrid's arrangement
_WHITE, _DARK = 'hsl(0, 0%, 100%)', 'hsla(0, 0%, 0%, 0.75)'
HYBRID = {
    **STREETS,
    'draw-once': True,
    'land': 'transparent',
    # black vanishes on imagery: lifts keep the aerialway blue
    'aerialway-lift': 'hsl(225, 60%, 58%)',
    'power': 'hsla(0, 0%, 90%, 0.6)',
    'cliff': 'hsla(0, 0%, 90%, 0.8)',
    'motorway': 'hsla(38, 95%, 70%, 0.85)', 'motorway-case': 'hsla(30, 60%, 25%, 0.5)',
    'motorway-bridge-case': 'hsla(30, 60%, 20%, 0.6)',
    'trunk': 'hsla(45, 95%, 75%, 0.8)', 'trunk-case': 'hsla(35, 50%, 25%, 0.45)',
    'trunk-bridge-case': 'hsla(35, 50%, 20%, 0.55)',
    'primary': 'hsla(50, 90%, 85%, 0.7)',
    'secondary': 'hsla(50, 90%, 92%, 0.6)', 'tertiary': 'hsla(50, 90%, 92%, 0.6)', 'road': 'hsla(0, 0%, 100%, 0.55)', 'road-low': 'hsla(0, 0%, 100%, 0.55)', 'minor-low': 'hsla(0, 0%, 100%, 0.55)', 'secondary-low': 'hsla(0, 0%, 100%, 0.55)', 'road-case': 'hsla(0, 0%, 0%, 0.25)',
    'primary-case': 'hsla(0, 0%, 0%, 0.25)', 'secondary-case': 'hsla(0, 0%, 0%, 0.25)',
    'tertiary-case': 'hsla(0, 0%, 0%, 0.25)',
    'road-bridge-case': 'hsla(0, 0%, 0%, 0.4)',
    'path': 'hsla(0, 0%, 100%, 0.4)', 'path-z16': 'hsla(0, 0%, 100%, 0.5)', 'path-case': 'hsla(0, 0%, 0%, 0.2)',
    'track': 'hsla(35, 70%, 75%, 0.8)', 'track-fill': 'hsla(0, 0%, 100%, 0.3)',
    'rail': 'hsla(0, 0%, 85%, 0.8)', 'rail-night': 'hsla(0, 0%, 85%, 0.8)',
    'boundary-country': 'hsl(345, 100%, 80%)', 'boundary-state': 'hsl(345, 80%, 85%)',
    'boundary-minor': 'hsla(345, 40%, 85%, 0.8)', 'boundary-halo': 'hsla(0, 0%, 0%, 0.4)',
    'road-label': _WHITE, 'road-label-halo': _DARK, 'road-label-night': _WHITE, 'road-label-halo-night': _DARK,
    'label': _WHITE, 'label-night': _WHITE, 'halo': _DARK, 'halo-night': _DARK, 'contour-halo': _DARK, 'water-halo': _DARK,
    'label-soft': 'hsl(0, 0%, 90%)', 'label-natural': 'hsl(0, 0%, 92%)', 'label-park': 'hsl(100, 60%, 85%)',
    'label-airport': 'hsl(225, 80%, 88%)', 'water-label': 'hsl(200, 80%, 85%)', 'housenumber': 'hsl(0, 0%, 88%)',
    'oneway': 'hsl(0, 0%, 90%)',
    'way-label': _WHITE, 'track-label': _WHITE,
    'contour': 'hsla(0, 0%, 100%, 0.45)', 'contour-index': 'hsla(0, 0%, 100%, 0.65)', 'contour-label': _WHITE,
}

# E-ink: black, white and a few greys, the classes told apart by pattern (sprite-src/pattern/) and
# the roads by the weight of their outline. No night: a still page is read under a lamp.
_K, _W = 'hsl(0, 0%, 0%)', 'hsl(0, 0%, 100%)'
EINK = {k: v for k, v in OUTDOOR.items()}
EINK.update({
    # the flat greys under the patterns, and alone below the zoom a pattern starts at
    'wood': 'hsl(0, 0%, 92%)', 'wood-low': 'hsl(0, 0%, 93%)', 'scrub': 'hsl(0, 0%, 94%)',
    'grass': 'hsl(0, 0%, 97%)', 'park': 'hsl(0, 0%, 95%)', 'wetland': 'hsl(0, 0%, 96%)',
    'rock': 'hsl(0, 0%, 93%)', 'sand': 'hsl(0, 0%, 96%)', 'glacier': 'hsl(0, 0%, 90%)', 'cemetery': 'hsl(0, 0%, 95%)',
    'military': 'hsl(0, 0%, 97%)', 'landcover-outline': 'hsl(0, 0%, 45%)',
    'waterway': 'hsl(0, 0%, 40%)', 'waterway-bed': 'hsl(0, 0%, 84%)', 'water': _W, 'water-night': _W, 'shoreline': 'hsl(0, 0%, 50%)', 'coastline': 'hsl(0, 0%, 20%)',
    'line-halo': _W, 'track-scale': 1.6, 'trail': 'hsl(0, 0%, 25%)', 'trail-alpine': 'hsl(0, 0%, 35%)',
    'mtb-easy': 'hsl(0, 0%, 25%)', 'mtb-medium': 'hsl(0, 0%, 25%)', 'mtb-hard': 'hsl(0, 0%, 25%)', 'mtb-extreme': 'hsl(0, 0%, 25%)',
    'contour': 'hsl(0, 0%, 76%)', 'contour-index': 'hsl(0, 0%, 58%)', 'contour-label': 'hsl(0, 0%, 30%)',
    'hillshade-shadow': 'hsl(0, 0%, 30%)', 'hillshade-highlight': _W, 'hillshade-accent': 'hsl(0, 0%, 45%)',
    'cliff': 'hsl(0, 0%, 20%)', 'way-label': _K, 'track-label': _K,
})
EINK.update({
    # the page stays white: a landuse is told by its edge (polygons_border), not a tint
    'land': _W, 'residential': _W, 'commercial': _W,
    'industrial': _W, 'education': _W, 'hospital': _W,
    'airport': _W, 'parking': _W, 'pitch': _W,
    'pitch-line': 'hsl(0, 0%, 50%)', 'military-line': 'hsl(0, 0%, 30%)', 'farmland': _W,
    'national-park': 'hsl(0, 0%, 97%)', 'national-park-line': 'hsl(0, 0%, 35%)',
    'wood-low': 'hsl(0, 0%, 93%)', 'barrier': 'hsl(0, 0%, 40%)',
    'water-label': _K,
    'aeroway': 'hsl(0, 0%, 80%)',
    'lowzoom-built': 'hsl(0, 0%, 93%)', 'glacier-low': _W, 'wetland-low': 'hsl(0, 0%, 94%)',
    'crop-low': _W, 'scrub-low': 'hsl(0, 0%, 96%)', 'heath-low': 'hsl(0, 0%, 96%)',
    'grass-low': 'hsl(0, 0%, 97%)', 'sand-low': 'hsl(0, 0%, 97%)', 'depth-200': 'hsl(0, 0%, 96%)',
    'depth-7000': 'hsl(0, 0%, 93%)',
    'motorway': _W, 'motorway-case': 'hsl(0, 0%, 35%)', 'motorway-bridge-case': 'hsl(0, 0%, 30%)',
    'trunk': _W, 'trunk-case': 'hsl(0, 0%, 35%)', 'trunk-bridge-case': 'hsl(0, 0%, 30%)',
    'primary': _W, 'secondary': _W, 'tertiary': _W, 'road': _W, 'road-low': 'hsl(0, 0%, 50%)', 'minor-low': 'hsl(0, 0%, 50%)', 'secondary-low': 'hsl(0, 0%, 50%)', 'casing-low': True, 'casing-from': 13, 'road-case': 'hsl(0, 0%, 50%)',
    'primary-case': 'hsl(0, 0%, 45%)', 'secondary-case': 'hsl(0, 0%, 50%)', 'tertiary-case': 'hsl(0, 0%, 50%)', 'road-bridge-case': 'hsl(0, 0%, 40%)', 'casing-scale': 1.8,
    'path': _W, 'path-z16': _W, 'path-case': 'hsl(0, 0%, 40%)', 'path-osm': 'hsl(0, 0%, 25%)', 'path-osm-case': _W, 'cycleway': 'hsl(0, 0%, 35%)',
    'bridleway': 'hsl(0, 0%, 45%)', 'track': 'hsl(0, 0%, 30%)', 'track-fill': _W, 'via-ferrata': _W, 'via-ferrata-case': 'hsl(0, 0%, 25%)',
    'no-access': 'hsla(0, 0%, 0%, 0.6)', 'construction': 'hsl(0, 0%, 60%)', 'ferry': 'hsl(0, 0%, 30%)',
    'aerialway': _K, 'aerialway-lift': _K, 'power': 'hsl(0, 0%, 45%)', 'rail': 'hsl(0, 0%, 30%)', 'rail-night': 'hsl(0, 0%, 30%)',
    'boundary-country': _K, 'boundary-state': 'hsl(0, 0%, 25%)', 'boundary-minor': 'hsl(0, 0%, 45%)',
    'boundary-halo': 'hsl(0, 0%, 85%)', 'building': 'hsl(0, 0%, 90%)', 'building-outline': 'hsl(0, 0%, 40%)',
    'road-label': _K, 'road-label-halo': _W, 'road-label-night': _K, 'road-label-halo-night': _W,
    'label': _K, 'label-night': _K, 'halo': _W, 'halo-night': _W, 'contour-halo': _W, 'water-halo': _W, 'label-soft': 'hsl(0, 0%, 25%)',
    'label-natural': 'hsl(0, 0%, 15%)', 'label-park': 'hsl(0, 0%, 15%)', 'label-airport': _K,
    'housenumber': 'hsl(0, 0%, 35%)', 'oneway': _K, 'oneway-arrow': 'dark', 'peak-icon': 'mono',
})

VARIANTS = {'streets': STREETS, 'outdoor': OUTDOOR, 'topo': TOPO, 'hybrid': HYBRID, 'eink': EINK}
