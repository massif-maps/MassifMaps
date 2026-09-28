"""What the outdoor and topo variants add: relief, contours, cliffs, trails by difficulty and the
waymarked routes. `dem`, `contours` and `routes` are optional sources like the bathymap - on the SDK
the relief is an app layer (HillshadeRasterTileLayer) and the contours come from
ContourTileDataSource or the prebaked archive, both with the `contour` layer's `ele` and `div`."""
from lib import by_hour, get, in_class, layer, zoom_ramp

SAC = [
    # (id, sac_scale values, colour key, dash): red to T3, blue from T4, the dash tightening with it
    ('trail-t1', ['hiking'], 'trail', [4, 2]),
    ('trail-t2', ['mountain_hiking'], 'trail', [2.5, 1.5]),
    ('trail-t3', ['demanding_mountain_hiking'], 'trail', [1, 1.5]),
    ('trail-t4', ['alpine_hiking'], 'trail-alpine', [2.5, 1.5]),
    ('trail-t5', ['demanding_alpine_hiking'], 'trail-alpine', [1, 1.5]),
    # its own layer rather than a second set in T5's filter: two sets in one filter are a when()
    ('trail-t6', ['difficult_alpine_hiking'], 'trail-alpine', [1, 1.5]),
]
TRAIL_WIDTH = zoom_ramp(12, 0.6, 15, 1.4, 18, 2.6, base=1.3)
TRAILS = ['path', 'bridleway']


def hillshade(v):
    c = v.palette
    # gone by z16 as in Standard: in a street the relief is noise, and on the SDK a raster over the
    # vector layer would grey the whole city
    return [{'id': 'hillshade', 'type': 'hillshade', 'source': 'dem', 'maxzoom': 16,
             'paint': {'hillshade-exaggeration': zoom_ramp(6, 0.45, 14, 0.35, 16, 0),
                       'hillshade-shadow-color': c['hillshade-shadow'],
                       'hillshade-highlight-color': c['hillshade-highlight'],
                       'hillshade-accent-color': c['hillshade-accent']},
             # what an app gives its HillshadeRasterTileLayer to match: CartoCSS cannot draw a raster
             'metadata': {'massif:sdk-layer': {'type': 'hillshade', 'exaggeration': 0.35, 'opacity': 0.55,
                                                'visibleZoomRange': [0, 16]}}}]


def contours(v):
    c = v.palette
    major = ['>=', get('div'), 100]
    return [
        layer('contour', 'line', 'contour', source='contours', minzoom=12, filter=['<', get('div'), 100],
              paint={'line-color': c['contour'], 'line-width': zoom_ramp(12, 0.4, 16, 0.9),
                     'line-opacity': zoom_ramp(12, 0.3, 14, 0.5)}, emissive=0.3),
        layer('contour-index', 'line', 'contour', source='contours', minzoom=11, filter=major,
              paint={'line-color': c['contour-index'], 'line-width': zoom_ramp(11, 0.6, 16, 1.4),
                     'line-opacity': zoom_ramp(11, 0.35, 14, 0.6)}, emissive=0.3),
    ]


def contour_labels(v):
    c = v.palette
    return [layer('contour-label', 'symbol', 'contour', source='contours', minzoom=13,
                  filter=['>=', get('div'), 100],
                  layout={'symbol-placement': 'line', 'text-field': ['to-string', get('ele')],
                          'text-font': ['Noto Sans Italic'], 'text-size': zoom_ramp(13, 9, 17, 11),
                          'text-padding': 4, 'text-max-angle': 25},
                  paint={'text-color': c['contour-label'], 'text-halo-color': c['halo'], 'text-halo-width': 1},
                  metadata={'massif:paint': {'text-color': by_hour(c['label-night'], c['contour-label']),
                                             'text-halo-color': by_hour(c['halo-night'], c['halo'])}})]


def cliffs(v):
    """MapTiler's two-layer cliff: the edge, and short teeth offset to the drop side."""
    c = v.palette
    edge = ['all', ['==', get('class'), 'cliff'], ['==', ['geometry-type'], 'LineString']]
    return [
        layer('cliff', 'line', 'mountain_peak', minzoom=13, filter=edge,
              paint={'line-color': c['cliff'], 'line-width': zoom_ramp(13, 0.6, 18, 1.6),
                     'line-opacity': zoom_ramp(13, 0.5, 16, 0.9)}, emissive=0.3),
        layer('cliff-teeth', 'line', 'mountain_peak', minzoom=15, filter=edge,
              paint={'line-color': c['cliff'], 'line-width': zoom_ramp(15, 2, 20, 6),
                     'line-offset': zoom_ramp(15, 1.2, 20, 3.5), 'line-dasharray': [0.2, 1],
                     'line-opacity': 0.7}, emissive=0.3),
    ]


def trails(c, brunnel_test):
    return [layer(id, 'line', 'transportation', minzoom=12,
                  filter=['all', in_class(TRAILS, 'subclass'), brunnel_test,
                          ['in', get('sac_scale'), ['literal', values]] if id != 'trail-t1' else
                          ['!', ['in', get('sac_scale'), ['literal', [v for _, vs, _, _ in SAC[1:] for v in vs]]]]],
                  layout={'line-join': 'round'},
                  paint={'line-color': c[key], 'line-width': TRAIL_WIDTH, 'line-dasharray': dash},
                  emissive=0.4)
            for id, values, key, dash in SAC]


def routes(v):
    """Waymarked Trails' convention: a wide translucent band under the way, by network rank."""
    c = v.palette
    width = zoom_ramp(9, ['match', get('network'), [1, 2], 2, 1.2], 14, ['match', get('network'), [1, 2], 5, 3.5],
                      18, 8)
    return [layer('route-' + cls, 'line', 'route', source='routes', minzoom=9, filter=['==', get('class'), cls],
                  layout={'line-join': 'round', 'line-cap': 'round'},
                  paint={'line-color': c['route-' + cls], 'line-width': width,
                         'line-opacity': zoom_ramp(9, 0.5, 14, 0.4)}, emissive=0.5)
            for cls in ('bicycle', 'hiking')]
