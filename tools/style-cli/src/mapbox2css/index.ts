import { type ContourOptions, isContourLayer, rewriteContourFields, rewriteContourFilter } from './contour.js';
import { Coverage } from './coverage.js';
import { Untranslatable, expandTokens, translateExpression } from './expression.js';
import { translateFilter, zoomPredicates } from './filter.js';
import { HANDLED_ELSEWHERE, followsLine, repeatsAlongLine, resolvePlacement } from './placement.js';
import { KNOWN_GAPS, LAYER_SYMBOLIZER, PROPERTY_MAP, VALUE_MAP } from './properties.js';
import { PLATE_MAP, asShieldDeclaration, isShieldLayer, plateRadius } from './shield.js';
import { type ExtractedIcon, type IconPlate, type SpriteSet, extractAllIconPlates, extractAllIcons, extractIcon, extractIconPlate } from './sprite.js';
import { type Schema, dropMissingFieldTests, mapSourceLayer, withMappingFilter } from './schema.js';
import { collapseBranches, splitLayer } from './split.js';
import { type HoistBlock, hoistVariables, paletteHeader } from './variables.js';
import { LIGHT_PRESET, importOnly, presetsOf, resolveConfig, sceneBrightness } from './config.js';
import { ICON_PARAMS, ICON_PARAM_SCOPE, type IconParamScope, RECOLOURABLE_ICON, foldConfig, foldLayer } from './fold.js';
import { applyLighting, emissiveProperty, lightingFactor } from './emissive.js';
import type { CartoProperty, Json, MapboxLayer, MapboxStyle, PropertyTable } from './types.js';

export interface ConvertResult {
    mss: string;
    project: string;
    coverage: Coverage;
    /** The palette stylesheet, for `variables.mss`. Null when hoisting is off. */
    variables: string | null;
    /** One more palette per extra `lightPreset`, over the SAME style.mss. Keyed by preset name. */
    presets: Map<string, string>;
    /** styleparameters a preset restates, merged over the shared project by `extends`. */
    presetOverrides?: Map<string, Record<string, Json>>;
    /** The preset project.json itself is, when there are others - so all of them have a name. */
    defaultPreset: string | null;
}

/** The palette is a stylesheet of its own, and has to be listed before the rules that read it. */
export const VARIABLES_FILE = 'variables.mss';

/** One pass of the emitter, for one config. See emitAll. */
interface EmitResult {
    mapBlock: string[];
    blocks: Array<{ selector: string; owner: string; declarations: string[] }>;
    order: Map<string, number>;
    brightness: number | null;
}

/** Two passes share a stylesheet only if they emitted the same rules with the same properties. */
function aligns(a: EmitResult, b: EmitResult): boolean {
    if (a.blocks.length !== b.blocks.length || a.mapBlock.length !== b.mapBlock.length) return false;
    const property = (declaration: string): string => declaration.slice(0, declaration.indexOf(':'));
    const same = (x: string[], y: string[]): boolean =>
        x.length === y.length && x.every((d, i) => property(d) === property(y[i]));
    if (!same(a.mapBlock, b.mapBlock)) return false;
    return a.blocks.every((block, i) =>
        block.selector === b.blocks[i].selector && same(block.declarations, b.blocks[i].declarations));
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

/**
 * One constant out of a value that branches, for the properties where a per-feature answer is not
 * available: an anchor and an offset are baked into the label's layout when the rule is built.
 *
 * The branch taken is the LAST - the highest step key, a case's fallback. That is the detailed end
 * of the ramp and what most features get: Standard anchors a POI by
 * `step(sizerank, "center", 5, "top")` and every POI in a Paris tile has sizerank 16.
 */
function representativeConstant(value: Json): Json | null {
    if (value === null || value === undefined) return null;
    if (typeof value === 'string' || typeof value === 'number') return value;
    if (!Array.isArray(value)) return null;

    const head = value[0];
    if (head === 'literal') return (value[1] ?? null) as Json;
    // `step` and `interpolate` both end on a value; `interpolate` carries a curve and an input
    // first, which changes nothing about where the last one is.
    if ((head === 'step' || head === 'interpolate') && value.length >= 4) {
        return representativeConstant(value[value.length - 1] as Json);
    }
    if ((head === 'case' || head === 'match') && value.length >= 4) {
        return representativeConstant(value[value.length - 1] as Json);
    }
    // A plain array of scalars is already the value - an offset pair.
    if (value.every((v) => typeof v === 'number')) return value as unknown as Json;
    return null;
}

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
    /** Retarget the style's source layers at another tile schema - see schema.ts. */
    schema?: Schema;
    /** Filled in during conversion: the style parameters the output declares (icons, colour tables). */
    styleParams?: Map<string, Json>;
    /** Filled in during conversion: any icon, for the size and offset a per-feature name has none of. */
    iconSample?: ExtractedIcon | null;
    /** Hoist colours, fonts and shared sizes into a palette stylesheet. On unless turned off. */
    variables?: boolean;
    /**
     * Extra `lightPreset` values to emit a palette for, sharing one style.mss with the default.
     * Empty for a style that has no such config - see config.ts.
     */
    presets?: string[];
    /**
     * Values for the style's own `config` knobs (Mapbox Standard's `schema`), overriding its
     * defaults. Every config read is resolved to a constant - see fold.ts for why it has to be.
     */
    config?: Record<string, Json>;
}

