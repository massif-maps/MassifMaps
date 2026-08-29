import type { Json, MapboxLayer } from './types.js';

/**
 * Resolving `["config", name]` to a constant, and evaluating everything that makes decidable.
 *
 * Mapbox Standard is written against its own config: 878 `["config", …]` reads across 150 layers,
 * and every colour among them goes through the same idiom - take the configured colour apart with
 * `to-hsla`, bind the three channels with `let`, adjust them, and put it back together with `hsl`:
 *
 *     ["let", "l_colorLand", ["at", 2, ["to-hsla", ["config", "colorLand"]]],
 *       ["hsl", 20, 20, ["-", ["var", "l_colorLand"], 10]]]
 *
 * CartoCSS has none of those operators, so untouched this is 74 colour properties that convert to
 * nothing. With the config value substituted the whole expression is constant arithmetic, and what
 * comes out is a plain colour. That is what this file does: substitute, then evaluate as far as the
 * constants reach, and leave everything else exactly as written.
 *
 * Structure is preserved on purpose. Only branches whose test is now DECIDED are removed, so the
 * layer set, its properties and their order come out identical for every config - which is what
 * lets one `style.mss` be shared by several palettes, one per `lightPreset`.
 */

/** A scalar. An array or object is still an expression until proven otherwise. */
function isScalar(node: Json): boolean {
    return node === null || typeof node === 'string' || typeof node === 'number' || typeof node === 'boolean';
}

/** A constant value, whether written bare or wrapped in `["literal", …]`. */
function constantOf(node: Json): { value: Json } | null {
    if (isScalar(node)) return { value: node };
    if (Array.isArray(node) && node[0] === 'literal' && node.length === 2) return { value: node[1] as Json };
    return null;
}

/** A constant list - what `to-hsla` produces and `at` reads. Always kept wrapped, never bare. */
function constantList(node: Json): Json[] | null {
    const constant = constantOf(node);
    return constant && Array.isArray(constant.value) ? constant.value : null;
}

const literal = (value: Json): Json => (isScalar(value) ? value : ['literal', value]);

function numbersOf(args: Json[]): number[] | null {
    const out: number[] = [];
    for (const arg of args) {
        const constant = constantOf(arg);
        if (!constant || typeof constant.value !== 'number') return null;
        out.push(constant.value);
    }
    return out;
}

const CSS_COLOUR = /^(hsla?|rgba?)\(([^()]*)\)$/i;

/** Mapbox's channel ranges: hue 0-360, saturation and lightness 0-100, alpha 0-1. */
export function toHsla(value: Json): [number, number, number, number] | null {
    if (typeof value !== 'string') return null;
    const text = value.trim();

    const functional = CSS_COLOUR.exec(text);
    if (functional) {
        const parts = functional[2].split(/[,/\s]+/).filter(Boolean);
        const numbers = parts.map((p) => parseFloat(p));
        if (numbers.some((n) => Number.isNaN(n))) return null;
        const alpha = numbers.length > 3 ? numbers[3] : 1;
        if (functional[1].toLowerCase().startsWith('hsl')) {
            return [numbers[0], numbers[1], numbers[2], alpha];
        }
        return rgbToHsl(numbers[0], numbers[1], numbers[2], alpha);
    }

    const hex = /^#([0-9a-f]{3,8})$/i.exec(text);
    if (hex) {
        const digits = hex[1];
        const expand = (i: number): number => (digits.length <= 4
            ? parseInt(digits[i] + digits[i], 16)
            : parseInt(digits.slice(i * 2, i * 2 + 2), 16));
        const alpha = digits.length === 4 || digits.length === 8 ? expand(3) / 255 : 1;
        return rgbToHsl(expand(0), expand(1), expand(2), alpha);
    }
    return null;
}

function rgbToHsl(r: number, g: number, b: number, a: number): [number, number, number, number] {
    const [rn, gn, bn] = [r / 255, g / 255, b / 255];
    const max = Math.max(rn, gn, bn);
    const min = Math.min(rn, gn, bn);
    const l = (max + min) / 2;
    if (max === min) return [0, 0, l * 100, a];
    const d = max - min;
    const s = d / (l > 0.5 ? 2 - max - min : max + min);
    const h = max === rn ? (gn - bn) / d + (gn < bn ? 6 : 0)
        : max === gn ? (bn - rn) / d + 2
        : (rn - gn) / d + 4;
    return [h * 60, s * 100, l * 100, a];
}

