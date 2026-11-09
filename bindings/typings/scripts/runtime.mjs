import { BANNER, enumTypeName } from './schema.mjs';
import { registeredClass } from './specs.mjs';

/**
 * One character per property type, so the runtime knows which native verb to call.
 *
 * `p` is a MapPos/MapBounds the generator flagged as a coordinate: it reads through getPos and
 * can be converted to another projection, which is the whole reason the flag exists.
 */
const CODES = {
    BOOL: 'b',
    INT: 'i',
    FLOAT: 'f',
    COLOR: 'c',
    STRING: 's',
    VARIANT: 'v',
    ENUM: 'e',
    OBJECT: 'o',
    STRUCT: 't'
};

function encode(prop) {
    // A bag first: the rest of the path is a KEY, and the argument is what ONE entry holds, so
    // `params.water_color` picks setString and a Variant bag picks the JSON verb.
    if (prop.indexed) return `${prop.name},g,${prop.indexed === 'string' ? 's' : 'v'}`;
    if (prop.type === 'ENUM') return `${prop.name},e,${enumTypeName(prop.enum || '')}`;
    if (prop.type === 'OBJECT') return `${prop.name},o,${prop.objectClass}`;
    if (prop.type === 'STRUCT') return `${prop.name},${prop.position ? 'p' : 't'}`;
    return `${prop.name},${CODES[prop.type] ?? 't'}`;
}

const PRELUDE = `${BANNER}
/* eslint-disable */

/**
 * @internal
 * @module
 */

/**
 * The runtime half of the surface API's typings.
 *
 * Only what marshalling needs: each class' OWN properties plus its base, so a dotted path is
 * resolved the way the C++ resolves it - walk the segment, follow the object it names, walk the
 * next. Flattening the closure per class instead would triple this table for nothing.
 *
 * A property is encoded as \`name,code\` with the code from the table below, plus one argument
 * for the two that need it:
 *
 *   b bool   i int    f float   c color (ARGB int)   s string   v variant (JSON)
 *   t struct (JSON)   p position struct (JSON, convertible between projections)
 *   e enum, followed by the enum's name       o object, followed by the declared class
 *   g bag, followed by the code ONE entry carries - the rest of the path is its key
 */
`;

