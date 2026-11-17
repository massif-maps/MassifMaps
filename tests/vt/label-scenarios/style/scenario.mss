/* The two label rules of the Massif style a street-vs-POI scenario needs, with its real values
   (styles/massif/carto/style.mss: road_label_minor and %poi), the fonts and colours aside. */

@spacing: 250;

#transportation_name[zoom >= 13][class = 'minor'] {
  text-placement: 'billboard-line';
  text-name: [name];
  text-face-name: 'Scenario';
  text-size: linear([view::zoom], (10, 8), (18, 14));
  text-transform: 'uppercase';
  text-character-spacing: (0.15 * linear([view::zoom], (10, 8), (18, 14)));
  text-max-char-angle-delta: 30;
  text-collision-padding: 1;
  text-fit-steps: 3;
  text-fit-ratio: 0.8;
  text-placement-priority: 23900000;
  text-spacing: @spacing;
  text-min-distance: @spacing;
}

#poi[zoom >= 17][name != null] {
  shield-placement: 'billboard';
  shield-name: [name];
  shield-face-name: 'Scenario';
  shield-size: 10.8;
  shield-wrap-width: 97.2;
  shield-wrap-before: true;
  shield-collision-padding: 2;
  shield-placement-priority: 25600000;
  shield-icon-name: 'o';
  shield-icon-face-name: 'Scenario';
  shield-icon-size: 12;
  shield-anchors: 'bottom,right,left';
  shield-text-optional: true;
  shield-text-radial-offset: 10.8;
  shield-text-horizontal-alignment: 'auto';
}
