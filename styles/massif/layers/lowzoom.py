"""Low-zoom landcover and ocean depth from the alpimaps bathymap archive (z0-6).

Optional: the style declares the `bathymap` source and an app that has no such archive leaves it
unreachable, so these layers draw nothing. On the SDK the archive is merged into the main tiles
(MergedMBVTTileDataSource) and the rules find `global_landcover` and `depth` by name.
"""
from lib import get, layer, zoom_ramp

SOURCE = 'bathymap'

# the archive's own draw order (its manifest lists them so); each class paints over the ones before
CLASSES = [('built', 'lowzoom-built'), ('glacier', 'glacier-low'), ('swamp', 'wetland-low'),
           ('marsh', 'wetland-low'), ('tree', 'wood-low'), ('crop', 'crop-low'), ('scrub', 'scrub-low'),
           ('heath', 'heath-low'), ('grass', 'grass-low'), ('sand', 'sand-low')]


def landcover(v):
    """Standard draws its landcover to z12 and fades it out as the detailed landuse arrives; this one
    hands over sooner, crossfading with OMT's landcover over z8-9 (land.py)."""
    c = v.palette
    return [layer('lowzoom-' + cls, 'fill', 'global_landcover', source=SOURCE, maxzoom=9,
                  filter=['==', get('class'), cls],
                  paint={'fill-color': c[key], 'fill-opacity': zoom_ramp(8, 1, 9, 0), 'fill-antialias': False},
                  emissive=0.2)
            for cls, key in CLASSES]


def depth(v):
    """Standard's water-depth: nested isobaths, each a darker veil over the shallower, gone by z8.
    Banded rather than Standard's ramp over min_depth, which CartoCSS cannot interpolate; the veils
    stacking is what darkens the deep sea anyway."""
    c = v.palette
    # min_depth 0 is the whole ocean: a regional tileset has no sea beyond its bounds, the archive does
    bands = [('water-depth-ocean', ['==', get('min_depth'), 0], 'water'),
             ('water-depth', ['all', ['>', get('min_depth'), 0], ['<', get('min_depth'), 7000]], 'depth-200'),
             ('water-depth-deep', ['>=', get('min_depth'), 7000], 'depth-7000')]
    return [layer(id, 'fill', 'depth', source=SOURCE, maxzoom=8, filter=filter,
                  paint={'fill-color': c[key], 'fill-antialias': False,
                         'fill-opacity': zoom_ramp(6, 1 if key == 'water' else 0.35, 8, 0)})
            for id, filter, key in bands]
