#!/usr/bin/env python3
"""Emit the facade API's property table from the Swig attribute macros.

The macros in all/modules/**/*.i already declare every settable property of every wrapped class,
so the facade's `set`/`get` paths are derived from them rather than hand-listed - a new setter
becomes a new path on the next build, and nothing is ever maintained twice.

Two outputs:
  PropertyAccessors.inc  the class headers and one thunk per accessor, at file scope
  PropertyTable.inc      the sorted tables, included inside an anonymous namespace

Modules behind a profile define this build does not set are skipped, the same way swigpp-*.py
skips them - otherwise a lite build would try to compile thunks for classes it does not have.
"""

import argparse
import os
import re
import sys

from build.sdk_build_utils import getDefaultProfileId, getProfile, validProfile

# Macro -> whether the declared value is an object reference rather than a value.
ATTRIBUTE_MACROS = {
    'attribute': False,
    'attributeval': False,
    'attributestring': False,
    'attributestring_polymorphic': True,
    'staticattribute': False,
    'staticattributestring': False,
    'staticattributestring_polymorphic': True,
}

BOOL_TYPES = {'bool'}
INT_TYPES = {'int', 'long', 'long long', 'short', 'char', 'signed char', 'unsigned char',
             'unsigned int', 'unsigned long', 'unsigned short', 'size_t', 'std::size_t'}
# The fixed-width spellings too: PackageInfo.size is a std::uint64_t, and without these it lands in
# STRUCT and silently loses its accessor - the same failure the unqualified enum had.
INT_TYPES |= set('%s%sint%d_t' % (prefix, sign, bits)
                 for prefix in ('', 'std::') for sign in ('', 'u') for bits in (8, 16, 32, 64))
FLOAT_TYPES = {'float', 'double'}

TYPE_NAMES = ['BOOL', 'INT', 'FLOAT', 'COLOR', 'ENUM', 'STRING', 'OBJECT', 'STRUCT', 'VARIANT']

# Types a value accessor can carry today. STRUCT needs JSON marshalling, so it is listed in the
# table but has no thunk yet. OBJECT gets its own accessor instead - see objectClassOf.
ACCESSIBLE_TYPES = {'BOOL', 'INT', 'FLOAT', 'COLOR', 'ENUM', 'STRING'}

# STRUCT properties carry JSON, and only for the types StructCodec knows. The rest - vectors,
# maps, BalloonPopupMargins, ClickInfo - stay accessorless until someone needs them.
CODEC_TYPES = {'massif::MapPos', 'massif::MapVec', 'massif::ScreenPos', 'massif::MapRange',
               'massif::MapBounds', 'massif::MapTile', 'std::vector<std::string>',
               'std::map<std::string, std::string>', 'std::map<std::string, massif::Variant>'}

FLAG_READONLY = 1
FLAG_STATIC = 2
FLAG_POSITION = 4
FLAG_PROJECTION = 8

# The struct types that are coordinates, and so can be converted between projections.
POSITION_TYPES = {'massif::MapPos', 'massif::MapBounds'}

# The class an object property must point at for it to answer "what projection is this in?".
PROJECTION_CLASS = 'massif::Projection'

SUPPORT_DEFINE = re.compile(r'#ifdef\s+(_MASSIF_\w+_SUPPORT)|defined\((_MASSIF_\w+_SUPPORT)\)')


def splitArgs(text):
  """Split a macro argument list on top-level commas, respecting <> and ()."""
  args, depth, current = [], 0, ''
  for ch in text:
    if ch in '<([':
      depth += 1
    elif ch in '>)]':
      depth -= 1
    if ch == ',' and depth == 0:
      args.append(current.strip())
      current = ''
    else:
      current += ch
  if current.strip():
    args.append(current.strip())
  return args


def stripArgMacro(cppType):
  # %arg(...) only exists to hide commas from the Swig preprocessor.
  match = re.match(r'^%arg\s*\((.*)\)$', cppType.strip())
  cppType = match.group(1).strip() if match else cppType.strip()
  # A few .i files spell an enum unqualified (RoutingAction::RoutingAction). Swig resolves it from
  # the %import; here it has to be qualified, or it classifies as a STRUCT and silently loses its
  # accessor - which is what happened to RoutingInstruction.action.
  match = re.match(r'^(\w+)::(\w+)$', cppType)
  if match and match.group(1) == match.group(2):
    cppType = 'massif::' + cppType
  return cppType


