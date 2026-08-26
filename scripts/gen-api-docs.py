#!/usr/bin/env python3
"""
Turns massif-api.json into the published surface API reference.

The schema already carries every spec factory, every settable property, every method and every
event of the facade — the same table `set`/`get`/`call` resolve against at runtime. Writing the
reference by hand would mean a second list to keep in step, and it would be wrong on the first
new property. So: one page per KIND (what an app creates), one for the value types a property or
a call result can be, one for the enums.

  python3 scripts/gen-api-docs.py

Reads  docs/api/massif-api.json (itself written by gen-api-tables.py --schema)
Writes docs/api/reference/*.md
"""

import argparse
import json
import os
import re

# One page per kind, in the order an app meets them. A kind missing here still gets a page,
# appended after these.
KIND_ORDER = [
    ('layer', 'Layers', 'What is drawn, in the order the map holds them.'),
    ('source', 'Sources', 'Where a layer\'s tiles come from — network, file, cache or another source.'),
    ('style', 'Styles', 'The vector tile decoder, and the style set it reads.'),
    ('styleset', 'Style sets', 'CartoCSS, inline or as a compiled style project.'),
    ('assets', 'Asset packages', 'The bundle, directory or zip a style project is read from.'),
    ('options', 'Options', 'Fog, sky, light and terrain — each a spec and a property path.'),
    ('element', 'Vector elements', 'Markers, popups, lines and polygons the app puts on the map.'),
    ('elementstyle', 'Element styles', 'How those elements are drawn. One style object, shared.'),
    ('geometry', 'Geometry', 'Points, lines, polygons and GeoJSON.'),
    ('feature', 'Features', 'A geometry with properties.'),
    ('search', 'Search', 'Querying the features of a vector tile layer.'),
    ('routing', 'Routing', 'Valhalla online and offline, route and map-match requests.'),
    ('geocoding', 'Geocoding', 'Forward and reverse geocoding, offline.'),
    ('data', 'Data', 'Raw bytes from a file, an asset or a URL.'),
    ('projection', 'Projections', 'Named, not constructed.'),
    ('bitmap', 'Bitmaps', 'Images handed to and from the SDK.'),
]

# Five factories are genuinely adaptive rather than boilerplate, so they are hand-written in
# SpecFactories.cpp and carry no !spec declaration for the schema to pick up. Listed here so the
# reference is not wrong by omission; keep in step with SpecFactories.cpp.
HAND_WRITTEN = {
    'geometry': [
        ('geojson', 'A GeoJSON reader, not a constructor.',
         [('geojson', 'string or object', 'the document, inline or as a string'),
          ('projection', 'string', 'target projection, by name — optional')]),
    ],
    'routing': [
        ('request', 'A route request.',
         [('points', 'positions', 'the waypoints, in order'),
          ('projection', 'string', 'their projection, by name')]),
        ('match-request', 'A map-matching request.',
         [('points', 'positions', 'the measured points'),
          ('projection', 'string', 'their projection, by name'),
          ('accuracy', 'number', 'metres')]),
    ],
    'geocoding': [
        ('request', 'A forward geocoding request.',
         [('query', 'string', 'what to search for'),
          ('projection', 'string', 'the projection results come back in')]),
        ('reverse-request', 'A reverse geocoding request.',
         [('location', 'position', 'the point to name'),
          ('projection', 'string', 'its projection, by name')]),
    ],
    'projection': [
        ('*any registered name*', 'A registry lookup, not a constructor — `"EPSG:4326"`, '
                                  '`"EPSG:3857"`.', []),
    ],
    'data': [
        ('url', 'Bytes from `file://`, `assets://` or `http(s)://`.',
         [('url', 'string', 'where to read from')]),
    ],
}

TYPE_LABEL = {
    'FLOAT': 'number',
    'INT': 'integer',
    'BOOL': 'boolean',
    'STRING': 'string',
    'COLOR': 'color',
    'ENUM': 'enum',
    'OBJECT': 'object',
    'STRUCT': 'struct',
    'VARIANT': 'variant',
}


def shortName(cppClass):
    return cppClass.split('::')[-1] if cppClass else ''


