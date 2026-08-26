import { type ContourOptions, isContourLayer, rewriteContourFields, rewriteContourFilter } from './contour.js';
import { Coverage } from './coverage.js';
import { Untranslatable, expandTokens, translateExpression } from './expression.js';
import { translateFilter, zoomPredicates } from './filter.js';
import { HANDLED_ELSEWHERE, followsLine, repeatsAlongLine, resolvePlacement } from './placement.js';
import { KNOWN_GAPS, LAYER_SYMBOLIZER, PROPERTY_MAP, VALUE_MAP } from './properties.js';
import { PLATE_MAP, asShieldDeclaration, isShieldLayer, plateRadius } from './shield.js';
import { type ExtractedIcon, type SpriteSet, extractIcon } from './sprite.js';
import { collapseBranches, splitLayer } from './split.js';
import type { CartoProperty, Json, MapboxLayer, MapboxStyle, PropertyTable } from './types.js';

export interface ConvertResult {
    mss: string;
    project: string;
    coverage: Coverage;
}

/** MapBox text-anchor -> the CartoCSS (horizontal, vertical) alignment pair. */
const TEXT_ANCHOR: Record<string, readonly [string, string]> = {
    center: ['middle', 'middle'],
    left: ['left', 'middle'],
    right: ['right', 'middle'],
    top: ['middle', 'top'],
    bottom: ['middle', 'bottom'],
    'top-left': ['left', 'top'],
    'top-right': ['right', 'top'],
    'bottom-left': ['left', 'bottom'],
    'bottom-right': ['right', 'bottom'],
};

/** Attachment names are identifiers in the CartoCSS grammar. */
function attachmentName(layerId: string): string {
    const cleaned = layerId.replace(/[^A-Za-z0-9_]/g, '_');
    return /^[0-9]/.test(cleaned) ? `_${cleaned}` : cleaned;
}

export interface ConvertOptions {
    contour?: ContourOptions;
    /** Loaded sprite sheets and where to write the sliced icons. Icons are dropped without them. */
    sprites?: { sheets: SpriteSet; outDir: string };
    /** Resolve SDF sprites to plain bitmaps, for an SDK without marker-sdf. Loses size and halo. */
    flattenSdf?: boolean;
    /** Multiplies the collision gap MapBox's text-padding asks for. 1 keeps the style's own. */
    labelSpacing?: number;
}

