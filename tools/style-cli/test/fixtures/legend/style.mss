@motorway: #e8a0a0;
@road_case: #8c8c8c;

#landcover[class = 'wood'] { polygon-fill: ([param::eink] = 1) ? #ffffff : #c8e6b0; }
#landcover[class = 'wood']::pattern { polygon-pattern-file: url('icons/forest.svg'); }
#water { polygon-fill: ([param::eink] = 1) ? #ffffff : #99ddff; }
#water['param::eink' = 1]::outline { line-color: #000000; line-width: 1; }

#transportation[zoom >= 10][class = 'motorway']::casing { line-color: @road_case; line-width: 8; }
#transportation[zoom >= 10][class = 'motorway'] { line-color: @motorway; line-width: 6; }
#transportation[zoom >= 10][class = 'primary'] { line-color: #fcd6a4; line-width: 4; line-border-color: @road_case; line-border-width: 1; }
#transportation[zoom >= 12][class = 'track'] {
  line-color: #8a5a2b;
  line-width: 1.5;
  [tracktype = 'grade1'] { line-width: 2; }
  [tracktype = 'grade3'] { line-dasharray: 6, 2; }
  [tracktype = 'grade5'] { line-dasharray: 2, 2; }
}
#transportation[zoom >= 12][class = 'path']::casing { line-color: #ffffff; line-width: 3; line-opacity: 0.8; }
#transportation[zoom >= 12][class = 'path'] {
  line-color: [param::trail_color];
  line-width: 1.5;
  [sac_scale = 'mountain_hiking'] { line-dasharray: 4, 2; }
  [sac_scale = 'alpine_hiking'] { line-color: #1f4e9c; line-dasharray: 2, 2; }
}
#transportation[zoom >= 12][class = 'path'][mtb_scale >= 0]['param::show_mtb' = 1]::mtb {
  line-color: #8e44ad; line-width: 1; line-offset: 3;
  [mtb_scale >= 3] { line-color: #000000; }
}

#poi[zoom >= 13][class = 'shelter'] { marker-file: url('icons/shelter.svg'); }
#poi[zoom >= 13][class = 'spring'] { marker-fill: #1e88e5; marker-width: 7; marker-line-color: #ffffff; marker-line-width: 1.5; }

#mountain_peak[zoom >= 11] {
  shield-name: [name];
  shield-face-name: 'Roboto Regular';
  shield-size: 11;
  shield-fill: #52667a;
  shield-halo-fill: #ffffff;
  shield-halo-radius: 1;
  shield-file: url('icons/peak.svg');
  shield-unlock-image: true;
  shield-text-dy: 8;
}

#transportation_name[zoom >= 9][network = 'us-interstate']::shield_icon {
  shield-name: [ref];
  shield-face-name: 'Roboto Bold';
  shield-size: 10;
  shield-fill: #ffffff;
  shield-file: url('icons/interstate.svg');
}
#transportation_name[zoom >= 9][network != 'us-interstate'][ref != null]::shield_plate {
  text-name: [ref];
  text-face-name: 'Roboto Bold';
  text-size: 10;
  text-fill: #ffffff;
  text-background-fill: #c0392b;
  text-background-border-fill: #ffffff;
  text-background-border-width: 1.5;
  text-background-radius: 3;
  [network = 'fr-departmental'] { text-fill: #000000; text-background-fill: #f7d117; }
}

#place[class = 'city'] { text-name: [name]; text-face-name: 'Roboto Bold'; text-size: 14; text-fill: #333333; text-halo-fill: #ffffff; text-halo-radius: 1.5; }
