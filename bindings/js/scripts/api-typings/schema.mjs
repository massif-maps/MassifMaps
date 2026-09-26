import { existsSync, readFileSync } from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
export const OUT_DIR = path.join(ROOT, 'src');

/**
 * Where `docs/api/massif-api.json` is, in order of preference.
 *
 * The plugin is checked out as `integrations/nativescript` inside the SDK, so the relative
 * path is the normal case; MASSIF_SDK_HOME is what the existing `scripts/typings` uses and
 * covers a standalone checkout.
 */
export function findSchema(explicit) {
    const candidates = [
        explicit,
        process.env.MASSIF_SDK_HOME && path.join(process.env.MASSIF_SDK_HOME, 'docs', 'api', 'massif-api.json'),
        path.resolve(ROOT, '..', '..', 'docs', 'api', 'massif-api.json')
    ].filter(Boolean);
    const found = candidates.find((c) => existsSync(c));
    if (!found) {
        throw new Error(`massif-api.json not found. Pass --schema <path> or set MASSIF_SDK_HOME.\ntried:\n  ${candidates.join('\n  ')}`);
    }
    return found;
}

export function loadSchema(explicit) {
    const file = findSchema(explicit);
    return { file, schema: JSON.parse(readFileSync(file, 'utf8')) };
}

/** How far a dotted path may walk into object properties. The SDK generator uses the same cap. */
export const MAX_DEPTH = 3;

export const SCALARS = {
    BOOL: 'boolean',
    INT: 'number',
    FLOAT: 'number',
    COLOR: 'number',
    STRING: 'string',
    VARIANT: 'Json'
};

/**
 * The by-value structs StructCodec moves as JSON. Anything not here is opaque - it has no
 * accessor either, so a path reaching one cannot be read in any language.
 */
export const STRUCTS = {
    'massif::MapPos': 'Position',
    'massif::MapVec': 'Position',
    'massif::ScreenPos': '[number, number]',
    'massif::MapRange': '[number, number]',
    'massif::MapBounds': 'Bounds',
    'massif::MapTile': 'Tile',
    'massif::ClickInfo': 'ClickInfo',
    'std::vector<std::string>': 'string[]',
    'std::map<std::string, std::string>': 'Record<string, string>',
    'std::map<std::string, massif::Variant>': 'Record<string, Json>'
};

/** `massif::ClickType::ClickType` -> `ClickType`; anything else just loses the namespaces. */
export function enumTypeName(cppEnum) {
    const parts = cppEnum.replace('massif::', '').split('::');
    return parts.length === 2 && parts[0] === parts[1] ? parts[0] : parts.join('');
}

/** A class' own properties plus every base's, nearest declaration first. */
export function ownProperties(cppClass, classes) {
    const out = [];
    const seen = new Set();
    const aliases = new Map();
    let walk = cppClass;
    while (walk) {
        const entry = classes[walk];
        if (!entry) break;
        for (const prop of entry.properties) {
            if (!seen.has(prop.name)) {
                seen.add(prop.name);
                out.push(prop);
            }
        }
        // A second spelling of one segment - `fog` for `fogOptions` - so it completes exactly
        // like the property it stands for and a dotted path carries on through it.
        for (const [alias, path] of Object.entries(entry.aliases ?? {})) {
            if (!aliases.has(alias)) aliases.set(alias, path);
        }
        walk = entry.base;
    }
    const byName = new Map(out.map((prop) => [prop.name, prop]));
    for (const [alias, path] of [...aliases].sort()) {
        const target = byName.get(path);
        if (target && !seen.has(alias)) out.push({ ...target, name: alias });
    }
    return out;
}

/** The same for methods and events, where a subclass registration wins over its base's. */
export function inherited(cppClass, classes, key) {
    const out = new Map();
    let walk = cppClass;
    while (walk) {
        const entry = classes[walk];
        if (!entry) break;
        for (const item of entry[key]) {
            if (!out.has(item.name)) out.set(item.name, item);
        }
        walk = entry.base;
    }
    return out;
}

