#!/usr/bin/env python3
"""
Turns the demo app's example files into the one list both the app and the website read.

An example is a .java file under the demo's examples/ package carrying an @ExampleInfo annotation.
That annotation is the single source of truth: this script parses it and writes

  * ExampleRegistry.java     - the ordered class list the gallery iterates (no runtime scanning);
  * docs/examples/examples.json - the same list as data, which website/src/pages/examples.js and
    docs/examples/index.mdx build the published gallery from.

So adding an example is adding one file. Nothing else has a list to keep in step.

Run it by hand, or let the gradle build do it (:app:generateExamples).
"""

import argparse
import json
import os
import re
import sys

# The annotation, with values in any order. Java string escapes are kept as written and unescaped
# below, so a description may contain a quote.
ANNOTATION = re.compile(r'@ExampleInfo\s*\((.*?)\)\s*(?:public\s+)?(?:final\s+)?class\s+(\w+)',
                        re.DOTALL)
# key = "value" | key = 123 | key = Sections.BASICS. A string value may be a concatenation of
# literals across lines; a constant is resolved against Sections.java below.
MEMBER = re.compile(r'(\w+)\s*=\s*((?:"(?:\\.|[^"\\])*"\s*\+?\s*)+|-?\d+|Sections\.\w+|\w+)')
STRING = re.compile(r'"((?:\\.|[^"\\])*)"')
# Sections.ALL: { BASICS, "Map basics", "blurb" },
SECTION_ROW = re.compile(r'\{\s*(\w+)\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\}')
SECTION_CONST = re.compile(r'String\s+(\w+)\s*=\s*"([^"]*)"')