export function convert(style: MapboxStyle, table: PropertyTable, options: ConvertOptions = {}): ConvertResult {
    options = { ...options, styleParams: options.styleParams ?? new Map() };
    const coverage = new Coverage();
    const allowed = new Map<string, CartoProperty>(table.properties.map((p) => [p.cartocss, p]));

    // A style may be configurable (Mapbox Standard). Its config is resolved to constants BEFORE
    // anything is translated: CartoCSS has no `let`/`to-hsla`/`at`, so a colour left reading its
    // config converts to nothing at all. See fold.ts.
    const importsOnly = importOnly(style);
    if (importsOnly) coverage.drop('the whole style', importsOnly, 'imports');
    const { parameters, undeclared } = resolveConfig(style);
    for (const name of undeclared) {
        coverage.approximate(`config "${name}" is read but declared in no schema; took what it was read as`);
    }
    const configValues = new Map<string, Json>(
        [...parameters].map(([name, spec]) => [name, spec.default]));
    for (const [name, value] of Object.entries(options.config ?? {})) configValues.set(name, value);
    if (configValues.size > 0) {
        coverage.approximate(`${configValues.size} config values baked in` +
            (configValues.has(LIGHT_PRESET) ? ` (${LIGHT_PRESET} = ${String(configValues.get(LIGHT_PRESET))})` : ''));
    }

    // The scene's brightness is a style value here, not a runtime one - see config.ts.
    const lights = (style as unknown as Record<string, Json>).lights;

    // Set while emitting, so the parameter is only declared when there is a building to gate.
    let usesBuildings = false;

    // The brightness of the style's OWN default preset, which the emissive fold measures against.
    // sceneBrightness is an ambient-only proxy, so its absolute value is not a light level - but
    // the RATIO between two presets of the same style is meaningful, and that is all this needs:
    // the default preset comes out unlit (factor 1) and every darker one follows it down.
    const defaultBrightness = lights === undefined ? null
        : sceneBrightness(lights, (node) => foldConfig(node, new Map(
            [...parameters].map(([name, spec]) => [name, spec.default]))));

    // Emitting is a function of the config, because a configurable style is converted ONCE PER
    // PRESET: folding preserves structure (see fold.ts), so the runs line up block for block and
    // only their literals differ - which is what lets one style.mss carry a palette per preset.
    function emitAll(values: Map<string, Json>, coverage: Coverage): EmitResult {
    const brightness = lights === undefined ? null : sceneBrightness(lights, (node) => foldConfig(node, values));
    const scene = brightness === null ? {} : { brightness };
    // Always, even for a style with no config of its own: the viewport terms fold here too, and a
    // filter left testing the pitch is untranslatable, which drops the whole LAYER.
    const layers = (style.layers ?? []).map((layer) => foldLayer(layer, values, scene));

    const mapBlock: string[] = [];
    // Kept as selector + declarations rather than joined text: the palette pass rewrites the
    // declarations after every layer is in, when it can tell a shared colour from a layer's own.
    const blocks: Array<{ selector: string; owner: string; declarations: string[] }> = [];
    // Source-layer name -> every MapBox layer index that draws it. One project entry pulls ALL of
    // them, so the whole source-layer has to sit at one depth and the question is which.
    const positions = new Map<string, number[]>();
    // The TARGET source layer of every layer that made it through, in style order. Kept separately
    // because --schema renames them, and the interleaving check has to see what was emitted.
    const emitted: string[] = [];
    // One map has one answer for each building setting; the first layer to state it wins.
    const buildingSettingsSeen = new Set<string>();

    layers.forEach((layer, index) => {
        if (layer.type === 'background') {
            mapBlock.push(...light(layer, backgroundProperties(layer, coverage)));
            return;
        }

        // A fill-extrusion's LOOK is a Map setting here, not a symbolizer property: the SDK lights
        // and bevels every building at once. Taken from the layers that draw the BUILDING source
        // layer only - Standard's indoor walls state their own ambient occlusion and come first,
        // so reading every extrusion gave the whole map the shading of an indoor floor plan.
        if (layer.type === 'fill-extrusion' && BUILDING_LAYER.test(layer['source-layer'] ?? '')) {
            mapBlock.push(...buildingMapSettings(layer, buildingSettingsSeen, coverage));
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

        // Retargeting at another tile schema is a rename plus a filter clause - what the source
        // schema said by splitting into layers, the target says with a `class` field.
        let target = sourceLayer;
        let schemaLayer = layer;
        if (options.schema) {
            const { mapping, why } = mapSourceLayer(sourceLayer, options.schema);
            if (!mapping) {
                coverage.drop(`source-layer "${sourceLayer}"`, why ?? 'no equivalent', layer.id);
                return;
            }
            target = mapping.layer;
            const merged = dropMissingFieldTests(withMappingFilter(layer, mapping), mapping.layer, (field) =>
                coverage.approximate(
                    `"${field}" test dropped on "${layer.id}": the target schema has no such field, ` +
                    'and a test on it can only fail, which would draw nothing at all'));
            if (merged === false) {
                coverage.drop(`layer "${layer.id}"`, 'its filter can never match the target schema', layer.id);
                return;
            }
            schemaLayer = { ...layer, filter: (merged ?? undefined) as MapboxLayer['filter'] };
        }
        (positions.get(target) ?? positions.set(target, []).get(target)!).push(index);
        emitted.push(target);

        // The contour schemas name the elevation differently; renaming it here means the filter,
        // the label and every paint expression all see the field the tiles actually carry.
        const retargeted = options.contour
            ? (rewriteContourFields(schemaLayer as unknown as Json, options.contour) as unknown as MapboxLayer)
            : schemaLayer;

        // A field-driven paint value becomes one attachment per branch - see split.ts.
        const variants = splitLayer(isContourLayer(layer) ? retargeted : schemaLayer, coverage);
        variants.forEach((variant, branch) => {
            const suffix = variants.length > 1 ? `_b${branch + 1}` : '';
            emitLayer(variant, `${attachmentName(layer.id)}${suffix}`, target, symbolizer, index);
        });
    });

    /**
     * Mapbox lights its 2D layers; we draw their colours as authored. Folding the layer's own
     * emissive-strength into the colour is what makes a night preset read as night - see
     * emissive.ts, which is also where the limits of the approximation are written down.
     */
    function light(layer: MapboxLayer, declarations: string[]): string[] {
        if (brightness === null || !defaultBrightness) return declarations;
        const litFraction = Math.max(0, Math.min(1, brightness / defaultBrightness));
        return declarations.map((declaration) => {
            const colon = declaration.indexOf(':');
            const property = declaration.slice(0, colon);
            // Map settings are not symbolizer properties, so they are not in the table.
            const known = allowed.get(property);
            if (known ? known.kind !== 'color' : !property.endsWith('-color')) return declaration;
            const source = emissiveProperty(property);
            const stated = source ? layer.paint?.[source] : undefined;
            // Only where the style STATES one. A zoom ramp has no single answer, so it takes the
            // mean of its stops, as every other zoom-driven value baked in here does.
            const emissive = stated === undefined ? null
                : typeof stated === 'number' ? stated : representativeScale(stated, 1);
            const factor = lightingFactor(emissive, litFraction);
            if (factor === null) return declaration;
            const value = declaration.slice(colon + 1, declaration.lastIndexOf(';')).trim();
            const lit = applyLighting(value, factor);
            return lit === value ? declaration : `${property}: ${lit};`;
        });
    }

    function emitLayer(layer: MapboxLayer, attachment: string, sourceLayer: string, symbolizer: string, layerIndex: number): void {
        const paramised = paramiseValues(layer, options, coverage);
        let declarations = layerDeclarations(paramised.layer, symbolizer, allowed, coverage, options, layerIndex);
        if (paramised.subs.size > 0) {
            declarations = declarations.map((declaration) => {
                for (const [sentinel, lookup] of paramised.subs) declaration = declaration.split(sentinel).join(lookup);
                return declaration;
            });
        }
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
            const buildings = buildingPredicate(symbolizer, sourceLayer);
            if (buildings) usesBuildings = true;
            const predicates = [
                ...zoomPredicates(layer.minzoom, layer.maxzoom),
                ...(buildings ? [buildings] : []),
                ...translateFilter(filter),
            ].map((p) => (p.startsWith('when(') ? ` ${p}` : p));
            selector = `#${sourceLayer}${predicates.join('')}::${attachment}`;
        } catch (error) {
            const why = error instanceof Untranslatable ? error.what : String(error);
            coverage.drop(`filter on "${layer.id}"`, `untranslatable filter: ${why}`, layer.id);
            return;
        }

        blocks.push({ selector, owner: layer.id, declarations: light(layer, declarations) });
    }

    if (usesBuildings) {
        options.styleParams!.set(BUILDINGS_PARAM, BUILDINGS_3D);
        // Whatever the layers did not state, so a converted style is not left on the SDK's own
        // (OSM-tuned) building lighting - see BUILDING_MAP_DEFAULTS.
        if (lights !== undefined) {
            mapBlock.push(...buildingLightSettings(lights, buildingSettingsSeen, coverage,
                (node) => foldConfig(node, values)));
        }
    }
    // The MEDIAN index, not the first. `road` has 82 layers spanning indices 3 to 130 in Mapbox
    // Standard, and its FIRST is one early tunnel layer - ordering by that sank all 82 beneath
    // landuse, so the landuse polygons painted over every road. The median puts a source-layer
    // where most of its layers actually are, which is the depth that misorders the fewest.
    const order = new Map<string, number>([...positions].map(([name, at]) => {
        const sorted = [...at].sort((a, b) => a - b);
        return [name, sorted[sorted.length >> 1]];
    }));
    reportInterleaving(emitted, order, coverage);
    reportSources(style, layers, coverage);
    return { mapBlock, blocks, order, brightness };
    }

    const base = emitAll(configValues, coverage);
    if (base.brightness !== null) {
        coverage.approximate(`["measure-light", "brightness"] resolved to ${base.brightness.toFixed(2)} from ` +
            "the style's own ambient light; our renderer cannot measure it back, and a ramp over it " +
            'whose stops are per-feature expressions snaps to the nearer end');
    }
    const { mapBlock, blocks, order } = base;

    // The Map block goes through the palette pass with the rules: a variant that recolours the
    // water and not the background behind it is not a variant anybody wants.
    const withMap = (result: EmitResult): HoistBlock[] =>
        [{ owner: 'map', declarations: result.mapBlock }, ...result.blocks];

    // Every preset is converted, and all of them are hoisted TOGETHER. A variable then stands for
    // the same sites in each one, so the palettes share `style.mss` and differ only in their
    // values. Hoisting each preset on its own instead let a colour two layers happen to share by
    // day - and not by night - name itself differently in the two files.
    const presetConfigs = new Map<string, Map<string, Json>>();
    for (const preset of options.presets ?? presetsOf(parameters)) {
        if (preset === configValues.get(LIGHT_PRESET)) continue;
        presetConfigs.set(preset, new Map(configValues).set(LIGHT_PRESET, preset));
    }
    // A parameter TABLE is a per-preset value like a palette entry: poi-label's colours are a
    // per-class table whose entries ramp with the scene brightness. Written into the shared
    // project they were simply overwritten by whichever preset was emitted last - which is how
    // every preset ended up drawing its POIs in the NIGHT colours.
    const presetParams = new Map<string, Map<string, Json>>();
    presetParams.set('', new Map(options.styleParams!));

    const alternates = new Map<string, EmitResult>();
    for (const [preset, values] of presetConfigs) {
        options.styleParams!.clear();
        const result = emitAll(values, new Coverage());
        presetParams.set(preset, new Map(options.styleParams!));
        // Only a run that lines up block for block can share the rules; folding is written to
        // guarantee that, and this is the check that it held.
        if (aligns(base, result)) alternates.set(preset, result);
        else coverage.drop(`preset "${preset}"`, 'its layers do not line up with the default preset', preset);
    }
    // Back to the default preset's table, which is what the shared project carries.
    options.styleParams!.clear();
    for (const [name, value] of presetParams.get('') ?? []) options.styleParams!.set(name, value);

    let hoisted = withMap(base);
    let palette: string[] = [];
    const presetPalettes = new Map<string, string>();
    let defaultPreset: string | null = null;
    if (options.variables !== false) {
        const result = hoistVariables(hoisted, allowed, [...alternates.values()].map(withMap));
        hoisted = result.blocks;
        palette = result.palette;
        for (const [index, preset] of [...alternates.keys()].entries()) {
            presetPalettes.set(preset, [...paletteHeader(style.name, preset), '', ...result.alternates[index], ''].join('\n'));
        }
        // The DEFAULT preset gets a named project too, so all four are picked the same way -
        // CompiledStyleSet takes a style NAME and finds <name>.json, and "project" is not one.
        const fallback = configValues.get(LIGHT_PRESET);
        if (presetPalettes.size > 0 && typeof fallback === 'string') defaultPreset = fallback;
    }
    const selectors = ['Map', ...blocks.map((block) => block.selector)];
    const [mapRule, ...ruleBlocks] = hoisted.map((block, index) => ({ ...block, selector: selectors[index] }));

    const header = [
        '/* Generated by massif-style mapbox2css. Do not edit by hand:',
        '   re-run the converter against the source style instead. */',
    ];
    if (palette.length > 0) header.push(`/* Colours, fonts and shared sizes live in ${VARIABLES_FILE}. */`);
    if (style.name) header.push(`/* Source style: ${style.name} */`);

    const render = (block: { selector: string; declarations: string[] }): string =>
        `${block.selector} {\n${block.declarations.map((d) => `  ${d}`).join('\n')}\n}`;

    const mss = [
        ...header,
        '',
        ...(mapRule.declarations.length > 0 ? [render(mapRule), ''] : []),
        ...ruleBlocks.map(render),
        '',
    ].join('\n');

    const variables = palette.length > 0 ? [...paletteHeader(style.name), '', ...palette, ''].join('\n') : null;

    // loadMapProject REVERSES this array (layerNames.insert(begin)), and MapBox layers run
    // bottom-to-top, so the project list is the draw order reversed.
    const projectLayers = [...order.entries()].sort((a, b) => a[1] - b[1]).map(([name]) => name).reverse();
    const styles = variables ? [VARIABLES_FILE, 'style.mss'] : ['style.mss'];
    const project = JSON.stringify(
        options.styleParams!.size > 0
            ? { styles, layers: projectLayers, styleparameters: Object.fromEntries([...options.styleParams!].sort()) }
            : { styles, layers: projectLayers },
        null, 2) + '\n';

    // Only what DIFFERS from the shared table: a preset project extends project.json, and
    // `extends` merges styleparameters, so an entry it does not restate is inherited.
    const shared = presetParams.get('') ?? new Map<string, Json>();
    const presetOverrides = new Map<string, Record<string, Json>>();
    for (const [preset, params] of presetParams) {
        if (preset === '') continue;
        const differing = [...params].filter(([name, value]) => shared.get(name) !== value);
        if (differing.length > 0) presetOverrides.set(preset, Object.fromEntries(differing.sort()));
    }

    return { mss, project, coverage, variables, presets: presetPalettes, defaultPreset, presetOverrides };
}

