/*
 * Starter CartoCSS for the preview.
 *
 * Written against the OpenMapTiles schema, because that is what the default source serves. A style
 * for a different vocabulary names different layers - `#road` rather than `#transportation` for a
 * MapBox Streets source - which is the first thing to check when a style renders nothing.
 */

const BASE = `Map {
  background-color: #f6f2ee;
}

#water { polygon-fill: #a8cdea; }
#waterway { line-color: #a8cdea; line-width: 1.2; }

#landcover[class='wood'] { polygon-fill: #cfe0c4; }
#landcover[class='grass'] { polygon-fill: #dcecd0; }
#landuse[class='residential'] { polygon-fill: #ece7e1; }

#building {
  polygon-fill: #ddd5cc;
  line-color: #ccc2b6;
  line-width: 0.6;
}

#transportation {
  line-color: #ffffff;
  line-width: 1.4;
  line-cap: round;
  line-join: round;
  [class='motorway'] { line-color: #f7c96b; line-width: 4; }
  [class='trunk'], [class='primary'] { line-color: #fbe0a6; line-width: 3; }
  [class='secondary'], [class='tertiary'] { line-width: 2.2; }
  [class='path'] { line-color: #cbbfae; line-width: 1; line-dasharray: 3, 2; }
}
`;

const LABELS = `
#transportation_name {
  text-name: [name];
  text-face-name: 'Roboto';
  text-size: 11;
  text-fill: #5b5348;
  text-halo-fill: #ffffffcc;
  text-halo-radius: 1.2;
  text-placement: line;
}

#place {
  text-name: [name];
  text-face-name: 'Roboto';
  text-fill: #3d3a34;
  text-halo-fill: #ffffffcc;
  text-halo-radius: 1.5;
  text-size: 12;
  [class='city'] { text-size: 16; }
  [class='town'] { text-size: 13; }
}
`;

export const STARTERS = [
  {
    id: 'basic',
    label: 'Basic',
    description: 'Land, water, roads and buildings — the smallest style that shows a map.',
    css: BASE,
  },
  {
    id: 'labelled',
    label: 'With labels',
    description: 'The same, plus place and street labels, so text placement can be judged.',
    css: BASE + LABELS,
  },
  {
    id: 'dark',
    label: 'Dark',
    description: 'A dark palette — useful for checking halo contrast and label legibility.',
    css: `Map { background-color: #14181f; }

#water { polygon-fill: #0f2233; }
#landcover[class='wood'] { polygon-fill: #1b2a20; }
#building { polygon-fill: #232a33; }

#transportation {
  line-color: #39424f;
  line-width: 1.4;
  line-cap: round;
  line-join: round;
  [class='motorway'] { line-color: #6b5a3a; line-width: 4; }
  [class='trunk'], [class='primary'] { line-color: #554a37; line-width: 3; }
}

#place {
  text-name: [name];
  text-face-name: 'Roboto';
  text-fill: #cdd4de;
  text-halo-fill: #000000aa;
  text-halo-radius: 1.6;
  text-size: 12;
  [class='city'] { text-size: 16; }
}
`,
  },
  {
    id: 'buildings',
    label: 'Labels and 3D buildings',
    description: 'Extruded buildings with labels over them — what the hour slider casts shadows '
      + 'from. Right-drag to tilt and see them.',
    css: `${BASE}
#building {
  building-fill: #d8d0c6;
  building-height: [render_height];
  building-min-height: [render_min_height];
}
${LABELS}`,
  },
];

/**
 * What the page opens on. Buildings, because the first question anyone asks a 3D map is whether it
 * is 3D - and because shadows need something to fall off.
 */
export const DEFAULT_STARTER = STARTERS.find((s) => s.id === 'buildings') ?? STARTERS[0];
