import { BANNER, closure, enumTypeName, inherited, kindPrefix, methodArgTuple, methodResult, ownProperties, specInterfaceName, valueType } from './schema.mjs';
import { constructorKeys, objectPropertyType, registeredClass } from './specs.mjs';
import { FACTORY_SPECS } from './factories.mjs';

const PRELUDE = `${BANNER}
/* eslint-disable @typescript-eslint/no-unused-vars */

/**
 * Types for the MassifMaps surface API.
 *
 * Nothing here exists at runtime: the file compiles to an empty module. Every type is derived
 * from the SDK's own property table, so a path that completes here is a path the C++ resolves,
 * and a value the compiler rejects is one the SDK would have dropped with a warning.
 */

export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

/** \`[lon, lat]\` or \`[lon, lat, altitude]\`, in the object's own projection unless one is named. */
export type Position = [number, number] | [number, number, number];
/** A \`[min, max]\` pair of positions. */
export type Bounds = [Position, Position];
/** \`[x, y, zoom]\`. */
export type Tile = [number, number, number];
export interface ClickInfo {
    clickType: number;
}

/**
 * A registered object, as the C++ side numbers it.
 *
 * The phantom member is erased at runtime and exists so a source handle cannot be passed where a
 * layer handle belongs. It is INVARIANT in the class - a \`Handle<PolygonGeometry>\` is not a
 * \`Handle<Geometry>\` - which is why an OBJECT property's value type is a bare \`Handle\`: the
 * SDK downcasts a written handle and refuses the wrong class at runtime, so a brand there would
 * only reject the subclasses it actually accepts.
 *
 * The default is \`any\`, so a bare \`Handle\` means "some object" rather than "every class at
 * once", which nothing would satisfy.
 */
export type Handle<C extends ClassName = any> = number & { readonly __massif: C };

/** A well-known projection name, or any name registered with the SDK. */
export type ProjectionName = 'EPSG:4326' | 'EPSG:3857' | (string & {});
`;