/**
 * MapBox states a building's shading per LAYER; the SDK shades every building at once, from the Map
 * block (`CartoCSSMapLoader`'s float settings). The names line up one for one, so the value is
 * carried verbatim - a zoom ramp included, since these are FloatFunctionProperty and evaluated per
 * frame. A boolean becomes 1/0 for the same reason.
 *
 * Left out because the SDK has no equivalent: `flood-light-*` (a per-building light source),
 * `cutoff-fade-range` and `vertical-scale`. `emissive-strength` is approximated into the colour
 * instead - see emissive.ts.
 */
/**
 * What MapBox itself uses when a layer states nothing. The SDK's own defaults are tuned for the
 * hand-written OSM style - ambient 0.35, ao-intensity 0.2, ao-attenuation 1.7 - and a converted
 * MapBox style that inherits them comes out markedly darker than the browser draws it. Stating
 * MapBox's defaults explicitly is what keeps the two comparable.
 */
const BUILDING_MAP_DEFAULTS: Record<string, string> = {
    // gl-js clamps a wall's shading to a FLOOR of about 0.7 (fill_extrusion.vertex.glsl mixes
    // 0.7..0.98 by light intensity); the SDK's wall factor is `1 - gradient` at the foot, so 0.3
    // puts the foot in the same place. Its own default of 0.65 takes it to 0.35 - less than half
    // the brightness gl-js gives it, which is why our side walls read so much darker.
    'building-vertical-gradient': '0.3',
    'building-ao-intensity': '0',              // gl-js: ambient-occlusion-intensity 0
    'building-ao-ground-radius': '3',          // gl-js: ambient-occlusion-ground-radius 3
    'building-ao-ground-attenuation': '0.69',  // gl-js: ambient-occlusion-ground-attenuation 0.69
    'building-edge-radius': '0',               // gl-js: edge-radius 0
    'building-rounded-roof': '1',              // gl-js: rounded-roof true
};

const BUILDING_MAP_SETTINGS: Record<string, string> = {
    'fill-extrusion-vertical-gradient': 'building-vertical-gradient',
    'fill-extrusion-ambient-occlusion-intensity': 'building-ao-intensity',
    'fill-extrusion-ambient-occlusion-ground-radius': 'building-ao-ground-radius',
    'fill-extrusion-ambient-occlusion-ground-attenuation': 'building-ao-ground-attenuation',
    'fill-extrusion-edge-radius': 'building-edge-radius',
    'fill-extrusion-rounded-roof': 'building-rounded-roof',
};

function buildingMapSettings(layer: MapboxLayer, seen: Set<string>, coverage: Coverage): string[] {
    const out: string[] = [];
    for (const [from, to] of Object.entries(BUILDING_MAP_SETTINGS)) {
        // `fill-extrusion-edge-radius` is a LAYOUT property, not a paint one - reading only paint
        // dropped Standard's 0.4 bevel silently.
        const value = layer.paint?.[from] ?? layer.layout?.[from];
        if (value === undefined || seen.has(to)) continue;
        const translated = typeof value === 'boolean'
            ? (value ? '1' : '0')
            : tryTranslate(value, from, layer.id, coverage);
        if (translated === null) continue;
        seen.add(to);
        out.push(`${to}: ${translated};`);
        coverage.emit(to);
    }
    return out;
}

/**
 * The building settings a converted MapBox style still needs after its own paint is read: MapBox's
 * defaults for what it left unstated, and its LIGHTS as the SDK's two building light knobs. The
 * lights block is MapBox's whole lighting model, and `building-ambient` / `building-light-intensity`
 * are the same two numbers - the SDK's own 0.35 ambient is what made a white building read grey.
 */
function buildingLightSettings(lights: Json, seen: Set<string>, coverage: Coverage,
        fold: (node: Json) => Json): string[] {
    const out: string[] = [];
    // The intensity may be a zoom RAMP, and these are FloatFunctionProperty, so it goes through
    // whole. Requiring a plain number left Standard's directional light unstated - its 0.2 is a
    // flat `interpolate(zoom, …)` - and the SDK fell back to 1.0, five times as strong, which is
    // what made every wall so much darker than the browser draws it.
    const intensityOf = (type: string): string | null => {
        const folded = fold(lights);
        if (!Array.isArray(folded)) return null;
        const light = folded.find((l) => !!l && typeof l === 'object'
            && (l as Record<string, Json>).type === type) as Record<string, Json> | undefined;
        const intensity = (light?.properties as Record<string, Json> | undefined)?.intensity;
        if (intensity === undefined) return null;
        if (typeof intensity === 'number') return String(round(intensity));
        try {
            return translateExpression(intensity);
        } catch {
            return null;
        }
    };
    for (const [type, name] of [['ambient', 'building-ambient'], ['directional', 'building-light-intensity']]) {
        if (seen.has(name)) continue;
        const intensity = intensityOf(type);
        if (intensity === null) continue;
        seen.add(name);
        out.push(`${name}: ${intensity};`);
        coverage.emit(name);
    }
    // gl-js clamps a wall's shading to `mix(0.7, 0.98, 1 - lightIntensity)`
    // (fill_extrusion.vertex.glsl), so the floor RISES as the directional light weakens: at
    // Standard's 0.2 it is 0.92, not the 0.7 the constant alone suggests. The SDK's wall factor is
    // `1 - gradient` at the foot, so this is the same floor expressed its way. Measured: with a
    // flat 0.3 the foot sat at 0.63 of the roof where the browser keeps it near 0.9.
    const rawDirectional = ((): Json | undefined => {
        const folded = fold(lights);
        if (!Array.isArray(folded)) return undefined;
        const light = folded.find((l) => !!l && typeof l === 'object'
            && (l as Record<string, Json>).type === 'directional') as Record<string, Json> | undefined;
        return (light?.properties as Record<string, Json> | undefined)?.intensity;
    })();
    // A flat ramp is still one number - representativeConstant takes the branch most features get.
    const reduced = rawDirectional === undefined ? null : representativeConstant(rawDirectional);
    const constant = typeof reduced === 'number' ? reduced : null;
    if (!seen.has('building-vertical-gradient') && constant !== null && Number.isFinite(constant)) {
        const floor = 0.7 + (1 - Math.min(Math.max(constant, 0), 1)) * 0.28;
        seen.add('building-vertical-gradient');
        out.push(`building-vertical-gradient: ${round(1 - floor)};`);
    }

    for (const [name, value] of Object.entries(BUILDING_MAP_DEFAULTS)) {
        if (seen.has(name)) continue;
        seen.add(name);
        out.push(`${name}: ${value};`);
    }
    return out;
}

/**
 * The style parameter that turns buildings off, flat, or 3D - the same three states, spelled the
 * same way, as the hand-written styles under `assets/style`: `['param::buildings'>0]` gates the
 * footprint and `>1` the extrusion. An app then has one knob for every converted style, and can
 * drop the 3D pass on a device that cannot afford it without editing the CartoCSS.
 *
 * It defaults to 3D, so a converted style draws what its source drew until an app says otherwise.
 */
const BUILDINGS_PARAM = 'buildings';
const BUILDINGS_3D = 2;

/** A source layer of footprints, whatever the schema calls it. */
const BUILDING_LAYER = /building/i;