/** Back out as CSS, which is what the CartoCSS grammar reads and what the palette shows. */
export function fromHsla(h: number, s: number, l: number, a: number): string {
    const round = (n: number): number => Math.round(n * 100) / 100;
    return a >= 1
        ? `hsl(${round(h)}, ${round(s)}%, ${round(l)}%)`
        : `hsla(${round(h)}, ${round(s)}%, ${round(l)}%, ${round(a)})`;
}

/**
 * Values the CAMERA decides rather than the config. None of them exist as a style value in this
 * SDK, so they are resolved at conversion time to what a flat, centred view sees.
 */
export interface Scene {
    /** See config.ts's sceneBrightness. */
    brightness?: number;
    /**
     * Leave `["measure-light", "brightness"]` in the tree instead of folding it, so the translator
     * emits `[view::brightness]` and the SDK resolves it per frame. Set by --live-light; `brightness`
     * is still needed, because a ramp the translator cannot carry falls back to that constant.
     */
    liveBrightness?: boolean;
}

/**
 * Viewport terms, and what they resolve to.
 *
 * Mapbox thins its labels by where the tile falls on a pitched screen. `road-label`'s filter is
 * `["case", ["<=", ["pitch"], 40], true, ["step", ["pitch"], …, ["<", ["distance-from-center"], 1] …]]`
 * - true below 40 degrees of pitch, and progressively stricter above it. Our label culler does its
 * own thinning and nothing feeds a camera angle back into a style, so these resolve to the flat,
 * centred view: the clause becomes `true` and folds out of the filter around it.
 *
 * Left alone they took 27 LAYERS with them - every label in the style - because an untranslatable
 * filter drops the whole rule, not just the test.
 */
const VIEWPORT: Record<string, number> = {
    pitch: 0,
    'distance-from-center': 0,
    'line-progress': 0,
};

/**
 * Runtime INTERACTION state, which the SDK has no notion of at all: nothing is hovered, selected or
 * on an active indoor floor, and nothing can become so. Folded to the value MapBox itself gives an
 * unset one, so the `case` around it collapses to the branch that draws the ordinary feature.
 *
 * Left alone they cost whole layers rather than one property: `["to-boolean", ["feature-state",
 * "select"]]` guards 3d-building's colour, and a dropped fill left the symbolizer on its default
 * black; `["is-active-floor"]` sits inside poi-label's filter.
 */
const UNSET_STATE: Record<string, Json> = {
    'feature-state': null,
    'is-active-floor': false,
};

interface Context {
    values: Map<string, Json>;
    scene: Scene;
    /** `let` bindings in scope, which is why folding cannot be a plain map over the children. */
    bindings: Map<string, Json>;
}

/**
 * Replaces every `["config", name]` this map covers, then evaluates as far as the constants go.
 * An unknown name is left alone, and so is anything reading a feature, the zoom or the scene light.
 */
export function foldConfig(node: Json, values: Map<string, Json>, scene: Scene = {}): Json {
    return fold(node, { values, scene, bindings: new Map() });
}