def anchor(text):
    """A slug for an explicit {#id}, so a link never has to guess the theme's own rules."""
    return re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')


def enumAnchor(name):
    return 'enum-' + anchor(shortName(name.rsplit('::', 1)[0]))


def escape(text):
    """A table cell: pipes break the row, and a newline ends it."""
    return (text or '').replace('|', '\\|').replace('\n', ' ').strip()


def typeCell(entry, enums):
    kind = entry.get('type', '')
    label = TYPE_LABEL.get(kind, kind.lower())
    if kind == 'ENUM' and entry.get('enum') in enums:
        return '[%s](enums.md#%s)' % (label, enumAnchor(entry['enum']))
    if kind in ('OBJECT', 'STRUCT') and entry.get('cppType'):
        return '%s `%s`' % (label, escape(entry['cppType']))
    return label


def chain(schema, cppClass):
    """A class and its bases, nearest first — lookups walk it, so the reference shows it."""
    out = []
    while cppClass and cppClass in schema['classes']:
        out.append(cppClass)
        cppClass = schema['classes'][cppClass]['base']
    return out


def propertyTable(schema, cppClass, lines):
    enums = schema['enums']
    aliasOf = {}
    for owner in chain(schema, cppClass):
        for alias, real in (schema['classes'][owner].get('aliases') or {}).items():
            aliasOf.setdefault(real, alias)

    wroteAny = False
    for index, owner in enumerate(chain(schema, cppClass)):
        properties = sorted(schema['classes'][owner]['properties'], key=lambda p: p['name'])
        if not properties:
            continue
        if index:
            lines.append('')
            lines.append('Inherited from `%s`:' % shortName(owner))
        lines.append('')
        lines.append('| Property | Type | Access | Description |')
        lines.append('|---|---|---|---|')
        for entry in properties:
            name = entry['name']
            spelling = '`%s`' % name
            if name in aliasOf:
                spelling += ' <br/>*or* `%s`' % aliasOf[name]
            flags = []
            if entry.get('readOnly'):
                flags.append('read-only')
            if entry.get('static'):
                flags.append('static')
            lines.append('| %s | %s | %s | %s |' % (
                spelling, typeCell(entry, enums), ', '.join(flags) or 'read/write',
                escape(entry.get('doc'))))
        wroteAny = True
    if not wroteAny:
        lines.append('')
        lines.append('*No settable properties.*')


def specSection(schema, spec, lines):
    cppClass = spec['cppClass']
    lines.append('')
    lines.append('## `"%s"` — %s {#spec-%s}'
                 % (spec['type'], shortName(cppClass), anchor(spec['type'])))
    lines.append('')

    # EVERY overload, not only the longest: the longest one the spec satisfies wins at runtime, so
    # a shorter form is a different set of keys, not a subset of the long one. Documenting only the
    # widest hid `position` on a marker, which is the form every example uses.
    constructors = spec.get('constructors') or []
    if constructors:
        lines.append('```json')
        for constructor in sorted(constructors, key=len, reverse=True):
            example = {'type': spec['type']}
            for argument in constructor:
                example[argument['key']] = '…'
            lines.append(json.dumps(example, ensure_ascii=False).replace('"…"', '…'))
        lines.append('```')

        keys, required = [], None
        for constructor in sorted(constructors, key=len, reverse=True):
            for argument in constructor:
                if argument['key'] not in [k['key'] for k in keys]:
                    keys.append(argument)
            names = set(a['key'] for a in constructor)
            required = names if required is None else (required & names)

        lines.append('')
        lines.append('| Key | Type | Always required | Notes |')
        lines.append('|---|---|---|---|')
        for argument in keys:
            notes = []
            if argument.get('childKind'):
                notes.append('an id of kind `%s`, or an inline spec' % argument['childKind'])
            if argument['key'] in (spec.get('defaults') or {}):
                notes.append('defaults to `%s`' % spec['defaults'][argument['key']])
            lines.append('| `%s` | %s | %s | %s |' % (
                argument['key'], typeCell(argument, schema['enums']),
                'yes' if argument['key'] in required else 'no',
                escape(', '.join(notes))))

    propertyTable(schema, cppClass, lines)
    eventsAndMethods(schema, cppClass, lines)