const TAIL = `
/**
 * Whether \`C\` says nothing about the class.
 *
 * True for \`any\` - the default, and so what a bare \`MassifLayer\` or \`MassifObject\` carries -
 * and for the whole \`ClassName\` union, which is what \`ClassAtPath\` falls back to when the table
 * does not record the class at the end of an object path. Both mean the same thing here: the class
 * is not known at compile time, the C++ resolves against the runtime one, and a table lookup can
 * only produce a wrong answer.
 *
 * The usual \`0 extends 1 & C\` probe does NOT work: \`C\` is constrained to \`ClassName\`, and the
 * intersection resolves against that constraint rather than staying deferred.
 */
type Unnarrowed<C extends ClassName> = ClassName extends C ? true : false;

export type Path<C extends ClassName> = keyof PropertyTypes[C] & string;
/**
 * The type at the end of a path.
 *
 * Constrained to \`Path | ValuePath\` rather than to either alone: \`set\` writes object paths
 * too, and \`get\` reads paths that continue into free-form data, which is the template arm.
 */
export type ValueAt<C extends ClassName, P extends Path<C> | ValuePath<C>> = P extends Path<C> ? PropertyTypes[C][P] : Json;

/**
 * A spec written inline, where an OBJECT property is expected.
 *
 * \`setObject\` carries only a handle, so a spec used to collapse to NULL_HANDLE and CLEAR the
 * property while reporting success - which is what blanked \`backgroundBitmap\`. \`set\` builds it
 * now, so the type has to allow it.
 */
export type SpecValue = { type: string } & { [key: string]: any };

/**
 * The type \`set\` ACCEPTS at a path: what \`get\` returns, plus an inline spec wherever the
 * value is an object handle.
 */
export type WriteAt<C extends ClassName, P extends Path<C> | ValuePath<C>> =
    ValueAt<C, P> extends Handle ? Handle | SpecValue : ValueAt<C, P>;

/*
 * The paths that are NOT read-only.
 *
 * \`readonly\` on an interface member only stops direct assignment - it does nothing about
 * set(path, value), where the path is a string. This is the standard writable-keys probe: two
 * mapped types differing only in \`readonly\` are assignable to each other exactly when the key
 * is writable.
 */
type IfWritable<T, K extends keyof T, Yes, No> = (<G>() => G extends { [Q in K]: T[K] } ? 1 : 2) extends <G>() => G extends { readonly [Q in K]: T[K] } ? 1 : 2 ? No : Yes;

export type WritablePath<C extends ClassName> = {
    [K in keyof PropertyTypes[C]]-?: IfWritable<PropertyTypes[C], K, K, never>;
}[keyof PropertyTypes[C]] &
    string;

/** The paths the SDK flags as coordinates, so \`getPos\` can convert them to another projection. */
export type PositionPath<C extends ClassName> = keyof PositionPaths[C] & string;

/**
 * The coordinate at a position path: a \`MapPos\` reads as a \`Position\`, a \`MapBounds\` as a
 * \`Bounds\`. \`PositionPaths\` only records THAT a path is a coordinate; which of the two it is
 * comes from the property table, so a caller does not have to narrow \`Position | Bounds\` back
 * down by hand at every click handler.
 */
export type PositionAt<C extends ClassName, P extends PositionPath<C>> = Unnarrowed<C> extends true
    ? Position | Bounds
    : P extends Path<C>
      ? Extract<PropertyTypes[C][P], Position | Bounds>
      : Position | Bounds;

/** The paths that point at another object. \`group\` scopes onto one; \`get\` cannot read one. */
export type ObjectPath<C extends ClassName> = keyof ObjectPaths[C] & string;

/** The paths a dotted path may keep walking past - a Variant, or a struct's own JSON. */
export type VariantPath<C extends ClassName> = keyof VariantPaths[C] & string;

/**
 * The paths \`get\` can read.
 *
 * An object property is excluded on purpose: the facade has no getObject, and a handle to an
 * intermediate is not what a caller wants anyway - \`group('fogOptions')\` is.
 *
 * The template arm is free-form data: the C++ keeps walking inside a Variant, so
 * \`feature.properties.name\` resolves even though no table can know the leaf. Its type comes
 * back as \`Json\`, which is what it honestly is.
 *
 * An UNNARROWED object - \`MassifLayer\`, which is what \`layers().get(i)\` and \`source()\` hand
 * back - takes any path, the way \`Path\` and \`WritablePath\` already do: nothing is known about the
 * class, so nothing can be said about its paths, and the C++ resolves the path either way. Without
 * the guard, \`Path<any>\` and \`ObjectPath<any>\` were both \`string\`, their \`Exclude\` was
 * \`never\`, and only the dotted arm survived - so \`get('maxZoom')\` was an error on every object
 * whose class had not been named.
 */
export type ValuePath<C extends ClassName> = Unnarrowed<C> extends true ? string : Exclude<Path<C>, ObjectPath<C>> | \`\${VariantPath<C>}.\${string}\`;

/** The class an object property points at, so a scope onto it stays typed all the way down. */
export type ClassAtPath<C extends ClassName, P extends ObjectPath<C>> = ObjectPaths[C][P] extends ClassName ? ObjectPaths[C][P] : ClassName;

export type SpecType<K extends Kind> = keyof SpecClass[K] & string;

/**
 * The spec of one type, as a function parameter.
 *
 * Intersected rather than \`Extract\`ed on purpose: TypeScript loses object-literal freshness
 * through a naked type parameter, so \`spec: S\` with \`S extends SpecOf[K]\` accepts a misspelt
 * key silently. This shape keeps the excess-property check AND still infers \`T\` from \`type\`.
 */
export type SpecArg<K extends Kind, T extends SpecType<K>> = SpecOf[K] & { type: T };

/** The concrete class a \`{ type: T }\` spec of kind \`K\` builds, so its properties complete. */
export type ClassOfSpec<K extends Kind, T extends SpecType<K>> = SpecClass[K][T] extends ClassName ? SpecClass[K][T] : ClassName;

export type MethodName<C extends ClassName> = keyof MethodTypes[C] & string;
/**
 * A method's parameters, as a tuple.
 *
 * \`unknown[]\` for an unnarrowed object, for the same reason \`ValuePath\` takes any path there -
 * and because this one is spread as a REST parameter: resolving to \`any\` is not a rest type at
 * all, so \`call('clearTileCaches', true)\` reported its argument as \`never\` rather than accepting
 * anything.
 */
export type MethodArgs<C extends ClassName, M extends MethodName<C>> = Unnarrowed<C> extends true ? unknown[] : MethodTypes[C][M] extends { args: infer A } ? (A extends unknown[] ? A : never) : never;
export type MethodResult<C extends ClassName, M extends MethodName<C>> = MethodTypes[C][M] extends { result: infer R } ? R : never;

/** The class of an object result, or never for a scalar one. */
export type MethodResultClass<C extends ClassName, M extends MethodName<C>> = MethodTypes[C][M] extends { resultClass: infer R } ? (R extends ClassName ? R : never) : never;

export type EventName<C extends ClassName> = keyof EventTypes[C] & string;

/** The class of an event's payload, so a handler's reads complete against the right table. */
export type PayloadClass<C extends ClassName, E extends EventName<C>> = EventTypes[C][E] extends ClassName ? EventTypes[C][E] : never;
`;

