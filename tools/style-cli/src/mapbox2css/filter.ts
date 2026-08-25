import { Untranslatable, translateExpression } from './expression.js';
import type { Json } from './types.js';

const LEGACY_COMPARISON: Record<string, string> = {
    '==': '=', '!=': '!=', '<': '<', '<=': '<=', '>': '>', '>=': '>=',
};

// MapBox's $type names against mapnik::geometry_type, which is what the decoder exposes.
const GEOMETRY_TYPE: Record<string, number> = { Point: 1, LineString: 2, Polygon: 3 };

/**
 * A MapBox filter -> CartoCSS selector fragments, already complete: either a bracketed test
 * (`[class = 'motorway']`) or a `when(...)`. `selector = *predicate` in the grammar, so the caller
 * concatenates them and the result means AND.
 *
 * A bracketed test only takes a CONSTANT on the right, so anything comparing two fields, or nesting
 * an or, falls back to when() - which carries a whole expression.
 */
export function translateFilter(filter: Json): string[] {
    if (filter === undefined || filter === null) return [];
    if (!Array.isArray(filter) || filter.length === 0) throw new Untranslatable('malformed filter');

    const head = filter[0];
    if (typeof head !== 'string') throw new Untranslatable('non-string filter operator');

    if (head === 'all') {
        return filter.slice(1).flatMap((sub) => translateFilter(sub as Json));
    }

    const bracketed = translateBracketed(filter as Json[]);
    return [bracketed ?? `when(${filterExpression(filter)})`];
}

/**
 * A filter as one CartoCSS boolean expression, for when().
 *
 * The legacy forms and the expression forms mean DIFFERENT things by the same shape:
 * `["==", "class", "x"]` compares the FIELD class, while `["==", ["get", "class"], "x"]` is an
 * expression whose first argument happens to be a get. Handing a legacy filter to the expression
 * translator reads its field name as a string literal, so the two are kept apart here.
 */
function filterExpression(filter: Json[]): string {
    const [head, ...args] = filter;

    if (head === 'all' || head === 'any') {
        // Expression-level booleans are && and ||; 'and'/'or' are predicate syntax only.
        const join = head === 'all' ? ' && ' : ' || ';
        return `(${args.map((a) => filterExpression(a as Json[])).join(join)})`;
    }
    if (head === '!' && args.length === 1) {
        return `(!${filterExpression(args[0] as Json[])})`;
    }

    if (isLegacy(filter)) {
        const key = fieldRef(args[0] as string);
        if (key === null) throw new Untranslatable(`filter key ${String(args[0])}`);
        const field = `[${key}]`;

        if (head === 'has') return `(${field} != null)`;
        if (head === '!has') return `(${field} = null)`;
        if (head === 'in' || head === '!in') {
            const op = head === 'in' ? '=' : '!=';
            const join = head === 'in' ? ' || ' : ' && ';
            const tests = args.slice(1).map((v) => `${field} ${op} ${literalFor(args[0] as string, v as Json)}`);
            return `(${tests.join(join)})`;
        }
        return `(${field} ${LEGACY_COMPARISON[head as string]} ${literalFor(args[0] as string, args[1] as Json)})`;
    }

    return translateExpression(filter);
}

/** MapBox's own test: a legacy filter names its field as a bare string. */
function isLegacy(filter: Json[]): boolean {
    const [head, ...args] = filter;
    if (typeof head !== 'string' || typeof args[0] !== 'string') return false;
    return head in LEGACY_COMPARISON || ['has', '!has', 'in', '!in'].includes(head);
}

/** $type compares against the numeric mapnik geometry type, everything else against the literal. */
function literalFor(key: string, value: Json): string {
    if (key === '$type' && typeof value === 'string' && value in GEOMETRY_TYPE) {
        return String(GEOMETRY_TYPE[value]);
    }
    return translateExpression(value);
}

/** The legacy forms that have a `[field op constant]` equivalent. Null when they do not. */
function translateBracketed(filter: Json[]): string | null {
    const [head, ...args] = filter;

    if (typeof head === 'string' && LEGACY_COMPARISON[head] && args.length === 2 && typeof args[0] === 'string') {
        const key = fieldRef(args[0]);
        const value = constant(args[0], args[1] as Json);
        return key !== null && value !== null ? `[${predicateKey(key)} ${LEGACY_COMPARISON[head]} ${value}]` : null;
    }

    if ((head === 'has' || head === '!has') && args.length === 1 && typeof args[0] === 'string') {
        const key = fieldRef(args[0]);
        return key === null ? null : `[${predicateKey(key)} ${head === 'has' ? '!=' : '='} null]`;
    }

    if (head === 'in' && args.length === 2 && typeof args[0] === 'string') {
        const key = fieldRef(args[0]);
        const value = constant(args[0], args[1] as Json);
        return key !== null && value !== null ? `[${predicateKey(key)} = ${value}]` : null;
    }

    return null;
}

/** MapBox legacy filter keys, including the two special ones. */
function fieldRef(key: string): string | null {
    if (key === '$type') return 'mapnik::geometry_type';
    if (key === '$id') return 'mapnik::feature_id';
    if (key.startsWith('$')) return null;
    return key;
}

/**
 * A field name as a bracketed predicate spells it. The grammar there is `(fieldid | string)`, and
 * a namespaced name is not a fieldid - it has to be quoted, the way the round-trip fixture writes
 * `#hillshade['param::relief' = false]`. Expression context (`[view::zoom]`) is the opposite.
 */
function predicateKey(key: string): string {
    return key.includes('::') ? `'${key}'` : key;
}

/** A filter's right-hand side, as the constant a bracketed predicate requires. */
function constant(key: string, value: Json): string | null {
    if (key === '$type') {
        return typeof value === 'string' && value in GEOMETRY_TYPE ? String(GEOMETRY_TYPE[value]) : null;
    }
    if (value === null || typeof value === 'boolean' || typeof value === 'number') return String(value);
    if (typeof value === 'string') return `'${value.replace(/'/g, "\\'")}'`;
    return null;
}

/** minzoom/maxzoom -> the zoom predicates CartoCSS understands natively. maxzoom is exclusive. */
export function zoomPredicates(minzoom?: number, maxzoom?: number): string[] {
    const out: string[] = [];
    if (typeof minzoom === 'number') out.push(`[zoom >= ${Math.floor(minzoom)}]`);
    if (typeof maxzoom === 'number') out.push(`[zoom < ${Math.ceil(maxzoom)}]`);
    return out;
}
