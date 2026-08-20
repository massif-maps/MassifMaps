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
FLOAT_TYPES = {'float', 'double'}

TYPE_NAMES = ['BOOL', 'INT', 'FLOAT', 'COLOR', 'ENUM', 'STRING', 'OBJECT', 'STRUCT']

# Types a value accessor can carry today. STRUCT needs JSON marshalling, so it is listed in the
# table but has no thunk yet. OBJECT gets its own accessor instead - see objectClassOf.
ACCESSIBLE_TYPES = {'BOOL', 'INT', 'FLOAT', 'COLOR', 'ENUM', 'STRING'}

FLAG_READONLY = 1
FLAG_STATIC = 2

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
  return match.group(1).strip() if match else cppType.strip()


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
      entries.append({
        'cppClass': cppClass,
        'cppType': stripArgMacro(cppType),
        'path': decapitalize(name),
        'type': classifyType(cppType, ATTRIBUTE_MACROS[macro]),
        'flags': flags,
        'getter': getter,
        'setter': setter,
      })
  return headers, entries


def parseModules(sourceDirs, defines):
  pattern = re.compile(r'^\s*[%!](' + '|'.join(sorted(ATTRIBUTE_MACROS, key=len, reverse=True)) +
                       r')\s*\((.*)\)\s*$')
  headers, entries, skipped = [], [], 0
  for sourceDir in sourceDirs:
    if not os.path.isdir(sourceDir):
      continue
    for root, _, fileNames in os.walk(sourceDir):
      for fileName in sorted(fileNames):
        if not fileName.endswith('.i'):
          continue
        moduleHeaders, moduleEntries = parseModule(os.path.join(root, fileName), defines, pattern)
        if moduleHeaders is None:
          skipped += 1
          continue
        if moduleEntries:
          headers += moduleHeaders
          entries += moduleEntries
  return headers, entries, skipped


def symbolOf(entry, prefix):
  return '%s_%s_%s' % (prefix, re.sub(r'\W', '_', entry['cppClass']), re.sub(r'\W', '_', entry['path']))


def accessible(entry):
  return entry['type'] in ACCESSIBLE_TYPES and not (entry['flags'] & FLAG_STATIC)


def objectClassOf(entry):
  """The C++ class an OBJECT property points at, or None when it cannot be named.

  Two spellings reach here: a real shared_ptr type, and the polymorphic macro's Java-ish
  'package.Class', whose class is by convention the same name in the massif namespace.
  """
  if entry['type'] != 'OBJECT' or (entry['flags'] & FLAG_STATIC):
    return None
  cppType = entry['cppType']
  match = re.match(r'^std::shared_ptr<\s*(.+?)\s*>$', cppType)
  if match:
    return match.group(1)
  match = re.match(r'^[\w.]*\.(\w+)$', cppType)
  if match:
    return 'massif::%s' % match.group(1)
  return None


def readExpr(entry):
  call = 'self->%s()' % entry['getter']
  if entry['type'] == 'COLOR':
    return 'value.intValue = %s.getARGB();' % call
  if entry['type'] == 'BOOL':
    return 'value.boolValue = %s;' % call
  if entry['type'] == 'FLOAT':
    return 'value.floatValue = static_cast<double>(%s);' % call
  if entry['type'] == 'STRING':
    return 'value.stringValue = %s;' % call
  return 'value.intValue = static_cast<long long>(%s);' % call  # INT, ENUM


def writeExpr(entry):
  if entry['type'] == 'COLOR':
    return 'self->%s(massif::Color(static_cast<int>(value.intValue)));' % entry['setter']
  if entry['type'] == 'BOOL':
    return 'self->%s(value.boolValue);' % entry['setter']
  if entry['type'] == 'FLOAT':
    return 'self->%s(static_cast<%s>(value.floatValue));' % (entry['setter'], entry['cppType'])
  if entry['type'] == 'STRING':
    return 'self->%s(value.stringValue);' % entry['setter']
  return 'self->%s(static_cast<%s>(value.intValue));' % (entry['setter'], entry['cppType'])