/** Every legal dotted path from a class, with the property row at the end of it. */
export function closure(cppClass, classes, depth = MAX_DEPTH, prefix = '', stack = []) {
    if (depth === 0 || stack.includes(cppClass)) return {};
    const paths = {};
    for (const prop of ownProperties(cppClass, classes)) {
        const p = prefix + prop.name;
        paths[p] = prop;
        if (prop.indexed) {
            // A bag: the property itself takes every key at once, and `p.<anything>` is one
            // entry. The keys are the app's, so that half is an index signature - see emitTypes.
            const entry = prop.indexed === 'string' ? 'string' : 'Json';
            paths[p] = { ...prop, readOnly: false };
            paths[`${p}.*`] = { ...prop, readOnly: false, indexed: undefined, bagType: entry };
        }
        if (prop.type === 'OBJECT') {
            Object.assign(paths, closure(prop.objectClass, classes, depth - 1, p + '.', [...stack, cppClass]));
        }
    }
    return paths;
}

export function valueType(prop, enums) {
    // A bag takes an object of entries, whether it is written as a property or as a spec key.
    if (prop.indexed) return `Record<string, ${prop.indexed === 'string' ? 'string' : 'Json'}>`;
    if (prop.bagType) return prop.bagType;
    if (SCALARS[prop.type]) return SCALARS[prop.type];
    if (prop.type === 'ENUM') {
        // `| number` because the SDK takes either: the value IS an int, and a constant name is
        // resolved to one. Names first so an editor completes those rather than offering `number`.
        const values = enums[prop.enum || ''];
        return values ? `${values.map((v) => `'${v.name}'`).join(' | ')} | number` : 'number';
    }
    // A bare Handle, not Handle<'X'>: the brand is invariant, so branding it here would reject
    // the SUBCLASSES the SDK accepts (a PolygonGeometry where a Geometry is declared). The class
    // is still known - ObjectPaths carries it, which is what `group` and ClassAtPath read.
    if (prop.type === 'OBJECT') return 'Handle';
    if (prop.type === 'STRUCT') return STRUCTS[prop.cppType] ?? 'Json';
    return 'unknown';
}

export const ARG_TYPES = {
    bool: 'boolean',
    int: 'number',
    float: 'number',
    string: 'string',
    tile: 'Tile',
    pos: 'Position',
    positions: 'Position[]',
    handle: 'Handle',
    json: 'Json'
};

export const RETURN_TYPES = {
    void: 'void',
    bool: 'boolean',
    int: 'number',
    float: 'number',
    string: 'string',
    json: 'Json',
    doubles: 'number[]'
};

export function methodArgTuple(method) {
    if (!method.args.length) return '[]';
    return `[${method.args.map((a) => `${a.name}: ${ARG_TYPES[a.type] ?? 'unknown'}`).join(', ')}]`;
}

export function methodResult(method) {
    if (method.returns === 'object') {
        return method.returnClass ? `Handle<'${method.returnClass}'>` : 'Handle';
    }
    return RETURN_TYPES[method.returns] ?? 'unknown';
}

/** `layer` -> `Layer`, `elementstyle` -> `Elementstyle`, matching the SDK generator's names. */
export function kindPrefix(kind) {
    return kind.charAt(0).toUpperCase() + kind.slice(1).replace(/-/g, '');
}

export function specInterfaceName(kind, type) {
    return `${kindPrefix(kind)}Spec_${type.replace(/-/g, '_')}`;
}

export const BANNER = [
    '// GENERATED FILE - do not edit by hand.',
    "// Regenerate with `npm run typings.api` (scripts/api-typings), which reads the SDK's",
    '// docs/api/massif-api.json - the same schema the C++ property table is built from.',
    ''
].join('\n');