def classifyType(cppType, polymorphic):
  if polymorphic:
    return 'OBJECT'
  cppType = stripArgMacro(cppType)
  if cppType in BOOL_TYPES:
    return 'BOOL'
  if cppType in INT_TYPES:
    return 'INT'
  if cppType in FLOAT_TYPES:
    return 'FLOAT'
  if cppType == 'massif::Color':
    return 'COLOR'
  if cppType == 'std::string':
    return 'STRING'
  # Its own type, not a STRUCT: a path can carry on walking inside a Variant.
  if cppType == 'massif::Variant':
    return 'VARIANT'
  if cppType.startswith('std::shared_ptr<'):
    return 'OBJECT'
  # namespace and enum share a name by convention: massif::PanningMode::PanningMode
  match = re.match(r'^massif::(\w+)::(\w+)$', cppType)
  if match and match.group(1) == match.group(2):
    return 'ENUM'
  return 'STRUCT'


def decapitalize(name):
  # java.beans.Introspector rule: an acronym keeps its case, so HTTPHeaders stays HTTPHeaders
  # while RangeStart becomes rangeStart. Predictable is what matters - readable spellings are
  # the alias table's job.
  if len(name) > 1 and name[0].isupper() and name[1].isupper():
    return name
  return name[0].lower() + name[1:] if name else name


SPEC_MACRO = re.compile(r'^\s*!spec\s*\((.*)\)\s*$')


def parseSpec(args):
  """!spec(cppClass, kind, type, alias(key, param), default(param, value))."""
  if len(args) < 3:
    return None
  entry = {'cppClass': args[0], 'kind': args[1], 'type': args[2], 'aliases': {}, 'defaults': {}}
  for extra in args[3:]:
    inner = re.match(r'^(alias|default)\s*\((.*)\)$', extra.strip())
    if not inner:
      print('warning: cannot parse spec option %s' % extra)
      continue
    parts = splitArgs(inner.group(2))
    if len(parts) != 2:
      continue
    if inner.group(1) == 'alias':
      entry['aliases'][parts[1]] = parts[0]                  # parameter -> readable spec key
    else:
      entry['defaults'][parts[0]] = parts[1]
  return entry


def parseModule(sourcePath, defines, pattern):
  """Returns (headers, entries, specs), or (None, None, None) when the module is out of profile."""
  headers, entries, specs, inCode = [], [], [], False
  with open(sourcePath) as f:
    for line in f:
      line = line.rstrip('\n')

      match = SUPPORT_DEFINE.search(line)
      if match:
        define = match.group(1) or match.group(2)
        if define not in defines:
          return None, None, None

      if line.strip() == '%{':
        inCode = True
        continue
      if line.strip() == '%}':
        inCode = False
        continue
      if inCode:
        match = re.match(r'\s*#include\s+"([^"]+)"', line)
        if match and not match.group(1).endswith('Exceptions.h'):
          headers.append(match.group(1))
        continue

      match = SPEC_MACRO.match(line)
      if match:
        entry = parseSpec(splitArgs(match.group(1)))
        if entry:
          entry['headers'] = headers
          specs.append(entry)
        continue

      match = pattern.match(line)
      if not match:
        continue
      macro, args = match.group(1), splitArgs(match.group(2))
      if len(args) < 4:
        print('warning: %s: cannot parse %s(%s)' % (sourcePath, macro, match.group(2)))
        continue
      cppClass, cppType, name, getter = args[0], args[1], args[2], args[3]
      setter = args[4] if len(args) > 4 and args[4] else ''
      flags = 0
      if not setter:
        flags |= FLAG_READONLY
      if macro.startswith('static'):
        flags |= FLAG_STATIC
      if stripArgMacro(cppType) in POSITION_TYPES:
        flags |= FLAG_POSITION
      entry = {
        'cppClass': cppClass,
        'cppType': stripArgMacro(cppType),
        'path': decapitalize(name),
        'type': classifyType(cppType, ATTRIBUTE_MACROS[macro]),
        'flags': flags,
        'getter': getter,
        'setter': setter,
      }
      # Whatever a class calls its projection - baseProjection, projection - this is how a read
      # finds out what coordinate system its positions are in, without the facade naming classes.
      if objectClassOf(entry) == PROJECTION_CLASS:
        entry['flags'] |= FLAG_PROJECTION
      entries.append(entry)
  # The spec entries share this module's header list, which is where its constructors are.
  for entry in specs:
    entry['headers'] = headers
  return headers, entries, specs