export function convert(style: MapboxStyle, table: PropertyTable, options: ConvertOptions = {}): ConvertResult {
    const coverage = new Coverage();
    const allowed = new Map<string, CartoProperty>(table.properties.map((p) => [p.cartocss, p]));
    const layers = style.layers ?? [];

    const mapBlock: string[] = [];
    const blocks: string[] = [];
    // Source-layer name -> the index of the first MapBox layer that draws it.
    const order = new Map<string, number>();

    layers.forEach((layer, index) => {
        if (layer.type === 'background') {
            mapBlock.push(...backgroundProperties(layer, coverage));
            return;
        }

        const symbolizer = LAYER_SYMBOLIZER[layer.type];
        if (!symbolizer) {
            coverage.drop(`layer type "${layer.type}"`, 'unsupported layer type', layer.id);
            return;
        }

        const sourceLayer = layer['source-layer'];
        if (!sourceLayer) {
            coverage.drop(`layer "${layer.id}"`, 'no source-layer', layer.id);
            return;
        }
        if (!order.has(sourceLayer)) order.set(sourceLayer, index);

        // The contour schemas name the elevation differently; renaming it here means the filter,
        // the label and every paint expression all see the field the tiles actually carry.
        const retargeted = options.contour
            ? (rewriteContourFields(layer as unknown as Json, options.contour) as unknown as MapboxLayer)
            : layer;

        // A field-driven paint value becomes one attachment per branch - see split.ts.
        const variants = splitLayer(isContourLayer(layer) ? retargeted : layer, coverage);
        variants.forEach((variant, branch) => {
            const suffix = variants.length > 1 ? `_b${branch + 1}` : '';
            emitLayer(variant, `${attachmentName(layer.id)}${suffix}`, sourceLayer, symbolizer, index);
        });
    });

    function emitLayer(layer: MapboxLayer, attachment: string, sourceLayer: string, symbolizer: string, layerIndex: number): void {
        const declarations = layerDeclarations(layer, symbolizer, allowed, coverage, options, layerIndex);
        if (declarations.length === 0) return;

        let selector: string;
        try {
            // `selector = *predicate` with a skipper, so bracketed tests can abut the layer name -
            // but `when(...)` is a bare word and would glue onto it (`#aviationwhen(...)`).
            let filter = layer.filter ?? null;
            if (options.contour && isContourLayer(layer)) {
                filter = rewriteContourFilter(filter, options.contour, () =>
                    coverage.approximate(
                        `contour nth_line test rewritten as div >= ${options.contour!.majorDiv}: ` +
                        'only the major/minor split survives, the base interval is not in the style',
                    ));
            }
            const predicates = [
                ...zoomPredicates(layer.minzoom, layer.maxzoom),
                ...translateFilter(filter),
            ].map((p) => (p.startsWith('when(') ? ` ${p}` : p));
            selector = `#${sourceLayer}${predicates.join('')}::${attachment}`;
        } catch (error) {
            const why = error instanceof Untranslatable ? error.what : String(error);
            coverage.drop(`filter on "${layer.id}"`, `untranslatable filter: ${why}`, layer.id);
            return;
        }

        blocks.push(`${selector} {\n${declarations.map((d) => `  ${d}`).join('\n')}\n}`);
    }

    reportInterleaving(layers, order, coverage);
    reportSources(style, layers, coverage);

    const header = [
        '/* Generated by massif-style mapbox2css. Do not edit by hand:',
        '   re-run the converter against the source style instead. */',
    ];
    if (style.name) header.push(`/* Source style: ${style.name} */`);

    const mss = [
        ...header,
        '',
        ...(mapBlock.length > 0 ? [`Map {\n${mapBlock.map((d) => `  ${d}`).join('\n')}\n}`, ''] : []),
        ...blocks,
        '',
    ].join('\n');

    // loadMapProject REVERSES this array (layerNames.insert(begin)), and MapBox layers run
    // bottom-to-top, so the project list is the draw order reversed.
    const projectLayers = [...order.entries()].sort((a, b) => a[1] - b[1]).map(([name]) => name).reverse();
    const project = JSON.stringify({ styles: ['style.mss'], layers: projectLayers }, null, 2) + '\n';

    return { mss, project, coverage };
}

function backgroundProperties(layer: MapboxLayer, coverage: Coverage): string[] {
    const out: string[] = [];
    for (const [name, value] of Object.entries(layer.paint ?? {})) {
        const target = PROPERTY_MAP.background[name];
        if (!target) {
            coverage.drop(name, KNOWN_GAPS[name] ?? 'not mapped', layer.id);
            continue;
        }
        const translated = tryTranslate(value, name, layer.id, coverage);
        if (translated !== null) {
            out.push(`${target}: ${translated};`);
            coverage.emit(target);
        }
    }
    return out;
}

