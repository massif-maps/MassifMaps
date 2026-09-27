/**
 * @internal
 * @module
 */
import { ALIASES, BASES, CLASS_NAMES, ENUMS, EVENTS, KIND_OF_CLASS, METHODS, PROPS, SPEC_CLASS } from './schema.js';

/**
 * Path resolution over the generated tables, the way the C++ property table does it.
 *
 * Nothing here talks to native. It answers one question - "what shape is the value at the end of
 * this path" - so `get`/`set` can pick the right verb, translate an enum constant, and parse a
 * struct back out of its JSON. A path it cannot resolve is not an error: the concrete class of an
 * intermediate object is often more derived than the declared one (a layer's `tileDecoder` is
 * really an MBVectorTileDecoder), and the C++ resolves those where this cannot. The callers fall
 * back to a string read instead of refusing.
 */

/**
 * b bool, i int, f float, c color, s string, v variant, t struct, p position, e enum, o object,
 * g bag - a property whose keys are the app's, where the rest of the path is one key.
 */
export type PropCode = 'b' | 'i' | 'f' | 'c' | 's' | 'v' | 't' | 'p' | 'e' | 'o' | 'g';

export interface PropInfo {
    code: PropCode;
    /** The enum's name for `e`, the declared class for `o`, one entry's code for `g`. */
    arg?: string;
}

/** v void, b bool, i int, f float, s string, j json, d doubles, o object. */
export type ResultCode = 'v' | 'b' | 'i' | 'f' | 's' | 'j' | 'd' | 'o';

export interface MethodInfo {
    code: ResultCode;
    /** The result's class, for `o`. */
    arg?: string;
}

const propCache: { [cls: string]: { [name: string]: PropInfo } } = {};
const aliasCache: { [cls: string]: { [alias: string]: string } } = {};
const methodCache: { [cls: string]: { [name: string]: MethodInfo } } = {};

function parseProps(cls: string) {
    let table = propCache[cls];
    if (!table) {
        table = propCache[cls] = {};
        const encoded = PROPS[cls];
        if (encoded) {
            for (const entry of encoded.split(';')) {
                const [name, code, arg] = entry.split(',');
                table[name] = arg ? { code: code as PropCode, arg } : { code: code as PropCode };
            }
        }
    }
    return table;
}

function parseAliases(cls: string) {
    let table = aliasCache[cls];
    if (!table) {
        table = aliasCache[cls] = {};
        const encoded = ALIASES[cls];
        if (encoded) {
            for (const entry of encoded.split(';')) {
                const [alias, path] = entry.split('=');
                table[alias] = path;
            }
        }
    }
    return table;
}

function parseMethods(cls: string) {
    let table = methodCache[cls];
    if (!table) {
        table = methodCache[cls] = {};
        const encoded = METHODS[cls];
        if (encoded) {
            for (const entry of encoded.split(';')) {
                const [name, code, arg] = entry.split(',');
                table[name] = arg ? { code: code as ResultCode, arg } : { code: code as ResultCode };
            }
        }
    }
    return table;
}

/**
 * Every property name on a class and its bases, nearest first.
 *
 * Used to hang getters off an event payload, so a handler reads `e.reason` rather than
 * `e.get('reason')`. Names only - the shapes come from findProperty when one is read.
 */
export function propertyNames(cls: string): string[] {
    const names: string[] = [];
    let walk: string | undefined = cls;
    while (walk) {
        for (const name of Object.keys(parseProps(walk))) {
            if (names.indexOf(name) < 0) {
                names.push(name);
            }
        }
        walk = BASES[walk];
    }
    return names;
}

/**
 * One property, found on the class or on any of its bases - which is where most of them live.
 *
 * An alias is a second spelling of one segment (`fog` for `fogOptions`), resolved here so nothing
 * above has to know one was used.
 */
export function findProperty(cls: string, name: string): PropInfo | null {
    let walk: string | undefined = cls;
    while (walk) {
        const found = parseProps(walk)[name] ?? parseProps(walk)[parseAliases(walk)[name]];
        if (found) {
            return found;
        }
        walk = BASES[walk];
    }
    return null;
}

/**
 * The shape at the end of a dotted path.
 *
 * Every segment but the last has to be an object property. A path that walks INTO a struct or a
 * variant (`feature.properties.name`, `clickInfo.clickType`) resolves to `v`: the C++ keeps
 * walking inside the JSON and answers with the leaf's natural type, which this table cannot know.
 */
export function resolvePath(cls: string, path: string): PropInfo | null {
    if (!path) {
        return null;
    }
    const segments = path.split('.');
    let current = cls;
    for (let i = 0; i < segments.length; i++) {
        const prop = findProperty(current, segments[i]);
        if (!prop) {
            return null;
        }
        if (i === segments.length - 1) {
            // A bag written whole takes an object of entries, which crosses as JSON.
            return prop.code === 'g' ? { code: 'v' } : prop;
        }
        if (prop.code === 'o') {
            current = prop.arg as string;
            continue;
        }
        // A bag: everything left is ONE key, whatever it contains - valhalla's parameters nest.
        if (prop.code === 'g') {
            return { code: (prop.arg as PropCode) ?? 's' };
        }
        // A struct or a variant: the rest of the path is read out of its JSON.
        return prop.code === 't' || prop.code === 'p' || prop.code === 'v' ? { code: 'v' } : null;
    }
    return null;
}