function fold(node: Json, context: Context): Json {
    if (Array.isArray(node)) {
        const head = node[0];
        if (head === 'config' && typeof node[1] === 'string' && context.values.has(node[1])) {
            return literal(context.values.get(node[1]) as Json);
        }
        if (head === 'measure-light' && node[1] === 'brightness' && context.scene.brightness !== undefined && !context.scene.liveBrightness) {
            return context.scene.brightness;
        }
        if (typeof head === 'string' && node.length === 1 && head in VIEWPORT) {
            return VIEWPORT[head];
        }
        if (typeof head === 'string' && head in UNSET_STATE) {
            return UNSET_STATE[head];
        }
        // `["image", name, { params: … }]` is Standard's RECOLOURABLE sprite. The SDK picks a
        // sprite by name and cannot tint one per feature, so the name is what survives and the
        // params go. Unwrapped here rather than in the translator because the icon-name machinery
        // walks the raw tree - a `match` over a field whose branches are `image` nodes only reads
        // as data-driven once the wrapper is off.
        if (head === 'image' && node.length >= 2) {
            return fold(node[1] as Json, context);
        }
        // `let` binds names for its body only, so its bindings are folded first and the body is
        // folded UNDER them - a generic map over the children would lose the scope.
        if (head === 'let' && node.length >= 2 && node.length % 2 === 0) {
            const scoped: Context = { ...context, bindings: new Map(context.bindings) };
            const kept: Json[] = [];
            for (let i = 1; i + 1 < node.length; i += 2) {
                const name = node[i];
                const value = fold(node[i + 1] as Json, scoped);
                // A constant binding always inlines. A NON-constant one inlines when it is read at
                // most ONCE in what follows: `let` exists to avoid repeating a subexpression, so a
                // single reader can never blow the expression up, and none at all is dead. The
                // wrapper is what hides the shape from everything downstream - poi-label names its
                // icon through one binding read once, beside four colour bindings its body never
                // reads, and leaving the `let` on cost every POI icon in Standard.
                const inline = typeof name === 'string'
                    && (constantOf(value) !== null || countVar(node.slice(i + 2) as Json, name) <= 1);
                if (inline) scoped.bindings.set(name as string, value);
                else kept.push(name as Json, value);
            }
            const body = fold(node[node.length - 1] as Json, scoped);
            return kept.length === 0 ? body : ['let', ...kept, body];
        }
        if (head === 'var' && typeof node[1] === 'string' && context.bindings.has(node[1])) {
            return context.bindings.get(node[1]) as Json;
        }
        // Simplified ONLY where something was actually substituted. Folding an expression nothing
        // resolved into would rewrite styles that have no config at all - it removed four layers
        // from a MapTiler style whose output was already verified on device - and the point here is
        // to resolve what the SDK cannot express, not to optimise anyone's expressions.
        const folded = node.map((child) => fold(child as Json, context));
        return folded.some((child, i) => child !== node[i]) ? simplify(folded) : node;
    }
    if (node && typeof node === 'object') {
        const entries = Object.entries(node);
        const folded = entries.map(([key, child]) => [key, fold(child, context)] as const);
        return folded.some(([, child], i) => child !== entries[i][1])
            ? Object.fromEntries(folded) : node;
    }
    return node;
}

/** How many times `["var", name]` is read in a subtree. Stops counting at 2 - only 1 matters. */
function countVar(node: Json, name: string): number {
    if (!Array.isArray(node)) return 0;
    if (node[0] === 'var' && node[1] === name) return 1;
    let seen = 0;
    for (const child of node) {
        seen += countVar(child as Json, name);
        if (seen > 1) return seen;
    }
    return seen;
}

/** Every operator whose value follows from constant arguments alone. */
function simplify(expr: Json[]): Json {
    const [head, ...args] = expr;
    const unchanged = expr as Json;
    switch (head) {
        case 'match': return simplifyMatch(args);
        case 'case': return simplifyCase(args);
        case 'coalesce': return simplifyCoalesce(args);
        case 'all':
        case 'any': return simplifyLogic(head, args) ?? unchanged;
        case '!': {
            const operand = constantOf(args[0] as Json);
            return operand ? !operand.value : unchanged;
        }
        // Only meaningful here over a folded UNSET_STATE, which is the whole reason it is listed:
        // it is what turns the `case` guarding a selected building into its ordinary colour.
        case 'to-boolean': {
            const operand = constantOf(args[0] as Json);
            return operand ? !!operand.value : unchanged;
        }
        case '==':
        case '!=':
        case '<':
        case '<=':
        case '>':
        case '>=': return compare(head, args) ?? unchanged;

        // The colour idiom: take a configured colour apart, adjust a channel, put it back.
        case 'to-hsla': {
            const value = constantOf(args[0] as Json);
            const hsla = value ? toHsla(value.value) : null;
            return hsla ? literal(hsla as unknown as Json) : unchanged;
        }
        case 'hsl':
        case 'hsla': {
            const numbers = numbersOf(args);
            if (!numbers || numbers.length < 3) return unchanged;
            return fromHsla(numbers[0], numbers[1], numbers[2], head === 'hsla' ? numbers[3] ?? 1 : 1);
        }
        case 'rgb':
        case 'rgba': {
            const numbers = numbersOf(args);
            if (!numbers || numbers.length < 3) return unchanged;
            const [h, s, l, a] = rgbToHsl(numbers[0], numbers[1], numbers[2], head === 'rgba' ? numbers[3] ?? 1 : 1);
            return fromHsla(h, s, l, a);
        }
        case 'at': {
            const index = constantOf(args[0] as Json);
            const list = constantList(args[1] as Json);
            if (!list || !index || typeof index.value !== 'number') return unchanged;
            return index.value >= 0 && index.value < list.length ? literal(list[index.value]) : unchanged;
        }

        case '+':
        case '*':
        case '-':
        case '/':
        case '%':
        case '^':
        case 'min':
        case 'max':
        case 'abs':
        case 'floor':
        case 'ceil':
        case 'round':
        case 'sqrt': return arithmetic(head, args) ?? unchanged;

        // `random(lo, hi, seed)` scatters a value per feature and CartoCSS has no seed to do it
        // with, so it folds to the MIDDLE of its range - one green for every tree instead of a
        // spread around one. The bounds are what carry the intent; the scatter is texture.
        case 'random': {
            const lo = constantOf(args[0] as Json);
            const hi = constantOf(args[1] as Json);
            if (typeof lo?.value !== 'number' || typeof hi?.value !== 'number') return unchanged;
            return (lo.value + hi.value) / 2;
        }

        case 'to-number': {
            const value = constantOf(args[0] as Json);
            if (!value) return unchanged;
            const number = Number(value.value);
            return Number.isFinite(number) ? number : unchanged;
        }
        case 'to-boolean': {
            const value = constantOf(args[0] as Json);
            return value ? Boolean(value.value) : unchanged;
        }
        case 'to-string': {
            const value = constantOf(args[0] as Json);
            return value && isScalar(value.value) ? String(value.value ?? '') : unchanged;
        }
        case 'concat': {
            const parts = args.map((a) => constantOf(a as Json));
            return parts.every((p) => p && typeof p.value === 'string')
                ? parts.map((p) => p!.value as string).join('') : unchanged;
        }
        case 'interpolate':
        case 'step': return simplifyRamp(head, args) ?? unchanged;
        default: return unchanged;
    }
}

