import { Untranslatable, conjunction, translateExpression, zoomOffsetLevels } from './expression.js';
import { negatedSet } from './narrow.js';
import type { Json } from './types.js';

const LEGACY_COMPARISON: Record<string, string> = {
    '==': '=', '!=': '!=', '<': '<', '<=': '<=', '>': '>', '>=': '>=',
};

// MapBox's $type names against mapnik::geometry_type, which is what the decoder exposes.
const GEOMETRY_TYPE: Record<string, number> = {
    Point: 1, MultiPoint: 1, LineString: 2, MultiLineString: 2, Polygon: 3, MultiPolygon: 3,
};

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
    if (bracketed !== null) return [bracketed];

    // "None of these" is a conjunction, so it brackets one test per value and the compiler keeps
    // its grip on the rule. The positive set test is a disjunction and has no bracketed form.
    const excluded = translateExcluded(filter);
    return excluded ?? [`when(${filterExpression(filter)})`];
}

function translateExcluded(filter: Json): string[] | null {
    const clause = negatedSet(filter);
    if (clause === null) return null;
    const tests = clause.values.map((v) => {
        const value = expressionConstant(clause.field, v);
        return value === null ? null : `[${predicateKey(clause.field)} != ${value}]`;
    });
    return tests.every((test) => test !== null) ? (tests as string[]) : null;
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
        const parts = args.map((a) => filterExpression(a as Json[]));
        // '&&' is unparseable here - see conjunction() in expression.ts.
        return head === 'any' ? `(${parts.join(' || ')})` : conjunction(parts);
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
            const tests = args.slice(1).map((v) => `${field} ${op} ${literalFor(args[0] as string, v as Json)}`);
            return head === 'in' ? `(${tests.join(' || ')})` : conjunction(tests);
        }
        return `(${field} ${LEGACY_COMPARISON[head as string]} ${literalFor(args[0] as string, args[1] as Json)})`;
    }

    const chain = setTestChain(filter);
    if (chain !== null) return chain;

    return translateExpression(filter);
}

/**
 * "input is one of these", in the two spellings a style writes it in: `["match", input, labels,
 * true, false]` (MapTiler's) and `["in", input, ["literal", labels]]`. The generic translation
 * wraps the first in `? true : false` - noise the decoder re-evaluates for every feature. As a
 * filter each is just the chain. The operands the other way round is the NEGATION, which is a
 * conjunction rather than an or-chain.
 *
 * The labels go through the same constant translation a bracketed test uses.
 * `mapnik::geometry_type` is a NUMBER (mapnikvt ExpressionContext.cpp), so a label naming it
 * `'LineString'` was a type mismatch, which `EQ` answers false for - the chain was false for every
 * feature and the rule never drew. 20 rules in OpenFreeMap Liberty and 11 in MapTiler streets-v4
 * were dead this way.
 */
function setTestChain(filter: Json[]): string | null {
    const test = setTest(filter);
    if (test === null) return null;
    const input = translateExpression(test.input);
    const key = expressionKey(test.input);
    const values = (key === null ? null : distinctConstants(key, test.labels))
        ?? test.labels.map((label) => translateExpression(label));
    const tests = values.map((value) => `${input} ${test.positive ? '=' : '!='} ${value}`);
    return test.positive ? `(${tests.join(' || ')})` : conjunction(tests);
}

interface SetTest { input: Json; labels: Json[]; positive: boolean }

function setTest(filter: Json[]): SetTest | null {
    const [head, ...args] = filter;

    if (head === 'match' && filter.length === 5 && typeof filter[3] === 'boolean' && filter[4] === !filter[3]) {
        const labels = Array.isArray(filter[2]) ? (filter[2] as Json[]) : [filter[2] as Json];
        return { input: args[0] as Json, labels, positive: filter[3] === true };
    }

    // The legacy `["in", "class", "a", "b"]` names its field bare and is handled with the other
    // legacy forms; this is the expression spelling.
    if ((head === 'in' || head === '!in') && filter.length === 3 && typeof args[0] !== 'string') {
        const literal = filter[2];
        if (!Array.isArray(literal) || literal[0] !== 'literal' || !Array.isArray(literal[1])) return null;
        return { input: args[0] as Json, labels: literal[1] as Json[], positive: head === 'in' };
    }

    return null;
}

