import { STRUCTS, enumTypeName, kindPrefix } from './schema.mjs';

/**
 * What the generated C++ factory reads out of a spec, per kind and type.
 *
 * `schema.kinds` records one entry per constructor overload as
 * `[index, args, requiredKeys, allKeys]`, where an arg is `['VALUE', key, readerExpression, null]`
 * or `['OBJECT', key, childKind, cppClass]`. That is the only place the constructor parameters
 * appear - `specs[].aliases` only carries the ones whose name had to be changed, which is why a
 * generator reading aliases alone misses `LocalVectorDataSource`'s `projection` entirely.
 *
 * A key is required when EVERY overload requires it: the longest constructor a spec fully
 * satisfies wins, so a key one overload can do without is optional.
 */
export function constructorKeys(schema, enums) {
    const index = {};
    for (const [kind, entries] of Object.entries(schema.kinds)) {
        for (const [type, , overloads] of entries) {
            const keys = {};
            const requiredEverywhere = overloads.length ? overloads.map(([, , required]) => new Set(required)).reduce((a, b) => new Set([...a].filter((k) => b.has(k)))) : new Set();
            for (const [, args] of overloads) {
                for (const arg of args) {
                    const [shape, key] = arg;
                    const type_ = shape === 'OBJECT' ? childType(arg[2], schema) : valueTypeOfReader(arg[2], enums);
                    // A key that appears in two overloads with different shapes keeps the union.
                    keys[key] = keys[key] && keys[key].type !== type_ ? { ...keys[key], type: `${keys[key].type} | ${type_}` } : { type: type_, required: requiredEverywhere.has(key) };
                    keys[key].required = requiredEverywhere.has(key);
                }
            }
            index[`${kind}/${type}`] = keys;
        }
    }
    return index;
}

const SPEC_KINDS_WITHOUT_SPECS = new Set(['data', 'projection', 'styleset-none']);

/**
 * What a nested object key accepts, wherever `childOf` reads one.
 *
 * The three shapes are that function's three branches: a STRING is a registry id, a NUMBER is a
 * live handle - which is how an app shares an object it already holds, the base map's source
 * under an overlay - and an object is a spec built on the spot.
 */
export function childType(childKind, schema) {
    const known = new Set(schema.specs.map((s) => s.kind));
    if (known.has(childKind) && !SPEC_KINDS_WITHOUT_SPECS.has(childKind)) {
        return `Handle | string | ${kindPrefix(childKind)}Spec`;
    }
    // `data` and `projection` have hand-written factories, so no spec interface is generated
    // for them - an id or a free-form object is all that can be said.
    return 'Handle | string | Record<string, Json>';
}

/**
 * The same, for an OBJECT key that is a writable PROPERTY rather than a constructor argument.
 *
 * `applySpecProperties` sends those through `applyObjectSpecProperty`, which is `childOf` again -
 * so a polygon style's `lineStyle` takes an inline `{ type: 'line', … }` exactly like a
 * constructor argument does, and typing it as a bare `Handle` refused the shape the SDK reads.
 *
 * A class no kind builds keeps the bare `Handle` it had: `applyObjectSpecProperty` bails with
 * "nothing builds a X" before it gets to `childOf`, so neither a spec nor a handle reaches the
 * setter and there is nothing better to say here.
 */
export function objectPropertyType(objectClass, schema) {
    const childKind = schema.kindOfClass?.[objectClass];
    return childKind ? childType(childKind, schema) : 'Handle';
}

/** Reads the argument type back out of the C++ reader expression the generator emitted. */
function valueTypeOfReader(expr, enums) {
    const struct = /structAt<([^>]+)>/.exec(expr);
    if (struct) return STRUCTS[struct[1]] ?? 'Json';
    const enumCast = /static_cast<(massif::\w+::\w+)>/.exec(expr);
    if (enumCast) {
        const values = enums[enumCast[1]];
        if (values) return values.map((v) => `'${v.name}'`).join(' | ');
        return 'number';
    }
    if (/variantAt/.test(expr)) return 'Json';
    if (/massif::Color/.test(expr)) return 'number';
    if (/stringAt/.test(expr)) return 'string';
    if (/boolAt/.test(expr)) return 'boolean';
    if (/intAt|floatAt|doubleAt|numberAt/.test(expr)) return 'number';
    return 'Json';
}

/**
 * The class a spec's handle actually carries.
 *
 * An `elementstyle` spec names the BUILDER, because the builder's setters are the schema; what
 * `create` registers is what `buildStyle()` returned. Everything else registers what it names.
 */
export function registeredClass(cppClass, classes) {
    if (cppClass.endsWith('Builder')) {
        const built = cppClass.slice(0, -'Builder'.length);
        if (classes[built]) return built;
    }
    return cppClass;
}

export { enumTypeName };
