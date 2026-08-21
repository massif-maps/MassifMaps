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


def parseModule(sourcePath, defines, pattern):
  """Returns (headers, entries), or (None, None) when the module is not in this profile."""
  headers, entries, inCode = [], [], False
  with open(sourcePath) as f:
    for line in f:
      line = line.rstrip('\n')

      match = SUPPORT_DEFINE.search(line)
      if match:
        define = match.group(1) or match.group(2)
        if define not in defines:
          return None, None

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
  return headers, entries


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
  headers, entries, skipped = [], [], 0
  for sourcePath in collectModulePaths(sourceDirs, modules):
    moduleHeaders, moduleEntries = parseModule(sourcePath, defines, pattern)
    if moduleHeaders is None:
      skipped += 1
      continue
    # Headers come from every in-profile module, attributes or not: a class with no properties
    # of its own still needs its base recorded.
    headers += moduleHeaders
    entries += moduleEntries
  return headers, entries, skipped


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

headers, entries, skipped = parseModules(re.split(r'[;,]', args.sourceDir), defines,
                                        re.split(r'[;,]', args.modules) if args.modules else None)
if not entries:
  print('No attribute macros found - is --sourcedir right?')
  sys.exit(-1)

os.makedirs(args.outDir, exist_ok=True)
emitAccessors(headers, entries, os.path.join(args.outDir, 'PropertyAccessors.inc'))
bases = parseBases(headers, re.split(r'[;,]', args.cppDir))
byClass = emitTable(entries, bases, os.path.join(args.outDir, 'PropertyTable.inc'))

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