ESCAPES = {'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'"}


def unescape(text):
  out, i = [], 0
  while i < len(text):
    if text[i] == '\\' and i + 1 < len(text):
      out.append(ESCAPES.get(text[i + 1], text[i + 1]))
      i += 2
    else:
      out.append(text[i])
      i += 1
  return ''.join(out)


def literal(value, constants):
  """An int, one or more concatenated string literals, or a Sections constant."""
  if STRING.search(value):
    return ''.join(unescape(m.group(1)) for m in STRING.finditer(value))
  if re.fullmatch(r'-?\d+', value):
    return int(value)
  # `section = Sections.BASICS`. Resolved rather than accepted verbatim, so a typo is a reported
  # unknown section instead of a section that silently does not exist.
  name = value.split('.')[-1]
  if name in constants:
    return constants[name]
  return value


def parseSections(path):
  """Section id, title and blurb, in the order Sections.ALL declares them, plus the constants."""
  with open(path) as f:
    source = f.read()
  constants = dict(SECTION_CONST.findall(source))
  sections = []
  for name, title, blurb in SECTION_ROW.findall(source):
    if name not in constants:
      print('  %s: section constant %s is not declared' % (os.path.basename(path), name))
      continue
    sections.append({'id': constants[name], 'title': unescape(title),
                     'description': unescape(blurb)})
  return sections, constants


def parseExamples(rootDir, package, constants):
  """Every annotated class under rootDir, with the fields the annotation carries."""
  examples, unannotated = [], []
  for dirPath, _, fileNames in os.walk(rootDir):
    for fileName in sorted(fileNames):
      if not fileName.endswith('.java'):
        continue
      path = os.path.join(dirPath, fileName)
      with open(path) as f:
        source = f.read()
      match = ANNOTATION.search(source)
      if not match:
        # Only report files that look like they meant to be one: the framework itself lives here.
        if 'extends MapExample' in source:
          unannotated.append(os.path.relpath(path, rootDir))
        continue
      fields = {key: literal(value, constants)
                for key, value in MEMBER.findall(match.group(1))}
      relative = os.path.relpath(path, rootDir).replace(os.sep, '/')
      subPackage = os.path.dirname(relative).replace('/', '.')
      fields['class'] = package + ('.' + subPackage if subPackage else '') + '.' + match.group(2)
      fields['file'] = relative
      fields['order'] = fields.get('order', 100)
      examples.append((path, fields))
  return examples, unannotated


def validate(examples, sections):
  """Everything that would otherwise fail silently: a typo'd section, a duplicate id, a gap."""
  known = set(section['id'] for section in sections)
  seen, problems = {}, []
  for path, fields in examples:
    where = fields.get('class', path)
    for required in ('id', 'title', 'description', 'section'):
      if not fields.get(required):
        problems.append('%s: no %s' % (where, required))
    if fields.get('section') and fields['section'] not in known:
      problems.append('%s: unknown section "%s" (add it to Sections.ALL)'
                      % (where, fields['section']))
    identifier = fields.get('id')
    if identifier in seen:
      problems.append('%s: id "%s" is already used by %s' % (where, identifier, seen[identifier]))
    elif identifier:
      seen[identifier] = where
  return problems


def emitRegistry(examples, sections, path, package):
  """The class list, ordered. Generated so the app needs no classpath scanning at startup."""
  order = {section['id']: index for index, section in enumerate(sections)}
  ordered = sorted(examples, key=lambda e: (order.get(e[1]['section'], len(order)),
                                            e[1]['order'], e[1]['title']))
  lines = [
    '// Generated by scripts/gen-examples.py. Do not edit.\n',
    'package %s;\n' % package,
    '\n',
    '/**\n',
    ' * Every example, in gallery order.\n',
    ' *\n',
    ' * Generated from the @ExampleInfo annotations rather than scanned at runtime: Android has no\n',
    ' * cheap way to enumerate a package, and a list nobody maintains cannot go stale.\n',
    ' */\n',
    'public final class ExampleRegistry {\n',
    '\n',
    '    public static final Class<?>[] ALL = {\n',
  ]
  for _, fields in ordered:
    lines.append('        %s.class,\n' % fields['class'])
  lines += [
    '    };\n',
    '\n',
    '    private ExampleRegistry() {\n',
    '    }\n',
    '}\n',
  ]
  write(path, ''.join(lines))
  return ordered


def findIosSource(iosDir, identifier):
  """
  The Objective-C example with the same id, when there is one.

  Matched by ID rather than by filename: the two demos name their files by each platform's
  convention, and the id is the thing both platforms and the website already agree on.
  """
  if not iosDir or not os.path.isdir(iosDir):
    return None
  for dirPath, _, fileNames in os.walk(iosDir):
    for fileName in sorted(fileNames):
      if not fileName.endswith('.m'):
        continue
      full = os.path.join(dirPath, fileName)
      with open(full, errors='ignore') as f:
        source = f.read()
      # The id is on the RETURN line, not the declaration's: matching one line at a time found
      # nothing and said nothing, which is how every iOS example stayed invisible to the website.
      if re.search(r'exampleId\s*\{\s*return\s+@"%s"' % re.escape(identifier), source):
        return full
  return None


def iosExampleIds(iosDir):
    """Every id the iOS demo declares, so one that matches no Android example is reported."""
    found = {}
    if not iosDir or not os.path.isdir(iosDir):
      return found
    for dirPath, _, fileNames in os.walk(iosDir):
      for fileName in sorted(fileNames):
        if not fileName.endswith('.m'):
          continue
        full = os.path.join(dirPath, fileName)
        with open(full, errors='ignore') as f:
          match = re.search(r'exampleId\s*\{\s*return\s+@"([^"]+)"', f.read())
        if match:
          found[match.group(1)] = os.path.basename(full)
    return found


def nativeScriptExampleIds(nsDir):
  """Every id the NativeScript demo declares, keyed to its .svelte file."""
  found = {}
  if not nsDir or not os.path.isdir(nsDir):
    return found
  for dirPath, _, fileNames in os.walk(nsDir):
    for fileName in sorted(fileNames):
      if not fileName.endswith('.svelte'):
        continue
      full = os.path.join(dirPath, fileName)
      with open(full, errors='ignore') as f:
        # The id is on the <ExampleShell> tag, the one thing all three platforms agree on.
        match = re.search(r'<ExampleShell[^>]*\bid="([^"]+)"', f.read())
      if match:
        found[match.group(1)] = full
  return found


def emitManifest(ordered, sections, path, screenshotDir, sourceRoot, exampleDir, iosDir=None,
                 nsDir=None, repoRoot=None):
  """
  The website's copy of the same list, with each example's source and screenshot.

  The SOURCE is embedded rather than linked because the website would otherwise need a raw text
  loader to read a path it computes at render time. It is generated, so it cannot drift; the app
  reads the same files straight out of the APK (see CodeActivity).
  """
  bySection = {section['id']: [] for section in sections}
  missing = []
  nsById = nativeScriptExampleIds(nsDir)

  def repoPath(full):
    """Repo-relative, so the website can link the file on GitHub."""
    return os.path.relpath(os.path.abspath(full), repoRoot).replace(os.sep, '/') \
        if repoRoot else None

  for _, fields in ordered:
    code = {'java': open(os.path.join(exampleDir, fields['file'])).read()}
    sources = {'java': sourceRoot + '/' + fields['file']}
    iosSource = findIosSource(iosDir, fields['id'])
    if iosSource:
      code['objc'] = open(iosSource).read()
      sources['objc'] = repoPath(iosSource)
    nsSource = nsById.get(fields['id'])
    if nsSource:
      code['ts'] = open(nsSource).read()
      sources['ts'] = repoPath(nsSource)
    shot = os.path.join(screenshotDir, fields['id'] + '.png')
    if not os.path.exists(shot):
      missing.append(fields['id'])
    bySection.setdefault(fields['section'], []).append({
      'id': fields['id'],
      'title': fields['title'],
      'description': fields['description'],
      'class': fields['class'],
      # Repo-relative, so the website can read the file and link to it on GitHub.
      'source': sources['java'],
      'sources': {key: value for key, value in sources.items() if value},
      'screenshot': 'screenshots/' + fields['id'] + '.png',
      'hasScreenshot': os.path.exists(shot),
      # One entry per language, so the website can offer a tab per binding. Java is the
      # reference; Objective-C and the NativeScript/Svelte source appear for an example
      # those demos have ported.
      'code': code,
    })
  manifest = {
    '_generated': 'scripts/gen-examples.py - do not edit',
    'sections': [dict(section, examples=bySection.get(section['id'], []))
                 for section in sections if bySection.get(section['id'])],
  }
  write(path, json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
  return missing


def write(path, content):
  os.makedirs(os.path.dirname(path), exist_ok=True)
  # Only touch the file when it changed, so a build does not re-run javac for nothing.
  if os.path.exists(path):
    with open(path) as f:
      if f.read() == content:
        return
  with open(path, 'w') as f:
    f.write(content)


here = os.path.dirname(os.path.abspath(__file__))
parser = argparse.ArgumentParser()
parser.add_argument('--examples', default=os.path.join(
    here, 'android-dev/app/src/main/java/com/massifmaps/MassifDemo/examples'),
    help='the demo\'s examples package')
parser.add_argument('--package', default='com.massifmaps.MassifDemo.examples')
parser.add_argument('--docs', default=os.path.join(here, '../docs/examples'),
                    help='where examples.json and screenshots/ live')
parser.add_argument('--ios', default=os.path.join(here, 'ios-dev/MassifDemo/Examples'),
                    help='the iOS demo\'s examples, matched to the Android ones by id')
parser.add_argument('--nativescript', default=os.path.join(
    here, '../integrations/nativescript/demo-snippets/svelte/examples'),
    help='the NativeScript demo\'s examples, matched to the Android ones by id')
parser.add_argument('--strict', action='store_true',
                    help='exit non-zero when an example is malformed, for CI')
args = parser.parse_args()

sections, constants = parseSections(os.path.join(args.examples, 'Sections.java'))
examples, unannotated = parseExamples(args.examples, args.package, constants)
if not examples:
  print('No @ExampleInfo classes under %s' % args.examples)
  sys.exit(1)

problems = validate(examples, sections)
for name in unannotated:
  problems.append('%s: extends MapExample but has no @ExampleInfo, so it is invisible' % name)

ordered = emitRegistry(examples, sections, os.path.join(args.examples, 'ExampleRegistry.java'),
                       args.package)
missing = emitManifest(ordered, sections, os.path.join(args.docs, 'examples.json'),
                       os.path.join(args.docs, 'screenshots'),
                       'scripts/android-dev/app/src/main/java/'
                       + args.package.replace('.', '/'),
                       args.examples, args.ios, args.nativescript,
                       os.path.abspath(os.path.join(here, '..')))

ported = iosExampleIds(args.ios)
nsPorted = nativeScriptExampleIds(args.nativescript)
ids = set(f['id'] for _, f in examples)
orphans = sorted((set(ported) | set(nsPorted)) - ids)

print('%d examples over %d sections' % (len(ordered), len(set(f['section'] for _, f in examples))))
if ported:
  print('  %d of them ported to iOS' % len(set(ported) & ids))
if nsPorted:
  print('  %d of them ported to NativeScript' % len(set(nsPorted) & ids))
for orphan in orphans:
  where = ported.get(orphan) or os.path.basename(nsPorted[orphan])
  problems.append('%s declares id "%s", which no Android example has'
                  % (os.path.basename(where), orphan))
if missing:
  # Not an error: a new example has no screenshot until scripts/capture-examples.py has run.
  print('  %d without a screenshot: %s' % (len(missing), ', '.join(missing)))
if problems:
  print('  %d problems:' % len(problems))
  for problem in problems:
    print('    ' + problem)
  if args.strict:
    sys.exit(1)
