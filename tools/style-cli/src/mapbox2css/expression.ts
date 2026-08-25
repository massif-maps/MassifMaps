import type { Json } from './types.js';

/** Thrown when an expression has no CartoCSS equivalent. The caller drops the property. */
export class Untranslatable extends Error {
    constructor(readonly what: string) {
        super(what);
    }
}

const COLOR_KEYWORDS = /^(#|rgb|rgba|hsl|hsla)/;

function quote(text: string): string {
    return `'${text.replace(/\\/g, '\\\\').replace(/'/g, "\\'")}'`;
}

function literal(value: Json): string {
    if (value === null) return 'null';
    if (typeof value === 'boolean') return String(value);
    if (typeof value === 'number') return String(value);
    if (typeof value === 'string') return COLOR_KEYWORDS.test(value) ? value : quote(value);
    throw new Untranslatable('array or object literal');
}

// CartoCSS '^' is XOR and there is no '%' at all (grammar term3), so MapBox's '^' goes
// through pow() and '%' is refused rather than mistranslated.
const BINARY: Record<string, string> = {
    '+': '+', '-': '-', '*': '*', '/': '/',
    '==': '=', '!=': '!=', '<': '<', '<=': '<=', '>': '>', '>=': '>=',
};

// Only what CartoCSSMapnikTranslator::_basicFuncMap carries. There is no abs/floor/ceil.
const UNARY_FN: Record<string, string> = {
    'upcase': 'uppercase', 'downcase': 'lowercase', 'length': 'length', 'ln': 'log',
};

const BINARY_FN: Record<string, string> = { '^': 'pow' };

const GEOMETRY_TYPE_FIELD = '[mapnik::geometry_type]';

/**
 * AND as nested ternaries, because `&&` cannot be parsed at all.
 *
 * CartoCSSParser's term3 has `qi::lit("&") > unary` for bitwise and, and `>` is an EXPECTATION:
 * on `&&` it consumes the first `&`, demands a unary, meets the second `&` and fails hard with no
 * backtrack to term0's `&&`. `||` has no such shadow and is emitted directly.
 */
/**
 * The legacy token syntax MapBox still accepts for text-field: "{ref} {name}". Left as a quoted
 * string the label would literally read "{name}", so the braces become field references and the
 * surrounding text a concat.
 */
export function expandTokens(text: string): string {
    const parts: string[] = [];
    const pattern = /\{([^{}]+)\}/g;
    let last = 0;
    for (let m = pattern.exec(text); m !== null; m = pattern.exec(text)) {
        if (m.index > last) parts.push(quote(text.slice(last, m.index)));
        parts.push(`[${m[1]}]`);
        last = m.index + m[0].length;
    }
    if (last === 0) return quote(text);
    if (last < text.length) parts.push(quote(text.slice(last)));
    return parts.reduce((acc, part) => `concat(${acc}, ${part})`);
}

export function conjunction(parts: string[]): string {
    return parts.reduceRight((rest, part, index) =>
        index === parts.length - 1 ? part : `(${part} ? ${rest} : false)`);
}

// ["geometry-type"] yields a NAME in MapBox and a number in the decoder, so a comparison against
// one of those names has to carry the mapping or it silently never matches.
const GEOMETRY_TYPE_VALUE: Record<string, number> = {
    Point: 1, MultiPoint: 1, LineString: 2, MultiLineString: 2, Polygon: 3, MultiPolygon: 3,
};

/**
 * MapBox expression -> a CartoCSS expression string.
 *
 * Zoom-driven `interpolate`/`step` become CartoCSS `linear()`/`step()` over `[view::zoom]`, which
 * is what keeps them re-evaluated per frame instead of frozen into the tile. An interpolation over
 * anything other than zoom has no such form and is refused.
 */
export function translateExpression(expr: Json, notes?: string[]): string {
    if (expr === null || typeof expr !== 'object') return literal(expr);
    // Pre-expression style spec: a property value can be a stop function object rather than an
    // expression array. Most real styles are still mostly written this way.
    if (!Array.isArray(expr)) return translateStopFunction(expr as Record<string, Json>, notes);
    if (expr.length === 0) throw new Untranslatable('empty expression');

    const [head, ...args] = expr;
    if (typeof head !== 'string') throw new Untranslatable('non-string operator');

    switch (head) {
        case 'literal':
            return literal(args[0] as Json);

        case 'get': {
            if (args.length !== 1 || typeof args[0] !== 'string') {
                throw new Untranslatable('get with a computed or scoped key');
            }
            return `[${args[0]}]`;
        }

        case 'has': {
            if (args.length !== 1 || typeof args[0] !== 'string') {
                throw new Untranslatable('has with a computed or scoped key');
            }
            return `([${args[0]}] != null)`;
        }

        case 'zoom':
            return '[view::zoom]';

        case 'geometry-type':
            return GEOMETRY_TYPE_FIELD;

        case 'id':
            return '[mapnik::feature_id]';

        case 'concat':
            // CartoCSS concat is binary; fold left rather than rely on '+' coercing strings.
            return args
                .map((a) => translateExpression(a as Json))
                .reduce((acc, part) => `concat(${acc}, ${part})`);

        case 'coalesce':
            return args.map((a) => `(${translateExpression(a as Json)})`).join(' ?? ');

        case 'to-string':
            return `('' + ${translateExpression(args[0] as Json)})`;

        case 'to-number':
            return `(0 + ${translateExpression(args[0] as Json)})`;

        case '!':
            return `(!${translateExpression(args[0] as Json)})`;

        case 'all':
        case 'any': {
            if (args.length === 0) return head === 'all' ? 'true' : 'false';
            const parts = args.map((a) => translateExpression(a as Json, notes));
            return head === 'any' ? `(${parts.join(' || ')})` : conjunction(parts);
        }

        case 'case':
            return translateCase(args as Json[]);

        case 'match':
            return translateMatch(args as Json[]);

        case 'step':
            return translateStep(args as Json[]);

        case 'interpolate':
            return translateInterpolate(args as Json[]);

        case 'min':
        case 'max': {
            // CartoCSS min/max are binary; fold left.
            const parts = args.map((a) => translateExpression(a as Json));
            return parts.reduce((acc, part) => `${head}(${acc}, ${part})`);
        }

        default: {
            if (BINARY[head] && args.length === 2) {
                let [a, b] = args.map((x) => translateExpression(x as Json, notes));
                if (a === GEOMETRY_TYPE_FIELD || b === GEOMETRY_TYPE_FIELD) {
                    const name = (a === GEOMETRY_TYPE_FIELD ? b : a).replace(/^'|'$/g, '');
                    const numeric = GEOMETRY_TYPE_VALUE[name];
                    if (numeric === undefined) throw new Untranslatable(`geometry type "${name}"`);
                    if (a === GEOMETRY_TYPE_FIELD) b = String(numeric); else a = String(numeric);
                }
                return `(${a} ${BINARY[head]} ${b})`;
            }
            if (BINARY_FN[head] && args.length === 2) {
                const [a, b] = args.map((x) => translateExpression(x as Json));
                return `${BINARY_FN[head]}(${a}, ${b})`;
            }
            if (UNARY_FN[head] && args.length === 1) {
                return `${UNARY_FN[head]}(${translateExpression(args[0] as Json)})`;
            }
            throw new Untranslatable(`["${head}", ...]`);
        }
    }
}

/** ["case", cond, val, ..., fallback] -> nested ternaries, which is what CartoCSS has. */
function translateCase(args: Json[]): string {
    if (args.length < 3 || args.length % 2 === 0) throw new Untranslatable('malformed case');
    const fallback = translateExpression(args[args.length - 1]);
    let out = fallback;
    for (let i = args.length - 3; i >= 0; i -= 2) {
        out = `(${translateExpression(args[i])} ? ${translateExpression(args[i + 1])} : ${out})`;
    }
    return out;
}

/**
 * ["match", input, label, value, ..., fallback]. CartoCSS has no match over an arbitrary input, so
 * this expands to equality ternaries. Multi-label branches (a label array) expand to an or-chain.
 */
function translateMatch(args: Json[]): string {
    if (args.length < 4 || args.length % 2 !== 0) throw new Untranslatable('malformed match');
    const input = translateExpression(args[0]);
    let out = translateExpression(args[args.length - 1]);
    for (let i = args.length - 3; i >= 1; i -= 2) {
        const labels = Array.isArray(args[i]) ? (args[i] as Json[]) : [args[i]];
        const test = labels.map((l) => `${input} = ${translateExpression(l)}`).join(' || ');
        out = `((${test}) ? ${translateExpression(args[i + 1])} : ${out})`;
    }
    return out;
}

/**
 * ["step", input, base, stop, value, ...] -> step([view::zoom], (0, base), (stop, value), ...).
 *
 * Every argument after the input has to be a (key, value) list - CartoCSSMapnikTranslator refuses
 * a flat one ("Expecting element list for interpolation function"). MapBox's base covers everything
 * below the first stop, which is a keyframe at zoom 0.
 */
function translateStep(args: Json[]): string {
    if (args.length < 3) throw new Untranslatable('malformed step');
    const input = requireZoom(args[0], 'step');
    const stops = [`(0, ${translateExpression(args[1])})`];
    for (let i = 2; i < args.length; i += 2) {
        stops.push(`(${translateExpression(args[i])}, ${translateExpression(args[i + 1])})`);
    }
    return `step(${input}, ${stops.join(', ')})`;
}

/**
 * ["interpolate", ["linear"], ["zoom"], z, v, ...] -> linear([view::zoom], (z, v), ...).
 * ["exponential", 1] is linear, so it is accepted; any other base is not.
 */
function translateInterpolate(args: Json[]): string {
    if (args.length < 4) throw new Untranslatable('malformed interpolate');
    const [kind, ...rest] = args;
    if (!Array.isArray(kind) || kind.length === 0) throw new Untranslatable('malformed interpolate type');

    let fn: string;
    if (kind[0] === 'linear') fn = 'linear';
    else if (kind[0] === 'cubic-bezier') fn = 'cubic';
    else if (kind[0] === 'exponential' && kind[1] === 1) fn = 'linear';
    else throw new Untranslatable(`interpolate ["${String(kind[0])}"${kind[1] !== undefined ? `, ${String(kind[1])}` : ''}]`);

    const input = requireZoom(rest[0], 'interpolate');
    const stops: string[] = [];
    for (let i = 1; i < rest.length; i += 2) {
        stops.push(`(${translateExpression(rest[i])}, ${translateExpression(rest[i + 1])})`);
    }
    return `${fn}(${input}, ${stops.join(', ')})`;
}

/**
 * CartoCSS interpolation functions only take a view variable as their input; interpolating over a
 * feature field would have to be unrolled, and there is no bounded way to do that.
 */
function requireZoom(input: Json, where: string): string {
    const translated = translateExpression(input);
    if (translated !== '[view::zoom]') {
        throw new Untranslatable(`${where} over ${translated} rather than zoom`);
    }
    return translated;
}

/**
 * The pre-expression "function" syntax: `{stops, base, property, type, default}`.
 *
 * A zoom function maps onto CartoCSS's own interpolation and stays per-frame. A function over a
 * FEATURE property can only be carried when it is a lookup (categorical/interval/identity), since
 * CartoCSS interpolation takes a view variable and nothing else.
 */
function translateStopFunction(fn: Record<string, Json>, notes?: string[]): string {
    const type = typeof fn.type === 'string' ? fn.type : undefined;
    const property = typeof fn.property === 'string' ? fn.property : undefined;
    const input = property !== undefined ? `[${property}]` : '[view::zoom]';

    if (type === 'identity') {
        return input;
    }

    const stops = fn.stops;
    if (!Array.isArray(stops) || stops.length === 0) {
        throw new Untranslatable(property !== undefined ? 'property function with no stops' : 'object literal');
    }
    const pairs = stops.map((stop) => {
        if (!Array.isArray(stop) || stop.length !== 2) throw new Untranslatable('malformed stop');
        return [stop[0] as Json, stop[1] as Json] as const;
    });

    if (property === undefined) {
        if (type === 'categorical') throw new Untranslatable('categorical zoom function');
        if (type === 'interval') {
            return `step(${input}, ${pairs.map(([k, v]) => `(${translateExpression(k, notes)}, ${translateExpression(v, notes)})`).join(', ')})`;
        }
        // exponential (the default). CartoCSS has linear and cubic, so a base other than 1 is an
        // approximation: it agrees at every stop and differs only in between.
        const base = typeof fn.base === 'number' ? fn.base : 1;
        if (base !== 1) {
            notes?.push(`exponential zoom function with base ${base} approximated as linear`);
        }
        return `linear(${input}, ${pairs.map(([k, v]) => `(${translateExpression(k, notes)}, ${translateExpression(v, notes)})`).join(', ')})`;
    }

    // Over a feature property. Only a lookup survives; an interpolation does not.
    if (type === 'categorical') {
        return lookupChain(input, pairs, '=', fn.default, notes);
    }
    if (type === 'interval') {
        return lookupChain(input, [...pairs].reverse(), '>=', fn.default, notes);
    }
    throw new Untranslatable(`${type ?? 'exponential'} interpolation over [${property}] rather than zoom`);
}

/** Stops as a ternary chain, since CartoCSS has no lookup construct over a field. */
function lookupChain(
    input: string,
    pairs: ReadonlyArray<readonly [Json, Json]>,
    op: string,
    fallback: Json | undefined,
    notes?: string[],
): string {
    let out = fallback !== undefined ? translateExpression(fallback, notes) : translateExpression(pairs[pairs.length - 1][1], notes);
    for (let i = pairs.length - 1; i >= 0; i--) {
        const [key, value] = pairs[i];
        out = `(${input} ${op} ${translateExpression(key, notes)} ? ${translateExpression(value, notes)} : ${out})`;
    }
    return out;
}