function layerDeclarations(
    layer: MapboxLayer,
    symbolizer: string,
    allowed: Map<string, CartoProperty>,
    coverage: Coverage,
    options: ConvertOptions,
    layerIndex = 0,
): string[] {
    const table = PROPERTY_MAP[layer.type] ?? {};
    const out: string[] = [];
    // A symbol layer's icon is a SECOND symbolizer in the same rule, so marker-* declarations sit
    // beside the text-* ones rather than replacing them - unless the icon is a road shield, whose
    // sprite is picked per feature and is drawn as a plate behind the text instead.
    const isShield = layer.type === 'symbol' && isShieldLayer(layer);
    const iconDeclarations = layer.type !== 'symbol'
        ? []
        : isShield
            ? plateDeclarations(layer, coverage)
            : markerDeclarations(layer, coverage, options);

    if (symbolizer === 'text') {
        out.push(`text-placement: '${resolvePlacement(layer, 'text')}';`);
        coverage.emit('text-placement');
    }

    for (const [name, value] of Object.entries({ ...layer.layout, ...layer.paint })) {
        if (name === 'visibility') {
            if (value === 'none') return []; // the whole layer is off
            continue;
        }
        if (HANDLED_ELSEWHERE.has(name)) continue;
        // text-field may be the legacy "{field}" token form rather than an expression.
        if (name === 'text-field' && typeof value === 'string' && value.includes('{')) {
            out.push(`text-name: ${expandTokens(value)};`);
            coverage.emit('text-name');
            continue;
        }
        // A text-field that branches per country (MapBox's shields slice the ref) is worth keeping
        // as its fallback rather than losing the label: the fallback IS the name for most features.
        if (name === 'text-field') {
            const text = tryTranslate(value, name, layer.id, coverage) ?? retryCollapsed(value, layer, coverage);
            if (text === null) continue;
            out.push(`text-name: ${text};`);
            coverage.emit('text-name');
            continue;
        }

        // MapBox names one anchor; CartoCSS splits it into a horizontal and a vertical alignment.
        // The senses agree - see the default derivation in TextSymbolizer.cpp, where a text pushed
        // right (dx > 0) defaults to alignment 'left', i.e. the edge nearest the anchor.
        if (name === 'text-anchor' && typeof value === 'string') {
            const anchor = TEXT_ANCHOR[value];
            if (!anchor) {
                coverage.drop(name, `unknown anchor "${value}"`, layer.id);
                continue;
            }
            out.push(`text-horizontal-alignment: '${anchor[0]}';`, `text-vertical-alignment: '${anchor[1]}';`);
            coverage.emit('text-horizontal-alignment');
            coverage.emit('text-vertical-alignment');
            continue;
        }

        // Everything MapBox measures in ems of the text size. CartoCSS takes pixels, so each one
        // is multiplied by text-size - the expression form included, or a zoom-driven size would
        // silently pin the value to one zoom.
        if (name === 'text-offset' && Array.isArray(value) && value.length === 2) {
            const dx = ems(value[0], layer, coverage, name);
            const dy = ems(value[1], layer, coverage, name);
            if (dx === null || dy === null) continue;
            out.push(`text-dx: ${dx};`, `text-dy: ${dy};`);
            coverage.emit('text-dx');
            coverage.emit('text-dy');
            continue;
        }

        // text-max-width is 10 ems by default; taken as pixels it wrapped every name onto one word
        // per line. A line-placed label is laid out along the line and MapLibre never wraps it.
        if (name === 'text-max-width') {
            if (followsLine(layer)) {
                out.push('text-wrap-width: 0;');
                coverage.emit('text-wrap-width');
                continue;
            }
            const width = ems(value, layer, coverage, name);
            if (width === null) continue;
            out.push(`text-wrap-width: ${width};`);
            coverage.emit('text-wrap-width');
            continue;
        }

        // The modern spelling of text-allow-overlap. 'cooperative' has no equivalent and is the
        // conservative 'never' here. icon-overlap belongs to the marker and is handled with it -
        // a lone marker-* declaration builds a marker with no file, which draws the default blue
        // ellipse over every feature of the layer.
        if (name === 'text-overlap') {
            out.push(`text-allow-overlap: ${value === 'always'};`);
            coverage.emit('text-allow-overlap');
            continue;
        }

        // MapBox places the LOWEST sort key first; CartoCSS's culler takes the highest priority.
        if (name === 'symbol-sort-key') continue; // folded into the layer's priority below

        // MapBox's text-opacity fades the WHOLE label; CartoCSS's fades only the fill, and the halo
        // keeps its own. A style that hides a label with `step(zoom, 0, …, 13, 1)` was leaving the
        // halo at full opacity - a white ghost of the name at every zoom it should not be at.
        if (name === 'text-opacity') {
            const translated = tryTranslate(value, name, layer.id, coverage);
            if (translated === null) continue;
            out.push(`text-opacity: ${translated};`, `text-halo-opacity: ${translated};`);
            coverage.emit('text-opacity');
            coverage.emit('text-halo-opacity');
            continue;
        }

        // MapBox pads a label's collision box by text-padding on EVERY side, so two labels end up
        // at least twice that apart; the culler's minimum-distance is one buffer between the pair.
        // Dropping it was why a converted style drew far more labels than MapTiler does.
        if (name === 'text-padding') {
            const translated = tryTranslate(value, name, layer.id, coverage);
            if (translated === null) continue;
            out.push(`text-min-distance: (${labelGap(options)} * ${translated});`);
            coverage.emit('text-min-distance');
            continue;
        }

        if (name === 'text-letter-spacing') {
            const spacing = ems(value, layer, coverage, name);
            if (spacing === null) continue;
            out.push(`text-character-spacing: ${spacing};`);
            coverage.emit('text-character-spacing');
            continue;
        }

        // text-line-height is a TOTAL line height in ems; text-line-spacing is what is added on top
        // of the font's own, which already is MapBox's 1.2 default.
        if (name === 'text-line-height' && typeof value === 'number') {
            const spacing = ems(value - 1.2, layer, coverage, name);
            if (spacing === null) continue;
            out.push(`text-line-spacing: ${spacing};`);
            coverage.emit('text-line-spacing');
            continue;
        }

        // MapBox dash lengths are multiples of the line width; CartoCSS's are pixels.
        if (name === 'line-dasharray' && Array.isArray(value) && value.every((v) => typeof v === 'number')) {
            const width = layer.paint?.['line-width'];
            const scale = representativeScale(width, 1);
            if (typeof width !== 'number') {
                // Taking 1 here made a lift's `[0.05, 4]` a 0.05-PIXEL dash, which is nothing at
                // all; the widths it ramps between are 3 and 4, so their mean is far closer.
                coverage.approximate(`line-dasharray scaled by ${round(scale)}, the mean of a ` +
                    'zoom-driven line-width: CartoCSS takes one dash pattern, not a ramp');
            }
            out.push(`line-dasharray: ${(value as number[]).map((v) => round(v * scale)).join(',')};`);
            coverage.emit('line-dasharray');
            continue;
        }

        // A fill's outline is a second symbolizer on the same rule, not a polygon property.
        if (name === 'fill-outline-color') {
            const translated = tryTranslate(value, name, layer.id, coverage);
            if (translated !== null) {
                out.push(`line-color: ${translated};`, 'line-width: 1;');
                coverage.emit('line-color');
            }
            continue;
        }

        const target = table[name];
        if (!target) {
            coverage.drop(name, KNOWN_GAPS[name] ?? 'not mapped', layer.id);
            continue;
        }
        if (!allowed.has(target)) {
            // The table above claims a property the decoder does not bind - a bug here, not in the
            // source style. Say so rather than blaming the style.
            coverage.drop(name, `maps to "${target}", absent from the generated allowlist`, layer.id);
            continue;
        }

        const translated = tryTranslate(value, name, layer.id, coverage);
        if (translated === null) continue;
        out.push(`${target}: ${remapValue(target, translated)};`);
        coverage.emit(target);
    }

    if (symbolizer === 'text') {
        const priority = placementPriority(layer, layerIndex, coverage);
        if (priority !== null) {
            out.push(`text-placement-priority: ${priority};`);
            coverage.emit('text-placement-priority');
        }
    }

    // text-padding is 2 px on every layer that states nothing, and the culler's default is 0. A
    // line-placed label is left out: when no minimum distance is stated the decoder floors it at
    // the label's own size, which is what stops a repeat of the same name being drawn twice where
    // two tiles cut the same road (TextSymbolizer, text-spacing). Writing 4 px there disabled that.
    if (symbolizer === 'text' && !followsLine(layer) && !out.some((d) => d.startsWith('text-min-distance:'))) {
        out.push(`text-min-distance: ${labelGap(options) * DEFAULT_TEXT_PADDING};`);
        coverage.emit('text-min-distance');
    }

    // MapBox repeats a line label every 250 px whether or not the layer says so; CartoCSS's spacing
    // defaults to 0, which is ONE label for the whole line - so a long road got its name once and a
    // contour ring got it once, where MapTiler writes it the length of both.
    if (symbolizer === 'text' && repeatsAlongLine(layer) && !out.some((d) => d.startsWith('text-spacing:'))) {
        out.push(`text-spacing: ${DEFAULT_SYMBOL_SPACING};`);
        coverage.emit('text-spacing');
    }

    // MapBox wraps at 10 ems whether or not the layer says so; CartoCSS's wrap-width defaults to 0,
    // which is no wrapping at all - so an unstated max-width has to be written out.
    if (symbolizer === 'text' && !followsLine(layer) && !out.some((d) => d.startsWith('text-wrap-width:'))) {
        const width = ems(DEFAULT_TEXT_MAX_WIDTH, layer, coverage, 'text-max-width');
        if (width !== null) {
            out.push(`text-wrap-width: ${width};`);
            coverage.emit('text-wrap-width');
        }
    }

    // text-name is what makes a text symbolizer exist at all; without it every other text property
    // is dropped with the rule - but an icon-only layer still has its marker to draw.
    if (symbolizer === 'text' && !out.some((d) => d.startsWith('text-name:'))) {
        // An icon stands on its own; a plate is a background FOR text and would be a floating box.
        if (isShield) {
            coverage.drop(`layer "${layer.id}"`, 'shield with no usable text-field, so no plate either', layer.id);
            return [];
        }
        if (out.length > 0 && iconDeclarations.length === 0) {
            coverage.drop(`layer "${layer.id}"`, 'symbol layer with neither text-field nor a usable icon', layer.id);
        }
        return iconDeclarations;
    }

    // An icon AND text is ONE symbol in MapBox. Emitted as a marker beside a text label they are
    // two labels that collide, and the marker wins - a city dot with no name beside it. The shield
    // is the one-label construct, so the whole rule is renamed into it.
    const shieldFile = iconDeclarations.find((d) => d.startsWith('shield-file:'));
    if (shieldFile) {
        const renamed = out.map(asShieldDeclaration).filter((d): d is string => d !== null);
        return [...renamed, ...iconDeclarations];
    }

    return [...out, ...iconDeclarations];
}

