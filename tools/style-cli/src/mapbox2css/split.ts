import type { Coverage } from './coverage.js';
import { translateFilter } from './filter.js';
import { closedSets, narrowLayer } from './narrow.js';
import type { Json, MapboxLayer } from './types.js';

/**
 * Two properties have to be split into per-branch attachments rather than left as a field-driven
 * expression, and both for the same reason: they name a RESOURCE, not a value.
 *
 * **`icon-image`** has to name one file, so splitting is what turns
 * `match(subclass, 'international', 'airport', …)` into attachments that each have a real icon.
 * **`text-font`** has to name one face: the symbolizer resolves it to a loaded `vt::Font` when it
 * builds the formatter for the rule, so an unsplit `match` left the face literally named `match`
 * and every label fell back to the default font.
 *
 * **Nothing else does.** A property value that reads a feature field is evaluated per feature:
 * `GenericFunctionProperty::getFunction` rebuilds the function from the bound context whenever the
 * expression has context variables, and only memoises when it does not.
 *
 * This used to cast a much wider net, on the strength of a measurement: two `line-color`
 * declarations reading `[class]` and `[paved]` produced `Color parsing failed` and lost their whole
 * rule for that tile. Re-measured on the same style at the same camera with a cold cache, it is
 * **0** - and a plate colour reading `[iso_a2]` and a regex on `[ref]` picks the right country's
 * shield colour per feature. Which of this converter's later fixes cured it is not established
 * (`InterpolateExpression` reading string keyframes as colours is the likeliest), but the
 * observation that justified the workaround no longer reproduces, and the workaround cost real
 * fidelity: a 28-branch country `case` exceeded the variant cap, so every road shield fell back to
 * white, and a track's width kept a fallback that made it half again too wide.
 */
const MUST_BE_CONSTANT = new Set(['icon-image', 'text-font']);

function mustNotReadFeature(name: string): boolean {
    return MUST_BE_CONSTANT.has(name);
}

/** A layer splitting into more than this many attachments is left whole - the compile cost is real. */
const MAX_VARIANTS = 8;
/** ...and how many a set that IS the whole filter may have: one rule each, nothing copied. */
const MAX_SET_VALUES = 24;

interface Branch {
    /** The MapBox filter selecting this branch, null when it is the fallback and stands alone. */
    when: Json | null;
    value: Json;
}

/** Does this value read anything about the feature? Zoom-only expressions do not. */
export function readsFeature(value: Json): boolean {
    if (Array.isArray(value)) {
        const head = value[0];
        if (head === 'get' || head === 'has' || head === 'id' || head === 'geometry-type') return true;
        return value.some((item) => readsFeature(item as Json));
    }
    if (value && typeof value === 'object') {
        if (typeof (value as { property?: unknown }).property === 'string') return true;
        return Object.values(value).some((item) => readsFeature(item as Json));
    }
    return false;
}

/** The branches of a top-level case/match over the feature, or null when it is another shape. */
function branchesOf(value: Json): Branch[] | null {
    if (!Array.isArray(value) || !readsFeature(value)) return null;
    const head = value[0];

    if (head === 'case' && value.length >= 4 && value.length % 2 === 0) {
        const conditions: Json[] = [];
        const branches: Branch[] = [];
        for (let i = 1; i + 1 < value.length; i += 2) {
            const condition = value[i] as Json;
            branches.push({ when: exclusive(conditions, condition), value: value[i + 1] as Json });
            conditions.push(condition);
        }
        branches.push({ when: exclusive(conditions, null), value: value[value.length - 1] as Json });
        return branches;
    }

    if (head === 'match' && value.length >= 5 && value.length % 2 === 1) {
        const input = value[1] as Json;
        const conditions: Json[] = [];
        const branches: Branch[] = [];
        for (let i = 2; i + 1 < value.length; i += 2) {
            const condition = matchCondition(input, value[i] as Json);
            branches.push({ when: exclusive(conditions, condition), value: value[i + 1] as Json });
            conditions.push(condition);
        }
        branches.push({ when: exclusive(conditions, null), value: value[value.length - 1] as Json });
        return branches;
    }

    return null;
}

/**
 * One `match` label set as a filter. A plain `["get", f]` input takes the LEGACY spelling, which
 * translateFilter can put in brackets (`[class = 'motorway']`); the expression spelling would
 * always land in a when().
 */
