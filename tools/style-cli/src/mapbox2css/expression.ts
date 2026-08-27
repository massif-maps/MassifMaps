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

        // Mapbox Standard's runtime knobs - lightPreset, theme, showPointOfInterestLabels. A style
        // parameter is the same idea: named, defaulted in the project, and set without a reload, so
        // day/night stays ONE style here as it is there. convert() declares what it finds.
        case 'config': {
            if (typeof args[0] !== 'string') throw new Untranslatable('config with a computed name');
            // ["config", name, importId] scopes the lookup to one import; a converted project has a
            // single style, so the name alone identifies it.
            if (args.length > 1) notes?.push(`config "${args[0]}" read across imports, taken as one parameter`);
            return `[param::${args[0]}]`;
        }

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

        // ["format", section, options, section, options, …]. The per-section options are the rich
        // part - a font, a scale, a colour for that run alone - and CartoCSS styles the whole
        // label at once, so only the TEXT is carried. Mapbox Standard writes every POI name as
        // `["format", ["coalesce", …], {}]`, an empty options object, which loses nothing.
        case 'format': {
            const sections = args.filter((_, i) => i % 2 === 0);
            const styled = args.filter((_, i) => i % 2 === 1)
                .some((options) => !!options && typeof options === 'object' && Object.keys(options).length > 0);
            if (styled) notes?.push('format section options dropped: CartoCSS styles the whole label at once');
            if (sections.length === 0) throw new Untranslatable('format with no sections');
            return sections
                .map((section) => translateExpression(section as Json))
                .reduce((acc, part) => `concat(${acc}, ${part})`);
        }

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

        // ["in", needle, haystack] - the EXPRESSION operator, which is a different thing from the
        // legacy ["in", key, v1, v2, ...] filter and was reaching here as an unknown operator. It
        // is how a modern style says "class is one of these", so a layer using it was dropped
        // whole: MapTiler streets-v4 lost every minor-road FILL that way and drew its outline
        // alone, which reads as grey roads.
        case 'in': {
            if (args.length !== 2) throw new Untranslatable('in with the wrong argument count');
            const haystack = literalList(args[1] as Json);
            if (haystack === null) {
                // A string haystack is a substring test, which CartoCSS has no form for.
                throw new Untranslatable('in over a string or a computed haystack');
            }
            if (haystack.length === 0) return 'false';
            const sliced = sliceSource(args[0] as Json);
            if (sliced !== null) {
                const alternatives = haystack.map((v) => (typeof v === 'string' ? v : null));
                if (alternatives.every((v): v is string => v !== null && v.length === sliced.length)) {
                    notes?.push(`slice(${sliced.of}, 0, ${sliced.length}) compared as a regex prefix`);
                    return `(${sliced.of} =~ '(${alternatives.map(escapeRegex).join('|')}).*')`;
                }
            }
            const needle = translateExpression(args[0] as Json, notes);
            return `(${haystack.map((v) => `${needle} = ${literal(v)}`).join(' || ')})`;
        }

        case 'case':
            return translateCase(args as Json[]);

        case 'match':
            return translateMatch(args as Json[]);

        case 'step':
            return translateStep(args as Json[]);

        case 'interpolate':
            return translateInterpolate(args as Json[], notes);

        case 'min':
        case 'max': {
            // CartoCSS min/max are binary; fold left.
            const parts = args.map((a) => translateExpression(a as Json));
            return parts.reduce((acc, part) => `${head}(${acc}, ${part})`);
        }

        default: {
            // `slice(x, 0, n) == 'D'` is how a style tests a PREFIX, and it is the only use of
            // slice that survives: CartoCSS has no substring, but `=~` is a FULL std::regex_match
            // (Predicate::applyOp, StringUtils::regexMatch), and a prefix is `D.*`. Without this every country-specific road shield fell through
            // to the style's fallback colour - French D-roads drew on a white plate, not a yellow
            // one - because the branch that picks the colour could not be translated at all.
            if ((head === '==' || head === '!=') && args.length === 2) {
                const prefix = prefixTest(args[0] as Json, args[1] as Json, notes)
                    ?? prefixTest(args[1] as Json, args[0] as Json, notes);
                if (prefix !== null) return head === '==' ? prefix : `(!${prefix})`;
            }

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
/**
 * `["slice", x, 0, n]` - the prefix form. Any other slice (a non-zero start, a computed bound) has
 * no regex equivalent and is left to be refused.
 */
function sliceSource(node: Json): { of: string; length: number } | null {
    if (!Array.isArray(node) || node[0] !== 'slice' || node.length !== 4) return null;
    if (node[2] !== 0 || typeof node[3] !== 'number') return null;
    return { of: translateExpression(node[1] as Json), length: node[3] };
}

/** `slice(x, 0, n) == 'PREFIX'` as a full-regex match, or null when it is not that shape. */
function prefixTest(maybeSlice: Json, maybeLiteral: Json, notes?: string[]): string | null {
    const sliced = sliceSource(maybeSlice);
    if (sliced === null || typeof maybeLiteral !== 'string') return null;
    // A prefix of a different length than the slice can never equal it.
    if (maybeLiteral.length !== sliced.length) return 'false';
    notes?.push(`slice(${sliced.of}, 0, ${sliced.length}) compared as a regex prefix`);
    return `(${sliced.of} =~ '${escapeRegex(maybeLiteral)}.*')`;
}

/** The literal is style data, so anything with meaning in a regex has to lose it. */
function escapeRegex(value: string): string {
    return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/**
 * The elements of an `in` haystack, or null when it is not a literal array. Only the spec form
 * counts: a bare array would swallow `["get", "class"]`, whose elements are strings too.
 */
function literalList(node: Json): Json[] | null {
    if (Array.isArray(node) && node[0] === 'literal' && Array.isArray(node[1])) return node[1] as Json[];
    return null;
}

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
    // A step over ZOOM is a per-frame function and maps onto CartoCSS's own; over a FIELD it is a
    // per-feature decision, which is a chain of ternaries. Unlike `interpolate` there is nothing to
    // unroll - a step has finitely many outcomes, one per stop. Mapbox Standard sizes 10 label
    // layers by `["step", ["get", "sizerank"], …]`, which was 35 text-sizes dropped.
    if (translateExpression(args[0]) !== '[view::zoom]') return stepOnField(args);
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
function translateInterpolate(args: Json[], notes?: string[]): string {
    if (args.length < 4) throw new Untranslatable('malformed interpolate');
    const [kind, ...rest] = args;
    if (!Array.isArray(kind) || kind.length === 0) throw new Untranslatable('malformed interpolate type');

    let fn: string;
    let base: number | null = null;
    if (kind[0] === 'linear') fn = 'linear';
    else if (kind[0] === 'cubic-bezier') fn = 'cubic';
    else if (kind[0] === 'exponential' && typeof kind[1] === 'number') {
        fn = 'linear';
        if (kind[1] !== 1) base = kind[1];
    } else {
        throw new Untranslatable(`interpolate ["${String(kind[0])}"${kind[1] !== undefined ? `, ${String(kind[1])}` : ''}]`);
    }

    const input = requireZoom(rest[0], 'interpolate');
    const pairs: Array<readonly [Json, Json]> = [];
    for (let i = 1; i < rest.length; i += 2) {
        pairs.push([rest[i] as Json, rest[i + 1] as Json] as const);
    }

    if (base !== null) {
        const resampled = resampleExponential(pairs, base);
        if (resampled) {
            notes?.push(`exponential interpolation with base ${base} resampled into ${EXPONENTIAL_SUBDIVISIONS} linear steps per stop interval`);
            return `linear(${input}, ${resampled.map(([k, v]) => `(${k}, ${v})`).join(', ')})`;
        }
        notes?.push(`exponential interpolation with base ${base} approximated as linear: its stops are not plain numbers`);
    }

    const stops = pairs.map(([k, v]) => `(${translateExpression(k)}, ${translateExpression(v)})`);
    return `${fn}(${input}, ${stops.join(', ')})`;
}

/**
 * MapBox interpolates an exponential ramp per segment as `t = (b^(x-x0) - 1) / (b^(x1-x0) - 1)`;
 * CartoCSS has `linear` and `cubic` and no base at all. Resampling the curve into extra linear
 * stops agrees at every original stop and stays close between them, where substituting a plain
 * linear does not - at base 2 over four zoom levels it is out by about a third at the midpoint.
 *
 * Returns null when a stop is not a plain number, which is when there is no curve to sample.
 */
const EXPONENTIAL_SUBDIVISIONS = 4;

function resampleExponential(pairs: ReadonlyArray<readonly [Json, Json]>, base: number): Array<readonly [number, number]> | null {
    const numeric: Array<readonly [number, number]> = [];
    for (const [k, v] of pairs) {
        if (typeof k !== 'number' || typeof v !== 'number') return null;
        numeric.push([k, v] as const);
    }
    if (numeric.length < 2) return null;

    const round = (n: number) => Math.round(n * 1e4) / 1e4;
    const out: Array<readonly [number, number]> = [];
    for (let i = 0; i + 1 < numeric.length; i++) {
        const [x0, y0] = numeric[i];
        const [x1, y1] = numeric[i + 1];
        out.push([round(x0), round(y0)] as const);
        const span = x1 - x0;
        const denom = Math.pow(base, span) - 1;
        if (!(span > 0) || denom === 0) continue;
        for (let j = 1; j < EXPONENTIAL_SUBDIVISIONS; j++) {
            const x = x0 + (span * j) / EXPONENTIAL_SUBDIVISIONS;
            const t = (Math.pow(base, x - x0) - 1) / denom;
            out.push([round(x), round(y0 + (y1 - y0) * t)] as const);
        }
    }
    const last = numeric[numeric.length - 1];
    out.push([round(last[0]), round(last[1])] as const);
    return out;
}

/** `["step", x, base, k1, v1, …]` over a field: the highest stop x reaches, else the base. */
function stepOnField(args: Json[]): string {
    const input = translateExpression(args[0]);
    let out = translateExpression(args[1]);
    // Built from the bottom up, so the outermost test is the highest stop - which is the one a
    // reader checks first and the order `step` itself resolves in.
    for (let i = 2; i + 1 < args.length; i += 2) {
        out = `((${input} >= ${translateExpression(args[i])}) ? ${translateExpression(args[i + 1])} : ${out})`;
    }
    return out;
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
