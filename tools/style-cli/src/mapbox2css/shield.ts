import { readsFeature } from './split.js';
import type { Json, MapboxLayer } from './types.js';

/**
 * A road shield is a sprite BEHIND its text, tinted by icon-color, with icon-halo-* as its border -
 * and the sprite is picked per feature (`concat('road_', to-string([ref_length]))`), so there is no
 * one image to slice out. CartoCSS draws the same thing without any sprite: `text-background-*` is
 * a rounded plate behind the label, so icon-color becomes the plate and the icon halo its border.
 *
 * The signal is the sprite name reading the FEATURE. A POI icon does that too, but its text sits
 * below the icon (`text-anchor: top`, `text-offset: [0, 0.8]`) rather than on it, and a town's
 * circle is picked by ZOOM, not by the feature - so neither is mistaken for a plate.
 */
const CENTRED_OFFSET_EMS = 0.3;

/** The plate's corner rounding. MapBox carries it in the sprite, so there is nothing to read. */
const PLATE_RADIUS = 2;

export function isShieldLayer(layer: MapboxLayer): boolean {
    const layout = layer.layout ?? {};
    if (layer.type !== 'symbol' || layout['text-field'] === undefined) return false;
    if (!readsFeature(layout['icon-image'] as Json)) return false;
    // Without a colour there is no plate to draw, only a border round nothing.
    if ((layer.paint?.['icon-color'] ?? layout['icon-color']) === undefined) return false;
    return textSitsOnIcon(layer);
}

function textSitsOnIcon(layer: MapboxLayer): boolean {
    const layout = layer.layout ?? {};
    const anchor = layout['text-anchor'];
    if (typeof anchor === 'string' && anchor !== 'center') return false;
    const offset = layout['text-offset'];
    if (!Array.isArray(offset)) return true;
    return offset.every((v) => typeof v === 'number' && Math.abs(v) <= CENTRED_OFFSET_EMS);
}

/** MapBox icon-* on the shield sprite -> the CartoCSS plate that replaces it. */
export const PLATE_MAP: ReadonlyArray<readonly [string, string]> = [
    ['icon-color', 'text-background-fill'],
    ['icon-opacity', 'text-background-opacity'],
    ['icon-halo-color', 'text-background-border-fill'],
    ['icon-halo-width', 'text-background-border-width'],
];

export function plateRadius(): string {
    return `text-background-radius: ${PLATE_RADIUS};`;
}

/**
 * MapBox draws an icon and its text as ONE symbol; a `markers` plus a `text` symbolizer are two
 * labels that then collide, and the marker wins - which is why a city dot appeared with no name
 * next to it. `ShieldSymbolizer` is the one-label construct, so a symbol layer that has both
 * becomes a shield and every `text-*` declaration is renamed into it.
 *
 * Most names just take the prefix. These do not: `shield-dx/dy` move the IMAGE, so the text's own
 * offset is `shield-text-dx/dy`, and a few carry the word `text` for the same reason.
 */
const SHIELD_RENAMES: Record<string, string> = {
    'text-dx': 'shield-text-dx',
    'text-dy': 'shield-text-dy',
    'text-opacity': 'shield-text-opacity',
    'text-transform': 'shield-text-transform',
    'text-name': 'shield-name',
};

/** A text declaration as the shield spells it, or null when the shield has no equivalent. */
export function asShieldDeclaration(declaration: string): string | null {
    const colon = declaration.indexOf(':');
    const name = declaration.slice(0, colon);
    if (!name.startsWith('text-')) return declaration;
    const renamed = SHIELD_RENAMES[name] ?? `shield-${name.slice('text-'.length)}`;
    return `${renamed}${declaration.slice(colon)}`;
}