function matchCondition(input: Json, labels: Json): Json {
    const field = Array.isArray(input) && input[0] === 'get' && typeof input[1] === 'string' ? input[1] : null;
    if (Array.isArray(labels)) {
        return (field !== null
            ? ['in', field, ...labels]
            : ['any', ...labels.map((l) => ['==', input, l as Json])]) as unknown as Json;
    }
    return (field !== null ? ['==', field, labels] : ['==', input, labels]) as unknown as Json;
}

/** MapBox takes the FIRST matching branch, so every later one has to exclude the earlier ones. */
function exclusive(earlier: Json[], own: Json | null): Json | null {
    const tests = [...earlier.map((c) => ['!', c] as unknown as Json), ...(own === null ? [] : [own])];
    if (tests.length === 0) return null;
    return (tests.length === 1 ? tests[0] : ['all', ...tests]) as Json;
}

/**
 * Every case/match over the feature replaced by its fallback. Field reads themselves are left alone,
 * which is what makes this usable for `text-field` too, where reading a field is the whole point.
 *
 * This is an APPROXIMATION, not an equivalence: the decoder does bind the feature and would have
 * evaluated the branches per feature (docs/contributing/style-tools.md, "A field in a value needs a
 * null guard"). Kept as the past-8-variants fallback until the guarded alternative is measured.
 */
export function collapseBranches(value: Json): Json {
    if (!readsFeature(value)) return value;

    if (Array.isArray(value)) {
        const head = value[0];
        if ((head === 'case' || head === 'match') && value.length >= 4) {
            return collapseBranches(value[value.length - 1] as Json);
        }
        return value.map((item) => collapseBranches(item as Json)) as unknown as Json;
    }

    const stops = value as { property?: unknown; default?: Json; stops?: Json[] };
    if (typeof stops.property === 'string') {
        if (stops.default !== undefined) return stops.default;
        const first = stops.stops?.[0];
        return Array.isArray(first) ? (first[1] as Json) : (null as unknown as Json);
    }
    return value;
}

/**
 * The same, for a property VALUE: null when a field read survives the collapse, because a bare
 * `["get", "width"]` has no fallback to fall back to and would take the rule down.
 */
function collapse(value: Json): Json | null {
    const collapsed = collapseBranches(value);
    return readsFeature(collapsed) ? null : collapsed;
}

/**
 * One MapBox layer -> the attachments it has to become. The common answer is the layer itself;
 * a field-driven paint value turns it into one variant per branch, each with a constant value and
 * the branch's condition added to the filter.
 */
export function splitLayer(layer: MapboxLayer, coverage: Coverage): MapboxLayer[] {
    let variants = splitIconByZoom(layer);

    for (const name of splittableProperties(layer)) {
        const expanded: MapboxLayer[] = [];
        for (const variant of variants) {
            const branches = branchesOf(valueOf(variant, name) as Json);
            if (!branches || variants.length * branches.length > MAX_VARIANTS) {
                expanded.push(variant);
                continue;
            }
            for (const branch of branches) {
                expanded.push(withValue(variant, name, branch.value, branch.when));
            }
        }
        variants = expanded;
    }

    return variants.map((variant) => resolveRemaining(variant, layer.id, coverage));
}

/**
 * `line-sort-key` orders features WITHIN one layer; CartoCSS has no equivalent, because a rule
 * draws its features in the order the tile lists them. Expanded into one attachment per key value,
 * LOWEST first, so the highest class is drawn last: without it a residential road painted over the
 * motorway it crosses wherever the tile happened to carry it later.
 *
 * The branch filters are mutually exclusive by construction (see `exclusive`), so reordering them
 * changes only which is drawn on top.
 */
export function expandSortKey(layer: MapboxLayer, coverage: Coverage): MapboxLayer[] {
    const key = layer.layout?.[SORT_KEY] as Json | undefined;
    if (key === undefined) return [layer];

    const branches = branchesOf(key);
    const values = branches?.map((branch) => branch.value);
    if (!branches || !values!.every((value) => typeof value === 'number')) {
        coverage.approximate(`${SORT_KEY} on "${layer.id}" is not a match over the feature, so its ` +
            'features keep the order the tile lists them in');
        return [layer];
    }
    if (branches.length > MAX_VARIANTS) {
        coverage.approximate(`${SORT_KEY} on "${layer.id}" has ${branches.length} values, past the ` +
            `${MAX_VARIANTS}-attachment cap, so its features keep the order the tile lists them in`);
        return [layer];
    }

    return [...branches]
        .sort((a, b) => (a.value as number) - (b.value as number))
        .map((branch) => withValue(layer, SORT_KEY, branch.value, branch.when));
}

