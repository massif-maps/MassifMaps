/* Listed AFTER style.mss. Massif's tracks are moved out of reach (osm.json sets track_min_zoom 24)
   and drawn again here as Alpimaps' OSM style does: a brown line (@track) under a white one dashed
   by tracktype, at Massif's width. tracktype is the grade's name in OpenMapTiles, its index in
   OSM's list on Alpimaps tiles. */
#transportation[zoom >= 12][class = 'track'][brunnel != 'tunnel']::osm_track_casing {
  line-color: @track;
  line-width: exponential(1.5, [view::zoom], (12, 1), (15, 2.4), (18, 5), (22, 16));
}
#transportation[zoom >= 14][class = 'track'][brunnel != 'tunnel'][tracktype != 'grade1'][tracktype != 0]::osm_track {
  line-color: #ffffff;
  line-width: exponential(1.5, [view::zoom], (14, 0.8), (15, 1.2), (18, 3), (22, 12));
  line-dasharray: 6, 3, 6;
  [tracktype = 'grade2'], [tracktype = 1] { line-dasharray: 7, 1; }
  [tracktype = 'grade3'], [tracktype = 2] { line-dasharray: 5, 2, 5; }
}