def parseParams(text):
  """[(type, name), ...] from a parameter list, with const and & stripped from the type."""
  params = []
  for arg in splitArgs(text):
    arg = re.sub(r'\s*=\s*.+$', '', arg).strip()             # a C++ default argument
    if not arg or arg == 'void':
      continue
    match = re.match(r'^(.*?[\w>])\s*[&*]?\s*(\w+)$', arg)
    if not match:
      return None                                            # unnamed parameter: not buildable
    cppType = re.sub(r'^const\s+', '', match.group(1)).strip()
    params.append((stripArgMacro(cppType), match.group(2)))
  return params


def parseConstructors(cppClass, headers, headerDirs):
  """The public constructors of one class, as [(type, name), ...] per overload.

  Read from the header rather than declared in the .i, because the signature IS the declaration -
  names, types and order are all there. Public and at class scope only: a listener nested in the
  same header has constructors too, and they are not something a spec can build.
  """
  shortName = cppClass.split('::')[-1]
  ctor = re.compile(r'^\s*(?:explicit\s+)?%s\s*\((.*)\)\s*;' % shortName)
  opening = re.compile(r'^\s*class\s+%s\b' % shortName)
  access = re.compile(r'^\s*(public|protected|private)\s*:')
  found = []
  for header in sorted(set(headers)):
    path = findHeader(header, headerDirs)
    if not path:
      continue
    depth, inClass, visibility = 0, False, 'private'
    for line in open(path, errors='ignore'):
      if not inClass:
        if opening.match(line):
          inClass, depth, visibility = True, 0, 'private'
        else:
          continue
      match = access.match(line)
      if match:
        visibility = match.group(1)
      # Depth 1 is the class body; deeper is a nested class, which a spec cannot build.
      if depth == 1 and visibility == 'public':
        match = ctor.match(line)
        if match:
          params = parseParams(match.group(1))
          if params is not None:
            found.append(params)
      depth += line.count('{') - line.count('}')
      if depth <= 0 and '}' in line:
        inClass = False
  return found


def findHeader(header, headerDirs):
  for headerDir in headerDirs:
    candidate = os.path.join(headerDir, header)
    if os.path.isfile(candidate):
      return candidate
  return None


def parseBases(headers, headerDirs):
  """class X : public Y, for every header the in-profile modules pull in.

  A property declared on a base is reachable from every class below it, so the table records the
  chain rather than flattening it - and a class with no properties of its own still gets an entry,
  or the chain breaks exactly where it matters (MemoryCacheTileDataSource -> CacheTileDataSource).
  """
  bases = {}
  pattern = re.compile(r'^\s*class\s+(\w+)\s*:\s*public\s+([\w:]+)')
  plain = re.compile(r'^\s*class\s+(\w+)\s*\{')
  for header in sorted(set(headers)):
    path = None
    for headerDir in headerDirs:
      candidate = os.path.join(headerDir, header)
      if os.path.isfile(candidate):
        path = candidate
        break
    if not path:
      continue
    with open(path, errors='ignore') as f:
      for line in f:
        match = pattern.match(line)
        if match:
          # A nested class names its base with '::'; those are listeners, not wrapped types.
          if '::' in match.group(2):
            bases.setdefault('massif::%s' % match.group(1), None)
          else:
            bases['massif::%s' % match.group(1)] = 'massif::%s' % match.group(2)
          continue
        match = plain.match(line)
        if match:
          bases.setdefault('massif::%s' % match.group(1), None)
  return bases


