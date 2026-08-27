/**
 * MapBox paint/layout property -> CartoCSS property, per layer type.
 *
 * A name absent here is dropped and counted; a name present but missing from the generated
 * allowlist (src/generated/properties.json) is a bug in this table and is reported as one, which is
 * what keeps the two from drifting.
 */
export const LAYER_SYMBOLIZER: Record<string, string> = {
    background: 'map',
    fill: 'polygon',
    line: 'line',
    symbol: 'text',
    'fill-extrusion': 'building',
    raster: 'raster',
    circle: 'marker',
};

export const PROPERTY_MAP: Record<string, Record<string, string>> = {
    line: {
        'line-color': 'line-color',
        'line-opacity': 'line-opacity',
        'line-width': 'line-width',
        'line-offset': 'line-offset',
        'line-dasharray': 'line-dasharray',
        'line-join': 'line-join',
        'line-cap': 'line-cap',
        'line-miter-limit': 'line-miterlimit',
        'line-pattern': 'line-pattern-file',
    },
    fill: {
        'fill-color': 'polygon-fill',
        'fill-opacity': 'polygon-opacity',
        'fill-pattern': 'polygon-pattern-file',
        // fill-outline-color is a second symbolizer, handled in layers.ts.
    },
    'fill-extrusion': {
        'fill-extrusion-color': 'building-fill',
        'fill-extrusion-opacity': 'building-fill-opacity',
        'fill-extrusion-height': 'building-height',
        'fill-extrusion-base': 'building-min-height',
    },
    symbol: {
        'text-field': 'text-name',
        'text-font': 'text-face-name',
        'text-size': 'text-size',
        'text-color': 'text-fill',
        'text-opacity': 'text-opacity',
        'text-halo-color': 'text-halo-fill',
        'text-halo-width': 'text-halo-radius',
        'text-transform': 'text-transform',
        // text-letter-spacing / text-line-height / text-max-width are in ems and are converted to
        // pixels in index.ts; symbol-placement is one third of the placement, see placement.ts.
        'text-allow-overlap': 'text-allow-overlap',
        'symbol-spacing': 'text-spacing',
    },
    raster: {
        'raster-opacity': 'raster-opacity',
        'raster-resampling': 'raster-filter-mode',
    },
    circle: {
        'circle-color': 'marker-fill',
        'circle-opacity': 'marker-fill-opacity',
        'circle-radius': 'marker-width',
        'circle-stroke-color': 'marker-line-color',
        'circle-stroke-width': 'marker-line-width',
        'circle-stroke-opacity': 'marker-line-opacity',
    },
    background: {
        'background-color': 'background-color',
    },
};

/**
 * Enum values MapBox and CartoCSS both have but spell differently. A value not listed passes
 * through: most enums (round/butt/square, left/right/center) already agree.
 */
export const VALUE_MAP: Record<string, Record<string, string>> = {
    'raster-filter-mode': { linear: 'bilinear', nearest: 'nearest' },
    'text-transform': { uppercase: 'uppercase', lowercase: 'lowercase', none: 'none' },
};

/**
 * MapBox layout/paint properties that are deliberately not translated, with the reason, so the
 * coverage report says WHY rather than just "unknown". These are the gaps investigated in
 * docs/contributing/style-tools.md.
 */
export const KNOWN_GAPS: Record<string, string> = {
    'line-blur': 'no CartoCSS equivalent',
    'line-gradient': 'no CartoCSS equivalent',
    'line-translate': 'screen-space translate has no equivalent',
    'line-translate-anchor': 'screen-space translate has no equivalent',
    'fill-translate': 'screen-space translate has no equivalent',
    'fill-translate-anchor': 'screen-space translate has no equivalent',
    'fill-antialias': 'always on in the vt renderer',
    'fill-extrusion-pattern': 'no CartoCSS equivalent',
    'fill-extrusion-vertical-gradient': 'always on in the 3D lighting shader',
    'fill-extrusion-translate': 'screen-space translate has no equivalent',
    'text-max-angle': 'no CartoCSS equivalent',
    'text-rotate': 'text-orientation would force the flat point placement (TextSymbolizer::getPlacement)',
    'text-line-height': 'a data-driven line height has no CartoCSS equivalent',
    'icon-offset': 'no CartoCSS equivalent',
    'icon-anchor': 'no CartoCSS equivalent',
    'icon-ignore-placement': 'no CartoCSS equivalent',
    'icon-optional': 'no CartoCSS equivalent',
    'icon-padding': 'collision padding has no CartoCSS equivalent',
    'icon-keep-upright': 'no CartoCSS equivalent',
    'icon-rotate': 'no CartoCSS equivalent',
    'icon-text-fit': 'no CartoCSS equivalent',
    'icon-text-fit-padding': 'no CartoCSS equivalent',
    'icon-halo-blur': 'no CartoCSS equivalent',
    'icon-translate': 'screen-space translate has no equivalent',
    'icon-translate-anchor': 'screen-space translate has no equivalent',
    'text-translate': 'screen-space translate has no equivalent',
    'text-translate-anchor': 'screen-space translate has no equivalent',
    'text-halo-blur': 'no CartoCSS equivalent',
    'text-ignore-placement': 'no CartoCSS equivalent (text-clip and text-allow-overlap are the near ones)',
    'symbol-avoid-edges': 'no CartoCSS equivalent',
    'symbol-z-order': 'no CartoCSS equivalent',
    'text-rotation-alignment': 'implied by symbol-placement',
    'text-pitch-alignment': 'no CartoCSS equivalent',
    'circle-blur': 'no CartoCSS equivalent',
    'circle-pitch-alignment': 'no CartoCSS equivalent',
    'circle-pitch-scale': 'no CartoCSS equivalent',
    'raster-hue-rotate': 'no CartoCSS equivalent',
    'raster-brightness-min': 'no CartoCSS equivalent',
    'raster-brightness-max': 'no CartoCSS equivalent',
    'raster-saturation': 'no CartoCSS equivalent',
    'raster-contrast': 'no CartoCSS equivalent',
    'raster-fade-duration': 'no CartoCSS equivalent',
};