def eventsAndMethods(schema, cppClass, lines):
    events, methods = [], []
    for owner in chain(schema, cppClass):
        events += schema['classes'][owner].get('events') or []
        methods += schema['classes'][owner].get('methods') or []
    if methods:
        lines.append('')
        lines.append('| Method | Arguments | Returns |')
        lines.append('|---|---|---|')
        for method in sorted(methods, key=lambda m: m['name']):
            args = ', '.join('%s: %s' % (a['name'], a['type']) for a in method['args'])
            lines.append('| `%s` | %s | %s |' % (method['name'], escape(args) or '—',
                                                 method['returns']))
    if events:
        lines.append('')
        lines.append('| Event | Payload | Consumable |')
        lines.append('|---|---|---|')
        for event in sorted(events, key=lambda e: e['name']):
            lines.append('| `%s` | %s | %s |' % (
                event['name'], shortName(event['payload']) or '—',
                'yes' if event.get('consumable') else 'no'))


def kindPage(schema, kind, title, blurb, position):
    specs = sorted([s for s in schema['specs'] if s['kind'] == kind], key=lambda s: s['type'])
    inKind = sorted(c for c, k in schema['kindOfClass'].items() if k == kind)
    withSpec = set(s['cppClass'] for s in specs)

    lines = [
        '---',
        'title: %s' % title,
        'description: "%s"' % blurb,
        'sidebar_position: %d' % position,
        '---',
        '',
        '<!-- Generated by scripts/gen-api-docs.py from docs/api/massif-api.json - do not edit. -->',
        '',
        '# %s' % title,
        '',
        '%s Created with `kind` **`%s`**.' % (blurb, kind),
        '',
    ]
    handWritten = HAND_WRITTEN.get(kind, [])
    if specs or handWritten:
        lines.append('| Type | Class |')
        lines.append('|---|---|')
        for spec in specs:
            lines.append('| [`"%s"`](#spec-%s) | `%s` |' % (
                spec['type'], anchor(spec['type']), shortName(spec['cppClass'])))
        for spec, _, _ in handWritten:
            lines.append('| [`"%s"`](#spec-%s) | *hand-written factory* |'
                         % (spec, anchor(spec)))
    else:
        lines.append('This kind has no spec factory — its objects are reached, not constructed.')

    for spec in specs:
        specSection(schema, spec, lines)

    for spec, blurb, keys in HAND_WRITTEN.get(kind, []):
        lines.append('')
        lines.append('## `"%s"` {#spec-%s}' % (spec, anchor(spec)))
        lines.append('')
        lines.append(blurb)
        if keys:
            lines.append('')
            lines.append('| Key | Type | Notes |')
            lines.append('|---|---|---|')
            for key, kindName, note in keys:
                lines.append('| `%s` | %s | %s |' % (key, kindName, note))

    bases = [c for c in inKind if c not in withSpec]
    if bases:
        lines.append('')
        lines.append('## Base classes')
        lines.append('')
        lines.append('Not constructed directly. Their properties are reachable on every '
                     'object above that derives from them.')
        for cppClass in bases:
            lines.append('')
            lines.append('#### `%s`' % shortName(cppClass))
            propertyTable(schema, cppClass, lines)
            eventsAndMethods(schema, cppClass, lines)

    return '\n'.join(lines) + '\n'


def typesPage(schema, position):
    """Everything with no kind: results, payloads, and the classes a property points at."""
    rest = sorted(c for c in schema['classes'] if c not in schema['kindOfClass'])
    lines = [
        '---',
        'title: Value types',
        'description: "The classes a property, an event payload or a call result can be."',
        'sidebar_position: %d' % position,
        '---',
        '',
        '<!-- Generated by scripts/gen-api-docs.py from docs/api/massif-api.json - do not edit. -->',
        '',
        '# Value types',
        '',
        'These are not created with a spec. They arrive as an event payload, a call result or the '
        'value of an `OBJECT` property, and their properties are readable through the same '
        'dotted paths.',
    ]
    for cppClass in rest:
        entry = schema['classes'][cppClass]
        if not entry['properties'] and not entry.get('methods') and not entry.get('events'):
            continue
        lines.append('')
        lines.append('## `%s`' % shortName(cppClass))
        propertyTable(schema, cppClass, lines)
        eventsAndMethods(schema, cppClass, lines)
    return '\n'.join(lines) + '\n'