def collectModulePaths(sourceDirs, modules):
  """The .i files to read: an explicit list when given, otherwise every one under sourceDirs.

  The explicit form is what lets a test build a small table over a handful of classes instead of
  needing the whole SDK to link.
  """
  if modules:
    return [path for path in modules if path]
  paths = []
  for sourceDir in sourceDirs:
    if not os.path.isdir(sourceDir):
      continue
    for root, _, fileNames in os.walk(sourceDir):
      paths += [os.path.join(root, name) for name in sorted(fileNames) if name.endswith('.i')]
  return paths


def parseModules(sourceDirs, defines, modules=None):
  pattern = re.compile(r'^\s*[%!](' + '|'.join(sorted(ATTRIBUTE_MACROS, key=len, reverse=True)) +
                       r')\s*\((.*)\)\s*$')
  headers, entries, specs, skipped = [], [], [], 0
  for sourcePath in collectModulePaths(sourceDirs, modules):
    moduleHeaders, moduleEntries, moduleSpecs = parseModule(sourcePath, defines, pattern)
    if moduleHeaders is None:
      skipped += 1
      continue
    specs += moduleSpecs
    # Headers come from every in-profile module, attributes or not: a class with no properties
    # of its own still needs its base recorded.
    headers += moduleHeaders
    entries += moduleEntries
  return headers, entries, specs, skipped


def symbolOf(entry, prefix):
  return '%s_%s_%s' % (prefix, re.sub(r'\W', '_', entry['cppClass']), re.sub(r'\W', '_', entry['path']))


def accessible(entry):
  if entry['type'] in ACCESSIBLE_TYPES:
    return True
  if entry['type'] == 'VARIANT':
    return True
  return entry['type'] == 'STRUCT' and entry['cppType'] in CODEC_TYPES


def objectClassOf(entry):
  """The C++ class an OBJECT property points at, or None when it cannot be named.

  Two spellings reach here: a real shared_ptr type, and the polymorphic macro's Java-ish
  'package.Class', whose class is by convention the same name in the massif namespace.
  """
  if entry['type'] != 'OBJECT':
    return None
  cppType = entry['cppType']
  match = re.match(r'^std::shared_ptr<\s*(.+?)\s*>$', cppType)
  if match:
    return match.group(1)
  match = re.match(r'^[\w.]*\.(\w+)$', cppType)
  if match:
    return 'massif::%s' % match.group(1)
  return None


def selfExpr(entry):
  # A static property has no instance, so the thunk ignores its obj and names the class.
  return ('%s::' % entry['cppClass']) if entry['flags'] & FLAG_STATIC else 'self->'


def readExpr(entry):
  # Stamped so a caller reading a bool as a float can be told from a real zero.
  call = '%s%s()' % (selfExpr(entry), entry['getter'])
  prefix = 'value.type = PT_%s; ' % entry['type']
  if entry['type'] == 'COLOR':
    # Unsigned: getARGB returns int, so an opaque colour would sign-extend to a negative and the
    # round-trip would not be symmetric. A colour is a bit pattern, not a quantity.
    return prefix + 'value.intValue = static_cast<unsigned int>(%s.getARGB());' % call
  if entry['type'] == 'BOOL':
    return prefix + 'value.boolValue = %s;' % call
  if entry['type'] == 'FLOAT':
    return prefix + 'value.floatValue = static_cast<double>(%s);' % call
  if entry['type'] == 'STRING':
    return prefix + 'value.stringValue = %s;' % call
  if entry['type'] in ('STRUCT', 'VARIANT'):
    return prefix + 'value.stringValue = StructCodec::encode(%s);' % call
  return prefix + 'value.intValue = static_cast<long long>(%s);' % call  # INT, ENUM


def writeExpr(entry):
  # asX() rather than the raw field: a caller that sets a bool through setFloat must not write
  # false, and the type it stamped is what makes the conversion possible.
  setter = selfExpr(entry) + entry['setter']
  if entry['type'] == 'COLOR':
    return '%s(massif::Color(static_cast<int>(value.asLong())));' % setter
  if entry['type'] == 'BOOL':
    return '%s(value.asBool());' % setter
  if entry['type'] == 'FLOAT':
    return '%s(static_cast<%s>(value.asDouble()));' % (setter, entry['cppType'])
  if entry['type'] == 'STRING':
    # asString, not the raw field: a caller that sets a string property through setFloat must not
    # write an empty one.
    return '%s(value.asString());' % setter
  if entry['type'] in ('STRUCT', 'VARIANT'):
    # A malformed struct leaves the property alone rather than writing a default over it.
    return ('%s decoded; if (StructCodec::decode(value.stringValue, decoded)) { %s(decoded); }'
            % (entry['cppType'], setter))
  return '%s(static_cast<%s>(value.asLong()));' % (setter, entry['cppType'])