function arithmetic(head: string, args: Json[]): Json | null {
    const value = compute(head, args);
    // A NaN or an infinity reaching the stylesheet is a broken declaration, not a value; leaving
    // the expression as it was gets it reported as untranslatable instead of drawn wrong.
    return typeof value === 'number' && !Number.isFinite(value) ? null : value;
}

function compute(head: string, args: Json[]): Json | null {
    const numbers = numbersOf(args);
    if (!numbers || numbers.length === 0) return null;
    switch (head) {
        case '+': return numbers.reduce((a, b) => a + b);
        case '*': return numbers.reduce((a, b) => a * b);
        // MapBox's unary minus negates; with two arguments it subtracts.
        case '-': return numbers.length === 1 ? -numbers[0] : numbers[0] - numbers[1];
        case '/': return numbers.length === 2 && numbers[1] !== 0 ? numbers[0] / numbers[1] : null;
        case '%': return numbers.length === 2 && numbers[1] !== 0 ? numbers[0] % numbers[1] : null;
        case '^': return numbers.length === 2 ? Math.pow(numbers[0], numbers[1]) : null;
        case 'min': return Math.min(...numbers);
        case 'max': return Math.max(...numbers);
        case 'abs': return Math.abs(numbers[0]);
        case 'floor': return Math.floor(numbers[0]);
        case 'ceil': return Math.ceil(numbers[0]);
        case 'round': return Math.round(numbers[0]);
        case 'sqrt': return Math.sqrt(numbers[0]);
        default: return null;
    }
}

function compare(head: string, args: Json[]): Json | null {
    const left = constantOf(args[0] as Json);
    const right = constantOf(args[1] as Json);
    // A third argument is a collator, whose comparison is not this one.
    if (!left || !right || args.length > 2) return null;
    if (head === '==') return left.value === right.value;
    if (head === '!=') return left.value !== right.value;
    if (typeof left.value !== typeof right.value) return null;
    const [a, b] = [left.value, right.value] as [number, number];
    return head === '<' ? a < b : head === '<=' ? a <= b : head === '>' ? a > b : a >= b;
}

function simplifyMatch(args: Json[]): Json {
    const unchanged = ['match', ...args] as Json;
    const input = constantOf(args[0] as Json);
    if (!input || args.length < 4 || args.length % 2 !== 0) return unchanged;
    for (let i = 1; i + 1 < args.length; i += 2) {
        const raw = args[i];
        // A label list is data, never an expression.
        const labels = Array.isArray(raw) && raw[0] !== 'literal' ? raw : [constantOf(raw as Json)?.value];
        if (!labels.every((l) => isScalar(l as Json))) return unchanged;
        if (labels.some((l) => l === input.value)) return args[i + 1];
    }
    return args[args.length - 1];
}