def emitAccessors(headers, entries, outPath):
  lines = [
    '// Generated by scripts/gen-api-tables.py. Do not edit.\n',
    '// One thunk per accessible accessor; included at file scope because of the #includes.\n',
    '\n',
  ]
  for header in sorted(set(headers)):
    lines.append('#include "%s"\n' % header)
  lines.append('\nnamespace massif { namespace api { namespace accessors {\n\n')
  for entry in entries:
    objectClass = objectClassOf(entry)
    if objectClass:
      lines.append('inline void %s(void* obj, ObjectRef& out) {\n'
                   '    out.obj = static_cast<%s*>(obj)->%s();\n'
                   '    out.cppClass = "%s";\n}\n' %
                   (symbolOf(entry, 'getobj'), entry['cppClass'], entry['getter'], objectClass))
      continue
    if not accessible(entry):
      continue
    cppClass = entry['cppClass']
    lines.append('inline void %s(void* obj, PropertyValue& value) {\n'
                 '    auto self = static_cast<%s*>(obj);\n'
                 '    %s\n}\n' % (symbolOf(entry, 'get'), cppClass, readExpr(entry)))
    if not (entry['flags'] & FLAG_READONLY):
      lines.append('inline void %s(void* obj, const PropertyValue& value) {\n'
                   '    auto self = static_cast<%s*>(obj);\n'
                   '    %s\n}\n' % (symbolOf(entry, 'set'), cppClass, writeExpr(entry)))
  lines.append('\n} } }\n')
  with open(outPath, 'w') as f:
    f.writelines(lines)


def emitTable(entries, outPath):
  byClass = {}
  for entry in entries:
    byClass.setdefault(entry['cppClass'], {})[entry['path']] = entry

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
      objFn = 'accessors::%s' % symbolOf(entry, 'getobj') if objectClassOf(entry) else 'nullptr'
      lines.append('    { "%s", PT_%s, %d, %s, %s, %s },\n' %
                   (path, entry['type'], entry['flags'], getFn, setFn, objFn))
    lines.append('};\n\n')

  lines.append('static const ClassEntry kClasses[] = {\n')
  for cppClass in sorted(byClass):
    symbol = re.sub(r'\W', '_', cppClass)
    lines.append('    { "%s", kProps_%s, %d },\n' % (cppClass, symbol, len(byClass[cppClass])))
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
parser.add_argument('--sourcedir', dest='sourceDir', default='../all/modules,../android/modules',
                    help='input directories containing subdirectories of Swig wrappers, comma or '
                         'semicolon separated')
parser.add_argument('--outdir', dest='outDir', default='../generated/api',
                    help='output directory for the generated tables')
args = parser.parse_args()

# The build's own defines win: generating for a different profile than the one being compiled
# would emit thunks for classes this build does not have, and fail at link time.
source = args.defines if args.defines else getProfile(args.profile).get('defines', '')
defines = set(d.strip() for d in re.split(r'[;,]', source) if d.strip())

headers, entries, skipped = parseModules(re.split(r'[;,]', args.sourceDir), defines)
if not entries:
  print('No attribute macros found - is --sourcedir right?')
  sys.exit(-1)

if not os.path.isdir(args.outDir):
  os.makedirs(args.outDir)
emitAccessors(headers, entries, os.path.join(args.outDir, 'PropertyAccessors.inc'))
byClass = emitTable(entries, os.path.join(args.outDir, 'PropertyTable.inc'))

counts = {}
for entry in entries:
  counts[entry['type']] = counts.get(entry['type'], 0) + 1
print('%d properties over %d classes (%d value, %d object, %d modules out of profile)' %
      (len(entries), len(byClass), sum(1 for e in entries if accessible(e)),
       sum(1 for e in entries if objectClassOf(e)), skipped))
print('  ' + '  '.join('%s=%d' % (name, counts[name]) for name in TYPE_NAMES if name in counts))