def emitAccessors(headers, entries, outPath):
  lines = [
    '// Generated by scripts/gen-api-tables.py. Do not edit.\n',
    '// One thunk per accessible accessor; included at file scope because of the #includes.\n',
    '\n',
  ]
  for header in sorted(set(headers)):
    lines.append('#include "%s"\n' % header)
  lines.append('#include "api/StructCodec.h"\n')
  lines.append('#include <memory>\n')
  lines.append('\nnamespace massif { namespace api { namespace accessors {\n\n')
  # typeid needs a COMPLETE type, and an object property can point at a class this profile only
  # forward-declares (VectorTileClickInfo.layer without Layer.i). Those keep the declared name.
  complete = set(entry['cppClass'] for entry in entries)
  for entry in entries:
    objectClass = objectClassOf(entry)
    if objectClass:
      # The CONCRETE class, not the declared one: a tileDecoder declared as VectorTileDecoder is
      # usually an MBVectorTileDecoder, and its own properties are unreachable otherwise.
      call = ('%s::%s()' % (entry['cppClass'], entry['getter'])
              if entry['flags'] & FLAG_STATIC
              else 'static_cast<%s*>(obj)->%s()' % (entry['cppClass'], entry['getter']))
      if objectClass in complete:
        read = ('    auto value = %s;\n'
                '    out.cppClass = value ? concreteClass(typeid(*value), "%s") : "%s";\n'
                '    out.obj = value;\n' % (call, objectClass, objectClass))
      else:
        read = ('    out.obj = %s;\n    out.cppClass = "%s";\n' % (call, objectClass))
      lines.append('inline void %s(%s, ObjectRef& out) {\n%s}\n' %
                   (symbolOf(entry, 'getobj'),
                    'void*' if entry['flags'] & FLAG_STATIC else 'void* obj', read))
      if not (entry['flags'] & FLAG_READONLY):
        # The cast is from shared_ptr<void>, so it is only sound because Context checks the
        # registered class against objectClass first - see isSubclassOf.
        write = ('%s::%s' % (entry['cppClass'], entry['setter'])
                 if entry['flags'] & FLAG_STATIC
                 else 'static_cast<%s*>(obj)->%s' % (entry['cppClass'], entry['setter']))
        lines.append('inline void %s(%s, const ObjectRef& value) {\n'
                     '    %s(std::static_pointer_cast<%s>(value.obj));\n}\n' %
                     (symbolOf(entry, 'setobj'),
                      'void*' if entry['flags'] & FLAG_STATIC else 'void* obj', write, objectClass))
      continue
    if not accessible(entry):
      continue
    cppClass = entry['cppClass']
    # A static thunk names the class instead, so it declares no self and ignores its obj.
    isStatic = entry['flags'] & FLAG_STATIC
    obj = 'void*' if isStatic else 'void* obj'
    bind = '' if isStatic else '    auto self = static_cast<%s*>(obj);\n' % cppClass
    lines.append('inline void %s(%s, PropertyValue& value) {\n%s    %s\n}\n' %
                 (symbolOf(entry, 'get'), obj, bind, readExpr(entry)))
    if not (entry['flags'] & FLAG_READONLY):
      lines.append('inline void %s(%s, const PropertyValue& value) {\n%s    %s\n}\n' %
                   (symbolOf(entry, 'set'), obj, bind, writeExpr(entry)))
  lines.append('\n} } }\n')
  lines.append('\nnamespace massif { namespace api {\n\n')
  lines.append('// Every class the profile has, by runtime type. Hashed on first use - see\n'
               '// concreteClass in PropertyTable.cpp.\n')
  lines.append('static const ClassTypeEntry kTypes[] = {\n')
  for cppClass in sorted(complete):
    lines.append('    { &typeid(%s), "%s" },\n' % (cppClass, cppClass))
  lines.append('};\n')
  lines.append('\n} }\n')
  with open(outPath, 'w') as f:
    f.writelines(lines)



