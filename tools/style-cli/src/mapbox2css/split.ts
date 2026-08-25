import type { Coverage } from './coverage.js';
import type { Json, MapboxLayer } from './types.js';

/**
 * A property VALUE that reads a feature field does not work in this decoder.
 *
 * `Symbolizer::createFeatureProcessor` runs once per rule with no feature bound, and
 * `ColorFunctionProperty::buildFunction` only defers to a per-frame function for view-state and
 * live-parameter expressions - a field expression is evaluated there and then, the field is null,
 * and a colour that collapses to null throws `Color parsing failed` and takes the whole rule with
 * it. A float quietly becomes 0 instead. Predicates are the supported mechanism, so a `case`/
 * `match` over a field becomes one attachment per branch, each with a constant value.
 *
 * `text-name` is exempt: the text is evaluated per feature inside the processor, not through a
 * Property, so it reads fields correctly today.
 */
const EXEMPT = new Set(['text-field', 'icon-image', 'visibility']);

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
    let variants = [layer];

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

/** The paint/layout names that read the feature, in a stable order. */
function splittableProperties(layer: MapboxLayer): string[] {
    return Object.entries({ ...layer.layout, ...layer.paint })
        .filter(([name, value]) => !EXEMPT.has(name) && readsFeature(value as Json))
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
