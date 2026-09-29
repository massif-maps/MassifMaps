from lib import get, layer, zoom_ramp


def extrusion(color, opacity):
    return layer('building-3d', 'fill-extrusion', 'building', minzoom=15,
                 paint={'fill-extrusion-color': color,
                        # Alpimaps' planetiler leaves out the 5 m it defaults to, OpenMapTiles writes it
                        'fill-extrusion-height': ['coalesce', get('render_height'), 5],
                        'fill-extrusion-base': ['coalesce', get('render_min_height'), 0]},
                 metadata={'massif:paint': {
                     'fill-extrusion-opacity': opacity,
                     'fill-extrusion-vertical-scale': zoom_ramp(15, 0, 15.3, 1),
                     'fill-extrusion-ambient-occlusion-intensity': 0.15,
                     'fill-extrusion-ambient-occlusion-ground-radius': zoom_ramp(17, 0, 17.8, 8)},
                     'massif:layout': {'fill-extrusion-edge-radius': 0.4}, 'massif:minzoom-param': 'building_min_zoom'})


def layers(v):
    if v.flags.get('mono'):
        # e-ink: outlined footprints, then half-clear blocks whose walls the lighting shades, so a
        # street still reads through them
        c = v.palette
        return [layer('building-flat', 'fill', 'building', minzoom=13, maxzoom=15,
                      paint={'fill-color': c['building'], 'fill-outline-color': c['building-outline']},
                      metadata={'massif:minzoom-param': 'building_min_zoom'}),
                extrusion(c['building'], 0.5)]
    return [
        layer('building', 'fill', 'building', minzoom=13, maxzoom=15,
              paint={'fill-color': 'hsl(40, 43%, 93%)', 'fill-outline-color': 'hsl(40, 25%, 85%)'},
              metadata={'massif:minzoom-param': 'building_min_zoom'}),
        # the SDK only: building_opacity looking straight down, so the tunnels show through;
        # opaque once the camera leans in
        extrusion('hsl(30, 43%, 93%)', ['interpolate', ['linear'], ['pitch'], 5, ['config', 'building_opacity'], 20, 1]),
    ]