function simplifyCase(args: Json[]): Json {
    if (args.length < 3 || args.length % 2 !== 1) return ['case', ...args];
    const kept: Json[] = [];
    for (let i = 0; i + 1 < args.length; i += 2) {
        const test = constantOf(args[i] as Json);
        if (!test) {
            kept.push(args[i], args[i + 1]);
            continue;
        }
        // A decided branch either wins outright - everything after it is dead - or is dropped.
        if (test.value) return kept.length === 0 ? args[i + 1] : ['case', ...kept, args[i + 1]];
    }
    const fallback = args[args.length - 1];
    return kept.length === 0 ? fallback : ['case', ...kept, fallback];
}

function simplifyCoalesce(args: Json[]): Json {
    const kept: Json[] = [];
    for (const arg of args) {
        const value = constantOf(arg);
        if (value && value.value !== null) return kept.length === 0 ? arg : ['coalesce', ...kept, arg];
        if (!value) kept.push(arg);
    }
    return kept.length === 0 ? null : kept.length === 1 ? kept[0] : ['coalesce', ...kept];
}

function simplifyLogic(head: string, args: Json[]): Json | null {
    const decisive = head !== 'all';
    const kept: Json[] = [];
    for (const arg of args) {
        const value = constantOf(arg);
        if (!value) {
            kept.push(arg);
            continue;
        }
        if (Boolean(value.value) === decisive) return decisive;
    }
    return kept.length === 0 ? !decisive : kept.length === args.length ? null : [head, ...kept];
}

/** A ramp over a now-constant input is just the value it lands on. */
function simplifyRamp(head: string, args: Json[]): Json | null {
    const inputIndex = head === 'interpolate' ? 1 : 0;
    const input = constantOf(args[inputIndex] as Json);
    if (!input || typeof input.value !== 'number') return null;

    const stops: Array<[number, Json]> = [];
    const first = inputIndex + 1;
    // `step`'s first argument after the input is the value BELOW every stop, so it is a stop at -inf.
    if (head === 'step') stops.push([-Infinity, args[first] as Json]);
    for (let i = head === 'step' ? first + 1 : first; i + 1 < args.length + 1 && i + 1 <= args.length; i += 2) {
        const key = constantOf(args[i] as Json);
        if (!key || typeof key.value !== 'number') return null;
        stops.push([key.value, args[i + 1] as Json]);
    }
    if (stops.length === 0) return null;

    let below: [number, Json] | null = null;
    let above: [number, Json] | null = null;
    for (const stop of stops) {
        if (stop[0] <= input.value) below = stop;
        else { above = stop; break; }
    }
    // Outside the stops mapbox CLAMPS to the nearest end. Interpolating past the first one instead
    // divides by a zero-width span: night's brightness of 0.06 under a ramp starting at 0.1 came
    // out as a literal NaN in the opacity of every landuse polygon.
    if (!below) return stops[0][1];
    if (!above || above[0] === below[0]) return below[1];
    if (head === 'step' || below[0] === -Infinity) return below[1];

    const kind = args[0];
    const linear = Array.isArray(kind) && (kind[0] === 'linear' || (kind[0] === 'exponential' && kind[1] === 1));
    const [a, b] = [constantOf(below[1]), constantOf(above[1])];
    const t = (input.value - below[0]) / (above[0] - below[0]);

    // Stops that are still expressions cannot be blended - Standard ramps a per-feature `match`
    // over the scene brightness, and at dusk the value lands between the two. Snapping to the
    // nearer end keeps ONE value per property, which is what every preset sharing a style.mss
    // depends on; the alternative is a colour that converts to nothing at that preset alone.
    if (!a || !b) return t < 0.5 ? below[1] : above[1];
    if (!linear) return null;

    if (typeof a.value === 'number' && typeof b.value === 'number') {
        return a.value + (b.value - a.value) * t;
    }
    // A colour ramp over the scene brightness is how Standard states a lit and an unlit form of the
    // same colour, so it has to resolve too - in HSL, which is the space it wrote them in.
    const [from, to] = [toHsla(a.value), toHsla(b.value)];
    if (!from || !to) return null;
    const mix = (i: number): number => from[i] + (to[i] - from[i]) * t;
    // Round the hue the short way, or a ramp from 350 to 10 sweeps backwards through the wheel.
    let hue = from[0] + (((to[0] - from[0]) % 360 + 540) % 360 - 180) * t;
    hue = ((hue % 360) + 360) % 360;
    return fromHsla(hue, mix(1), mix(2), mix(3));
}