/**
 * A road shield as the plate CartoCSS draws behind a label: the sprite is tinted by icon-color and
 * outlined by its halo, and `text-background-*` is exactly that without needing the image. See
 * shield.ts for what makes a layer one.
 */
function plateDeclarations(layer: MapboxLayer, coverage: Coverage): string[] {
    const out: string[] = [];
    for (const [from, to] of PLATE_MAP) {
        emitTranslated(out, coverage, layer, from, to, undefined, false);
    }
    out.push(plateRadius());
    coverage.emit('text-background-radius');
    coverage.note(`"${layer.id}": shield sprite drawn as a text background plate, so the ` +
        'country-specific artwork is lost but the ref stays readable');
    return out;
}

/**
 * The image half of a shield. ShieldSymbolizer draws the bitmap at its own size - there is no
 * equivalent of marker-width - so icon-size cannot be carried, and unlock-image is what lets the
 * text move off the icon (a POI name sits below its pin).
 */
function shieldImageDeclarations(layer: MapboxLayer, icon: ExtractedIcon, coverage: Coverage): string[] {
    // shield-placement comes from the renamed text-placement - one label, one placement.
    const out = [`shield-file: url('${icon.file}');`, 'shield-unlock-image: true;'];
    coverage.emit('shield-file');
    coverage.emit('shield-unlock-image');

    // A distance field stays crisp at any size and takes its colour from the style, so an SDF
    // sprite goes in as one rather than being resolved to pixels and tinted here.
    if (icon.sdf) {
        out.push('shield-sdf: true;');
        coverage.emit('shield-sdf');
        emitTranslated(out, coverage, layer, 'icon-color', 'shield-icon-fill', undefined, false);
        emitTranslated(out, coverage, layer, 'icon-opacity', 'shield-icon-opacity', undefined, false);
    }

    // Locked to the image, the text is centred ON it. MapBox anchors the TEXT and leaves the icon
    // on the point, so the image is moved clear by half its height instead - a city name sits above
    // its dot, a POI name below its pin, and neither is drawn over the other.
    const clearance = iconClearance(layer, icon);
    if (clearance !== 0) {
        out.push(`shield-dy: ${clearance};`);
        coverage.emit('shield-dy');
    }
    return out;
}

