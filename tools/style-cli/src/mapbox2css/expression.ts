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

/**
 * MapBox expression -> a CartoCSS expression string.
 *
 * Zoom-driven `interpolate`/`step` become CartoCSS `linear()`/`step()` over `[view::zoom]`, which
 * is what keeps them re-evaluated per frame instead of frozen into the tile. An interpolation over
 * anything other than zoom has no such form and is refused.
 */
export function translateExpression(expr: Json): string {
    if (expr === null || typeof expr !== 'object') return literal(expr);
    if (!Array.isArray(expr)) throw new Untranslatable('object literal');
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
            return '[mapnik::geometry_type]';

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
            const op = head === 'all' ? ' && ' : ' || ';
            return `(${args.map((a) => translateExpression(a as Json)).join(op)})`;
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
                const [a, b] = args.map((x) => translateExpression(x as Json));
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