/** The whole layer, folded - filter, layout and paint alike, since Standard keys all three. */
export function foldLayer(layer: MapboxLayer, values: Map<string, Json>, scene: Scene = {}): MapboxLayer {
    // The icon's `params` have to be taken before the fold, which unwraps
    // `["image", name, { params }]` down to the name. They are the icon's colours - a disc, its
    // ring and the glyph - and the sheet bakes only the icon's OWN defaults into its flat render,
    // so these are the only place the style's per-feature colours survive. Folded on their own,
    // since they read `config` and `let` like the rest of the layer.
    const params = foldImageParams(layer.layout?.['icon-image'] as Json, values, scene);
    const folded = foldConfig(layer as unknown as Json, values, scene) as unknown as MapboxLayer;
    if (!params) return folded;
    const scope = imageParamScope(layer.layout?.['icon-image'] as Json);
    const layout: Record<string, Json> = {
        ...folded.layout, [RECOLOURABLE_ICON]: true, [ICON_PARAMS]: params,
    };
    if (scope) layout[ICON_PARAM_SCOPE] = scope as unknown as Json;
    return { ...folded, layout };
}

/** The synthetic layout keys foldLayer leaves behind; `icon-image` no longer says any of them. */
export const RECOLOURABLE_ICON = 'massif:recolourable-icon';
export const ICON_PARAMS = 'massif:icon-params';
export const ICON_PARAM_SCOPE = 'massif:icon-param-scope';

/** Which features the params apply to, when a `match` recolours some of its branches and not all. */
export interface IconParamScope {
    field: string;
    labels: string[];
}

function hasImageParams(node: Json): boolean {
    if (!Array.isArray(node)) return false;
    if (node[0] === 'image' && node.length >= 3) return true;
    return node.some((child) => hasImageParams(child as Json));
}

/**
 * The branch the params belong to, when they do not cover the whole layer.
 *
 * Standard's transit label recolours seven networks by name and lets every other one through to the
 * sheet's own artwork - `match(get(network), [bus, …], image(…, {params}), image(…))`. Applied to
 * all of them, Paris's own metro roundel (white disc, blue ring, blue M) came out as the generic
 * blue square with a white M. Null when the params cover everything, which needs no test at all.
 */
function imageParamScope(node: Json): IconParamScope | null {
    if (!Array.isArray(node)) return null;
    if (node[0] === 'match' && node.length >= 5 && node.length % 2 === 1
        && !hasImageParams(node[node.length - 1] as Json)) {
        let input = node[1] as Json;
        while (Array.isArray(input) && (input[0] === 'string' || input[0] === 'to-string')) {
            input = input[1] as Json;
        }
        if (Array.isArray(input) && input[0] === 'get' && typeof input[1] === 'string') {
            for (let i = 2; i + 1 < node.length; i += 2) {
                if (!hasImageParams(node[i + 1] as Json)) continue;
                const raw = Array.isArray(node[i]) ? node[i] as Json[] : [node[i] as Json];
                if (!raw.every((label) => typeof label === 'string')) return null;
                return { field: input[1], labels: raw as string[] };
            }
        }
    }
    for (const child of node) {
        const found = imageParamScope(child as Json);
        if (found) return found;
    }
    return null;
}

/**
 * The params of the FIRST `["image", name, { params }]` in the value, folded. A branching
 * icon-image may state several - Standard's transit label recolours its known networks and falls
 * through to a bare name - and one rule carries one set of colours, so the first is the rule's.
 *
 * Folded IN PLACE of that image node where the tree allows, because the bindings around it are
 * still in scope there: Standard's POI icon decomposes its colour into four `let` vars before
 * naming the icon, and the params read them. Taken out and folded alone they are `["var", …]` and
 * nothing resolves - which cost the POI disc its class colour. A tree that does not collapse to
 * the params (a `match` on a FEATURE field keeps every branch) falls back to folding them alone,
 * which is enough where the params read only `config`.
 */
function foldImageParams(image: Json, values: Map<string, Json>, scene: Scene): Json | null {
    let params: Json | null = null;
    const substitute = (node: Json): Json => {
        if (params !== null || !Array.isArray(node)) return node;
        if (node[0] === 'image' && node.length >= 3 && node[2] && typeof node[2] === 'object' && !Array.isArray(node[2])) {
            const own = (node[2] as Record<string, Json>).params;
            if (own && typeof own === 'object' && !Array.isArray(own)) {
                params = own;
                return own;
            }
        }
        return node.map(substitute) as unknown as Json;
    };
    const substituted = substitute(image);
    if (params === null) return null;
    const inPlace = foldConfig(substituted, values, scene);
    if (inPlace && typeof inPlace === 'object' && !Array.isArray(inPlace)) return inPlace;
    return foldConfig(params, values, scene);
}