/** Which way, and how far, the image moves so the text does not land on it. */
function iconClearance(layer: MapboxLayer, icon: ExtractedIcon): number {
    const anchor = layer.layout?.['text-anchor'];
    const offset = layer.layout?.['text-offset'];
    const dy = Array.isArray(offset) && typeof offset[1] === 'number' ? offset[1] : 0;

    // 'bottom' anchors the text's bottom edge, so the text is ABOVE and the icon goes below.
    const below = anchor === 'bottom' || (anchor === undefined && dy < 0);
    const above = anchor === 'top' || (anchor === undefined && dy > 0);
    if (!below && !above) return 0;
    return round((below ? 1 : -1) * icon.height / 2);
}

/**
 * One number to bake an icon-size into the bitmap with. A zoom ramp has no single answer, so it
 * takes the mean of its stops - the icon is then right in the middle of the range and a little off
 * at both ends, which beats drawing every sprite at full size.
 */
function representativeScale(size: Json | undefined, fallback = 1): number {
    if (size === undefined) return fallback;
    if (typeof size === 'number') return size;

    const numbers: number[] = [];
    const walk = (value: Json): void => {
        if (typeof value === 'number') numbers.push(value);
        else if (Array.isArray(value)) value.forEach((item) => walk(item as Json));
        else if (value && typeof value === 'object') Object.values(value).forEach((item) => walk(item as Json));
    };
    // Drop the zoom keys: in `interpolate(…, 6, 0.6, 14, 0.7)` only every second number is a size.
    // interpolate(…, zoom, z0, v0, z1, v1) puts its values at 4, 6, …; step(zoom, v0, z1, v1) at 2, 4, …
    if (Array.isArray(size) && (size[0] === 'interpolate' || size[0] === 'step')) {
        for (let i = size[0] === 'step' ? 2 : 4; i < size.length; i += 2) walk(size[i] as Json);
    } else {
        walk(size);
    }
    const sizes = numbers.filter((n) => n > 0);
    return sizes.length === 0 ? fallback : sizes.reduce((a, b) => a + b, 0) / sizes.length;
}