def qualify(cppType):
  """massif::X for an SDK class named unqualified in a signature (TileDataSource, Color)."""
  if '::' in cppType or cppType.startswith('std::'):
    return cppType
  return 'massif::%s' % cppType


def specArgReader(cppType, key, default, childKind, childClass):
  """The C++ that turns one spec key into one constructor argument, or None when it cannot."""
  cppType = stripArgMacro(cppType)
  match = re.match(r'^std::shared_ptr<\s*(.+?)\s*>$', cppType)
  if match:
    if not childKind:
      return None                                            # nothing declares a kind for it
    return ('OBJECT', childKind, qualify(match.group(1)))
  if cppType in BOOL_TYPES:
    return ('boolAt(spec, "%s", %s)' % (key, default or 'false'), None, None)
  if cppType in INT_TYPES:
    return ('static_cast<%s>(intAt(spec, "%s", %s))' % (cppType, key, default or '0'), None, None)
  if cppType in FLOAT_TYPES:
    return ('static_cast<%s>(floatAt(spec, "%s", %s))' % (cppType, key, default or '0'), None, None)
  if cppType == 'std::string':
    return ('stringAt(spec, "%s", %s)' % (key, '"%s"' % default if default else '""'), None, None)
  if qualify(cppType) == 'massif::Color':
    return ('massif::Color(static_cast<int>(intAt(spec, "%s", %s)))' % (key, default or '0'),
            None, None)
  if qualify(cppType) == 'massif::Variant':
    return ('variantAt(spec, "%s")' % key, None, None)
  match = re.match(r'^massif::(\w+)::(\w+)$', cppType)
  if match and match.group(1) == match.group(2):
    return ('static_cast<%s>(intAt(spec, "%s", %s))' % (cppType, key, default or '0'), None, None)
  # The same struct types a property can carry, through the same codec.
  if qualify(cppType) in CODEC_TYPES:
    return ('structAt<%s>(spec, "%s")' % (qualify(cppType), key), None, None)
  return None