export function emitRuntime(schema) {
    const { classes, enums } = schema;
    const named = Object.keys(classes).sort();
    const out = [PRELUDE, '\n'];

    out.push("/** Per class, its own properties (not its bases'), encoded as described above. */\n");
    out.push('export const PROPS: { [cls: string]: string } = {\n');
    let rows = 0;
    for (const cppClass of named) {
        const props = classes[cppClass].properties;
        if (!props.length) continue;
        rows += props.length;
        out.push(`    '${cppClass}': '${props.map(encode).join(';')}',\n`);
    }
    out.push('};\n\n');

    // A second spelling of one segment, resolved the way the C++ resolves it - so the runtime
    // picks the right verb for `fog.rangeStart` rather than falling back to guessing by value.
    out.push('/** Per class, its aliases as `alias=path`. */\n');
    out.push('export const ALIASES: { [cls: string]: string } = {\n');
    for (const cppClass of named) {
        const aliases = Object.entries(classes[cppClass].aliases ?? {});
        if (!aliases.length) continue;
        out.push(`    '${cppClass}': '${aliases.map(([alias, path]) => `${alias}=${path}`).join(';')}',\n`);
    }
    out.push('};\n\n');

    out.push("/** Each class' base, so a lookup walks the chain the C++ property table walks. */\n");
    out.push('export const BASES: { [cls: string]: string } = {\n');
    for (const cppClass of named) {
        if (classes[cppClass].base) out.push(`    '${cppClass}': '${classes[cppClass].base}',\n`);
    }
    out.push('};\n\n');

    // One table rather than two objects: both directions are needed and both are derivable from
    // this, and two literal maps over 99 constants cost twice what one string does.
    out.push('/** Per enum, its constants as `NAME=value` - see enumValue and enumName. */\n');
    out.push('export const ENUMS: { [enumName: string]: string } = {\n');
    for (const cppEnum of Object.keys(enums).sort()) {
        const members = enums[cppEnum].map((v) => `${v.name}=${v.value}`).join(',');
        out.push(`    ${enumTypeName(cppEnum)}: '${members}',\n`);
    }
    out.push('};\n\n');

    out.push('/** The class `create` registers, per kind and spec type. */\n');
    out.push('export const SPEC_CLASS: { [kind: string]: { [type: string]: string } } = {\n');
    const byKind = {};
    for (const spec of schema.specs) (byKind[spec.kind] ??= []).push(spec);
    for (const kind of Object.keys(byKind).sort()) {
        const pairs = [...byKind[kind]].sort((a, b) => a.type.localeCompare(b.type)).map((s) => `'${s.type}': '${registeredClass(s.cppClass, classes)}'`);
        out.push(`    '${kind}': { ${pairs.join(', ')} },\n`);
    }
    out.push('};\n\n');

    // What lets `set` accept an inline spec for an OBJECT property: given the property's class,
    // which kind builds it. Includes the hand-written factories (massif::Bitmap -> 'bitmap'),
    // which appear in no !spec and so in no SPEC_CLASS entry.
    out.push('/** The kind that builds each class, base chain included. */\n');
    out.push('export const KIND_OF_CLASS: { [cls: string]: string } = {\n');
    for (const [cls, kind] of Object.entries(schema.kindOfClass ?? {})) {
        out.push(`    '${cls}': '${kind}',\n`);
    }
    out.push('};\n\n');

    // A call always comes back as a handle, so the runtime needs the shape to read out of it.
    out.push('/**\n');
    out.push(' * Per class, its own methods: `name,resultCode` and, for an object result, its class.\n');
    out.push(' *\n');
    out.push(' *   v void   b bool   i int   f float   s string   j json   d doubles   o object\n');
    out.push(' */\n');
    out.push('export const METHODS: { [cls: string]: string } = {\n');
    const RESULT = { void: 'v', bool: 'b', int: 'i', float: 'f', string: 's', json: 'j', doubles: 'd' };
    for (const cppClass of named) {
        const methods = classes[cppClass].methods;
        if (!methods.length) continue;
        const encoded = methods.map((m) => (m.returns === 'object' ? `${m.name},o,${m.returnClass ?? ''}` : `${m.name},${RESULT[m.returns] ?? 'j'}`));
        out.push(`    '${cppClass}': '${encoded.join(';')}',\n`);
    }
    out.push('};\n\n');

    out.push('/**\n');
    out.push(" * Per class, its own events: the payload's class ('' when it carries none), and\n");
    out.push(' * whether the SDK asks the listener if the event was consumed.\n');
    out.push(' */\n');
    out.push('export const EVENTS: { [cls: string]: { [event: string]: { payload: string; consume: boolean } } } = {\n');
    for (const cppClass of named) {
        const events = classes[cppClass].events;
        if (!events.length) continue;
        const pairs = events.map((e) => `'${e.name}': { payload: '${e.payload ?? ''}', consume: ${!!e.consumable} }`);
        out.push(`    '${cppClass}': { ${pairs.join(', ')} },\n`);
    }
    out.push('};\n\n');

    // The native class names, so an object built with the OLD api can be adopted without the
    // caller naming its C++ class: Java gives com.massifmaps.layers.VectorTileLayer and
    // Objective-C gives MSFVectorTileLayer, and both map back mechanically. A list, not a map:
    // the qualified name is always `massif::` plus the short one, so half a map would be filler.
    out.push('/** Every class the tables know, by its Java/ObjC leaf name. */\n');
    out.push(`export const CLASS_NAMES = '${named.map((c) => c.replace('massif::', '')).join(',')}';\n`);

    return { text: out.join(''), rows };
}
