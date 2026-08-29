import type { CartoProperty, Json } from './types.js';

/**
 * Hoisting literals out of the rules into `@name` declarations.
 *
 * A converted style is one flat wall of rules with the palette spread through it, so making an
 * eink or a dark version meant editing a generated file the next conversion overwrites. The
 * literals come out into their own stylesheet instead, which is the shape the hand-written styles
 * under `assets/style` already use: `eink/style.less` is nothing but `@name: value;`, the rules
 * live in `shared/*.less`, and the variant's project.json lists its palette FIRST - the compiler
 * keeps the first declaration of a variable it sees (CartoCSSCompiler::buildPropertyLists).
 */

/** One emitted rule: the MapBox layer it came from, and its declarations. */
export interface HoistBlock {
    owner: string;
    declarations: string[];
}

export interface HoistResult {
    /** The `@name: value;` lines, for a stylesheet of their own. */
    palette: string[];
    /** The same lines for each alternate pass, in the order they were given. Same names. */
    alternates: string[][];
    blocks: HoistBlock[];
}

/** A colour literal in any spelling the translator emits. None of them nest a paren. */
const COLOUR = /#[0-9a-fA-F]{3,8}\b|\b(?:hsla?|rgba?)\([^()]*\)/g;

/** A bare number, only ever matched against a whole declaration value. */
const NUMBER = /^-?\d+(?:\.\d+)?$/;

/** The two font properties. The generated table calls them "ignored" - they take a raw name. */
const FONT_PROPERTIES = new Set(['text-face-name', 'shield-face-name']);

/** A number is only worth a name once it is shared; a colour is worth one even used once. */
const NUMBER_USES = 2;

/**
 * The mapnik names of the numbers a variant retunes. Every other float in the table is structural
 * - a placement priority, a feature id - and naming it says nothing a palette reader could act on.
 */
const SIZE_ROLES = new Set([
    'size', 'stroke-width', 'width', 'height', 'min-height', 'halo-radius', 'opacity',
    'fill-opacity', 'stroke-opacity', 'spacing', 'minimum-distance', 'wrap-width',
    'line-spacing', 'character-spacing', 'image-scale',
]);

type Kind = 'font' | 'color' | 'number' | 'scene';

/**
 * Map settings a light preset restates. They are not symbolizer properties, so `allowed` has no
 * entry for them and kindOf used to return null: the palette pass then kept the DEFAULT preset's
 * value for every variant, and dawn, dusk and night all rendered with the DAY sun - a roof lit
 * from 70 degrees where MapBox lights it from 40, which is most of why their facades had nuance
 * and ours did not.
 *
 * A scene setting is taken WHOLE, ramp included: naming one stop of `building-light-intensity`
 * says nothing a reader of the palette could act on.
 */
const MAP_SCENE_SETTINGS = new Set([
    'sun-azimuth', 'sun-altitude', 'sun-intensity', 'ambient-intensity',
    'building-ambient', 'building-light-intensity', 'building-vertical-gradient',
    'shadow-strength', 'fog-range-start', 'fog-range-end', 'fog-star-intensity',
]);

interface Entry {
    kind: Kind;
    /** The literal as first written, which is what the palette declares. */
    raw: string;
    /** The same literal in each alternate pass, in order - one palette line each. */
    variants: string[];
    /** Which layers use it, and how often under which property - both feed the name. */
    owners: Set<string>;
    properties: Map<string, number>;
    uses: number;
    order: number;
}

/** Whitespace inside `hsl( 0 , 0% , 100% )` is not a difference; case in `#FFF` is not either. */
function normalise(value: string): string {
    return value.replace(/\s+/g, '').toLowerCase();
}

function kindOf(property: string, allowed: Map<string, CartoProperty>): Kind | null {
    if (FONT_PROPERTIES.has(property)) return 'font';
    const known = allowed.get(property);
    if (!known) {
        // Map settings are not symbolizer properties, so they are not in the table.
        if (property.endsWith('-color')) return 'color';
        return MAP_SCENE_SETTINGS.has(property) ? 'scene' : null;
    }
    if (known.kind === 'color') return 'color';
    return known.kind === 'float' && known.mapnik !== null && SIZE_ROLES.has(known.mapnik) ? 'number' : null;
}

/** A number that only restates the property's own default carries no meaning into the palette. */
function isDefault(property: string, value: string, allowed: Map<string, CartoProperty>): boolean {
    const fallback = Number(allowed.get(property)?.default);
    return Number.isFinite(fallback) && Number(value) === fallback;
}

/** The property's role, short enough to suffix a name with: `line-color` -> `color`. */
function role(property: string, allowed: Map<string, CartoProperty>): string {
    const known = allowed.get(property);
    return slug(known?.mapnik ?? property);
}