const SORT_KEY = 'line-sort-key';

/**
 * A layer whose filter pins a field to a set AND whose paint branches on that same field, as one
 * attachment per value. The set test cannot bracket - it is a disjunction - so left whole it is a
 * when() the decoder evaluates per feature, and the paint chain re-tests the field it just passed.
 * Split, each attachment is one bracketed test and a constant (narrow.ts does the folding).
 *
 * Also when the set is the WHOLE filter, even though nothing branches on it: there each attachment
 * carries one bracketed test and nothing else, so the split trades a when() for N rules that the
 * decoder can prune - which is the trade the styles want.
 */
export function expandSetFilter(layer: MapboxLayer): MapboxLayer[] {
    for (const { field, values } of closedSets(layer.filter as Json | undefined)) {
        if (values.length > MAX_SET_VALUES) continue;
        const expanded = values.map((value) => narrowLayer({
            ...layer,
            filter: mergeFilter(layer.filter, ['==', field, value] as unknown as Json),
        }));
        const whole = expanded.every((variant) => isOnlyTest(variant.filter));
        // The cap is there to stop a cartesian blow-up when the REST of the filter is copied into
        // every attachment. Where the set is the whole filter there is no rest, so the only cost is
        // one bracketed rule per value - which is what a category of sixteen poi classes needs.
        if (values.length > MAX_VARIANTS && !whole) continue;
        if (!branchesOn(layer, field) && !whole) continue;
        // Splitting COPIES the rest of the filter into every attachment, so it only pays when that
        // rest brackets: otherwise the one when() it removes comes back N times. Measured on
        // MapTiler topo-v4, which is full of layers testing a class set AND something else.
        if (expanded.every((variant) => brackets(variant.filter as Json | undefined))) return expanded;
    }
    return [layer];
}

/**
 * Is the residue of the split FREE? The `==` it pinned on its own, or beside tests that bracket -
 * a mode switch on a style parameter, say. Those cost one more predicate per rule and no per-feature
 * work, which is not the blow-up the cap guards against.
 */
function isOnlyTest(filter: Json | undefined): boolean {
    if (!Array.isArray(filter)) return false;
    return filter[0] === '==' || (filter[0] === 'all' && brackets(filter));
}

function brackets(filter: Json | undefined): boolean {
    if (filter === undefined || filter === null) return true;
    try {
        return !translateFilter(filter).some((predicate) => predicate.startsWith('when('));
    } catch {
        return false;
    }
}

/** Does any paint or layout value pick a branch by this field? */
function branchesOn(layer: MapboxLayer, field: string): boolean {
    return Object.values({ ...layer.layout, ...layer.paint })
        .some((value) => selectsOn(value as Json, field));
}

function selectsOn(value: Json, field: string): boolean {
    if (!Array.isArray(value)) return false;
    if (value[0] === 'match' && readsField(value[1] as Json, field)) return true;
    if (value[0] === 'case') {
        for (let i = 1; i + 1 < value.length; i += 2) {
            if (readsField(value[i] as Json, field)) return true;
        }
    }
    return value.some((item) => selectsOn(item as Json, field));
}

function readsField(value: Json, field: string): boolean {
    if (!Array.isArray(value)) return false;
    if (value[0] === 'get' && value[1] === field) return true;
    return value.some((item) => readsField(item as Json, field));
}

/**
 * A sprite name that changes with ZOOM (`{stops: [[6, 'circle'], [12, ' ']]}`) cannot interpolate -
 * it names one file per zoom band. Each band becomes its own attachment, which is what puts the
 * dot back under a town name up to the zoom the style drops it at.
 */
function splitIconByZoom(layer: MapboxLayer): MapboxLayer[] {
    for (const name of ZOOM_BANDED) {
        const bands = zoomBandsOf(layer.layout?.[name] as Json, name === 'icon-image');
        if (!bands) continue;
        return bands
            .map(({ from, to, value }) => ({
                ...layer,
                minzoom: Math.max(from, layer.minzoom ?? 0),
                maxzoom: Math.min(to, layer.maxzoom ?? 24),
                layout: { ...layer.layout, [name]: value },
            }))
            .filter((variant) => variant.minzoom < variant.maxzoom);
    }
    return [layer];
}