/** MapBox's icon-* onto marker-*, once the sprite has been sliced into its own file. */
function markerDeclarations(layer: MapboxLayer, coverage: Coverage, options: ConvertOptions): string[] {
    const image = layer.layout?.['icon-image'];
    if (image === undefined) return [];
    if (typeof image !== 'string') {
        coverage.drop('icon-image', 'data-driven icon name', layer.id);
        return [];
    }
    if (!options.sprites) {
        coverage.drop('icon-image', 'no sprite loaded (pass --sprite or let the style provide one)', layer.id);
        return [];
    }

    // With text beside it the icon becomes a SHIELD - one label, no collision with its own name.
    // ShieldSymbolizer has no `sdf`, so the field is resolved and the style's icon-color baked in.
    const asShield = layer.layout?.['text-field'] !== undefined;
    // A shield has no image-size property, so icon-size is resampled into the bitmap. The COLOUR
    // is not baked: shield-sdf keeps the distance field, and shield-icon-fill tints it.
    const scale = asShield ? representativeScale(layer.layout?.['icon-size']) : 1;
    const icon = extractIcon(options.sprites.sheets, image, options.sprites.outDir, options.flattenSdf,
        undefined, scale);
    if (!icon) {
        coverage.drop('icon-image', `"${image}" is not in the sprite`, layer.id);
        return [];
    }

    if (asShield) return shieldImageDeclarations(layer, icon, coverage);

    const out = [
        `marker-file: url('${icon.file}');`,
        `marker-placement: '${resolvePlacement(layer, 'icon')}';`,
    ];
    coverage.emit('marker-file');
    coverage.emit('marker-placement');

    const sdf = icon.sdf && !options.flattenSdf;
    if (sdf) {
        out.push('marker-sdf: true;');
        coverage.emit('marker-sdf');
    }

    // icon-size scales the sprite's own size. A distance field is SCALED rather than resampled, so
    // a zoom-driven size survives; a flattened bitmap can only take a constant.
    const size = layer.layout?.['icon-size'];
    const sizeExpr = size === undefined
        ? String(round(icon.width))
        : sdf
            ? scaledSize(size, icon.width, name('icon-size', layer, coverage))
            : typeof size === 'number' ? String(round(icon.width * size)) : null;
    if (sizeExpr === null) {
        coverage.drop('icon-size', 'a flattened bitmap cannot take a zoom-driven size (drop --sdf-flatten)', layer.id);
        out.push(`marker-width: ${round(icon.width)};`);
    } else {
        out.push(`marker-width: ${sizeExpr};`);
    }
    coverage.emit('marker-width');

    // An SDF icon carries no colour of its own, so marker-color IS the icon colour and MapBox
    // defaults it to black. A plain sprite already has its colours and must not be tinted.
    if (icon.sdf) {
        emitTranslated(out, coverage, layer, 'icon-color', 'marker-color', '#000000', !sdf);
    }
    emitTranslated(out, coverage, layer, 'icon-opacity', 'marker-opacity', undefined, !sdf);

    // Halos are grown from the field, so they only exist in SDF mode. The halo carries its own
    // opacity, so icon-opacity has to reach it too or a faded-out icon keeps a solid outline.
    if (sdf) {
        emitTranslated(out, coverage, layer, 'icon-halo-color', 'marker-halo-fill', undefined, false);
        emitTranslated(out, coverage, layer, 'icon-halo-width', 'marker-halo-radius', undefined, false);
        emitTranslated(out, coverage, layer, 'icon-opacity', 'marker-halo-opacity', undefined, false);
    }
    const overlap = layer.layout?.['icon-overlap'];
    if (layer.layout?.['icon-allow-overlap'] === true || overlap !== undefined) {
        out.push(`marker-allow-overlap: ${layer.layout?.['icon-allow-overlap'] === true || overlap === 'always'};`);
        coverage.emit('marker-allow-overlap');
    }
    return out;
}

function round(value: number): number {
    return Math.round(value * 100) / 100;
}

