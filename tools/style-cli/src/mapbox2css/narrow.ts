import type { Json, MapboxLayer } from './types.js';

/**
 * What a layer's own filter already guarantees about a feature, so that neither the filter nor its
 * property values pay to test it again.
 *
 * A `when(...)` is a WhenPredicate, and PredicateContainsChecker returns indeterminate for one
 * against anything (libs-massif/cartocss PredicateUtils.h): it prunes nothing and the decoder
 * evaluates the whole expression per feature. A bracketed `[class = 'motorway']` is an OpPredicate
 * the compiler can reason about. So an attachment that already pins a field wants exactly one
 * bracketed test for it, and its values want the branch, not the chain.
 */
interface Facts {
    /** Field -> the one value it can hold here. */
    eq: Map<string, Json>;
    /** Field -> values it cannot hold. */
    ne: Map<string, Json[]>;
    /** Field -> the closed set it is drawn from, when the filter states one. */
    oneOf: Map<string, Json[]>;
}

type ClauseOp = 'eq' | 'ne' | 'in' | 'nin';

export interface Clause {
    op: ClauseOp;
    field: string;
    values: Json[];
}

/**
 * A split attachment restated against what its filter proves. `line-sort-key` expansion is what
 * makes this pay: each branch arrives carrying the layer's whole class list, a negation per earlier
 * branch, and a paint chain re-testing the class it was split on - so seven roads cost seven
 * when() and a six-deep ternary each, for a rule that already knows the class is 'minor'.
 */
export function narrowLayer(layer: MapboxLayer): MapboxLayer {
    const clauses = flatten(layer.filter as Json | undefined);
    const facts = collectFacts(clauses);
    if (facts.eq.size === 0) return layer;

    return {
        ...layer,
        filter: rebuildFilter(clauses, facts),
        layout: narrowProperties(layer.layout, facts),
        paint: narrowProperties(layer.paint, facts),
    };
}

function narrowProperties(
    props: Record<string, Json> | undefined, facts: Facts,
): Record<string, Json> | undefined {
    if (!props) return props;
    return Object.fromEntries(Object.entries(props).map(([k, v]) => [k, narrowValue(v, facts)]));
}

/** Every `all` unpacked, so a clause is judged on its own. */
function flatten(filter: Json | undefined): Json[] {
    if (filter === undefined || filter === null) return [];
    if (!Array.isArray(filter)) return [filter];
    return filter[0] === 'all' ? filter.slice(1).flatMap((sub) => flatten(sub as Json)) : [filter];
}

function collectFacts(clauses: Json[]): Facts {
    const facts: Facts = { eq: new Map(), ne: new Map(), oneOf: new Map() };

    for (const clause of clauses) {
        const parsed = parseClause(clause);
        if (parsed === null) continue;
        const { op, field, values } = parsed;
        if (op === 'eq' || (op === 'in' && values.length === 1)) facts.eq.set(field, values[0]);
        else if (op === 'in') facts.oneOf.set(field, intersect(facts.oneOf.get(field), values));
        else push(facts.ne, field, values);
    }

    // A closed set minus its excluded members is an equality: the fallback branch of an expanded
    // match arrives as "one of these seven, and none of the other six".
    for (const [field, values] of facts.oneOf) {
        if (facts.eq.has(field)) continue;
        const excluded = facts.ne.get(field) ?? [];
        const left = values.filter((v) => !excluded.some((x) => same(x, v)));
        if (left.length === 1) facts.eq.set(field, left[0]);
    }

    return facts;
}

/**
 * The fields a filter pins to a small closed set without pinning them to one value. Each is a
 * layer that can become one attachment per value instead of one rule testing the set per feature.
 */
export function closedSets(filter: Json | undefined): Array<{ field: string; values: Json[] }> {
    const facts = collectFacts(flatten(filter));
    return [...facts.oneOf]
        .filter(([field]) => !facts.eq.has(field))
        .map(([field, values]) => ({ field, values: allowed(values, facts.ne.get(field)) }))
        .filter(({ values }) => values.length > 1);
}

function allowed(values: Json[], excluded: Json[] | undefined): Json[] {
    return excluded === undefined ? values : values.filter((v) => !excluded.some((x) => same(x, v)));
}

/** A negated set test, which is a conjunction and so brackets one test per value. */
export function negatedSet(filter: Json): Clause | null {
    const parsed = parseClause(filter);
    return parsed !== null && (parsed.op === 'nin' || parsed.op === 'ne') ? parsed : null;
}