/**
 * The labels of a set test as DISTINCT constants, or null when one of them is not a constant.
 * Several labels can name one constant - LineString and MultiLineString are both geometry type 2 -
 * and collapsed to one, the test brackets.
 */
function distinctConstants(key: string, labels: Json[]): string[] | null {
    const values = labels.map((label) => expressionConstant(key, label));
    return values.some((value) => value === null) ? null : [...new Set(values as string[])];
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

    // `["!", ["has", f]]` is `!has`, which brackets; left as a negation it became a when().
    if (head === '!' && args.length === 1 && Array.isArray(args[0])) {
        const inner = args[0] as Json[];
        if (inner[0] === 'has') return translateBracketed(['!has', inner[1] as Json]);
        if (inner[0] === '!has') return translateBracketed(['has', inner[1] as Json]);
    }

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

    // The EXPRESSION spelling of the same tests. A bracketed predicate is a plain filter the
    // decoder can decide per rule; when() carries a whole expression it has to evaluate per
    // feature, so a style written in expressions - which is every modern one - was paying for a
    // when() on tests that are ordinary comparisons.
    if (typeof head === 'string' && LEGACY_COMPARISON[head] && args.length === 2) {
        const key = expressionKey(args[0] as Json);
        const value = key === null ? null : expressionConstant(key, args[1] as Json);
        return key !== null && value !== null ? `[${predicateKey(key)} ${LEGACY_COMPARISON[head]} ${value}]` : null;
    }

    // A set test naming ONE distinct constant is an equality test - which is every
    // `["LineString", "MultiLineString"]` geometry test, both being mapnik geometry type 2.
    const test = setTest(filter);
    const setKey = test?.positive ? expressionKey(test.input) : null;
    const setValues = setKey === null ? null : distinctConstants(setKey, test!.labels);
    if (setKey !== null && setValues !== null && setValues.length === 1) {
        return `[${predicateKey(setKey)} = ${setValues[0]}]`;
    }

    return null;
}

/**
 * `["get", k]`, `["geometry-type"]`, `["id"]` and a LIVE config as the field a bracketed predicate
 * names.
 *
 * A config left live (see convert's liveConfig) is a style parameter, and one in a filter is what
 * lets a style carry two layer sets and switch between them: the decoder pre-evaluates a parameter
 * comparison with no feature in hand (PredicatePreEvaluator), so the losing set is pruned whole
 * rather than tested per feature. Left to the generic path it became a when(), which prunes nothing.
 */
function expressionKey(node: Json): string | null {
    if (!Array.isArray(node)) return null;
    if (node[0] === 'get' && node.length === 2 && typeof node[1] === 'string') return node[1];
    if (node[0] === 'geometry-type' && node.length === 1) return 'mapnik::geometry_type';
    if (node[0] === 'id' && node.length === 1) return 'mapnik::feature_id';
    if (node[0] === 'config' && node.length >= 2 && typeof node[1] === 'string') return `param::${node[1]}`;
    return null;
}

/** The right-hand side of one of those, as a constant. geometry-type compares against a number. */
function expressionConstant(key: string, value: Json): string | null {
    if (key === 'mapnik::geometry_type') {
        return typeof value === 'string' && value in GEOMETRY_TYPE ? String(GEOMETRY_TYPE[value]) : null;
    }
    if (value === null || typeof value === 'boolean' || typeof value === 'number') return String(value);
    if (typeof value === 'string') return `'${value.replace(/'/g, "\\'")}'`;
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

/**
 * minzoom/maxzoom -> the zoom predicates CartoCSS understands natively. maxzoom is exclusive.
 * Shifted by a level for the same reason the ramps are - see ZOOM_INPUT.
 */
export function zoomPredicates(minzoom?: number, maxzoom?: number): string[] {
    const out: string[] = [];
    if (typeof minzoom === 'number') out.push(`[zoom >= ${Math.floor(minzoom) + zoomOffsetLevels()}]`);
    if (typeof maxzoom === 'number') out.push(`[zoom < ${Math.ceil(maxzoom) + zoomOffsetLevels()}]`);
    return out;
}