function buildingPredicate(symbolizer: string, sourceLayer: string): string | null {
    if (symbolizer === 'building') return `['param::${BUILDINGS_PARAM}'>1]`;
    return BUILDING_LAYER.test(sourceLayer) ? `['param::${BUILDINGS_PARAM}'>0]` : null;
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
        // A CASING, not a line: `line-gap-width` is the road it runs either side of, and
        // `line-width` is the casing on ONE side. Both are folded into the width here and the
        // fill layer above covers the middle, which is how a casing has always been drawn in
        // CartoCSS. Dropped, the casing drew as a solid band the full width of the road - which
        // is every road in Mapbox Standard, 28 layers of them, and the reason ours came out as
        // lavender slabs where the browser draws a white road with a thin edge.
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
        if (name === 'text-anchor') {
            const constant = typeof value === 'string' ? value : representativeConstant(value as Json);
            if (typeof constant !== 'string') {
                coverage.drop(name, 'anchor branches on something with no constant form', layer.id);
                continue;
            }
            if (constant !== value) {
                coverage.approximate(`text-anchor on "${layer.id}" branches; took "${constant}"`);
            }
            const anchor = TEXT_ANCHOR[constant];
            if (!anchor) {
                coverage.drop(name, `unknown anchor "${constant}"`, layer.id);
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
        if (name === 'text-offset') {
            const pair = Array.isArray(value) && value.length === 2 && value.every((v) => typeof v === 'number')
                ? value as Json[]
                : representativeConstant(value as Json) as Json[] | null;
            if (!Array.isArray(pair) || pair.length !== 2) {
                coverage.drop(name, 'offset branches on something with no constant form', layer.id);
                continue;
            }
            if (pair !== value) {
                coverage.approximate(`text-offset on "${layer.id}" branches; took [${pair.join(', ')}]`);
            }
            const dx = ems(pair[0], layer, coverage, name);
            const dy = ems(pair[1], layer, coverage, name);
            if (dx === null || dy === null) continue;
            out.push(`text-dx: ${dx};`, `text-dy: ${dy};`);
            coverage.emit('text-dx');
            coverage.emit('text-dy');
            // An offset TRANSLATES the text in MapBox, but with no alignment given the SDK reads a
            // non-zero dx/dy as the anchor instead (TextSymbolizer::getFormatterOptions), so
            // MapTiler's text-offset [0, 0.05] hung every road ref off its shield's bottom edge.
            // Pin the anchor the offset moves - MapBox's default, centre.
            if (layer.layout?.['text-anchor'] === undefined) {
                out.push(`text-horizontal-alignment: 'middle';`, `text-vertical-alignment: 'middle';`);
                coverage.emit('text-horizontal-alignment');
                coverage.emit('text-vertical-alignment');
            }
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
            // Not on a line-placed label, for the same reason the default below skips one: an
            // unstated minimum lets the decoder floor it at the label's own size, which is what
            // stops one road shield being drawn twice where two tiles cut the same road. MapBox's
            // 2 px is a collision pad, not a repeat distance, and writing it here disabled that
            // floor - two D 1508 shields a few pixels apart.
            if (followsLine(layer)) {
                coverage.drop('text-padding', 'a line-placed label keeps the decoder\'s own repeat floor', layer.id);
                continue;
            }
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

        // A pattern names a SPRITE, and the raw name reached the decoder as a file name that
        // never existed (`misc:construction_pattern`): the sheet qualifier only says where to LOOK.
        if (name === 'fill-pattern' || name === 'line-pattern') {
            out.push(...patternDeclarations(name, value as Json, table[name], layer, coverage, options));
            continue;
        }

        // MapBox DISABLES fill-color under a fill-pattern and fades the pattern with fill-opacity;
        // the pattern is its own symbolizer here, with its own fill and opacity, so a polygon-fill
        // beside it would paint a solid layer under the hatch and polygon-opacity would fade only
        // that. MapTiler's construction areas are a 0.15 hatch and drew fully saturated.
        if (layer.paint?.['fill-pattern'] !== undefined && (name === 'fill-color' || name === 'fill-opacity')) {
            if (name === 'fill-color') {
                coverage.drop(name, 'MapBox disables fill-color under a fill-pattern', layer.id);
                continue;
            }
            emitTranslated(out, coverage, layer, 'fill-opacity', 'polygon-pattern-opacity', undefined, false);
            continue;
        }

        // MapBox dash lengths are multiples of the line width; CartoCSS's are pixels.
        if (name === 'line-dasharray') {
            const pattern = dashPattern(value as Json);
            if (pattern === null) {
                coverage.drop(name, 'no literal dash pattern to take', layer.id);
                continue;
            }
            if (pattern !== value) {
                // MapTiler ramps its path dashes over zoom and CartoCSS takes ONE pattern. The
                // base (the widest band, and every stop below the first) is what is on screen at
                // nearly every zoom; taking nothing left every footway drawn solid.
                coverage.approximate(`line-dasharray taken at one stop, ${pattern.join(',')}: ` +
                    'CartoCSS takes one dash pattern, not a ramp');
            }
            const width = layer.paint?.['line-width'];
            const scale = representativeScale(width, 1);
            if (typeof width !== 'number') {
                // Taking 1 here made a lift's `[0.05, 4]` a 0.05-PIXEL dash, which is nothing at
                // all; the widths it ramps between are 3 and 4, so their mean is far closer.
                coverage.approximate(`line-dasharray scaled by ${round(scale)}, the mean of a ` +
                    'zoom-driven line-width: CartoCSS takes one dash pattern, not a ramp');
            }
            out.push(`line-dasharray: ${pattern.map((v) => round(v * scale)).join(',')};`);
            coverage.emit('line-dasharray');
            continue;
        }

        // A fill's outline is a second symbolizer on the same rule, not a polygon property.
        if (name === 'fill-outline-color') {
            const translated = tryTranslate(value, name, layer.id, coverage);
            if (translated !== null) {
                out.push(`line-color: ${translated};`, `line-width: ${FILL_OUTLINE_WIDTH};`);
                coverage.emit('line-color');
                coverage.approximate(
                    `fill-outline-color drawn as a line of width ${FILL_OUTLINE_WIDTH}: MapBox's is a ` +
                    '1-DEVICE-pixel hairline (gl.LINES) and a CartoCSS width scales with the display, ' +
                    'so no constant is right at every dpi');
            }
            continue;
        }

        // Placed with the icon instead (variableAnchorDeclarations): each says where the text goes
        // relative to one, so on a layer with no icon there is nothing for them to describe.
        if (VARIABLE_ANCHOR_LAYOUT.has(name)) {
            if (layer.layout?.['icon-image'] === undefined) {
                coverage.drop(name, 'positions the text against an icon, and this layer has none', layer.id);
            }
            continue;
        }

        // One occlusion opacity per LABEL: it is a TextSymbolizer property here, so a symbol's
        // icon and its text share it. MapBox states the pair together where it states both
        // (Standard's natural-point-label sets 0 twice), so the icon's is taken only where the
        // text states none - and an icon-only layer is a marker, which is not a label.
        //
        // Only a STATED value is carried, which is MapBox's own meaning: absent is "occluded by
        // the terrain alone", the SDK's default, and 0 is "occluded by 3D content too". That is
        // what makes Standard's road and water names go behind a building while its POI labels
        // stay drawn - and what stops every label paying for the occlusion pass.
        if (name === 'icon-occlusion-opacity') {
            if (layer.paint?.['text-occlusion-opacity'] !== undefined) {
                coverage.drop(name, 'the label carries one occlusion opacity, taken from text-occlusion-opacity', layer.id);
                continue;
            }
            if (layer.layout?.['text-field'] === undefined) {
                coverage.drop(name, 'an icon-only layer draws a marker, which is not a label', layer.id);
                continue;
            }
            const translated = tryTranslate(value, name, layer.id, coverage);
            if (translated === null) continue;
            out.push(`text-occlusion-opacity: ${translated};`);
            coverage.emit('text-occlusion-opacity');
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

    // MapBox's spacing is a SCREEN distance it keeps over the whole line; the decoder walks it in
    // tile units and restarts per FEATURE, so a road cut into many short ways got a shield on each
    // one - six "D 41" where MapTiler draws two. The culler is what measures across tiles and
    // features, so the same distance is handed to it as the floor between two labels of the group.
    const spacingDecl = out.find((d) => d.startsWith('text-spacing:'));
    if (symbolizer === 'text' && spacingDecl && !out.some((d) => d.startsWith('text-min-distance:'))) {
        out.push(`text-min-distance: ${spacingDecl.slice('text-spacing:'.length).trim().replace(/;$/, '')};`);
        coverage.emit('text-min-distance');
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
 * The disc under a recolourable icon, as the shield's icon PLATE.
 *
 * MapBox draws these as ONE image whose `params` colour a disc, its ring and the glyph, and the
 * sheet bakes only the icon's own defaults into its flat render. The glyph is the distance field
 * above - extractIconPlate cropped it to the disc's own box - so the plate needs no padding and its
 * border falls exactly where the artwork's ring was. All three colours are style properties here,
 * so they are evaluated per feature, which is what gets a POI its class colour back.
 */
function iconPlateDeclarations(layer: MapboxLayer, icon: ExtractedIcon, coverage: Coverage): string[] {
    const params = layer.layout?.[ICON_PARAMS] as Record<string, Json> | undefined;
    if (!icon.plate || !params) return [];
    const out: string[] = [];
    // Where the plate covers only some features, its colours are transparent for the others - a
    // plate with no fill and no border draws nothing (TileLabel::Style::Plate::draws).
    const scoped = (value: string) => (icon.plateWhen ? `(${icon.plateWhen} ? ${value} : transparent)` : value);
    const colour = (name: string, target: string, gate = false) => {
        if (params[name] === undefined) return;
        const translated = tryTranslate(params[name], `icon-image params.${name}`, layer.id, coverage);
        if (translated === null) return;
        out.push(`${target}: ${gate ? scoped(translated) : translated};`);
        coverage.emit(target);
    };

    // The glyph, unless the layer states an icon-color of its own - that one is already out.
    if (layer.paint?.['icon-color'] === undefined) colour('icon', 'shield-icon-fill');
    colour('background', 'shield-icon-background-fill', true);
    // Both paddings default to a text plate's, which would grow the disc off its own artwork.
    out.push(`shield-icon-background-radius: ${round(icon.plate.radius)};`,
        'shield-icon-background-padding-x: 0;',
        'shield-icon-background-padding-y: 0;');
    coverage.emit('shield-icon-background-radius');
    coverage.emit('shield-icon-background-padding-x');
    coverage.emit('shield-icon-background-padding-y');
    if (icon.plate.borderWidth > 0) {
        out.push(`shield-icon-background-border-width: ${round(icon.plate.borderWidth)};`);
        coverage.emit('shield-icon-background-border-width');
        colour('background-stroke', 'shield-icon-background-border-fill', true);
    }
    // MapBox's `icon-stroke` is the outline it draws UNDER the glyph, which is exactly what the
    // SDK grows from a distance field - so it is the icon HALO, not the plate's border (that one is
    // the disc's ring). Standard sets it transparent while its POI background is a circle, so this
    // is what a style asking for `backgroundPointOfInterestLabels: none` gets: a coloured glyph
    // with a white outline and no disc at all.
    if (params['icon-stroke'] !== undefined && icon.plate.strokeWidth > 0) {
        colour('icon-stroke', 'shield-icon-halo-fill');
        out.push(`shield-icon-halo-radius: ${round(icon.plate.strokeWidth)};`);
        coverage.emit('shield-icon-halo-radius');
    }
    return out;
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
function shieldImageDeclarations(layer: MapboxLayer, icon: ExtractedIcon, scale: number, coverage: Coverage): string[] {
    // shield-placement comes from the renamed text-placement - one label, one placement.
    // A per-feature name arrives as a whole expression (see dynamicIconDeclarations); a constant
    // one is just a path and takes the url() around it here.
    const fileValue = icon.file.startsWith('@@EXPR@@')
        ? icon.file.slice('@@EXPR@@'.length) : `url('${icon.file}')`;
    const out = [`shield-file: ${fileValue};`, 'shield-unlock-image: true;'];
    coverage.emit('shield-file');
    coverage.emit('shield-unlock-image');
    // icon-size is a zoom ramp of its own - mapbox animates an icon independently of its name - and
    // shield-image-scale takes a function, so the ramp goes through whole rather than collapsed to
    // a mean. Divided by the sheet's pixelRatio: a 2x sheet is drawn at half scale to come out the
    // size the style asked for.
    const sized = translateExpression(layer.layout?.['icon-size'] ?? 1);
    // The divisor is spelled as a float on purpose: `/` between two INTEGERS truncates
    // (Expression.cpp's DivOperator), so a default icon-size of 1 over a 2x sheet came out `(1) / 2`
    // = 0 and every icon of thirteen POI layers drew at zero size.
    const drawScale = `((${sized}) / ${icon.pixelRatio.toFixed(1)})`;
    out.push(`shield-image-scale: ${drawScale};`);
    coverage.emit('shield-image-scale');

    // A distance field stays crisp at any size and takes its colour from the style, so an SDF
    // sprite goes in as one rather than being resolved to pixels and tinted here.
    //
    // Only a sprite the SHEET calls a field. `shield-sdf` makes the renderer read the red channel
    // as signed distance (GlyphMode::SDF), so a colour bitmap handed to it comes out as whatever
    // its red channel happened to cross the edge on: Standard's transit icons are a blue disc with
    // a white glyph, and they drew as the glyph alone with the disc gone - the colours inverted.
    if (icon.sdf) {
        // Per FEATURE where the plate only covers some of them: a distance field for those, the
        // sheet's own artwork for the rest. `sdf` is a BoolProperty read with an expression
        // context, so the one rule can say both - which is what keeps Paris's own metro roundel
        // (white disc, blue ring, blue M) instead of recolouring it as a generic transit square.
        out.push(`shield-sdf: ${icon.plateWhen ?? 'true'};`);
        coverage.emit('shield-sdf');
        emitTranslated(out, coverage, layer, 'icon-color', 'shield-icon-fill', undefined, false);
        emitTranslated(out, coverage, layer, 'icon-opacity', 'shield-icon-opacity', undefined, false);
        // The icon's own outline. It is what draws the dark ring round a white city dot and the
        // white one round a POI glyph, and it is NOT the text halo - the two are separate in
        // mapbox and now separate here.
        emitTranslated(out, coverage, layer, 'icon-halo-color', 'shield-icon-halo-fill', undefined, false);
        emitTranslated(out, coverage, layer, 'icon-halo-width', 'shield-icon-halo-radius', undefined, false);
        out.push(...iconPlateDeclarations(layer, icon, coverage));
    } else if (layer.layout?.[RECOLOURABLE_ICON] === true) {
        // A recolourable sprite whose artwork is NOT a disc with a glyph on it (extractIconPlate
        // took it apart where it is): the sheet ships one flat render with the icon's own default
        // params and nothing here can tint it per feature.
        coverage.approximate(`"${layer.id}" recolours its icon per feature; drawn with the ` +
            "sprite sheet's own colours, so a per-class tint is lost");
    }

    out.push(...variableAnchorDeclarations(layer, coverage));

    // Locked to the image, the text is centred ON it. MapBox anchors the TEXT and leaves the icon
    // on the point, so the image is moved clear by half its height instead - a city name sits above
    // its dot, a POI name below its pin, and neither is drawn over the other.
    const clearance = iconClearance(layer, icon, scale);
    if (clearance !== 0) {
        out.push(`shield-dy: ${clearance};`);
        coverage.emit('shield-dy');
    }
    return out;
}

/** Every literal dash pattern inside a value, in the order MapBox states them. */
function dashPatterns(value: Json): number[][] {
    if (Array.isArray(value) && value.length > 0 && value.every((v) => typeof v === 'number')) {
        return [value as number[]];
    }
    if (Array.isArray(value)) {
        return value.flatMap((part) => dashPatterns(part as Json));
    }
    if (value && typeof value === 'object') {
        const stops = (value as { stops?: unknown }).stops;
        if (Array.isArray(stops)) return stops.flatMap((s) => dashPatterns((s as Json[])[1]));
    }
    return [];
}

/**
 * The one dash pattern to draw, out of whatever MapBox states - CartoCSS takes a pattern, not a
 * ramp. The first one that actually dashes: MapTiler's disputed border ramps from `[1, 0]` (a
 * SOLID line, below z5) to `[3, 2, 0.1, 2]`, and taking the base there would draw it solid at every
 * zoom anyone looks at. Null when no literal pattern is reachable at all.
 */
function dashPattern(value: Json): number[] | null {
    const patterns = dashPatterns(value);
    if (!patterns.length) return null;
    const dashes = (p: number[]) => p.length > 1 && p.some((v, i) => i % 2 === 1 && v > 0);
    return patterns.find(dashes) ?? patterns[0];
}

/** The layout properties variableAnchorDeclarations owns, so the generic loop leaves them alone. */
const VARIABLE_ANCHOR_LAYOUT = new Set([
    'text-variable-anchor', 'text-optional', 'text-radial-offset', 'text-justify',
]);

/** MapBox's variable anchor -> the SDK's, which spells the corners without the hyphen. */
const VARIABLE_ANCHORS = new Set([
    'center', 'left', 'right', 'top', 'bottom',
    'top-left', 'top-right', 'bottom-left', 'bottom-right',
]);

/**
 * MapBox tries each of `text-variable-anchor` in turn and keeps the first side the label fits on,
 * falling back to the icon alone when `text-optional` allows it. `ShieldSymbolizer` does the same
 * thing from the same list, so the four properties that describe it map straight across - all of
 * them shield-only, because a side to place the text on presupposes an icon to place it beside.
 */
function variableAnchorDeclarations(layer: MapboxLayer, coverage: Coverage): string[] {
    const layout = layer.layout ?? {};
    const out: string[] = [];

    const variable = layout['text-variable-anchor'];
    if (Array.isArray(variable)) {
        const anchors = variable.filter((a): a is string => typeof a === 'string' && VARIABLE_ANCHORS.has(a));
        if (anchors.length < variable.length) {
            coverage.drop('text-variable-anchor', 'unknown anchor in the list', layer.id);
        }
        if (anchors.length) {
            out.push(`shield-anchors: '${anchors.map((a) => a.replace('-', '')).join(',')}';`);
            coverage.emit('shield-anchors');
        }
    }

    // Without this a label that fits on no side is dropped WITH its icon.
    if (layout['text-optional'] === true) {
        out.push('shield-text-optional: true;');
        coverage.emit('shield-text-optional');
    } else if (layout['text-optional'] !== undefined && layout['text-optional'] !== false) {
        coverage.drop('text-optional', 'only a literal true is carried', layer.id);
    }

    // The gap between icon and text, which the SDK mirrors per side - so it is stated once, as dx,
    // whichever side wins. Only read when no text-offset states it, as MapBox does.
    const radial = layout['text-radial-offset'];
    if (typeof radial === 'number' && radial !== 0 && layout['text-offset'] === undefined) {
        const gap = ems(radial, layer, coverage, 'text-radial-offset');
        if (gap !== null) {
            out.push(`shield-text-dx: ${gap};`);
            coverage.emit('shield-text-dx');
        }
    } else if (radial !== undefined && typeof radial !== 'number') {
        coverage.drop('text-radial-offset', 'only a literal offset is carried', layer.id);
    }

    // How a WRAPPED label justifies itself once a side is chosen; 'auto' follows that side.
    const justify = layout['text-justify'];
    if (typeof justify === 'string') {
        const align = justify === 'center' ? 'middle' : justify;
        out.push(`shield-text-horizontal-alignment: '${align}';`);
        coverage.emit('shield-text-horizontal-alignment');
    } else if (justify !== undefined) {
        coverage.drop('text-justify', 'only a literal justification is carried', layer.id);
    }

    return out;
}

/**
 * Which way, and how far, the image moves so the text does not land on it.
 *
 * MapBox measures `text-offset` from the anchor the ICON also sits on, so an offset smaller than
 * half the icon leaves the two overlapping - a city dot at 0.15 em landed on its own name's
 * descender. The SDK centres the text on the IMAGE instead, so what it needs is the offset TOPPED
 * UP to half the icon, never the two added: a POI states 0.8 em, already clear of its pin, and
 * adding half an icon on top of that floated the name well below it.
 */
function iconClearance(layer: MapboxLayer, icon: ExtractedIcon, scale: number): number {
    const anchor = layer.layout?.['text-anchor'];
    const offset = layer.layout?.['text-offset'];
    const dy = Array.isArray(offset) && typeof offset[1] === 'number' ? offset[1] : 0;
    if (layer.layout?.['text-radial-offset'] !== undefined) return 0;
    // The offset is in ems of the text size, the icon in pixels, so they meet at a representative
    // size - the same approximation representativeScale makes for a zoom-driven icon-size.
    const emPixels = Math.abs(dy) * representativeScale(layer.layout?.['text-size'], 16);

    // 'bottom' anchors the text's bottom edge, so the text is ABOVE and the icon goes below.
    const below = anchor === 'bottom';
    const above = anchor === 'top';
    if (!below && !above) return 0;
    const shortfall = icon.height * scale / 2 - emPixels;
    if (shortfall <= 0) return 0;
    return round((below ? 1 : -1) * shortfall);
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


/**
 * The field a data-driven `icon-image` names its sprite from, or null when it names a constant.
 *
 * MapTiler writes `coalesce(image(subclass), image(class), image('dot'))`: the first name that IS
 * in the sprite wins. CartoCSS has no coalesce, so only the FIRST field is used - a POI whose
 * subclass has no sprite gets its label without an icon, where MapTiler falls back to the dot.
 */
function dynamicIconField(image: Json): { fields: string[]; fallback: string | null } | null {
    // `["image", …]` is unwrapped in fold.ts (the SDK cannot tint a sprite per feature), and the
    // type assertions around a name carry nothing, so both are seen through here rather than being
    // matched on. Standard names a POI icon `case(has(maki_beta), coalesce(maki_beta, maki), maki)`.
    const fieldOf = (node: Json): string | null => {
        if (!Array.isArray(node)) return null;
        if (node[0] === 'image' || node[0] === 'string' || node[0] === 'to-string') {
            return fieldOf(node[1] as Json);
        }
        return node[0] === 'get' && typeof node[1] === 'string' ? node[1] : null;
    };
    const constantOf = (node: Json): string | null => {
        if (Array.isArray(node) && node[0] === 'image' && typeof node[1] === 'string') return node[1];
        return typeof node === 'string' ? node : null;
    };

    const fields: string[] = [];
    let fallback: string | null = null;
    // Only the VALUE branches name an icon; a condition tests something else entirely.
    const gather = (node: Json): void => {
        if (Array.isArray(node) && node[0] === 'coalesce') {
            node.slice(1).forEach((branch) => gather(branch as Json));
            return;
        }
        if (Array.isArray(node) && node[0] === 'case' && node.length >= 4) {
            for (let i = 2; i + 1 < node.length; i += 2) gather(node[i] as Json);
            gather(node[node.length - 1] as Json);
            return;
        }
        const field = fieldOf(node);
        if (field !== null) {
            if (!fields.includes(field)) fields.push(field);
            return;
        }
        fallback = constantOf(node) ?? fallback;
    };

    gather(image);
    return fields.length ? { fields, fallback } : null;
}


/**
 * An `icon-image` that names its sprite from the feature, as a chain of parameter lookups.
 *
 * MapTiler writes these three ways, and all three are lookups on ONE field:
 *   coalesce(image(subclass), image(class), image('dot'))
 *   match(get(class), ['bed_and_breakfast', …], get(class), 'apartment', 'lodging', 'lodging')
 *   case(get(cuisine) == 'turkish', 'kebab', …, match(get(class), …))
 *
 * A style parameter per label IS that lookup - `[param::t-<label>]` is null when the label has no
 * entry, so `??` falls through exactly as coalesce and the fallback branch do. The alternative was
 * one attachment per branch, which cost a rule each and ran into MAX_VARIANTS: MapTiler's
 * accommodation table has nine branches, so it did not split at all and every hotel lost its icon.
 */
function iconExpression(image: Json, layer: MapboxLayer, coverage: Coverage, options: ConvertOptions): string | null {
    const sprites = options.sprites!;
    const slug = safeParamName(layer.id);
    let sample: ExtractedIcon | null = null;
    let tableIndex = 0;

    // A recolourable icon is cut in two - the glyph as a field, the disc as the shield's plate - so
    // the style's colours reach it per feature (extractIconPlate). Its own parameter table, because
    // the two halves live under the same NAME and a rule must not pick up the other one's file.
    const plateMode = layer.layout?.[ICON_PARAMS] !== undefined;
    const paramPrefix = plateMode ? GLYPH_PARAM_PREFIX : ICON_PARAM_PREFIX;
    // The params may cover only some of the layer's features (see IconParamScope). The rest keep
    // the sheet's own artwork, so the rule has to pick per feature between a field and a raster -
    // which `shield-sdf` allows, being read with an expression context like every other property.
    const scope = layer.layout?.[ICON_PARAM_SCOPE] as IconParamScope | undefined;
    const plateWhen = scope
        ? `(${scope.labels.map((l) => `[${scope.field}] = '${l}'`).join(' || ')})`
        : null;
    // What the layer NAMES outright, and what a whole-sheet lookup could reach. Separate because
    // one rule states one plate geometry: Standard's transit roundel is a rounded square and its
    // POI icons are circles, and a median over the sheet gives the transit rule the POI's radius.
    const namedPlates: IconPlate[] = [];
    const sheetPlates: IconPlate[] = [];

    // A `match` states its sprite names as LABELS even when the branch resolves per feature, and a
    // rule carries one plate geometry: Standard's transit roundel is a rounded square while its POI
    // icons are circles, so a median over the whole sheet gives the transit rule a circle's radius.
    // Only the geometry is taken here - the name still comes from the whole-sheet table.
    const notePlate = (name: string) => {
        if (!plateMode) return;
        const icon = extractIconPlate(sprites.sheets, name, sprites.outDir);
        if (icon?.plate) namedPlates.push(icon.plate);
    };

    const named = (name: string): string | null => {
        // In plate mode the rule declares `shield-sdf`, and that is a statement about EVERY file it
        // can name: a raster cut handed to it is read as a distance field and comes out inverted.
        // So a sprite with no field is not named at all - it draws nothing rather than a blob.
        const icon = plateMode
            ? extractIconPlate(sprites.sheets, name, sprites.outDir)
            : extractIcon(sprites.sheets, name, sprites.outDir, options.flattenSdf, undefined, 1);
        if (!icon) return null;
        if (icon.plate) namedPlates.push(icon.plate);
        if (!sample || (icon.plate && !sample.plate)) sample = icon;
        return icon.file;
    };

    // The labels a condition accepts for one field, or null when it tests something else.
    const labelsOf = (condition: Json, field: string): string[] | null => {
        if (!Array.isArray(condition)) return null;
        const [op, a, b] = condition as Json[];
        if (op === '==' && Array.isArray(a) && a[0] === 'get' && a[1] === field && typeof b === 'string') return [b];
        if (op === 'in' && Array.isArray(a) && a[0] === 'get' && a[1] === field
            && Array.isArray(b) && b[0] === 'literal' && Array.isArray(b[1])) {
            return (b[1] as Json[]).every((l) => typeof l === 'string') ? b[1] as string[] : null;
        }
        if (op === 'any' || op === 'all') {
            const parts = (condition as Json[]).slice(1).map((c) => labelsOf(c as Json, field));
            if (op === 'any' && parts.every((x) => x !== null)) return parts.flat() as string[];
        }
        return null;
    };

    // The field a case's first condition tests, so the rest can be checked against it.
    const fieldOfCase = (condition: Json): string | null => {
        const walk = (node: Json): string | null => {
            if (!Array.isArray(node)) return null;
            if (node[0] === 'get' && typeof node[1] === 'string') return node[1];
            for (const child of node.slice(1)) {
                const found = walk(child as Json);
                if (found) return found;
            }
            return null;
        };
        return walk(condition);
    };

    // A name built from a literal prefix and ONE field: `road_{ref_length}` and its expression
    // spelling `concat('road_', get(ref_length))`. The parameter name is built the same way, so the
    // lookup lands on icon-road_3 - which is what gets a junction its real shield artwork.
    const prefixed = (prefix: string, field: string): string | null => {
        const all = ensureEverySprite();
        const matching = all.names.filter((n) => n.startsWith(prefix));
        if (matching.length === 0) return null;
        for (const name of matching) options.styleParams!.set(`${paramPrefix}${name}`, all.file.get(name)!);
        return `[param::${paramPrefix}${prefix}[${field}]]`;
    };

    /** Every sprite written out, each under the file the rule should name it by. */
    let everySprite: { names: string[]; file: Map<string, string> } | null = null;
    const ensureEverySprite = () => {
        if (!everySprite) {
            const file = new Map<string, string>();
            let best: ExtractedIcon | null;
            if (plateMode) {
                // ONLY the sprites that split. A rule that declares `shield-sdf` says it of every
                // file it can name, so seeding this from the raster cut and overwriting the ones
                // that split left a third of the table pointing at colour bitmaps - each read as a
                // distance field, and each drawn as an inverted blob.
                const glyphs = extractAllIconPlates(sprites.sheets, sprites.outDir);
                for (const { name, icon } of glyphs.entries) {
                    file.set(name, icon.file);
                    if (icon.plate) sheetPlates.push(icon.plate);
                }
                if (glyphs.skipped.length > 0) {
                    coverage.approximate(`"${layer.id}" resolves its icon through the whole sheet, ` +
                        `and ${glyphs.skipped.length} of its sprites are not a disc with a glyph on ` +
                        "it - MapBox's generic pin among them. Those are left out, so a feature " +
                        'naming one draws its label alone');
                }
                best = glyphs.sample;
            } else {
                const raster = extractAllIcons(sprites.sheets, sprites.outDir, options.flattenSdf);
                for (const name of raster.names) file.set(name, `icons/${name}.png`);
                best = raster.sample;
            }
            everySprite = { names: [...file.keys()], file };
            if (!sample || (best?.plate && !sample.plate)) sample = best;
        }
        return everySprite;
    };

    /**
     * A case or match no ONE-field table can carry, written out as a ternary instead.
     *
     * MapTiler picks a shield's artwork on `class` AND `iso_a2` AND the route network at once, so
     * the branch conditions read several fields - which a ternary handles and a parameter name
     * cannot. Only the NAMES need the table treatment, and each branch spells its own.
     */
    const asTernary = (node: Json): string | null => {
        if (!Array.isArray(node)) return null;
        const pairs: Array<[Json, Json]> = [];
        let tail: Json;
        if (node[0] === 'case' && node.length >= 4 && node.length % 2 === 0) {
            for (let i = 1; i + 1 < node.length; i += 2) pairs.push([node[i] as Json, node[i + 1] as Json]);
            tail = node[node.length - 1] as Json;
        } else if (node[0] === 'match' && node.length >= 5 && node.length % 2 === 1) {
            const input = node[1] as Json;
            for (let i = 2; i + 1 < node.length; i += 2) {
                const labels = Array.isArray(node[i]) ? node[i] as Json[] : [node[i] as Json];
                for (const label of labels) if (typeof label === 'string') notePlate(label);
                const test = labels.length === 1
                    ? ['==', input, labels[0]]
                    : ['any', ...labels.map((l) => ['==', input, l])];
                pairs.push([test as unknown as Json, node[i + 1] as Json]);
            }
            tail = node[node.length - 1] as Json;
        } else {
            return null;
        }

        let expr = build(tail);
        if (expr === null) return null;
        for (const [condition, value] of [...pairs].reverse()) {
            // A branch that cannot be spelled is SKIPPED, not fatal. One of MapTiler's shield
            // branches tests `slice(ref, 2, 3)`, which is not a prefix and has no CartoCSS form -
            // and dropping the whole expression for it cost every shield its artwork, where
            // dropping the branch costs one country its variant.
            const name = build(value);
            let test: string | null = null;
            try {
                test = name === null ? null : translateExpression(condition);
            } catch {
                test = null;
            }
            if (test === null) {
                coverage.approximate(`one icon-image branch on "${layer.id}" has no CartoCSS form ` +
                    'and is skipped: those features take the next branch that matches');
                continue;
            }
            expr = `((${test}) ? ${name} : ${expr})`;
        }
        return expr;
    };

    const build = (node: Json): string | null => {
        // The type assertions and coercions carry nothing but their value, and Standard wraps every
        // POI icon name in them - `case(has(maki_beta), to-string(coalesce(string(maki_beta),
        // string(maki))), string(maki))`. Unseen, every branch built to null and the whole
        // expression with it, which is why a POI drew its label and no icon.
        if (Array.isArray(node) && node.length === 2
            && (node[0] === 'string' || node[0] === 'to-string' || node[0] === 'number')) {
            return build(node[1] as Json);
        }
        if (typeof node === 'string') {
            const token = node.match(/^([^{}]*)\{([A-Za-z0-9_:-]+)\}$/);
            if (token) return prefixed(token[1], token[2]);
            const file = named(node);
            return file ? `'${file}'` : `''`;
        }
        if (Array.isArray(node) && node[0] === 'concat') {
            const parts = (node as Json[]).slice(1);
            // A leading 'sheet:' only chose a sprite sheet. Every sheet is written out under bare
            // names now (see extractAllIcons), so the prefix is dropped and the rest resolved.
            if (typeof parts[0] === 'string' && /:$/.test(parts[0] as string) && parts.length === 2) {
                return build(parts[1] as Json);
            }
            // Otherwise the pieces spell a file NAME. mapnik interpolates every [field] in a
            // string, so the path carries them directly - which is what lets a name read two
            // fields (`AL-highway_2` is iso_a2 and ref_length) where a parameter lookup, holding
            // one, cannot.
            const spelled = parts.map((part) => {
                let piece = part as Json;
                while (Array.isArray(piece) && piece[0] === 'to-string') piece = piece[1] as Json;
                if (typeof piece === 'string' || typeof piece === 'number') return String(piece);
                if (Array.isArray(piece) && piece[0] === 'get' && typeof piece[1] === 'string') return `[${piece[1]}]`;
                return null;
            });
            if (spelled.some((piece) => piece === null)) return null;
            ensureEverySprite();
            return `url('icons/${spelled.join('')}.png')`;
        }
        if (Array.isArray(node) && node[0] === 'image') return build(node[1] as Json);
        if (Array.isArray(node) && node[0] === 'get' && typeof node[1] === 'string') {
            // Named after the value itself - the global one-parameter-per-sprite table covers it,
            // and the table is what gives `??` a miss to fall through on.
            const all = ensureEverySprite();
            for (const name of all.names) options.styleParams!.set(`${paramPrefix}${name}`, all.file.get(name)!);
            const field = `[param::${paramPrefix}[${node[1]}]]`;
            if (!plateWhen || node[1] !== scope!.field) return field;
            // The features the params do NOT cover: their own artwork, from the raster table, and
            // `shield-sdf` false for them (see plateWhen in shieldImageDeclarations).
            const raster = extractAllIcons(sprites.sheets, sprites.outDir, options.flattenSdf);
            for (const name of raster.names) {
                options.styleParams!.set(`${ICON_PARAM_PREFIX}${name}`, `icons/${name}.png`);
            }
            return `(${plateWhen} ? ${field} : [param::${ICON_PARAM_PREFIX}[${node[1]}]])`;
        }
        if (Array.isArray(node) && node[0] === 'coalesce') {
            const parts = (node as Json[]).slice(1).map((b) => build(b as Json)).filter((x): x is string => x !== null);
            return parts.length ? parts.join(' ?? ') : null;
        }

        // match / case both become one table on one field, plus whatever their fallback is.
        let field: string | null = null;
        const branches: { labels: string[]; value: Json }[] = [];
        let fallback: Json | null = null;
        if (Array.isArray(node) && node[0] === 'match' && node.length >= 5 && node.length % 2 === 1) {
            const input = node[1];
            if (!Array.isArray(input) || input[0] !== 'get' || typeof input[1] !== 'string') return asTernary(node);
            field = input[1];
            for (let i = 2; i + 1 < node.length; i += 2) {
                const raw = Array.isArray(node[i]) ? node[i] as Json[] : [node[i] as Json];
                if (!raw.every((l) => typeof l === 'string' || typeof l === 'number')) return asTernary(node);
                branches.push({ labels: raw.map(String), value: node[i + 1] as Json });
            }
            fallback = node[node.length - 1] as Json;
        } else if (Array.isArray(node) && node[0] === 'case' && node.length >= 4 && node.length % 2 === 0) {
            field = fieldOfCase(node[1] as Json);
            if (!field) return asTernary(node);
            for (let i = 1; i + 1 < node.length; i += 2) {
                const labels = labelsOf(node[i] as Json, field);
                if (!labels) return asTernary(node);
                branches.push({ labels, value: node[i + 1] as Json });
            }
            fallback = node[node.length - 1] as Json;
        } else {
            return asTernary(node);
        }

        const table = `${slug}-t${tableIndex++}`;
        let wrote = 0;
        for (const { labels, value } of branches) {
            for (const label of labels) {
                notePlate(label);
                const selfNamed = Array.isArray(value) && value[0] === 'get' && value[1] === field;
                const name = selfNamed ? label : (typeof value === 'string' ? value : null);
                if (name === null) continue;
                const file = named(name);
                if (!file) continue;
                options.styleParams!.set(`${table}-${label}`, file);
                wrote++;
            }
        }
        const rest = fallback === null ? null : build(fallback);
        const tail = rest && rest !== `''` ? ` ?? ${rest}` : '';
        if (wrote === 0) return rest;
        return `[param::${table}-[${field}]]${tail}`;
    };

    const expr = build(image);
    if (!expr || !sample) return null;
    // One rule, one plate: the geometry is a declaration, not a per-feature lookup. The MEDIAN of
    // what the layer's icons measure - Standard's POI sheet is one disc size and its transit sheet
    // another, and each is a rule of its own, so the spread inside one is a rounding.
    const plates = namedPlates.length > 0 ? namedPlates : sheetPlates;
    if (plates.length > 0) {
        const median = (values: number[]) => values.slice().sort((a, b) => a - b)[values.length >> 1];
        sample = {
            ...(sample as ExtractedIcon),
            plateWhen: plateWhen ?? undefined,
            plate: {
                radius: median(plates.map((p) => p.radius)),
                borderWidth: median(plates.map((p) => p.borderWidth)),
                strokeWidth: median(plates.map((p) => p.strokeWidth)),
            },
        };
    }
    options.iconSample = sample;
    coverage.note(`"${layer.id}": icon named per feature, resolved through style parameters`);
    return `(${expr})`;
}

/** A style parameter name is a bare identifier - a layer id is not. */
/**
 * A colour as a hex literal, or null when it is not one this understands.
 *
 * A style parameter carries a plain VALUE, and the SDK parses it back with parseColor - which does
 * not take `hsl(...)`. Left as MapTiler writes them, every parameter failed to parse and took its
 * whole rule down with it (the water in a probe style simply stopped drawing). Hex is the form that
 * survives the round trip.
 */
function colourToHex(value: Json): string | null {
    if (typeof value !== 'string') return null;
    const text = value.trim();
    if (/^#[0-9a-fA-F]{6}$/.test(text)) return text.toLowerCase();
    if (/^#[0-9a-fA-F]{3}$/.test(text)) {
        return `#${text.slice(1).split('').map((c) => c + c).join('')}`.toLowerCase();
    }
    const hex = (r: number, g: number, b: number) =>
        `#${[r, g, b].map((v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0')).join('')}`;

    const rgb = text.match(/^rgba?\(([^)]+)\)$/i);
    if (rgb) {
        const parts = rgb[1].split(',').map((v) => parseFloat(v));
        if (parts.length < 3 || parts.some((v) => Number.isNaN(v))) return null;
        if (parts.length > 3 && parts[3] !== 1) return null; // alpha has no hex form parseColor takes
        return hex(parts[0], parts[1], parts[2]);
    }

    const hsl = text.match(/^hsla?\(([^)]+)\)$/i);
    if (hsl) {
        const parts = hsl[1].split(',').map((v) => parseFloat(v));
        if (parts.length < 3 || parts.some((v) => Number.isNaN(v))) return null;
        if (parts.length > 3 && parts[3] !== 1) return null;
        const [h, sPct, lPct] = parts;
        const sat = sPct / 100, light = lPct / 100;
        const c = (1 - Math.abs(2 * light - 1)) * sat;
        const hp = (((h % 360) + 360) % 360) / 60;
        const x = c * (1 - Math.abs((hp % 2) - 1));
        const m = light - c / 2;
        const [r, g, b] = hp < 1 ? [c, x, 0] : hp < 2 ? [x, c, 0] : hp < 3 ? [0, c, x]
            : hp < 4 ? [0, x, c] : hp < 5 ? [x, 0, c] : [c, 0, x];
        return hex((r + m) * 255, (g + m) * 255, (b + m) * 255);
    }
    return null;
}

/** Below this a nested ternary is smaller and easier to read than a table of parameters. */
const MIN_PARAM_CASES = 8;

/**
 * A `match` over ONE field whose results are all constants, or null for any other shape.
 *
 * MapTiler keys a road shield's colour off `iso_a2` this way - 54 countries on `Highway shields`
 * alone, and the same again for its halo. As a ternary that is the largest expression in the style
 * and the decoder walks it per feature; as a style parameter per country it is one lookup.
 */
function constantMatchOnField(node: Json): { field: string; cases: Array<[string[], Json]>; fallback: Json } | null {
    if (!Array.isArray(node) || node[0] !== 'match') return null;
    const input = node[1];
    if (!(Array.isArray(input) && input[0] === 'get' && typeof input[1] === 'string')) return null;
    const cases: Array<[string[], Json]> = [];
    for (let i = 2; i + 1 < node.length; i += 2) {
        const raw = node[i];
        const labels = Array.isArray(raw) ? raw : [raw];
        if (!labels.every((l) => typeof l === 'string')) return null;
        const result = node[i + 1];
        if (typeof result !== 'string' && typeof result !== 'number') return null;
        cases.push([labels as string[], result as Json]);
    }
    if (cases.length < MIN_PARAM_CASES) return null;
    return { field: input[1], cases, fallback: node[node.length - 1] as Json };
}

/**
 * Turns the single-field `match` in a property value into a style-parameter lookup, in place.
 *
 * The rewritten value carries a SENTINEL string where the match was, so the whole expression can go
 * through the normal translation - conditions, nesting and all - and the lookup is substituted into
 * the finished declaration afterwards. That keeps this out of every emitter's signature.
 */
function paramiseValues(layer: MapboxLayer, options: ConvertOptions, coverage: Coverage):
        { layer: MapboxLayer; subs: Map<string, string> } {
    const subs = new Map<string, string>();
    const paint: Record<string, Json> = { ...(layer.paint ?? {}) } as Record<string, Json>;
    let changed = false;

    for (const [property, value] of Object.entries(paint)) {
        // The whole value, or the FALLBACK of a case whose own conditions read several fields -
        // which is exactly how MapTiler writes it: a handful of network special cases over a long
        // per-country table.
        const whole = constantMatchOnField(value);
        const isCase = Array.isArray(value) && value[0] === 'case' && value.length >= 4;
        const tail = isCase ? constantMatchOnField((value as Json[])[value.length - 1] as Json) : null;
        const found = whole ?? tail;
        if (!found) continue;

        // Only when every result survives as hex - a value the SDK cannot parse back takes its
        // whole rule down, so a table with one bad entry is not worth the trade.
        const asHex = found.cases.map(([labels, result]) =>
            [labels, typeof result === 'number' ? result : colourToHex(result)] as const);
        if (asHex.some(([, result]) => result === null)) continue;

        const prefix = `${safeParamName(layer.id)}-${safeParamName(property)}`;
        for (const [labels, result] of asHex) {
            for (const label of labels) options.styleParams!.set(`${prefix}-${label}`, result as Json);
        }
        const fallback = tryTranslate(found.fallback, property, layer.id, coverage);
        if (fallback === null) continue;
        const sentinel = `@@param${subs.size}@@`;
        subs.set(`'${sentinel}'`, `([param::${prefix}-[${found.field}]] ?? ${fallback})`);

        if (whole) {
            paint[property] = sentinel as unknown as Json;
        } else {
            const rewritten = [...(value as Json[])];
            rewritten[rewritten.length - 1] = sentinel as unknown as Json;
            paint[property] = rewritten as unknown as Json;
        }
        changed = true;
        coverage.approximate(`${property} on "${layer.id}" reads ${found.cases.length} cases of ` +
            `[${found.field}] from style parameters instead of a nested ternary`);
    }

    return { layer: changed ? { ...layer, paint } as MapboxLayer : layer, subs };
}

function safeParamName(id: string): string {
    return id.replace(/[^A-Za-z0-9]+/g, '-').replace(/^-+|-+$/g, '').toLowerCase();
}

/**
 * A fill or line pattern: a sprite sliced out to its own file, like a marker's. MapBox may qualify
 * the name with the sheet it lives in and CartoCSS wants a path, so the qualifier is dropped and
 * the sprite written under its bare name - which is also where every other sheet's icons land.
 */
function patternDeclarations(
    name: string, value: Json, target: string | undefined,
    layer: MapboxLayer, coverage: Coverage, options: ConvertOptions,
): string[] {
    if (!target) return [];
    if (typeof value !== 'string') {
        coverage.drop(name, 'a data-driven pattern names no one sprite', layer.id);
        return [];
    }
    if (!options.sprites) {
        coverage.drop(name, 'no sprite loaded (pass --sprite or let the style provide one)', layer.id);
        return [];
    }
    const bare = value.includes(':') ? value.slice(value.indexOf(':') + 1) : value;
    // Flattened: a pattern is painted as it stands, and there is no pattern-sdf to tint one with.
    const icon = extractIcon(options.sprites.sheets, value, options.sprites.outDir, true,
        undefined, 1, bare);
    if (!icon) {
        coverage.drop(name, `"${value}" is not in the sprite`, layer.id);
        return [];
    }
    coverage.emit(target);
    return [`${target}: url('${icon.file}');`];
}

/** MapBox's icon-* onto marker-*, once the sprite has been sliced into its own file. */
function markerDeclarations(layer: MapboxLayer, coverage: Coverage, options: ConvertOptions): string[] {
    const image = layer.layout?.['icon-image'];
    if (image === undefined) return [];
    if (!options.sprites) {
        coverage.drop('icon-image', 'no sprite loaded (pass --sprite or let the style provide one)', layer.id);
        return [];
    }
    // MapTiler names a POI's icon from the feature. The SDK resolves shield-file per feature and
    // mapnik interpolates [field] inside a string, so the whole sheet is written out and the field
    // goes in the file name - see dynamicIconField.
    // A legacy token name (`road_{ref_length}`) is data-driven too, even though it is a string.
    if (typeof image !== 'string' || /\{[A-Za-z0-9_:-]+\}/.test(image)) {
        const expr = iconExpression(image, layer, coverage, options);
        if (expr && options.iconSample) {
            const scale = layer.layout?.['text-field'] !== undefined
                ? representativeScale(layer.layout?.['icon-size']) : 1;
            return shieldImageDeclarations(layer, { ...options.iconSample, file: `@@EXPR@@${expr}` }, scale, coverage);
        }
    }
    if (typeof image !== 'string') {
        coverage.drop('icon-image', 'data-driven icon name', layer.id);
        return [];
    }

    // With text beside it the icon becomes a SHIELD - one label, no collision with its own name.
    // ShieldSymbolizer has no `sdf`, so the field is resolved and the style's icon-color baked in.
    const asShield = layer.layout?.['text-field'] !== undefined;
    // A shield is SHIPPED at the sprite's own resolution and shrunk by shield-image-scale. Baking
    // icon-size into the bitmap instead resampled the distance field into fewer texels than it
    // needs, so the smaller the icon the blockier and softer it drew.
    const scale = asShield ? representativeScale(layer.layout?.['icon-size']) : 1;
    const icon = extractIcon(options.sprites.sheets, image, options.sprites.outDir, options.flattenSdf,
        undefined, 1);
    if (!icon) {
        coverage.drop('icon-image', `"${image}" is not in the sprite`, layer.id);
        return [];
    }

    if (asShield) return shieldImageDeclarations(layer, icon, scale, coverage);

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

    // icon-size scales the sprite's own size. `marker-width` is read at DECODE (it sizes the
    // raster), so a zoom-driven size is evaluated per tile zoom rather than per frame - which is
    // how a raster marker has to behave anyway, and is why it is carried here for a plain sprite
    // too. Dropping it left the marker at its native size: Standard draws a crosswalk at
    // icon-size 0.2 from a 61 px sprite, so every crossing came out five times too big.
    const size = layer.layout?.['icon-size'];
    const sizeExpr = size === undefined
        ? String(round(icon.width))
        : scaledSize(size, icon.width, name('icon-size', layer, coverage));
    out.push(`marker-width: ${sizeExpr};`);
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
/**
 * Width of the line that stands in for `fill-outline-color`. MapBox draws that outline with
 * `gl.LINES`, so it is one DEVICE pixel whatever the display; a CartoCSS width is multiplied by the
 * pixel scale, so `1` came out ~3 px on a 2.75x phone and small buildings were solid outline
 * (measured at La Clusaz z14.5: 7456 outline pixels against 1747 of fill). This reads as a hairline
 * from 2x up, which is every phone.
 */
const FILL_OUTLINE_WIDTH = 0.4;
/** One style parameter per sprite name, so a per-feature lookup can fall through when it misses. */
const ICON_PARAM_PREFIX = 'icon-';
/** The glyph FIELD of a recolourable icon, a different file under the same name. */
const GLYPH_PARAM_PREFIX = 'glyph-';

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
    // The two disagree on which side is positive: mapbox offsets a line to the RIGHT of its
    // direction of travel, mapnik to the LEFT. Left as-is, a cycleway drawn beside its road lands
    // on the wrong side of it. CartoCSS has no unary minus before a parenthesised value, hence
    // `0 - x` rather than `-x`.
    if (target === 'line-offset') return `(0 - (${translated}))`;
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
function reportInterleaving(emitted: string[], order: Map<string, number>, coverage: Coverage): void {
    let inversions = 0;
    let previous = -1;
    for (const sourceLayer of emitted) {
        const position = order.get(sourceLayer);
        if (position === undefined) continue;
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
