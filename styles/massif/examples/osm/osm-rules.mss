/* Listed AFTER style.mss. Massif's tracks are moved out of reach (osm.json sets track_min_zoom 24)
   and drawn again here the OSM way: brown, dashed by tracktype, over a pale casing, at Massif's width.
   tracktype is the grade's name in OpenMapTiles, its index in OSM's list on Alpimaps tiles. */
#transportation[zoom >= 12][class = 'track'][brunnel != 'tunnel']::osm_track_casing {
  line-color: #ffffff;
  line-opacity: 0.6;
  line-width: exponential(1.5, [view::zoom], (12, 1.5), (15, 2.6), (18, 5), (22, 16));
}
#transportation[zoom >= 12][class = 'track'][brunnel != 'tunnel']::osm_track {
  line-color: @track;
  line-width: exponential(1.5, [view::zoom], (12, 0.5), (15, 1.2), (18, 3), (22, 12));
  [tracktype = null] { line-dasharray: 3, 4, 11, 4; }
  [tracktype = 'grade2'], [tracktype = 1] { line-dasharray: 8.8, 3.2; }
  [tracktype = 'grade3'], [tracktype = 2] { line-dasharray: 5.6, 4; }
  [tracktype = 'grade4'], [tracktype = 3] { line-dasharray: 3.2, 4.8; }
  [tracktype = 'grade5'], [tracktype = 4] { line-dasharray: 1.6, 6.4; }
}