/**
 * Properties whose value is a NAME or a TEXT rather than a number, and which a style may still
 * ramp over zoom. Neither can interpolate, and `InterpolateExpression` reads a string keyframe as
 * a COLOUR - so `step(zoom, [name], 15, concat(...))` had the decoder trying to parse "Beauregard"
 * as a colour and losing the whole rule. One attachment per band says the same thing in a form the
 * renderer has.
 */
const ZOOM_BANDED = ['icon-image', 'text-field'];

interface ZoomBand { from: number; to: number; value: Json }

/** The zoom bands of a step/stops expression, or null when it is not one. */
function zoomBandsOf(value: Json, requireString: boolean): ZoomBand[] | null {
    const stops: Array<[number, Json]> = [];

    if (Array.isArray(value) && value[0] === 'step'
        && Array.isArray(value[1]) && value[1][0] === 'zoom' && value.length >= 4) {
        stops.push([0, value[2] as Json]);
        for (let i = 3; i + 1 < value.length; i += 2) stops.push([value[i] as number, value[i + 1] as Json]);
    } else if (value && typeof value === 'object' && !Array.isArray(value)) {
        const legacy = value as { property?: unknown; stops?: Array<[number, Json]> };
        if (typeof legacy.property === 'string' || !Array.isArray(legacy.stops)) return null;
        stops.push(...legacy.stops.map(([z, v]) => [z, v] as [number, Json]));
    } else {
        return null;
    }

    if (!stops.every(([z]) => typeof z === 'number')) return null;
    if (requireString && !stops.every(([, v]) => typeof v === 'string')) return null;
    return stops.map(([from, value], i) => ({ from, to: stops[i + 1]?.[0] ?? 24, value }));
}

/**
 * A feature-driven icon name is resolved as a chain of style-parameter lookups instead (see
 * iconExpression), which is one rule rather than one attachment per branch and has no MAX_VARIANTS:
 * MapTiler's accommodation table has nine branches, so it did not split at all and every hotel lost
 * its icon, and its food table nests a second lookup inside its own fallback.
 */
function isLookupTable(value: Json): boolean {
    return Array.isArray(value) && (value[0] === 'match' || value[0] === 'case' || value[0] === 'coalesce');
}

/** The paint/layout names that read the feature and may not, in a stable order. */
function splittableProperties(layer: MapboxLayer): string[] {
    return Object.entries({ ...layer.layout, ...layer.paint })
        .filter(([name, value]) => mustNotReadFeature(name) && readsFeature(value as Json))
        .filter(([name, value]) => !(name === 'icon-image' && isLookupTable(value as Json)))
        .map(([name]) => name);
}

function valueOf(layer: MapboxLayer, name: string): Json | undefined {
    return layer.paint?.[name] ?? layer.layout?.[name];
}

function withValue(layer: MapboxLayer, name: string, value: Json, when: Json | null): MapboxLayer {
    const inLayout = layer.layout?.[name] !== undefined;
    const filter = when === null ? layer.filter : mergeFilter(layer.filter, when);
    return {
        ...layer,
        filter,
        layout: inLayout ? { ...layer.layout, [name]: value } : layer.layout,
        paint: inLayout ? layer.paint : { ...layer.paint, [name]: value },
    };
}

function mergeFilter(filter: Json | undefined, extra: Json): Json {
    if (filter === undefined || filter === null) return extra;
    return ['all', filter, extra] as unknown as Json;
}

/** Whatever splitting left behind is collapsed to its fallback, or dropped with the reason. */
function resolveRemaining(layer: MapboxLayer, layerId: string, coverage: Coverage): MapboxLayer {
    const layout = { ...layer.layout };
    const paint = { ...layer.paint };
    let changed = false;

    for (const name of splittableProperties(layer)) {
        // An icon-image that did not split is left alone: markerDeclarations already drops an
        // unresolvable sprite, and shield.ts needs to still see that the layer HAD one.
        if (name === 'icon-image') continue;
        const target = layout[name] !== undefined ? layout : paint;
        const resolved = collapse(target[name] as Json);
        changed = true;
        if (resolved === null) {
            coverage.drop(name, 'reads a feature field, which a property value cannot do here', layerId);
            delete target[name];
        } else {
            coverage.approximate(
                `${name} on "${layerId}" kept only its fallback: a field-driven value breaks the rule, ` +
                'and this one does not split into branches');
            target[name] = resolved;
        }
    }

    return changed ? { ...layer, layout, paint } : layer;
}