/** What the name falls back to when the layers using a value have nothing in common. */
function group(property: string, allowed: Map<string, CartoProperty>): string {
    const known = allowed.get(property);
    return known?.mapnik ? `${slug(known.symbolizer)}_${slug(known.mapnik)}` : slug(property);
}

function slug(text: string): string {
    const cleaned = text.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '').toLowerCase();
    return /^[0-9]/.test(cleaned) ? `v${cleaned}` : cleaned;
}

/**
 * What the layers sharing a value have in common, which is the best name available for it.
 *
 * The tokens they all START with, first - "Minor road" out of "Minor road" and "Minor road bridge".
 * Failing that the one token MOST of them carry, which is what names the grey seven railway layers
 * share when three of them are tunnels. Failing that nothing, and the property names it instead.
 */
function commonScope(owners: Iterable<string>): string {
    const tokenised = [...owners].map((owner) => slug(owner).split('_').filter(Boolean));
    if (tokenised.length === 0) return '';

    let prefix = tokenised[0];
    for (const tokens of tokenised.slice(1)) {
        let shared = 0;
        while (shared < prefix.length && shared < tokens.length && prefix[shared] === tokens[shared]) shared++;
        prefix = prefix.slice(0, shared);
    }
    if (prefix.length > 0) return prefix.join('_');

    const counts = new Map<string, number>();
    for (const tokens of tokenised) {
        for (const token of new Set(tokens)) counts.set(token, (counts.get(token) ?? 0) + 1);
    }
    let best = '';
    let bestCount = 0;
    for (const [token, count] of counts) {
        if (count > bestCount) [best, bestCount] = [token, count];
    }
    return bestCount * 2 > tokenised.length ? best : '';
}

/** The property an entry is mostly used under - the one that should give it its name. */
function mainProperty(properties: Map<string, number>): string {
    let best = '';
    let bestCount = 0;
    for (const [property, count] of properties) {
        if (count > bestCount) [best, bestCount] = [property, count];
    }
    return best;
}

/** Every literal in one declaration that could take a name, with the raw text to replace. */
function literals(property: string, value: string, kind: Kind, allowed: Map<string, CartoProperty>): string[] {
    if (kind === 'color') return value.match(COLOUR) ?? [];
    // A font or a number only counts as the WHOLE value: a number inside a zoom ramp is a stop,
    // and naming a stop says nothing a reader of the palette could act on.
    if (kind === 'font') return /^'[^']*'$/.test(value) ? [value] : [];
    if (kind === 'scene') return [value.trim()];
    return NUMBER.test(value) && !isDefault(property, value, allowed) ? [value] : [];
}

const DECLARATION = /^([a-z][a-z0-9-]*):\s*([\s\S]*);$/;

/** One declaration, split into what can take a name and what cannot. */
function siteLiterals(declaration: string, allowed: Map<string, CartoProperty>):
        { property: string; value: string; kind: Kind; found: string[] } | null {
    const match = DECLARATION.exec(declaration.trim());
    if (!match) return null;
    const [, property, value] = match;
    const kind = kindOf(property, allowed);
    if (kind === null) return null;
    return { property, value, kind, found: literals(property, value, kind, allowed) };
}

/**
 * `alternates` are the SAME rules emitted for another config - another `lightPreset`, in practice.
 * An entry is then keyed by what it is in EVERY pass, not just the default one, so a name means the
 * same sites everywhere and the palettes are interchangeable over one style.mss. Keyed by the
 * default alone, a colour two layers share by day and not by night named itself differently in the
 * two files, and the night palette silently stopped being a drop-in.
 */
