import { readsFeature } from './split.js';
import type { Json, MapboxLayer } from './types.js';

/**
 * A road shield is a sprite BEHIND its text, tinted by icon-color, with icon-halo-* as its border -
 * and the sprite is picked per feature (`concat('road_', to-string([ref_length]))`). Where that
 * name can be spelled as a path mapnik interpolates, the real artwork is drawn and none of this
 * applies. What is left here is the FALLBACK, for a name CartoCSS cannot assemble (`slice`, say):
 * `text-background-*` is a rounded plate behind the label, so icon-color becomes the plate and the
 * icon halo its border.
 *
 * The signal is the sprite name reading the FEATURE. A POI icon does that too, but its text sits
 * below the icon (`text-anchor: top`, `text-offset: [0, 0.8]`) rather than on it, and a town's
 * circle is picked by ZOOM, not by the feature - so neither is mistaken for a plate.
 */
const CENTRED_OFFSET_EMS = 0.3;

/** The plate's corner rounding. MapBox carries it in the sprite, so there is nothing to read. */
const PLATE_RADIUS = 2;

/**
 * Can the sprite NAME be spelled out per feature? Mirrors what iconExpression builds: a file path
 * carries every `[field]` mapnik interpolates, so a name assembled from literals and fields is
 * resolvable however many fields it reads, and a case only needs each of its branches to be.
 */
export function canSpellIconName(image: Json): boolean {
    if (typeof image === 'string') return true;
    if (!Array.isArray(image)) return false;
    const head = image[0];
    if (head === 'image' || head === 'to-string') return canSpellIconName(image[1] as Json);
    if (head === 'get') return typeof image[1] === 'string';
    if (head === 'coalesce') return image.slice(1).some((b) => canSpellIconName(b as Json));
    if (head === 'concat') {
        return image.slice(1).every((part) => {
            let piece = part as Json;
            while (Array.isArray(piece) && piece[0] === 'to-string') piece = piece[1] as Json;
            if (typeof piece === 'string' || typeof piece === 'number') return true;
            return Array.isArray(piece) && piece[0] === 'get' && typeof piece[1] === 'string';
        }) || (typeof image[1] === 'string' && /:$/.test(image[1] as string) && image.length === 3
            && canSpellIconName(image[2] as Json));
    }
    if (head === 'case' && image.length >= 4 && image.length % 2 === 0) {
        for (let i = 2; i < image.length; i += 2) if (!canSpellIconName(image[i] as Json)) return false;
        return canSpellIconName(image[image.length - 1] as Json);
    }
    if (head === 'match' && image.length >= 5 && image.length % 2 === 1) {
        for (let i = 3; i < image.length; i += 2) if (!canSpellIconName(image[i] as Json)) return false;
        return canSpellIconName(image[image.length - 1] as Json);
    }
    return false;
}

export function isShieldLayer(layer: MapboxLayer): boolean {
    const layout = layer.layout ?? {};
    if (layer.type !== 'symbol' || layout['text-field'] === undefined) return false;
    if (!readsFeature(layout['icon-image'] as Json)) return false;
    // A name that can be spelled reaches the real sprite, so there is nothing to fake.
    if (canSpellIconName(layout['icon-image'] as Json)) return false;
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