function push(target: Map<string, Json[]>, field: string, values: Json[]): void {
    target.set(field, [...(target.get(field) ?? []), ...values]);
}

function intersect(previous: Json[] | undefined, values: Json[]): Json[] {
    return previous === undefined ? values : previous.filter((v) => values.some((w) => same(v, w)));
}

/**
 * The pinned fields first, then whatever an equality does not already prove. Only the equalities
 * judge: the exclusions were READ OFF these clauses, so letting them judge would have every
 * `[subclass != 'junction']` prove itself and drop out of the filter it is the whole point of.
 */
function rebuildFilter(clauses: Json[], facts: Facts): Json | undefined {
    const pinned: Facts = { eq: facts.eq, ne: new Map(), oneOf: new Map() };
    const kept = [...facts.eq].map(([field, value]) => ['==', field, value] as unknown as Json);
    kept.push(...clauses.filter((clause) => evalTest(clause, pinned) !== true));
    if (kept.length === 0) return undefined;
    return (kept.length === 1 ? kept[0] : ['all', ...kept]) as Json;
}

/** A test the facts settle, or null when the feature still has to be read for it. */
function evalTest(node: Json, facts: Facts): boolean | null {
    if (node === true || node === false) return node;
    if (!Array.isArray(node)) return null;

    const head = node[0];
    if (head === 'all' || head === 'any') {
        const results = node.slice(1).map((sub) => evalTest(sub as Json, facts));
        const decisive = head === 'all' ? false : true;
        if (results.includes(decisive)) return decisive;
        return results.includes(null) ? null : !decisive;
    }
    if (head === '!' && node.length === 2) {
        const inner = evalTest(node[1] as Json, facts);
        return inner === null ? null : !inner;
    }

    const parsed = parseClause(node);
    return parsed === null ? null : evalClause(parsed, facts);
}

function evalClause({ op, field, values }: Clause, facts: Facts): boolean | null {
    const known = facts.eq.get(field);
    if (known !== undefined) {
        const hit = values.some((v) => same(v, known));
        return op === 'eq' || op === 'in' ? hit : !hit;
    }

    const excluded = facts.ne.get(field);
    if (excluded && values.every((v) => excluded.some((x) => same(x, v)))) {
        return op === 'eq' || op === 'in' ? false : true;
    }
    return null;
}

/** The equality-shaped filters, in both the legacy and the expression spelling. */
function parseClause(node: Json): Clause | null {
    if (!Array.isArray(node) || node.length < 2) return null;
    const head = node[0];

    if (head === '!' && node.length === 2) {
        const inner = parseClause(node[1] as Json);
        const inverse: Record<ClauseOp, ClauseOp> = { eq: 'ne', ne: 'eq', in: 'nin', nin: 'in' };
        return inner === null ? null : { ...inner, op: inverse[inner.op] };
    }

    // `["match", input, labels, true, false]` is how a boolean set test is spelled.
    if (head === 'match' && node.length === 5 && node[3] === true && node[4] === false) {
        const labels = Array.isArray(node[2]) ? (node[2] as Json[]) : [node[2] as Json];
        return build('in', node[1] as Json, labels);
    }

    if ((head === '==' || head === '!=') && node.length === 3) {
        return build(head === '==' ? 'eq' : 'ne', node[1] as Json, [node[2] as Json]);
    }

    if (head === 'in' || head === '!in') {
        const op: ClauseOp = head === 'in' ? 'in' : 'nin';
        // Legacy: `["in", "class", "a", "b"]`. Expression: `["in", ["get","class"], ["literal",[…]]]`.
        if (typeof node[1] === 'string') return build(op, node[1], node.slice(2) as Json[]);
        const literal = node[2];
        if (!Array.isArray(literal) || literal[0] !== 'literal' || !Array.isArray(literal[1])) return null;
        return build(op, node[1] as Json, literal[1] as Json[]);
    }

    return null;
}

/** The field an input resolves to once it is known to be non-null, guard and all. */
function pinnedField(node: Json): string | null {
    if (Array.isArray(node) && node[0] === 'coalesce' && node.length >= 2) {
        return pinnedField(node[1] as Json);
    }
    return fieldOf(node);
}