def enumsPage(schema, position):
    lines = [
        '---',
        'title: Enums',
        'description: "Every enum the surface API accepts, by name and by number."',
        'sidebar_position: %d' % position,
        '---',
        '',
        '<!-- Generated by scripts/gen-api-docs.py from docs/api/massif-api.json - do not edit. -->',
        '',
        '# Enums',
        '',
        'An enum property takes either the **name** or the number. The name is the portable one — '
        'the numbers are an implementation detail of the C++ enum and may shift when a value is '
        'inserted.',
    ]
    for name in sorted(schema['enums'], key=lambda n: shortName(n)):
        lines.append('')
        lines.append('## %s {#%s}' % (shortName(name.rsplit('::', 1)[0]),
                                      enumAnchor(name)))
        lines.append('')
        lines.append('| Value | # | Description |')
        lines.append('|---|---|---|')
        for value in schema['enums'][name]:
            lines.append('| `%s` | %d | %s |' % (value['name'], value['value'],
                                                 escape(value.get('doc'))))
    return '\n'.join(lines) + '\n'


def indexPage(schema, pages):
    lines = [
        '---',
        'title: API reference',
        'description: "Every spec type, property, method and event of the surface API."',
        'sidebar_position: 1',
        '---',
        '',
        '<!-- Generated by scripts/gen-api-docs.py from docs/api/massif-api.json - do not edit. -->',
        '',
        '# API reference',
        '',
        'Generated from the same table the SDK resolves `set`, `get`, `call` and `create` '
        'against, so it cannot drift from the build. If you are meeting the surface API for the '
        'first time, read [how it works](../index.mdx) first.',
        '',
        '| Kind | Page | Spec types |',
        '|---|---|---|',
    ]
    for kind, title, _, fileName, count in pages:
        lines.append('| `%s` | [%s](%s) | %d |' % (kind, title, fileName, count))
    lines.append('')
    lines.append('Plus [value types](types.md) — what a property, payload or result can be — and '
                 'every [enum](enums.md).')
    lines.append('')
    lines.append('%d classes, %d properties, %d spec types, %d enums.' % (
        len(schema['classes']),
        sum(len(c['properties']) for c in schema['classes'].values()),
        len(schema['specs']), len(schema['enums'])))
    return '\n'.join(lines) + '\n'


def write(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        with open(path) as handle:
            if handle.read() == content:
                return False
    with open(path, 'w') as handle:
        handle.write(content)
    return True


here = os.path.dirname(os.path.abspath(__file__))
parser = argparse.ArgumentParser()
parser.add_argument('--schema', default=os.path.join(here, '../docs/api/massif-api.json'))
parser.add_argument('--out', default=os.path.join(here, '../docs/api/reference'))
args = parser.parse_args()

with open(args.schema) as handle:
    schema = json.load(handle)

known = [k for k, _, _ in KIND_ORDER]
order = KIND_ORDER + [(k, k.title(), '', ) for k in sorted(schema['kinds']) if k not in known]

pages, written = [], 0
for index, (kind, title, blurb) in enumerate(order):
    fileName = kind + '.md'
    count = len([s for s in schema['specs'] if s['kind'] == kind]) + len(HAND_WRITTEN.get(kind, []))
    write(os.path.join(args.out, fileName), kindPage(schema, kind, title, blurb, index + 2))
    pages.append((kind, title, blurb, fileName, count))

write(os.path.join(args.out, 'types.md'), typesPage(schema, len(order) + 2))
write(os.path.join(args.out, 'enums.md'), enumsPage(schema, len(order) + 3))
write(os.path.join(args.out, 'index.md'), indexPage(schema, pages))
write(os.path.join(args.out, '_category_.json'), json.dumps({
    'label': 'API reference',
    'position': 2,
    'link': {'type': 'doc', 'id': 'api/reference/index'},
}, indent=2) + '\n')

print('%d kind pages + types, enums and an index in %s' % (len(pages), os.path.relpath(args.out)))
