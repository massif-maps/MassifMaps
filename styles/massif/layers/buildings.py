from lib import by_hour, get, layer, zoom_ramp


# the SDK only: building_opacity looking straight down, so the tunnels show through; opaque once the
# camera leans in
OPACITY = ['interpolate', ['linear'], ['pitch'], 5, ['config', 'building_opacity'], 20, 1]


def extrusion(color, night=None):
    return layer('building-3d', 'fill-extrusion', 'building', minzoom=15, emissive=by_hour(0.1, 0),
                 paint={'fill-extrusion-color': color,
                        # Alpimaps' planetiler leaves out the 5 m it defaults to, OpenMapTiles writes it
                        'fill-extrusion-height': ['coalesce', get('render_height'), 5],
                        'fill-extrusion-base': ['coalesce', get('render_min_height'), 0]},
                 metadata={'massif:paint': {
                     # Standard's night block: its colour less 40 hue, 43 saturation and 35 lightness,
                     # with a tenth of it emitted at night only, so a block still stands off the dark street
                     'fill-extrusion-color': by_hour(night, color) if night else color,
                     'fill-extrusion-opacity': OPACITY,
                     'fill-extrusion-vertical-scale': zoom_ramp(15, 0, 15.3, 1),
                     'fill-extrusion-ambient-occlusion-intensity': 0.15,
                     # the converter moves the ground AO a level earlier (AO_GROUND_ZOOM_SHIFT): this
                     # fades it in over the SDK's z17-18, where 17-17.8 read as a switch
                     'fill-extrusion-ambient-occlusion-ground-radius': zoom_ramp(18, 0, 19, 8)},
                     'massif:layout': {'fill-extrusion-edge-radius': 0.4}, 'massif:minzoom-param': 'building_min_zoom'})


def layers(v):
    if v.flags.get('mono'):
        # e-ink: outlined footprints, then grey blocks whose walls the lighting shades
        c = v.palette
        return [layer('building-flat', 'fill', 'building', minzoom=13, maxzoom=15,
                      paint={'fill-color': c['building'], 'fill-outline-color': c['building-outline']},
                      metadata={'massif:minzoom-param': 'building_min_zoom'}),
                extrusion(c['building'])]
    return [
        layer('building', 'fill', 'building', minzoom=13, maxzoom=15,
              paint={'fill-color': 'hsl(40, 43%, 93%)', 'fill-outline-color': 'hsl(40, 25%, 85%)'},
              metadata={'massif:minzoom-param': 'building_min_zoom'}),
        extrusion('hsl(30, 43%, 93%)', 'hsl(0, 0%, 58%)'),
    ]