/** Every label a match tests, so a guard's default can be checked against all of them at once. */
function labelsOf(value: Json[]): Json[] {
    const labels: Json[] = [];
    for (let i = 2; i + 1 < value.length; i += 2) {
        labels.push(...(Array.isArray(value[i]) ? (value[i] as Json[]) : [value[i] as Json]));
    }
    return labels;
}

function build(op: ClauseOp, input: Json, values: Json[]): Clause | null {
    if (values.length === 0 || !values.every(isLiteral)) return null;
    const field = comparableField(input, values);
    return field === null ? null : { op, field, values };
}

/** The field a test names. A legacy filter names it bare, an expression through `get`. */
function fieldOf(node: Json): string | null {
    if (typeof node === 'string') return node.startsWith('$') ? null : node;
    if (!Array.isArray(node)) return null;
    if (node[0] === 'get' && node.length === 2 && typeof node[1] === 'string') return node[1];
    return null;
}

/**
 * The same, seeing through the null guard a style wraps a field in - but only where the guard
 * cannot fire: `coalesce(f, '') = ''` is TRUE for a missing field where `f = ''` is false, so the
 * two are the same test only when none of the values compared against it is a default.
 */
function comparableField(node: Json, values: Json[]): string | null {
    if (Array.isArray(node) && node[0] === 'coalesce' && node.length >= 2) {
        const defaults = node.slice(2) as Json[];
        if (defaults.some((d) => values.some((v) => same(v, d)))) return null;
        return comparableField(node[1] as Json, values);
    }
    return fieldOf(node);
}

function isLiteral(value: Json): boolean {
    return value === null || ['string', 'number', 'boolean'].includes(typeof value);
}

function same(a: Json, b: Json): boolean {
    return a === b;
}

/** A property value with every branch the facts already decide taken out. */
function narrowValue(value: Json, facts: Facts): Json {
    if (!Array.isArray(value) || value.length === 0) return value;
    const head = value[0];
    if (head === 'literal') return value;
    if (head === 'match') return narrowMatch(value as Json[], facts);
    if (head === 'case') return narrowCase(value as Json[], facts);

    const settled = evalTest(value, facts);
    if (settled !== null) return settled as unknown as Json;

    return [head, ...value.slice(1).map((item) => narrowValue(item as Json, facts))] as unknown as Json;
}

function narrowMatch(value: Json[], facts: Facts): Json {
    if (value.length < 5 || value.length % 2 === 0) return value as unknown as Json;
    // A field with a known non-null value passes through any null guard around it unchanged, so the
    // input resolves whatever the guard's default is.
    const pinned = pinnedField(value[1] as Json);
    const known = pinned === null ? undefined : facts.eq.get(pinned) ?? undefined;
    const excludedField = comparableField(value[1] as Json, labelsOf(value));
    const excluded = excludedField === null ? [] : facts.ne.get(excludedField) ?? [];

    const kept: Json[] = [];
    for (let i = 2; i + 1 < value.length; i += 2) {
        const labels = Array.isArray(value[i]) ? (value[i] as Json[]) : [value[i] as Json];
        if (known !== undefined && known !== null) {
            if (labels.some((l) => same(l, known))) return narrowValue(value[i + 1] as Json, facts);
            continue;
        }
        if (labels.every((l) => excluded.some((x) => same(x, l)))) continue;
        kept.push(value[i] as Json, narrowValue(value[i + 1] as Json, facts));
    }

    const fallback = narrowValue(value[value.length - 1] as Json, facts);
    if (kept.length === 0) return fallback;
    return ['match', narrowValue(value[1] as Json, facts), ...kept, fallback] as unknown as Json;
}

function narrowCase(value: Json[], facts: Facts): Json {
    if (value.length < 4 || value.length % 2 !== 0) return value as unknown as Json;

    const kept: Json[] = [];
    for (let i = 1; i + 1 < value.length; i += 2) {
        const settled = evalTest(value[i] as Json, facts);
        if (settled === false) continue;
        // A condition the facts prove is the branch: nothing after it can be reached.
        if (settled === true) {
            const taken = narrowValue(value[i + 1] as Json, facts);
            return kept.length === 0 ? taken : (['case', ...kept, taken] as unknown as Json);
        }
        kept.push(narrowValue(value[i] as Json, facts), narrowValue(value[i + 1] as Json, facts));
    }

    const fallback = narrowValue(value[value.length - 1] as Json, facts);
    if (kept.length === 0) return fallback;
    return ['case', ...kept, fallback] as unknown as Json;
}
