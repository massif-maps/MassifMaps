/* Round-trip fixture for `css2xml --roundtrip`. Every construct here is one the XML parser and
   the XML generator both have to carry; if either side forgets one, the re-compiled document
   differs and the run fails. Add a construct here whenever mapnikvt gains one. */

@road: #ff0000;

Map {
  background-color: #f0f0f0;
  north-pole-color: #ffffff;
  buffer-size: 64.0;

  /* sun / shadows / fog / buildings - Map::Settings, only ever written when the style set them */
  sun-azimuth: 315;
  sun-altitude: linear([view::zoom], (5, 20), (12, 60));
  sun-color: #fff8e0;
  ambient-intensity: 0.4;
  shadow-strength: 0.7;
  shadow-cascades: 3;
  terrain-lighting: 1;
  colors-prelit: 1;
  building-roof-shade: 0.8;
  building-ao-intensity: 0.25;
  /* what mapbox2css emits: the style's zoom ramp, and our tilt ramp the shadows ignore */
  building-height-scale: linear(([view::zoom] - 1), (15, 0), (15.3, 1));
  building-height-view-scale: 1 - ([param::building_tilt_drop] * 0.01) * linear([view::tilt], (80, 0), (90, 1));
  building-fade-on-appear: 0;
  text-occlusion-opacity: 0.3;
  fog-color: #b0c4de;
  fog-range-end: 6;
  fog-star-intensity: 0.15;
  terrain-max-visible-distance: 40000;
}

#landcover {
  polygon-fill: #d0e0c0;
}

/* composite slots: the three config symbolizers */
#hillshade {
  hillshade-opacity: linear([view::zoom], (8, 0.3), (14, 0.9));
  hillshade-exaggeration: 0.6;
  hillshade-shadow-color: #202030;
  hillshade-method: combined;
}

#hillshade['param::relief'=false] {
  hillshade-visible: false;
}

#contour {
  contour-base-interval: 20;
  contour-min-visible-zoom: 12;
  line-color: #804000;
  line-width: 1;
}

#satellite {
  raster-opacity: 0.5;
  raster-filter-mode: bicubic;
}

/* expression operators the generator emits */
#transportation {
  line-color: @road;
  line-width: [width] ?? 2;
  line-opacity: ([class] & 1) ^ 2;
  line-offset: min([a], 4) + max([b], 2);
  line-cap: round;
  line-join: bevel;
}

#transportation['render::3d' = true] {
  line-width: 4;
}

#transportation[zoom >= 12][class = 'motorway'] {
  line-width: 6;
}

#transportation::labels {
  text-name: [name];
  text-face-name: 'DIN Pro Regular';
  text-size: [param::selected_id] = [id] ? 14 : 11;
  text-fill: #333333;
  text-placement: line;
}
