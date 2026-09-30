#!/usr/bin/env python3
"""Build, pack and publish every Massif Maps npm package.

    python3 scripts/npm-packages.py pack 6.1.0-rc.1 [--only api,web] [--styles-version 1.2.0]
    python3 scripts/npm-packages.py publish [--tag next] [--dry-run]

`pack` writes one tarball per package into dist/npm; `publish` publishes the tarballs found there,
dependencies first. api, web and style-tools take the SDK version; styles keeps its own and is only
packed when --styles-version is given. Prerequisites per package: BUILDING.md, "npm packages".
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'dist', 'npm')

# Publish order: @massif-maps/web depends on @massif-maps/api at the same version.
PACKAGES = {
    'api': '@massif-maps/api',
    'web': '@massif-maps/web',
    'style-tools': '@massif-maps/style-tools',
    'styles': '@massif-maps/styles',
}


def run(cmd, cwd):
    rel = os.path.relpath(cwd, ROOT)
    print('+ (cd %s && %s)' % (cwd if rel.startswith('..') else rel, ' '.join(cmd)), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def require(paths, how):
    missing = [os.path.relpath(p, ROOT) for p in paths if not os.path.exists(p)]
    if missing:
        sys.exit('missing %s - %s' % (', '.join(missing), how))


def npm_pack(cwd, out):
    run(['npm', 'pack', '--pack-destination', out], cwd)


def pack_versioned(cwd, version, out, steps):
    """npm version rewrites package.json and its lock: put them back so a local run leaves no diff."""
    saved = {f: open(os.path.join(cwd, f)).read() for f in ('package.json', 'package-lock.json')
             if os.path.exists(os.path.join(cwd, f))}
    try:
        run(['npm', 'ci'], cwd)
        run(['npm', 'version', version, '--no-git-tag-version', '--allow-same-version'], cwd)
        for step in steps:
            run(step, cwd)
        npm_pack(cwd, out)
    finally:
        for f, text in saved.items():
            open(os.path.join(cwd, f), 'w').write(text)


def pack_api(version, out):
    pack_versioned(os.path.join(ROOT, 'bindings', 'js'), version, out,
                   [['npm', 'run', 'build'], ['npm', 'run', 'check']])


def pack_web(version, out):
    dist = os.path.join(ROOT, 'dist', 'web')
    require([os.path.join(dist, 'massif-web.wasm'), os.path.join(dist, 'massif-web-full.wasm')],
            'build both variants first (BUILDING.md, "Web SDK")')
    require([os.path.join(ROOT, 'bindings', 'js', 'dist', 'index.js')], 'pack api first')
    builder = os.path.join(ROOT, 'web', 'package')
    run(['npm', 'ci'], builder)
    run(['node', 'build.mjs', '--version', version], builder)
    npm_pack(dist, out)


def pack_style_tools(version, out):
    cwd = os.path.join(ROOT, 'tools', 'style-cli')
    require([os.path.join(cwd, 'wasm', 'massif-style.wasm'), os.path.join(cwd, 'wasm', 'massif-style.mjs')],
            'build the style compiler to wasm first (BUILDING.md, "Style tools")')
    pack_versioned(cwd, version, out, [['npm', 'run', 'build']])


def pack_styles(version, out):
    cli = os.path.join(ROOT, 'tools', 'style-cli')
    run(['npm', 'ci'], cli)
    run(['npm', 'run', 'build'], cli)
    run(['npm', 'ci'], os.path.join(ROOT, 'tools', 'style-sprite'))
    with tempfile.TemporaryDirectory() as tmp:
        dist = os.path.join(tmp, 'dist')
        run([sys.executable, os.path.join('release', 'release.py'), version, '--out', dist],
            os.path.join(ROOT, 'styles', 'massif'))
        npm_pack(os.path.join(dist, 'npm'), out)


PACKERS = {'api': pack_api, 'web': pack_web, 'style-tools': pack_style_tools, 'styles': pack_styles}


def tarballs(out):
    """{package key: tarball}, in publish order; npm names a tarball after the scoped name."""
    found = {}
    for key, name in PACKAGES.items():
        prefix = name.lstrip('@').replace('/', '-') + '-'
        matches = [t for t in glob.glob(os.path.join(out, prefix + '*.tgz'))
                   if re.fullmatch(r'\d.*', os.path.basename(t)[len(prefix):-len('.tgz')])]
        if len(matches) > 1:
            sys.exit('more than one %s tarball in %s: %s' % (name, out, ', '.join(map(os.path.basename, matches))))
        if matches:
            found[key] = matches[0]
    return found


def pack(args):
    only = args.only.split(',') if args.only else ['api', 'web', 'style-tools'] + (['styles'] if args.styles_version else [])
    unknown = set(only) - set(PACKAGES)
    if unknown:
        sys.exit('unknown package %s; one of %s' % (', '.join(sorted(unknown)), ', '.join(PACKAGES)))
    if 'styles' in only and not args.styles_version:
        sys.exit('styles has its own version: pass --styles-version')
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    for key, tgz in tarballs(out).items():
        if key in only:
            os.remove(tgz)
    for key in PACKAGES:
        if key in only:
            PACKERS[key](args.styles_version if key == 'styles' else args.version, out)
    for key, tgz in tarballs(out).items():
        print('%-26s %s' % (PACKAGES[key], os.path.relpath(tgz, ROOT)))


def publish(args):
    found = tarballs(os.path.abspath(args.out))
    if not found:
        sys.exit('no tarball in %s - run pack first' % args.out)
    versions = {k: re.search(r'-(\d[^/]*)\.tgz$', t).group(1) for k, t in found.items()}
    sdk = {v for k, v in versions.items() if k != 'styles'}
    if len(sdk) > 1:
        sys.exit('api, web and style-tools must share one version, found %s' % ', '.join(sorted(sdk)))
    if 'web' in found and 'api' not in found:
        print('warning: publishing @massif-maps/web without @massif-maps/api - its dependency must already be on npm')
    for key, tgz in found.items():
        published = subprocess.run(['npm', 'view', f'{PACKAGES[key]}@{versions[key]}', 'version'],
                                   capture_output=True, text=True).stdout.strip()
        if published == versions[key]:
            print(f'{PACKAGES[key]}@{versions[key]} is already on npm, skipped')
            continue
        tag = args.tag or ('next' if '-' in versions[key] else 'latest')
        cmd = ['npm', 'publish', tgz, '--access', 'public', '--tag', tag]
        if os.environ.get('GITHUB_ACTIONS') == 'true':
            cmd.append('--provenance')
        if args.dry_run:
            cmd.append('--dry-run')
        run(cmd, ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('pack', help='build every package and npm pack it')
    p.add_argument('version', help='SDK version, no leading v (6.1.0, 6.1.0-rc.1)')
    p.add_argument('--only', help='comma-separated subset of: ' + ', '.join(PACKAGES))
    p.add_argument('--styles-version', help='also pack @massif-maps/styles at this version')
    p.add_argument('--out', default=OUT)
    p.set_defaults(func=pack)
    p = sub.add_parser('publish', help='npm publish the tarballs pack wrote')
    p.add_argument('--tag', help='npm dist-tag (default: next for a prerelease version, latest otherwise)')
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--out', default=OUT)
    p.set_defaults(func=publish)
    args = parser.parse_args()
    args.version = getattr(args, 'version', '').lstrip('v') or None
    args.func(args)


if __name__ == '__main__':
    main()
