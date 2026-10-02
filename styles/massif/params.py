"""Every style parameter an app or an extending project can set, with its default. A variant may
state its own default (build.py's `Variant(params=...)`); a layer reads one through a
`massif:minzoom-param` or `lib.gate`. The names follow Alpimaps' OSM style where it had one."""

SWITCH = [0, 1]

PARAMS = {
    'poiStyle': {'default': 'badge', 'values': ['badge', 'plain']},
    'building_opacity': {'default': 1},
    'contour_opacity': {'default': 1},
    'emphasis_rails': {'default': 0, 'values': SWITCH},
    'show_tram': {'default': 1, 'values': SWITCH},
    'show_underground': {'default': 1, 'values': SWITCH},
    'show_boundaries': {'default': 1, 'values': SWITCH},
    'sub_boundaries': {'default': 1, 'values': SWITCH},
    'road_shields': {'default': 1, 'values': SWITCH},
    'highlight_drinking_water': {'default': 0, 'values': SWITCH},
    'label_occlusion': {'default': 1, 'values': SWITCH},
    'poi_on_roof': {'default': 1, 'values': SWITCH},
    'show_caravan_site': {'default': 1, 'values': SWITCH},
    'campsite_allow_overlap': {'default': 0, 'values': SWITCH},
    'polygons_border': {'default': 0, 'values': SWITCH},
    'lighting': {'default': 1, 'values': SWITCH},
    'sac_scale_labels': {'default': 0, 'values': SWITCH},
    'road_osm_low': {'default': 0, 'values': SWITCH},
    'track_min_zoom': {'default': 12},
    'path_min_zoom': {'default': 12},
    'tunnel_min_zoom': {'default': 12},
    'water_min_zoom': {'default': 17},
    'campsite_min_zoom': {'default': 15},
    'building_min_zoom': {'default': 14},
    'city_min_zoom': {'default': 3},
    'river_label_min_zoom': {'default': 9},
    'forest_pattern_zoom': {'default': 11},
    'scrub_pattern_zoom': {'default': 12},
    'rock_pattern_zoom': {'default': 12},
    'wetland_pattern_zoom': {'default': 13},
}