/**
 * What decides which of two colliding labels survives.
 *
 * MapBox's `symbol-sort-key` orders symbols only WITHIN a layer - between layers the style's own
 * order decides, later winning. The culler here compares one priority across every layer at once
 * and only falls back to the layer index, so a village with `rank 1` (priority -1) beat a town with
 * `rank 12` (-12) whatever layer each came from: Annecy's neighbours were drawn and Rumilly was not.
 *
 * Folding the layer's position in as the leading term restores MapBox's meaning - the sort key
 * still separates labels inside a layer, and it can no longer reach across one. The stride only has
 * to exceed the range a sort key spans (MapTiler's widest is the capital's -1000).
 */
const LAYER_PRIORITY_STRIDE = 100000;

function placementPriority(layer: MapboxLayer, layerIndex: number, coverage: Coverage): string | null {
    const base = layerIndex * LAYER_PRIORITY_STRIDE;
    const sortKey = layer.layout?.['symbol-sort-key'];
    if (sortKey === undefined) return String(base);

    const translated = tryTranslate(sortKey, 'symbol-sort-key', layer.id, coverage);
    // MapBox places the LOWEST key first, and the culler takes the highest priority.
    return translated === null ? String(base) : `(${base} - ${translated})`;
}

/**
 * What one unit of MapBox's text-padding is worth as a minimum-distance. MapBox pads a label's
 * collision box on EVERY side, so two labels end up at least twice the padding apart, while
 * minimum-distance is the one buffer between the pair - hence the 2. --label-spacing scales it for
 * a map that wants thinning beyond what the style asks for.
 */
function labelGap(options: ConvertOptions): number {
    return 2 * (options.labelSpacing ?? 1);
}

/** MapBox defaults for a layer that never states them. */
const DEFAULT_TEXT_SIZE = 16;
const DEFAULT_TEXT_MAX_WIDTH = 10;
const DEFAULT_TEXT_PADDING = 2;
const DEFAULT_SYMBOL_SPACING = 250;

/** A value in ems of the layer's own text-size, as the pixels CartoCSS wants. */
function ems(value: Json, layer: MapboxLayer, coverage: Coverage, from: string): string | null {
    const size = layer.layout?.['text-size'] ?? DEFAULT_TEXT_SIZE;
    if (typeof value === 'number' && typeof size === 'number') return String(round(value * size));

    const valueExpr = typeof value === 'number' ? String(round(value)) : tryTranslate(value, from, layer.id, coverage);
    const sizeExpr = typeof size === 'number' ? String(size) : tryTranslate(size, 'text-size', layer.id, coverage);
    return valueExpr === null || sizeExpr === null ? null : `(${valueExpr} * ${sizeExpr})`;
}

/** icon-size is a multiplier on the sprite's own width. */
function scaledSize(size: Json, width: number, translated: string | null): string {
    if (typeof size === 'number') return String(round(width * size));
    return translated === null ? String(round(width)) : `(${translated} * ${round(width)})`;
}

/** Translates a MapBox paint/layout value onto a CartoCSS property, or reports why it could not. */
function emitTranslated(
    out: string[],
    coverage: Coverage,
    layer: MapboxLayer,
    from: string,
    to: string,
    fallback: string | undefined,
    constantOnly: boolean,
): void {
    const value = layer.paint?.[from] ?? layer.layout?.[from];
    if (value === undefined) {
        if (fallback !== undefined) {
            out.push(`${to}: ${fallback};`);
            coverage.emit(to);
        }
        return;
    }
    if (constantOnly && typeof value !== 'string' && typeof value !== 'number') {
        coverage.drop(from, `a flattened bitmap needs a constant ${from}`, layer.id);
        return;
    }
    const translated = name(from, layer, coverage);
    if (translated !== null) {
        out.push(`${to}: ${translated};`);
        coverage.emit(to);
    }
}

/** The translated form of a layer property, counting the drop itself when it has none. */
function name(property: string, layer: MapboxLayer, coverage: Coverage): string | null {
    const value = layer.paint?.[property] ?? layer.layout?.[property];
    if (value === undefined) return null;
    return tryTranslate(value, property, layer.id, coverage);
}

/** Second chance for a value one of whose branches has no CartoCSS form: keep the fallback branch. */
function retryCollapsed(value: Json, layer: MapboxLayer, coverage: Coverage): string | null {
    const collapsed = collapseBranches(value);
    if (JSON.stringify(collapsed) === JSON.stringify(value)) return null;
    const translated = tryTranslate(collapsed, 'text-field', layer.id, coverage);
    if (translated !== null) {
        coverage.approximate(`text-field on "${layer.id}" kept only its fallback branch: another ` +
            'branch has no CartoCSS form, and no label at all is worse than the common one');
    }
    return translated;
}

