import type { Coverage } from './coverage.js';
import type { Json, MapboxLayer } from './types.js';

/**
 * Two property kinds have to be split into per-branch attachments rather than left as one
 * field-driven expression.
 *
 * **Colours**, because they break. Measured on MapTiler topo-v4 at z14 with a cleared cache: two
 * `line-color` declarations reading `[class]` and `[paved]` produced `Color parsing failed` and
 * lost their whole rule for that tile - no roads on some tiles, roads on others. Replacing only the
 * field with a constant, nested ternary intact, brought it to 0. The mechanism is NOT established:
 * `TileReader` does bind the feature before `createFeatureProcessor` and caches a processor per
 * attribute set, and `Rule::calculateReferencedFields` gathers the fields property expressions use,
 * so this ought to work. It does not, and a predicate does.
 *
 * **`icon-image`**, because a sprite name is not a value the renderer can evaluate at all - it has
 * to name one file. Splitting is what turns `match(subclass, 'international', 'airport', …)` into
 * attachments that each have a real icon.
 *
 * The net is deliberately wide: narrowing it to colours alone, on the theory that a float merely
 * evaluates to 0, put 49 `Color parsing failed` back on the same camera that had none. Whatever the
 * mechanism is, it is not confined to the property that reads the field, so nothing that reads one
 * is emitted.
 *
 * `text-field` is exempt: the text is evaluated per feature inside the processor, not through a
 * Property, so it reads fields correctly today.
 */
const EXEMPT = new Set([
    'text-field',
    'visibility',
    // Placement priority only decides who wins a collision. Dropping it let a village label cull
    // Annecy; keeping the field costs nothing if it does evaluate to 0, and
    // docs/features/label-styling.md uses `text-placement-priority: [ele]` for exactly this.
    'symbol-sort-key',
]);

function mustNotReadFeature(name: string): boolean {
    return !EXEMPT.has(name);
}

/** A layer splitting into more than this many attachments is left whole - the compile cost is real. */
const MAX_VARIANTS = 8;

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
 * Every case/match over the feature replaced by its fallback - the branch the decoder evaluates to
 * anyway with no feature bound. Field reads themselves are left alone, which is what makes this
 * usable for `text-field` too, where reading a field is the whole point.
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

/** The paint/layout names that read the feature and may not, in a stable order. */
function splittableProperties(layer: MapboxLayer): string[] {
    return Object.entries({ ...layer.layout, ...layer.paint })
        .filter(([name, value]) => mustNotReadFeature(name) && readsFeature(value as Json))
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