export function emitTypes(schema) {
    const { classes, enums } = schema;
    const named = Object.keys(classes).sort();
    const out = [PRELUDE, '\n'];

    out.push('export type ClassName =\n');
    for (const cppClass of named) out.push(`    | '${cppClass}'\n`);
    out.push('    ;\n\n');

    // --- enums ---------------------------------------------------------------------------------
    out.push('// --- enums ---------------------------------------------------------------\n');
    out.push('//\n// A string union of the constant names. The plugin translates a name to the int\n');
    out.push('// the C++ stores, in both directions, so an app never handles the number.\n\n');
    for (const cppEnum of Object.keys(enums).sort()) {
        out.push(`export type ${enumTypeName(cppEnum)} =\n`);
        for (const value of enums[cppEnum]) {
            if (value.doc) out.push(`    /** ${value.doc} */\n`);
            out.push(`    | '${value.name}'\n`);
        }
        out.push('    ;\n\n');
    }

    // --- properties ----------------------------------------------------------------------------
    out.push('// --- properties ----------------------------------------------------------\n');
    out.push('//\n// One entry per class: every path reachable from it, and the type at the end.\n\n');
    out.push('/** @internal A lookup table, not documentation - see Path and ValueAt. */\n');
    out.push('export interface PropertyTypes {\n');
    let total = 0;
    for (const cppClass of named) {
        const paths = closure(cppClass, classes);
        total += Object.keys(paths).length;
        out.push(`    '${cppClass}': {\n`);
        for (const p of Object.keys(paths).sort()) {
            const prop = paths[p];
            if (prop.doc) out.push(`        /** ${prop.readOnly ? '(read-only) ' : ''}${prop.doc} */\n`);
            if (p.endsWith('.*')) {
                // A bag's keys are the app's, not the SDK's, so they are a template-literal index
                // signature rather than a list: `params.water_color` completes and typechecks.
                out.push(`        [key: \`${p.slice(0, -1)}\${string}\`]: ${valueType(prop, enums)};\n`);
            } else {
                out.push(`        ${prop.readOnly ? 'readonly ' : ''}'${p}': ${valueType(prop, enums)};\n`);
            }
        }
        out.push('    };\n');
    }
    out.push('}\n\n');

    // Two more tables, so PropertyTypes stays a plain "path -> value" map.
    out.push('/** @internal Per class, the paths flagged as coordinates - see PositionPath. */\n');
    out.push('export interface PositionPaths {\n');
    for (const cppClass of named) {
        const paths = closure(cppClass, classes);
        out.push(`    '${cppClass}': {\n`);
        for (const p of Object.keys(paths).sort()) {
            if (!p.endsWith('.*') && (paths[p].position)) out.push(`        '${p}': true;\n`);
        }
        out.push('    };\n');
    }
    out.push('}\n\n');

    out.push('/** @internal Per class, the paths that point at another object - see ObjectPath. */\n');
    out.push('export interface ObjectPaths {\n');
    for (const cppClass of named) {
        const paths = closure(cppClass, classes);
        out.push(`    '${cppClass}': {\n`);
        for (const p of Object.keys(paths).sort()) {
            // The class it points at, not `true`: ClassAtPath reads it straight out of here.
            if (!p.endsWith('.*') && (paths[p].type === 'OBJECT')) out.push(`        '${p}': '${paths[p].objectClass}';\n`);
        }
        out.push('    };\n');
    }
    out.push('}\n\n');

    // A path does not STOP at free-form data: the C++ keeps walking inside a Variant or a
    // struct's JSON (`feature.properties.name`, `clickInfo.clickType`, `mapTile.2`) and answers
    // with the leaf's natural type. These are the paths it may continue past.
    out.push('/** @internal Per class, the paths a dotted path may continue INTO - see ValuePath. */\n');
    out.push('export interface VariantPaths {\n');
    for (const cppClass of named) {
        const paths = closure(cppClass, classes);
        out.push(`    '${cppClass}': {\n`);
        for (const p of Object.keys(paths).sort()) {
            if (!p.endsWith('.*') && (paths[p].type === 'VARIANT' || paths[p].type === 'STRUCT')) out.push(`        '${p}': true;\n`);
        }
        out.push('    };\n');
    }
    out.push('}\n');

    out.push(TAIL);

    // --- specs ---------------------------------------------------------------------------------
    out.push('\n// --- specs ---------------------------------------------------------------\n');
    out.push("//\n// A spec's keys are its constructor parameters plus any writable property of the\n");
    out.push('// class it builds - the factory consumes what it needs and the rest is applied as\n');
    out.push('// properties, at any nesting depth. That IS the complete set the C++ accepts, so\n');
    out.push('// there is no string index signature: a key not listed is one `create` would drop\n');
    out.push('// with a warning, and it is better said at compile time. Cast if you are writing a\n');
    out.push('// spec against a newer SDK than these typings were generated from.\n');
    out.push('//\n// A key is REQUIRED when every constructor overload requires it - `create` fails\n');
    out.push('// with RESULT_BAD_SPEC when no overload is satisfied.\n\n');

    const byKind = {};
    for (const spec of schema.specs) (byKind[spec.kind] ??= []).push(spec);

    const ctorKeys = constructorKeys(schema, enums);

    for (const kind of Object.keys(byKind).sort()) {
        const names = [];
        const emitted = {};
        for (const spec of [...byKind[kind]].sort((a, b) => a.type.localeCompare(b.type))) {
            const name = specInterfaceName(kind, spec.type);
            names.push(name);
            // One entry per KEY: a constructor argument and a writable property can share a name
            // (a source's `url`, a popup's `description`), and declaring it twice is an error.
            // The property comes first and the constructor entry widens it, because a child may
            // arrive as a spec or as a registry id where the property is only ever a handle.
            const keys = {};
            for (const prop of ownProperties(spec.cppClass, classes)) {
                if (prop.readOnly) continue;
                const key = spec.aliases[prop.name] ?? prop.name;
                // An OBJECT property in a SPEC is not the bare handle it is on a path: the spec
                // path resolves it with childOf, so an id and an inline spec are there too.
                const type = prop.type === 'OBJECT' && !prop.indexed ? objectPropertyType(prop.objectClass, schema) : valueType(prop, enums);
                keys[key] = { type, doc: prop.doc, required: false };
            }
            for (const [key, arg] of Object.entries(ctorKeys[`${kind}/${spec.type}`] ?? {})) {
                keys[key] = keys[key] ? { ...keys[key], type: mergeTypes(keys[key].type, arg.type), required: arg.required } : { type: arg.type, doc: undefined, required: arg.required };
            }
            out.push(`export interface ${name} {\n`);
            out.push(`    type: '${spec.type}';\n`);
            for (const key of Object.keys(keys).sort()) {
                const entry = keys[key];
                if (entry.doc) out.push(`    /** ${entry.doc} */\n`);
                out.push(`    ${/^[A-Za-z_$][\w$]*$/.test(key) ? key : `'${key}'`}${entry.required ? '' : '?'}: ${entry.type};\n`);
            }
            out.push('}\n\n');
            emitted[name] = keys;
        }
        for (const extra of FACTORY_SPECS[kind] ?? []) {
            names.push(extra.name);
            out.push(`/**\n${extra.doc.map((line) => ` * ${line}`.trimEnd()).join('\n')}\n */\n`);
            out.push(`export interface ${extra.name} {\n`);
            for (const key of extra.keys) {
                if (key.doc) out.push(`    /** ${key.doc} */\n`);
                out.push(`    ${key.name}${key.required || key.name === 'type' ? '' : '?'}: ${key.type};\n`);
            }
            // The factory only replaces the CONSTRUCTOR: applySpecProperties still runs over what
            // it did not consume, so the sibling spec's plain properties apply here as well.
            const inherited = emitted[extra.inheritOptional] ?? {};
            for (const key of Object.keys(inherited).sort()) {
                const entry = inherited[key];
                if (entry.required || extra.keys.some((k) => k.name === key)) continue;
                if (entry.doc) out.push(`    /** ${entry.doc} */\n`);
                out.push(`    ${/^[A-Za-z_$][\w$]*$/.test(key) ? key : `'${key}'`}?: ${entry.type};\n`);
            }
            out.push('}\n\n');
        }
        out.push(`export type ${kindPrefix(kind)}Spec = ${names.length ? names.join(' | ') : 'never'};\n\n`);
    }

    out.push('export interface SpecOf {\n');
    for (const kind of Object.keys(byKind).sort()) out.push(`    '${kind}': ${kindPrefix(kind)}Spec;\n`);
    out.push('}\n');
    out.push('export type Kind = keyof SpecOf & string;\n\n');

    out.push('/** @internal The class each spec type builds - see ClassOfSpec. */\n');
    out.push('export interface SpecClass {\n');
    for (const kind of Object.keys(byKind).sort()) {
        out.push(`    '${kind}': {\n`);
        for (const spec of [...byKind[kind]].sort((a, b) => a.type.localeCompare(b.type))) {
            out.push(`        '${spec.type}': '${registeredClass(spec.cppClass, classes)}';\n`);
        }
        // A factory type the schema has no constructor for is still a type `create` accepts, so
        // it belongs here too - SpecType reads this table, not the spec interfaces.
        for (const extra of FACTORY_SPECS[kind] ?? []) {
            if (extra.specType) out.push(`        '${extra.specType}': '${extra.cppClass}';\n`);
        }
        out.push('    };\n');
    }
    out.push('}\n\n');

    // --- methods -------------------------------------------------------------------------------
    out.push('// --- methods -------------------------------------------------------------\n');
    out.push('//\n// Arguments are a labelled tuple, so an editor shows the parameter names.\n\n');
    out.push('/** @internal A lookup table - see MethodName, MethodArgs and MethodResult. */\n');
    out.push('export interface MethodTypes {\n');
    for (const cppClass of named) {
        const methods = inherited(cppClass, classes, 'methods');
        out.push(`    '${cppClass}': {\n`);
        for (const name of [...methods.keys()].sort()) {
            const method = methods.get(name);
            // The result CLASS beside the result type: a Handle's brand carries the base chain
            // rather than a single class, so the class cannot be inferred back out of it.
            const resultClass = method.returns === 'object' && method.returnClass ? `; resultClass: '${method.returnClass}'` : '';
            out.push(`        ${name}: { args: ${methodArgTuple(method)}; result: ${methodResult(method)}${resultClass} };\n`);
        }
        out.push('    };\n');
    }
    out.push('}\n\n');

    // --- events --------------------------------------------------------------------------------
    out.push('// --- events --------------------------------------------------------------\n\n');
    out.push('/** @internal A lookup table - see EventName and PayloadClass. */\n');
    out.push('export interface EventTypes {\n');
    for (const cppClass of named) {
        const events = inherited(cppClass, classes, 'events');
        out.push(`    '${cppClass}': {\n`);
        for (const name of [...events.keys()].sort()) {
            const event = events.get(name);
            // The payload's CLASS, for the same reason - see PayloadClass.
            out.push(`        '${name}': ${event.payload ? `'${event.payload}'` : 'null'};\n`);
        }
        out.push('    };\n');
    }
    out.push('}\n');

    return { text: out.join(''), paths: total };
}

/** Union of two spelt-out types, dropping members the other already covers. */
function mergeTypes(a, b) {
    const parts = [];
    for (const part of [...a.split(' | '), ...b.split(' | ')]) {
        if (!parts.includes(part)) parts.push(part);
    }
    return parts.join(' | ');
}