/**
 * text-font is a LIST of faces in preference order and CartoCSS takes one name. A `match` over the
 * feature wraps each list in `["literal", [...]]`, and after the split collapses that branch the
 * value is the literal itself - reading element 0 then emitted the face name `literal`, which
 * resolves to nothing and drew eight layers in the fallback font.
 */
function firstFontName(value: Json): Json | undefined {
    if (Array.isArray(value) && value[0] === 'literal' && Array.isArray(value[1])) {
        return firstFontName(value[1] as Json);
    }
    return Array.isArray(value) && typeof value[0] === 'string' ? value[0] : undefined;
}

function tryTranslate(value: Json, name: string, layerId: string, coverage: Coverage): string | null {
    const notes: string[] = [];
    try {
        const fontList = name === 'text-font' ? firstFontName(value) : undefined;
        const translated = fontList !== undefined
            ? translateExpression(fontList, notes)
            : translateExpression(value, notes);
        notes.forEach((note) => coverage.approximate(note));
        return translated;
    } catch (error) {
        coverage.drop(name, describe(error), layerId);
        return null;
    }
}

function remapValue(target: string, translated: string): string {
    const map = VALUE_MAP[target];
    if (!map) return translated;
    const bare = translated.replace(/^'|'$/g, '');
    return map[bare] !== undefined ? `'${map[bare]}'` : translated;
}

/**
 * A CartoCSS project is ONE datasource; a MapBox style may draw from several tilesets and says so
 * per layer. Flattened into one project the difference disappears, and every source-layer that came
 * from another tileset silently draws nothing - which is what hid MapTiler topo-v4's peaks, since
 * `peak` and `volcano` live in its `landform` source and not in the planet tiles the rest uses.
 *
 * There is nothing to translate here: the app has to point a layer, or a composite slot, at each
 * tileset. Naming them is what turns "the peaks are missing" into a wiring question.
 */
function reportSources(style: MapboxStyle, layers: MapboxLayer[], coverage: Coverage): void {
    const bySource = new Map<string, Set<string>>();
    for (const layer of layers) {
        const source = (layer as { source?: string }).source;
        const sourceLayer = layer['source-layer'];
        if (!source || !sourceLayer) continue;
        if (!bySource.has(source)) bySource.set(source, new Set());
        bySource.get(source)!.add(sourceLayer);
    }
    if (bySource.size < 2) return;

    const urlOf = (name: string): string => {
        const source = (style as { sources?: Record<string, { url?: string; tiles?: string[] }> }).sources?.[name];
        return source?.url ?? source?.tiles?.[0] ?? '';
    };
    const biggest = [...bySource.entries()].sort((a, b) => b[1].size - a[1].size)[0][0];
    const others = [...bySource.entries()]
        .filter(([name]) => name !== biggest)
        .map(([name, set]) => `  "${name}" (${[...set].sort().join(', ')})${urlOf(name) ? ` <- ${urlOf(name)}` : ''}`);

    coverage.note(
        `the style draws from ${bySource.size} tilesets and a CartoCSS project has one. "${biggest}" ` +
        'is the biggest and is what the project assumes; every layer below needs its own datasource ' +
        'or composite slot in the app, or it draws nothing:\n' + others.join('\n'),
    );
}

/**
 * One entry in the project's `layers` pulls EVERY attachment of that name, so two MapBox layers on
 * different source-layers cannot be interleaved. Report each inversion rather than let the style
 * quietly draw in the wrong order.
 */
function reportInterleaving(layers: MapboxLayer[], order: Map<string, number>, coverage: Coverage): void {
    let inversions = 0;
    let previous = -1;
    for (const layer of layers) {
        const sourceLayer = layer['source-layer'];
        if (!sourceLayer || !order.has(sourceLayer)) continue;
        const position = order.get(sourceLayer)!;
        if (position < previous) inversions++;
        previous = position;
    }
    if (inversions > 0) {
        coverage.note(
            `${inversions} layer(s) draw out of order: a CartoCSS project entry pulls every ` +
            `attachment of its source-layer, so interleaved source-layers cannot be preserved.`,
        );
    }
}

function describe(error: unknown): string {
    return error instanceof Untranslatable ? `untranslatable: ${error.what}` : String(error);
}
