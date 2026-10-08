"""What the outdoor and topo variants add: relief, contours, cliffs, trails by difficulty and the
waymarked routes. `dem`, `contours` and `routes` are optional sources like the bathymap - on the SDK
the relief is an app layer (HillshadeRasterTileLayer) and the contours come from
ContourTileDataSource or the prebaked archive, both with the `contour` layer's `ele` and `div`."""
from lib import by_hour, gate, get, halo, hex_color, in_class, layer, scaled, zoom_ramp

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
# Alpimaps' planetiler writes a scale's index in OSM's list (hiking = 0), OpenMapTiles the name
SAC_ORDER = ['hiking', 'mountain_hiking', 'demanding_mountain_hiking', 'alpine_hiking', 'demanding_alpine_hiking',
             'difficult_alpine_hiking']
SAC = [(id, values + [SAC_ORDER.index(v) for v in values], key, dash) for id, values, key, dash in SAC]
TRAIL_WIDTH = zoom_ramp(12, 0.6, 15, 1.4, 18, 2.6, base=1.3)
TRAILS = ['path', 'bridleway']

# mtb:scale by the colour of a French VTT waymark, and a dash that says the same on e-ink;
# a thin line beside the path rather than on it, which already says the hiking difficulty
MTB = [
    ('mtb-easy', ['0-', '0', '0+', '1-', '1', '1+'], 'mtb-easy', None),
    ('mtb-medium', ['2-', '2', '2+'], 'mtb-medium', [4, 1.5]),
    ('mtb-hard', ['3-', '3', '3+'], 'mtb-hard', [2, 1.5]),
    ('mtb-extreme', ['4-', '4', '4+', '5-', '5', '5+', '6'], 'mtb-extreme', [1, 1.5]),
]


def or_param(name, auto):
    """the variant's `auto` value until an app sets parameter `name`: `auto` as a string, a negative as a number"""
    value = ['config', name]
    return ['case', ['<', value, 0], auto, value] if isinstance(auto, (int, float)) else ['match', value, 'auto', auto, value]


def hillshade(v):
    c = v.palette
    # gone by z16 as in Standard: in a street the relief is noise, and on the SDK a raster over the
    # vector layer would grey the whole city. The SDK slot ends at `hillshade_max_zoom` instead.
    return [{'id': 'hillshade', 'type': 'hillshade', 'source': 'dem', 'maxzoom': 16,
             'paint': {'hillshade-exaggeration': zoom_ramp(6, c['relief'] + 0.1, 14, c['relief'], 16, 0),
                       'hillshade-shadow-color': c['hillshade-shadow'],
                       'hillshade-highlight-color': c['hillshade-highlight'],
                       'hillshade-accent-color': c['hillshade-accent']},
             # the HillshadeRasterTileLayer drawing the same as the paint above: MapLibre's exaggeration is its
             # contrast, and heightScale 1 is MapLibre's slope (docs/features/hillshade.md#matching-maplibre)
             'metadata': {'massif:sdk-layer': {'type': 'hillshade', 'hillshadeMethod': 'STANDARD', 'contrast': c['relief'],
                                                'heightScale': 1, 'shadowColor': hex_color(c['hillshade-shadow']),
                                                'highlightColor': hex_color(c['hillshade-highlight']),
                                                'accentColor': hex_color(c['hillshade-accent']),
                                                'visibleZoomRange': [0, 16]},
                          # the composite slot reads these over parameters, so an app's choice survives a variant change
                          'massif:sdk-slot': {'hillshadeMethod': or_param('hillshade_method', 'standard'),
                                              'contrast': or_param('hillshade_contrast', c['relief']),
                                              'heightScale': or_param('hillshade_height_scale', 1),
                                              'shadowColor': or_param('hillshade_shadow_color', hex_color(c['hillshade-shadow'])),
                                              'highlightColor': or_param('hillshade_highlight_color', hex_color(c['hillshade-highlight'])),
                                              'accentColor': or_param('hillshade_accent_color', hex_color(c['hillshade-accent']))},
                          'massif:maxzoom-param': 'hillshade_max_zoom'}}]


def faded(*stops):
    """a contour opacity ramp, and the SDK's copy scaled by `contour_opacity`"""
    live = [x if i % 2 == 0 else ['*', x, ['config', 'contour_opacity']] for i, x in enumerate(stops)]
    return {'paint': {'line-opacity': zoom_ramp(*stops)}, 'metadata': {'massif:paint': {'line-opacity': zoom_ramp(*live)}}}


def contours(v):
    c = v.palette
    major = ['>=', get('div'), 100]
    minor, index = faded(12, 0.3, 14, 0.5), faded(11, 0.35, 14, 0.6)
    return [
        layer('contour', 'line', 'contour', source='contours', minzoom=12, filter=['<', get('div'), 100],
              paint={'line-color': c['contour'], 'line-width': zoom_ramp(12, 0.4, 16, 0.9), **minor['paint']},
              metadata=minor['metadata'], emissive=0.3),
        layer('contour-index', 'line', 'contour', source='contours', minzoom=11, filter=major,
              paint={'line-color': c['contour-index'], 'line-width': zoom_ramp(11, 0.6, 16, 1.4), **index['paint']},
              metadata=index['metadata'], emissive=0.3),
    ]