export function hoistVariables(blocks: HoistBlock[], allowed: Map<string, CartoProperty>,
        alternates: HoistBlock[][] = []): HoistResult {
    const entries = new Map<string, Entry>();
    // "block:declaration" -> the entry key of each literal in it, in the order they appear. The
    // rewrite reads this rather than re-deriving a key, since a key now spans every pass.
    const sites = new Map<string, string[]>();

    blocks.forEach((block, blockIndex) => {
        block.declarations.forEach((declaration, declarationIndex) => {
            const site = siteLiterals(declaration, allowed);
            if (!site || site.found.length === 0) return;

            // What this same site reads in every other pass. A pass that does not line up here -
            // a different count of colours in the value - makes the site unnameable, not wrong.
            const others: string[][] = [];
            for (const pass of alternates) {
                const other = siteLiterals(pass[blockIndex]?.declarations[declarationIndex] ?? '', allowed);
                if (!other || other.found.length !== site.found.length) return;
                others.push(other.found);
            }

            const keys: string[] = [];
            site.found.forEach((raw, i) => {
                const variants = others.map((found) => found[i]);
                const key = [site.kind, normalise(raw), ...variants.map(normalise)].join('|');
                keys.push(key);
                let entry = entries.get(key);
                if (!entry) {
                    entry = { kind: site.kind, raw, variants, owners: new Set(), properties: new Map(),
                        uses: 0, order: entries.size };
                    entries.set(key, entry);
                }
                entry.owners.add(block.owner);
                entry.properties.set(site.property, (entry.properties.get(site.property) ?? 0) + 1);
                entry.uses++;
            });
            sites.set(`${blockIndex}:${declarationIndex}`, keys);
        });
    });

    // Named most-used first, so the colour a variant most wants to change gets the bare name and
    // the rarer ones carry the suffix. Ties keep first-appearance order, to stay reproducible.
    const named = [...entries.entries()]
        // A scene setting earns a name only where a preset actually changes it; hoisting one every
        // preset agrees on is a variable nobody would ever override.
        .filter(([, entry]) => entry.kind !== 'scene'
            || entry.variants.some((variant) => normalise(variant) !== normalise(entry.raw)))
        .filter(([, entry]) => entry.kind !== 'number' || entry.uses >= NUMBER_USES)
        .sort((a, b) => b[1].uses - a[1].uses || a[1].order - b[1].order);

    const names = new Map<string, string>();
    const taken = new Set<string>();
    for (const [key, entry] of named) {
        const property = mainProperty(entry.properties);
        // A font is named by the font: what a variant swaps is the family, not the layer using it.
        // Everything else is named by the layers - outright when there is one, by what they share
        // when there are several, and by the property alone when they share nothing (a white halo
        // belongs to no single label layer).
        // A Map setting names itself: the background is the first thing a variant changes, and
        // naming it after some line layer that happens to share the grey helps nobody.
        const mapProperty = [...entry.properties.keys()].find((p) => !allowed.has(p) && !FONT_PROPERTIES.has(p));
        const scope = commonScope(entry.owners);
        const base = entry.kind === 'font' ? `font_${slug(entry.raw)}`
            : mapProperty ? slug(mapProperty.replace(/-colou?r$/, ''))
            : scope ? `${scope}_${role(property, allowed)}`
            : group(property, allowed);
        let name = base;
        for (let n = 2; taken.has(name); n++) name = `${base}_${n}`;
        taken.add(name);
        names.set(key, name);
    }

    const rewritten = blocks.map((block, blockIndex) => ({
        owner: block.owner,
        declarations: block.declarations.map((declaration, declarationIndex) => {
            const keys = sites.get(`${blockIndex}:${declarationIndex}`);
            const site = keys && siteLiterals(declaration, allowed);
            if (!site || !keys) return declaration;

            // The literals were collected in order, so they are replaced in order - a value with
            // two colours in it (a zoom ramp) has one name per stop.
            let i = 0;
            const out = site.kind === 'color'
                ? site.value.replace(COLOUR, (raw) => {
                    const name = names.get(keys[i++] ?? '');
                    return name ? `@${name}` : raw;
                })
                : (names.has(keys[0] ?? '') ? `@${names.get(keys[0])}` : site.value);
            return out === site.value ? declaration : `${site.property}: ${out};`;
        }),
    }));

    const order: Record<Kind, number> = { font: 0, color: 1, number: 2, scene: 3 };
    const sorted = [...named]
        .map(([key, entry]) => ({ name: names.get(key)!, entry }))
        .sort((a, b) => order[a.entry.kind] - order[b.entry.kind] || a.name.localeCompare(b.name));
    const palette = sorted.map(({ name, entry }) => `@${name}: ${entry.raw};`);
    const alternatePalettes = alternates.map((_, index) =>
        sorted.map(({ name, entry }) => `@${name}: ${entry.variants[index]};`));

    return { palette, alternates: alternatePalettes, blocks: rewritten };
}

/** The header the palette stylesheet carries, which is also where the variant recipe is written. */
export function paletteHeader(styleName?: Json, preset?: string): string[] {
    const header = preset ? [
        `/* The palette of the converted style for lightPreset "${preset}": the same variable names`,
        '   as variables.mss, with the values that preset resolves to. Drop-in over the same',
        '   style.mss - list it instead in a project.json of its own:',
        '',
        `     { "extends": "./project.json", "styles": ["${preset}.mss", "style.mss"] }`,
        '',
        '   Generated. Edit it and the next conversion overwrites it; copy it first. */',
    ] : [
        '/* The palette of the converted style: every colour, font and shared size it uses.',
        '',
        '   To make a variant (dark, eink, high-contrast), COPY this file, edit the values, and',
        '   list the copy in place of this one in a project.json of its own:',
        '',
        '     { "extends": "./project.json", "styles": ["dark.mss", "style.mss"] }',
        '',
        '   The palette has to come FIRST: the compiler keeps the first declaration of a variable',
        '   it reads. style.mss is generated and is not meant to be edited. */',
    ];
    if (typeof styleName === 'string') header.push(`/* Source style: ${styleName} */`);
    return header;
}