def emitSpecs(specs, bases, headerDirs, outPath):
  """One builder per declared class, from its own constructor signatures.

  A factory used to be a hand-written branch per class - the one place the facade grew when the SDK
  did. The signature already carries the names, the types and the order, so it is read instead.
  """
  # A shared_ptr parameter names a BASE (TileDataSource); the kind that builds it is declared on
  # the concrete classes below it, so the chain is walked to find one.
  kindOf = {}
  for entry in specs:
    cppClass = entry['cppClass']
    while cppClass:
      kindOf.setdefault(cppClass, entry['kind'])
      cppClass = bases.get(cppClass)

  lines = [
    '// Generated by scripts/gen-api-tables.py. Do not edit.\n',
    '// One builder per !spec class, from its own constructor signatures.\n\n',
    '#include "api/SpecBuilders.h"\n',
  ]
  for header in sorted(set(header for entry in specs for header in entry['headers'])):
    lines.append('#include "%s"\n' % header)
  lines.append('\nnamespace massif { namespace api {\n\n')
  dispatch, unbuildable = {}, []
  for entry in specs:
    cppClass = entry['cppClass']
    symbol = re.sub(r'\W', '_', cppClass)
    ctors = parseConstructors(cppClass, entry['headers'], headerDirs)
    usable = []
    for index, params in enumerate(sorted(ctors, key=len, reverse=True)):
      reads, required, keys, skip = [], [], [], None
      for cppType, name in params:
        key = entry['aliases'].get(name, name)
        default = entry['defaults'].get(name)
        reader = specArgReader(cppType, key, default,
                               kindOf.get(qualify(re.sub(r'^std::shared_ptr<\s*|\s*>$', '', cppType))),
                               None)
        if reader is None:
          skip = '%s %s' % (cppType, name)
          break
        keys.append(key)
        if reader[0] == 'OBJECT':
          reads.append(('OBJECT', key, reader[1], reader[2]))
          required.append(key)
        else:
          reads.append(('VALUE', key, reader[0], None))
          if default is None:
            required.append(key)
      if skip:
        unbuildable.append('%s: %s' % (cppClass, skip))
        continue
      usable.append((index, reads, required, keys))

    if not usable:
      continue
    for index, reads, required, keys in usable:
      name = 'make_%s_%d' % (symbol, index)
      lines.append('Result %s(Context& context, const Variant& spec, ObjectRef& object,\n'
                   '                 std::set<std::string>& consumed) {\n' % name)
      args = []
      for order, (shape, key, a, b) in enumerate(reads):
        lines.append('    consumed.insert("%s");\n' % key)
        if shape == 'OBJECT':
          lines.append('    std::shared_ptr<void> arg%d;\n'
                       '    { Result child = childOf(context, spec, "%s", "%s", "%s", arg%d);\n'
                       '      if (child != RESULT_OK) return child; }\n' % (order, key, a, b, order))
          args.append('std::static_pointer_cast<%s>(arg%d)' % (b, order))
        else:
          args.append(a)
      lines.append('    object.obj = std::make_shared<%s>(%s);\n' % (cppClass, ', '.join(args)))
      lines.append('    object.cppClass = "%s";\n    return RESULT_OK;\n}\n\n' % cppClass)
    dispatch.setdefault(entry['kind'], []).append((entry['type'], symbol, usable))

  lines.append('Result buildFromConstructor(Context& context, const std::string& kind,\n'
               '                                   const Variant& spec, ObjectRef& object,\n'
               '                                   std::set<std::string>& consumed) {\n')
  lines.append('    std::string type = stringAt(spec, "type");\n    consumed.insert("type");\n')
  for kind in sorted(dispatch):
    lines.append('    if (kind == "%s") {\n' % kind)
    for specType, symbol, usable in sorted(dispatch[kind]):
      lines.append('        if (type == "%s") {\n' % specType)
      # Longest first: the overload the spec fully satisfies is the one it meant.
      for index, reads, required, keys in usable:
        guard = ' && '.join('spec.containsObjectKey("%s")' % key for key in required) or 'true'
        lines.append('            if (%s) {\n'
                     '                return make_%s_%d(context, spec, object, consumed);\n'
                     '            }\n' % (guard, symbol, index))
      lines.append('            return RESULT_BAD_SPEC;\n        }\n')
    lines.append('    }\n')
  lines.append('    return RESULT_UNKNOWN_TYPE;\n}\n')
  lines.append('\n} }\n')
  with open(outPath, 'w') as f:
    f.writelines(lines)
  return dispatch, unbuildable


def emitTable(entries, bases, outPath):
  byClass = {}
  for entry in entries:
    byClass.setdefault(entry['cppClass'], {})[entry['path']] = entry

  # Every class the headers declare gets an entry, with or without properties of its own: a
  # registration target with none still has to resolve so its base chain can be walked.
  allClasses = set(byClass) | set(bases)

  lines = [
    '// Generated by scripts/gen-api-tables.py. Do not edit.\n',
    '// One row per Swig attribute macro; see all/native/api/PropertyTable.h.\n',
    '\n',
  ]
  for cppClass in sorted(byClass):
    symbol = re.sub(r'\W', '_', cppClass)
    lines.append('static const PropertyEntry kProps_%s[] = {\n' % symbol)
    for path in sorted(byClass[cppClass]):
      entry = byClass[cppClass][path]
      getFn = 'accessors::%s' % symbolOf(entry, 'get') if accessible(entry) else 'nullptr'
      setFn = ('accessors::%s' % symbolOf(entry, 'set')
               if accessible(entry) and not (entry['flags'] & FLAG_READONLY) else 'nullptr')
      objectClass = objectClassOf(entry)
      objFn = 'accessors::%s' % symbolOf(entry, 'getobj') if objectClass else 'nullptr'
      objSetFn = ('accessors::%s' % symbolOf(entry, 'setobj')
                  if objectClass and not (entry['flags'] & FLAG_READONLY) else 'nullptr')
      lines.append('    { "%s", PT_%s, %d, %s, %s, %s, %s, %s },\n' %
                   (path, entry['type'], entry['flags'], getFn, setFn, objFn, objSetFn,
                    '"%s"' % objectClass if objectClass else 'nullptr'))
    lines.append('};\n\n')

  lines.append('static const ClassEntry kClasses[] = {\n')
  for cppClass in sorted(allClasses):
    props = byClass.get(cppClass)
    symbol = re.sub(r'\W', '_', cppClass)
    base = bases.get(cppClass)
    lines.append('    { "%s", %s, %d, %s },\n' % (
        cppClass,
        'kProps_%s' % symbol if props else 'nullptr',
        len(props) if props else 0,
        '"%s"' % base if base else 'nullptr'))
  lines.append('};\n')

  with open(outPath, 'w') as f:
    f.writelines(lines)
  return byClass