def contour_labels(v):
    c = v.palette
    return [layer('contour-label', 'symbol', 'contour', source='contours', minzoom=13,
                  filter=['>=', get('div'), 100],
                  layout={'symbol-placement': 'line', 'text-field': ['to-string', get('ele')],
                          'text-font': 'italic', 'text-size': zoom_ramp(13, 9, 17, 11),
                          'text-padding': 4, 'text-max-angle': 25},
                  paint={'text-color': c['contour-label'], 'text-halo-color': c['contour-halo'], 'text-halo-width': 0.8},
                  metadata={'massif:paint': {'text-color': by_hour(c['label-night'], c['contour-label']),
                                             'text-halo-color': by_hour(c['halo-night'], c['contour-halo'])}})]


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


def trails(v, brunnel_test):
    c = v.palette
    width = scaled(TRAIL_WIDTH, c.get('track-scale', 1))
    out = halo(c, 'trail-halo', ['all', ['==', get('class'), 'path'], in_class(TRAILS, 'subclass'), brunnel_test],
               width, 'path_min_zoom')
    paved = ['==', get('surface'), 'paved']
    # one layer per subclass: with both scale spellings the scale is a set, and two sets are a when()
    for id, values, key, dash in SAC:
        for sub in TRAILS:
            scale = ['in', get('sac_scale'), ['literal', values]] if id != 'trail-t1' else \
                ['!', ['in', get('sac_scale'), ['literal', [v for _, vs, _, _ in SAC[1:] for v in vs]]]]
            # T1 split on the surface, so `path_osm` can draw a paved one as OSM Carto's footway
            for suffix, surface in ((('', ['!=', get('surface'), 'paved']), ('-paved', paved))
                                    if id == 'trail-t1' else (('', None),)):
                lay = layer(id + suffix + ('' if sub == 'path' else '-' + sub), 'line', 'transportation', minzoom=12,
                            filter=['all', ['==', get('subclass'), sub], brunnel_test, scale, *([surface] if surface else [])],
                            layout={'line-join': 'round'},
                            paint={'line-color': c[key], 'line-width': width, 'line-dasharray': dash},
                            metadata={'massif:minzoom-param': 'path_min_zoom'},
                            emissive=0.4)
                out.append(gate(lay, v, 'path_osm', 0) if suffix else lay)
    return out + osm_paved(v, brunnel_test)


def osm_paved(v, brunnel_test):
    """`path_osm`: a paved path no harder than T1 drawn as a footway (roads.osm_footway)"""
    from layers.roads import osm_footway
    scale = ['!', ['in', get('sac_scale'), ['literal', [v for _, vs, _, _ in SAC[1:] for v in vs]]]]
    test = ['all', ['==', get('class'), 'path'], ['==', get('subclass'), 'path'], ['==', get('surface'), 'paved'],
            scale, brunnel_test]
    return osm_footway(v.palette, 'path-osm-paved', test, 12, v)


def sac_labels(v):
    """`sac_scale_labels`, on for e-ink where a dash alone is hard to read: the grade (T1..T6) on a
    small upright plate along the trail, as a road carries its number. A path with no scale gets none."""
    c = v.palette
    return [gate(layer(id + '-label' + ('' if sub == 'path' else '-' + sub), 'symbol', 'transportation', minzoom=14,
                       filter=['all', ['==', get('subclass'), sub], ['in', get('sac_scale'), ['literal', values]]],
                       layout={'symbol-placement': 'line', 'symbol-spacing': 300, 'symbol-avoid-edges': True,
                               'icon-image': 'shield-plate-mono', 'icon-text-fit': 'both',
                               'icon-text-fit-padding': [0.5, 2, 0.5, 2], 'text-field': 'T%d' % grade,
                               'text-font': 'bold', 'text-size': 9, 'text-padding': 2,
                               'text-rotation-alignment': 'viewport', 'icon-rotation-alignment': 'viewport'},
                       paint={'text-color': c[key]}), v, 'sac_scale_labels')
            for grade, (id, values, key, _) in enumerate(SAC, 1) for sub in TRAILS]


def mtb(v, brunnel_test):
    """the MTB line beside the path, while `mtb_markings` is on"""
    c = v.palette
    out = []
    for id, values, key, dash in MTB:
        paint = {'line-color': c[key], 'line-width': zoom_ramp(14, 0.8, 18, 2),
                 'line-offset': zoom_ramp(14, 2.5, 18, 6, base=1.3)}
        if dash:
            paint['line-dasharray'] = dash
        out.append(gate(layer(id, 'line', 'transportation', minzoom=14,
                              filter=['all', brunnel_test, ['in', get('mtb_scale'), ['literal', values]]],
                              paint=paint, emissive=0.4), v, 'mtb_markings'))
    return out


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