export function findMethod(cls: string, name: string): MethodInfo | null {
    let walk: string | undefined = cls;
    while (walk) {
        const found = parseMethods(walk)[name];
        if (found) {
            return found;
        }
        walk = BASES[walk];
    }
    return null;
}

/** A method addressed through a path: everything before the last dot traverses objects. */
export function resolveMethod(cls: string, path: string): MethodInfo | null {
    const cut = path.lastIndexOf('.');
    if (cut < 0) {
        return findMethod(cls, path);
    }
    const owner = resolvePath(cls, path.substring(0, cut));
    return owner && owner.code === 'o' ? findMethod(owner.arg as string, path.substring(cut + 1)) : null;
}

export interface EventInfo {
    /** The payload's class, or '' when the event carries none. */
    payload: string;
    /** Whether the SDK asks the listener if the event was consumed. */
    consume: boolean;
}

/** One event's shape, found on the class or on any of its bases. Null when it is not an event. */
export function findEvent(cls: string, event: string): EventInfo | null {
    let walk: string | undefined = cls;
    while (walk) {
        const table = EVENTS[walk];
        if (table && event in table) {
            return table[event];
        }
        walk = BASES[walk];
    }
    return null;
}

/** Every facade event a class answers to, its own and its bases'. */
export function eventNames(cls: string): string[] {
    const names: string[] = [];
    let walk: string | undefined = cls;
    while (walk) {
        for (const name of Object.keys(EVENTS[walk] ?? {})) {
            if (names.indexOf(name) < 0) {
                names.push(name);
            }
        }
        walk = BASES[walk];
    }
    return names;
}

/*
 * Both enum directions, built on first use out of the one `NAME=value` table.
 *
 * Lazily, because an app that never reads or writes an enum property pays nothing, and because
 * parsing 99 constants once is cheaper to ship than two literal maps of them.
 */
interface EnumTables {
    byName: { [name: string]: number };
    byValue: { [enumType: string]: { [value: string]: string } };
}

let enumTables: EnumTables | undefined;

function tables(): EnumTables {
    if (!enumTables) {
        const built: EnumTables = { byName: {}, byValue: {} };
        for (const enumType of Object.keys(ENUMS)) {
            const reverse: { [value: string]: string } = (built.byValue[enumType] = {});
            for (const member of ENUMS[enumType].split(',')) {
                const cut = member.lastIndexOf('=');
                const name = member.substring(0, cut);
                const value = member.substring(cut + 1);
                built.byName[name] = Number(value);
                reverse[value] = name;
            }
        }
        enumTables = built;
    }
    return enumTables;
}

/** The int the C++ stores for an enum constant, or undefined when the name is not one. */
export function enumValue(name: string): number | undefined {
    return tables().byName[name];
}

/** The constant name for a value, so a read comes back as the string the typings promise. */
export function enumName(enumType: string | undefined, value: number): string | number {
    const table = enumType ? tables().byValue[enumType] : undefined;
    return table && table[value] !== undefined ? table[value] : value;
}

/** The class `create` registers for a kind and spec type. */
export function classOfSpec(kind: string, type: string): string | null {
    return SPEC_CLASS[kind]?.[type] ?? null;
}

/** The other direction: which kind builds a class, for a spec written into an OBJECT property. */
export function specKindOf(cppClass: string): string | null {
    return KIND_OF_CLASS[cppClass] ?? null;
}

let known: { [short: string]: true } | undefined;

function knownClasses() {
    if (!known) {
        known = {};
        for (const name of CLASS_NAMES.split(',')) {
            known[name] = true;
        }
    }
    return known;
}

/** `VectorTileLayer` (the Java/ObjC leaf) to `massif::VectorTileLayer`. */
export function classOfShortName(short: string | null): string | null {
    return short && knownClasses()[short] ? `massif::${short}` : null;
}

/** Whether a class is one the tables know, so a caller-supplied name can be checked. */
export function isKnownClass(cls: string): boolean {
    return !!knownClasses()[cls.replace('massif::', '')];
}

/**
 * Whether `cls` IS a `base`, walking the chain the same way property lookups do.
 *
 * What `instanceof` was: the facade addresses classes by name, so "is this layer a raster layer"
 * is a question about the table, not about a JavaScript prototype.
 */
export function isSubclassOf(cls: string, base: string): boolean {
    let walk: string | undefined = cls;
    while (walk) {
        if (walk === base) {
            return true;
        }
        walk = BASES[walk];
    }
    return false;
}
