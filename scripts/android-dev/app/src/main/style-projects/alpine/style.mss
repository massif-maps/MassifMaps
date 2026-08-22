/*
 * A small OpenMapTiles-schema style, used by the "Style parameters" example.
 *
 * The point of it is the two `param::` values declared in alpine.json: a colour, which the SDK
 * classifies as LIVE (setting it swaps the value the decoded tiles already point at), and a
 * boolean sitting in a FILTER, which decides what a tile contains and so re-decodes it. Both are
 * changed at runtime with the decoder's setStyleParameter - no rebuild, no restart.
 *
 * `layers` in alpine.json is TOP -> BOTTOM and must list every layer used here.
 */

Map {
  background-color: #f4f1ea;
}

#water {
  polygon-fill: [param::water_color];
}

#waterway {
  line-color: [param::water_color];
  line-width: linear([view::zoom], (10, 0.6), (16, 2.4));
}

#landcover {
  polygon-fill: #dbe7c8;
  polygon-opacity: 0.65;
}

#transportation {
  line-color: #ffffff;
  line-width: linear([view::zoom], (10, 0.5), (16, 2.6));
}

#transportation['class'='motorway'] {
  line-color: #f0b26b;
  line-width: linear([view::zoom], (8, 1), (16, 6));
}

#transportation['class'='primary'] {
  line-color: #f7dca0;
  line-width: linear([view::zoom], (10, 0.8), (16, 4));
}

/* A filter on a parameter: turning it off changes what the TILE holds, so it re-decodes. */
#building['param::show_buildings'=true][zoom>=14] {
  polygon-fill: #ded7cc;
  polygon-opacity: 0.9;
}

/*
 * Labels. `text-face-name: 'Arial'` resolves through the system font fallback (Roboto on Android),
 * so this project bundles no font, and every point label is a `billboard` so it stays upright when
 * the map is tilted over 3D terrain.
 */

#transportation_name[zoom>=13] {
  text-name: [name];
  text-face-name: 'Arial';
  text-size: 11;
  text-fill: #55606b;
  text-halo-fill: #ffffffcc;
  text-halo-radius: 1.5;
  text-placement: line;
  text-spacing: 220;
}

#place[zoom>=6] {
  text-placement: billboard;
  text-name: [name];
  text-face-name: 'Arial';
  text-size: linear([view::zoom], (6, 11), (14, 18));
  text-fill: #2b3239;
  text-halo-fill: #ffffffdd;
  text-halo-radius: 2;
}

#mountain_peak[zoom>=11] {
  text-placement: billboard;
  text-name: [name];
  text-face-name: 'Arial';
  text-size: 11;
  text-fill: #4a3b23;
  text-halo-fill: #ffffffdd;
  text-halo-radius: 1.5;
  text-dy: -8;
  marker-placement: billboard;
  marker-fill: #6b5a3a;
  marker-line-color: #ffffffcc;
  marker-line-width: 1;
  marker-width: 5;
}