parser = argparse.ArgumentParser()
parser.add_argument('--profile', dest='profile', default=getDefaultProfileId(), type=validProfile,
                    help='Build profile, deciding which modules exist. Ignored when --defines is given')
parser.add_argument('--defines', dest='defines', default='',
                    help='The build\'s own defines, comma or semicolon separated. Takes precedence '
                         'over --profile, so the build compiles thunks for exactly the classes it has')
# all/modules only, matching the build: a platform module set pulls in headers that are
# Objective-C on Apple, and PropertyTable.cpp is a plain C++ translation unit.
parser.add_argument('--sourcedir', dest='sourceDir', default='../all/modules',
                    help='input directories containing subdirectories of Swig wrappers, comma or '
                         'semicolon separated')
parser.add_argument('--modules', dest='modules', default='',
                    help='explicit .i files, comma separated, instead of walking --sourcedir')
parser.add_argument('--cppdir', dest='cppDir', default='../all/native',
                    help='directories containing C++ headers, for the base-class chain')
parser.add_argument('--outdir', dest='outDir', default='../generated/api',
                    help='output directory for the generated tables')
args = parser.parse_args()

# The build's own defines win: generating for a different profile than the one being compiled
# would emit thunks for classes this build does not have, and fail at link time.
source = args.defines if args.defines else getProfile(args.profile).get('defines', '')
defines = set(d.strip() for d in re.split(r'[;,]', source) if d.strip())

headers, entries, specs, skipped = parseModules(re.split(r'[;,]', args.sourceDir), defines,
                                        re.split(r'[;,]', args.modules) if args.modules else None)
if not entries:
  print('No attribute macros found - is --sourcedir right?')
  sys.exit(-1)

os.makedirs(args.outDir, exist_ok=True)
emitAccessors(headers, entries, os.path.join(args.outDir, 'PropertyAccessors.inc'))
bases = parseBases(headers, re.split(r'[;,]', args.cppDir))
byClass = emitTable(entries, bases, os.path.join(args.outDir, 'PropertyTable.inc'))
built, unbuildable = emitSpecs(specs, bases, re.split(r'[;,]', args.cppDir),
                               os.path.join(args.outDir, 'SpecConstructors.inc'))

counts = {}
for entry in entries:
  counts[entry['type']] = counts.get(entry['type'], 0) + 1
print('%d properties over %d classes, %d classes in the chain (%d value, %d object, %d modules out of profile)' %
      (len(entries), len(byClass), len(set(byClass) | set(bases)),
       sum(1 for e in entries if accessible(e)),
       sum(1 for e in entries if objectClassOf(e)), skipped))
print('  ' + '  '.join('%s=%d' % (name, counts[name]) for name in TYPE_NAMES if name in counts))

# A property with no accessor is silently unreadable - the failure mode that hid
# RoutingInstruction.action and PackageInfo.size. Name what is still out of reach, by type, so the
# next gap costs a glance instead of a device session.
unreachable = {}
for entry in entries:
  if accessible(entry) or objectClassOf(entry):
    continue
  unreachable.setdefault(entry['cppType'], []).append(entry['cppClass'] + '.' + entry['path'])
if unreachable:
  print('  %d properties have no accessor:' % sum(len(v) for v in unreachable.values()))
  for cppType in sorted(unreachable, key=lambda t: (-len(unreachable[t]), t)):
    print('    %-42s %2d  e.g. %s' % (cppType, len(unreachable[cppType]), unreachable[cppType][0]))

print('  %d classes build from their constructors, over %d kinds' %
      (sum(len(v) for v in built.values()), len(built)))
for note in sorted(set(unbuildable)):
  print('    overload skipped, no reader for %s' % note)
