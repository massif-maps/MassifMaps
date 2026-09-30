/*
 * The peak finder's generated text: both shaders with the palette written in, and the summit names'
 * CartoCSS. scripts/gen-peak-finder-assets.mjs writes the same text out for the native ports.
 */
import { RELIEF_DEPTH_OUTLINE_SHADER, RELIEF_SURFACE_SHADER } from './relief-shaders.js';
import { peaksStyle } from './peaks-style.js';

// White paper, black ink: the palette is constants in the shaders, so it is written into the source.
const COLOURS = { uPaperColor: [1, 1, 1, 1], uInkColor: [0, 0, 0, 1], uShadeColor: [0, 0, 0, 1] };
const withColours = (shader) => Object.entries(COLOURS).reduce((glsl, [name, rgba]) =>
  glsl.replace(new RegExp(`const vec4 ${name} = vec4\\([^)]*\\);`), `const vec4 ${name} = vec4(${rgba.join(', ')});`), shader);

export const SURFACE_SHADER = withColours(RELIEF_SURFACE_SHADER);
export const INK_SHADER = withColours(RELIEF_DEPTH_OUTLINE_SHADER);

/** How the summit names are laid out, before the sliders move it: the Alpimaps app's defaults. */
export const LABEL = { layout: 'top', band: 0.12, angle: 45, size: 13, wrap: 70, occlusion: 0.15 };

/** The row the names hang from, as a fraction of the view: low enough for a wrapped name (Alpimaps' labelRowFraction). */
export function labelBand(label, viewHeightDp) {
  const width = (label.wrap || 160) + label.size * 2.5 + 10;
  const angle = label.angle * Math.PI / 180;
  const row = width * Math.sin(angle) + (label.size * 2.6 + 4) * Math.cos(angle) + 10;
  return viewHeightDp > 0 ? Math.min(0.9, row / viewHeightDp) : 0.2;
}

/**
 * Every name in one row above the skyline. The eye's altitude is baked into the rank, and the
 * selected summit is the style parameter `selected_peak`, compared with `[name]|[ele]`.
 */
export function summitsStyle(label, eyeElevation, faces = { regular: 'Roboto', bold: 'Roboto-Bold' }) {
  const css = peaksStyle({
    eyeElevation, textAngle: label.angle, textSize: label.size, wrapWidth: label.wrap, band: label.band, topOffset: label.band,
    pinTop: label.layout === 'top', followSkyline: label.layout === 'skyline', minDistance: 1, persistPasses: 10, maxRows: 1,
  });
  return "Map { param-selected_peak: ''; }\n@selected: [name] + '|' + [ele] = [param::selected_peak];\n" + css
    .replace('  text-name: [name];', `  text-name: [name];\n  text-face-name: @selected ? '${faces.bold}' : '${faces.regular}';`)
    .replace(/\n {2}text-fill: ([^;]+);/, '\n  text-fill: @selected ? #2f4f9e : $1;')
    .replace(/text-placement-priority: ([^;]+);/, 'text-placement-priority: @selected ? 100000 : $1;');
}
