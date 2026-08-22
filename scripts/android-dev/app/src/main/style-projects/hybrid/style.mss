/*
 * A HYBRID overlay: roads and labels only, over satellite imagery.
 *
 * No `Map { background-color }` at all - the layer has to be transparent or it would paint over
 * the raster underneath it. Everything is drawn light with a dark halo, which is what stays
 * legible on imagery whatever the ground is.
 *
 * `text-face-name: 'Arial'` resolves through the system font fallback (Roboto on Android), so this
 * project bundles no font.
 *
 * Every point label is `billboard`: on 3D terrain the default point placement lies FLAT on the
 * ground and a summit name ends up painted down a slope. A billboard stays upright and faces the
 * camera, which is what a POI has to do.
 */

#transportation {
  line-color: #ffffff;
  line-opacity: 0.75;
  line-width: linear([view::zoom], (11, 0.6), (16, 2.6));
}

#transportation['class'='motorway'] {
  line-color: #ffd08a;
  line-opacity: 0.9;
  line-width: linear([view::zoom], (9, 1.2), (16, 5));
}

#transportation['class'='primary'],
#transportation['class'='secondary'] {
  line-color: #ffffff;
  line-opacity: 0.9;
  line-width: linear([view::zoom], (11, 1), (16, 3.6));
}

#waterway {
  line-color: #9ad3f0;
  line-opacity: 0.8;
  line-width: linear([view::zoom], (11, 0.6), (16, 2));
}

#transportation_name[zoom>=13] {
  text-name: [name];
  text-face-name: 'Arial';
  text-size: 11;
  text-fill: #ffffff;
  text-halo-fill: #00000099;
  text-halo-radius: 1.5;
  text-placement: line;
  text-spacing: 200;
}

/* Summits: the label an outdoor map is actually for, with its altitude. */
#mountain_peak[zoom>=10] {
  text-placement: billboard;
  text-name: [name];
  text-face-name: 'Arial';
  text-size: linear([view::zoom], (10, 11), (15, 14));
  text-fill: #ffffff;
  text-halo-fill: #000000aa;
  text-halo-radius: 2;
  text-dy: -9;
  marker-placement: billboard;
  marker-fill: #ffffff;
  marker-line-color: #00000088;
  marker-line-width: 1;
  marker-width: 5;
}

#mountain_peak[zoom>=12]::ele {
  text-placement: billboard;
  text-name: [ele] + ' m';
  text-face-name: 'Arial';
  text-size: 10;
  text-fill: #ffe9a8;
  text-halo-fill: #000000aa;
  text-halo-radius: 1.5;
  text-dy: 8;
}

#place[zoom>=9] {
  text-placement: billboard;
  text-name: [name];
  text-face-name: 'Arial';
  text-size: linear([view::zoom], (9, 12), (14, 17));
  text-fill: #ffffff;
  text-halo-fill: #000000aa;
  text-halo-radius: 2;
}

#poi[zoom>=14] {
  text-placement: billboard;
  text-name: [name];
  text-face-name: 'Arial';
  text-size: 10;
  text-fill: #eaf6ff;
  text-halo-fill: #000000aa;
  text-halo-radius: 1.5;
  text-dy: 7;
  marker-placement: billboard;
  marker-fill: #eaf6ff;
  marker-line-color: #00000088;
  marker-line-width: 1;
  marker-width: 4;
}
