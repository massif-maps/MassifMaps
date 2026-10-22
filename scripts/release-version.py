#!/usr/bin/env python3
"""The next version of a release product, from its git tags, as key=value lines on stdout.

    python3 scripts/release-version.py styles auto >> "$GITHUB_OUTPUT"
    python3 scripts/release-version.py sdk minor --prerelease      # 6.3.0-rc.1, then -rc.2 ...
    python3 scripts/release-version.py tools 6.3.0                 # an explicit version
    python3 scripts/release-version.py styles auto --sdk-planned 6.3.0   # adds min_sdk=, see below

Products: sdk (vX.Y.Z), styles (massif-styles-vX.Y.Z), tools (style-tools-vX.Y.Z). Bump: none,
patch, minor, major, auto or a version. `auto` takes the commits since the last final tag that
touch the product (the paths of release-notes.py): a breaking one is a major, a feat a minor,
anything else a patch, and none at all releases nothing. A skipped product prints an empty version.

A styles release also prints `min_sdk`, the oldest SDK it needs (styles/massif/compat.json). `next` there
means the SDK released together with the styles, so --sdk-planned must be that version. A number
above the latest SDK release needs the same: the styles cannot ship before the SDK they need.
"""
import argparse
import importlib.util
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
COMPAT = os.path.join(HERE, '..', 'styles', 'massif', 'compat.json')
PREFIXES = {'sdk': 'v', 'styles': 'massif-styles-v', 'tools': 'style-tools-v'}
VERSION = re.compile(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?')
SEMVER_NUMBERS = re.compile(r'(\d+)\.(\d+)\.(\d+)')
BREAKING = re.compile(r'^BREAKING[ -]CHANGE: ', re.M)


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), os.path.join(HERE, name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tags, notes = load('release-tags'), load('release-notes')


def suggest(product, base, ref):
    """(bump the commits since `base` call for, how many touch the product)"""
    bump, count = 'patch', 0
    for _, message, paths in notes.commits(base, ref):
        if product not in notes.products(paths):
            continue
        count += 1
        match = notes.HEADER.match(message.splitlines()[0])
        if match and match[3] or BREAKING.search(message):
            bump = 'major'
        elif match and match[1] == 'feat' and bump != 'major':
            bump = 'minor'
    return (bump if count else 'none'), count


def bumped(base, bump):
    major, minor, patch = (int(n) for n in SEMVER_NUMBERS.fullmatch(base).groups())
    return {'major': f'{major + 1}.0.0', 'minor': f'{major}.{minor + 1}.0', 'patch': f'{major}.{minor}.{patch + 1}'}[bump]


def next_rc(existing, prefix, version):
    found = [int(m[1]) for t in existing if (m := re.fullmatch(re.escape(prefix + version) + r'-rc\.(\d+)', t))]
    return f'{version}-rc.{max(found, default=0) + 1}'


def triple(version):
    return tuple(int(n) for n in version.split('-')[0].split('.'))


def min_sdk(planned):
    value = json.load(open(COMPAT))['minSdk']
    released = max((t for t in tags.tags_of('v') if not tags.SEMVER.fullmatch(t)[5]), key=tags.key)[1:]
    if value == 'next':
        if not planned:
            raise SystemExit('::error::styles need the SDK released with them (minSdk is "next"): release the sdk too')
        value = planned.split('-')[0]
    if triple(value) > triple(released) and not (planned and triple(value) <= triple(planned)):
        raise SystemExit(f'::error::styles need SDK {value}, the latest is {released}: release the sdk too')
    return value


def plan(product, bump, prerelease, ref, sdk_planned=''):
    prefix = PREFIXES[product]
    existing = tags.tags_of(prefix)
    finals = [t for t in existing if not tags.SEMVER.fullmatch(t)[5]]
    if not finals:
        raise SystemExit(f'::error::no final {prefix}* tag: tag the commit of the last release first')
    base = max(finals, key=tags.key)
    suggested, count = suggest(product, base, ref)
    out = {'product': product, 'base': base, 'suggested': suggested, 'commits': count, 'version': '', 'tag': ''}
    if bump == 'none' or (bump == 'auto' and suggested == 'none'):
        return out
    if bump in ('auto', 'patch', 'minor', 'major'):
        version = bumped(base[len(prefix):], suggested if bump == 'auto' else bump)
    elif VERSION.fullmatch(bump):
        version = bump
    else:
        raise SystemExit(f'::error::{bump} is not none, auto, patch, minor, major or a version')
    if prerelease and '-' not in version:
        version = next_rc(existing, prefix, version)
    tag = prefix + version
    if tag in existing:
        raise SystemExit(f'::error::{tag} already exists')
    tags.previous(tag)
    out.update(version=version, tag=tag)
    if product == 'styles':
        out['min_sdk'] = min_sdk(sdk_planned)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('product', choices=sorted(PREFIXES))
    parser.add_argument('bump')
    parser.add_argument('--prerelease', action='store_true', help='release candidate: <version>-rc.N, N counted from the tags')
    parser.add_argument('--ref', default='HEAD')
    parser.add_argument('--sdk-planned', default='', help='the SDK version released in the same run, for a styles release')
    args = parser.parse_args()
    for k, v in plan(args.product, args.bump.lstrip('v'), args.prerelease, args.ref, args.sdk_planned).items():
        print(f'{k}={v}')


if __name__ == '__main__':
    main()
