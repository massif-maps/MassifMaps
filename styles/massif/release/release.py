"""Package a Massif release: every flavour as a folder and a zip, and the npm package; and, from a
machine rather than CI, publish it.

    python3 styles/massif/release/release.py 1.2.0 [--only FLAVOUR]... [--screenshots]
    python3 styles/massif/release/release.py 1.2.0-rc.1 --npm next [--dry-run] [--github]

Flavours (docs/styles/massif.mdx says which to pick):
  maplibre                    the five MapLibre/Mapbox GL styles and their sprite
  cartocss                    the SDK's CartoCSS project, sprite icons
  cartocss-compiled           the same compiled to mapnik XML, one file per variant
  cartocss-iconfont           the CartoCSS project with POI icons drawn from a font
  cartocss-iconfont-compiled  that compiled

Needs a built tools/style-cli (and its wasm for the compiled flavours), tools/style-sprite's
node_modules, and for --screenshots this folder's (playwright). How a release is cut, locally or by
release-styles.yml: docs/contributing/massif-style-release.md.
"""
import argparse
import copy
import json
import os
import shutil
import subprocess
import sys
import zipfile

RELEASE = os.path.dirname(os.path.abspath(__file__))
HERE = os.path.dirname(RELEASE)
sys.path.insert(0, HERE)

import build  # noqa: E402

ROOT = os.path.join(HERE, '..', '..')
SITE = 'https://massif-maps.github.io/MassifMaps/styles/massif'
TAG_PREFIX = 'massif-styles-v'
ICONFONT = os.path.join(ROOT, 'tools', 'style-sprite', 'iconfont.mjs')
FACE = 'MassifIcons'
# the sources anyone can reach; the others are archives an app serves itself (docs/styles/massif-maplibre.md)
PUBLIC_SOURCES = {'openmaptiles', 'dem'}
FLAVOURS = ['maplibre', 'cartocss', 'cartocss-compiled', 'cartocss-iconfont', 'cartocss-iconfont-compiled']


# one transparent pixel: MapLibre throws on a raster source with no tile URL and draws nothing at all
# when a raster layer reads GeoJSON, so a raster stand-in is one z0 tile that costs no request
BLANK_TILE = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII='


def placeholder(source):
    """An empty stand-in with the source's name, so the layers reading it still validate and draw nothing
    until an app puts the real source back."""
    if source['type'] == 'raster':
        return {'type': 'raster', 'tiles': [BLANK_TILE], 'tileSize': 256, 'maxzoom': 0}
    return {'type': 'geojson', 'data': {'type': 'FeatureCollection', 'features': []}}


def web_style(doc, version, base_url):
    doc = copy.deepcopy(doc)
    private = {name: src for name, src in doc['sources'].items() if name not in PUBLIC_SOURCES}
    for name, src in private.items():
        doc['sources'][name] = placeholder(src)
    if base_url:
        doc['sprite'] = base_url.rstrip('/') + '/sprite/sprite'
    doc.setdefault('metadata', {}).update({
        'massif:version': version,
        'massif:placeholder-sources': {name: src.get('url', src.get('tiles')) for name, src in private.items()}})
    return doc


def maplibre(out, version, base_url):
    os.makedirs(out)
    for name in build.VARIANTS:
        doc = json.load(open(os.path.join(HERE, name + '.json')))
        json.dump(web_style(doc, version, base_url), open(os.path.join(out, name + '.json'), 'w'),
                  separators=(',', ':'), ensure_ascii=False)
    shutil.copytree(os.path.join(HERE, 'sprite'), os.path.join(out, 'sprite'))


def iconfont(tmp):
    subprocess.run(['node', ICONFONT, os.path.join(HERE, 'sprite-src', 'poi'), tmp, FACE, '--alias', '-poi'], check=True)
    # 27: the glyph's height in the 48 px badge (iconfont.mjs's box), so icon-size scales it as the sprite
    return ['--icon-font', FACE, '--icon-font-map', os.path.join(tmp, FACE + '.json'), '--icon-font-size', '27',
            '--fonts', tmp]


def compiled(project, out):
    """Every variant and example of a CartoCSS project, compiled; the files it reads sit beside."""
    shutil.copytree(project, out, ignore=shutil.ignore_patterns('*.mss', '*.json'))
    shutil.copy(os.path.join(project, 'legend.json'), out)
    for f in sorted(os.listdir(project)):
        if f.endswith('.json') and f not in ('project.json', 'legend.json'):
            subprocess.run(['node', build.CLI, 'css2xml', f, os.path.join(out, f[:-5] + '.xml')], cwd=project,
                           check=True, stderr=subprocess.DEVNULL)


def npm_package(dist, version):
    pkg = os.path.join(dist, 'npm')
    os.makedirs(pkg)
    for flavour in FLAVOURS:
        if os.path.isdir(os.path.join(dist, flavour)):
            shutil.copytree(os.path.join(dist, flavour), os.path.join(pkg, flavour))
    json.dump({
        'name': '@massif-maps/styles',
        'version': version,
        'description': 'The Massif map styles: streets, outdoor, topo, hybrid and e-ink, for MapLibre and the Massif Maps SDK',
        'license': 'MIT-0',
        'repository': {'type': 'git', 'url': 'https://github.com/massif-maps/MassifMaps', 'directory': 'styles/massif'},
        'homepage': 'https://massif-maps.github.io/MassifMaps/docs/styles/massif',
        'keywords': ['map', 'style', 'maplibre', 'mapbox', 'cartocss', 'openmaptiles'],
        'files': FLAVOURS + ['README.md', 'LICENSE'],
    }, open(os.path.join(pkg, 'package.json'), 'w'), indent=2)
    shutil.copy(os.path.join(RELEASE, 'npm-readme.md'), os.path.join(pkg, 'README.md'))
    shutil.copy(os.path.join(HERE, 'LICENSE'), pkg)


def zip_dir(folder, path):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for base, _, files in os.walk(folder):
            for f in sorted(files):
                full = os.path.join(base, f)
                z.write(full, os.path.relpath(full, folder))


def publish(dist, version, npm_tag, github, dry_run):
    if npm_tag:
        subprocess.run(['npm', 'publish', '--access', 'public', '--tag', npm_tag, *(['--dry-run'] if dry_run else [])],
                       cwd=os.path.join(dist, 'npm'), check=True)
    if github and not dry_run:
        assets = sorted(os.path.join(dist, f) for f in os.listdir(dist) if f.endswith('.zip'))
        subprocess.run(['gh', 'release', 'create', TAG_PREFIX + version, '--repo', 'massif-maps/MassifMaps',
                        '--title', 'Massif styles ' + version, '--notes', 'Massif map styles %s: %s/v%s' % (version, SITE, version),
                        *(['--prerelease'] if '-' in version else []), *assets], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('version')
    parser.add_argument('--out', default=os.path.join(HERE, 'dist'))
    parser.add_argument('--base-url', help='where the maplibre flavour is served, for its absolute sprite URL '
                        '(default: the website\'s v<version> folder; "" keeps it relative)')
    parser.add_argument('--only', choices=FLAVOURS, action='append')
    parser.add_argument('--screenshots', action='store_true', help='one JPEG per variant, zipped (playwright)')
    parser.add_argument('--npm', metavar='TAG', help='publish dist/npm under that dist-tag: next to try a release, latest for real')
    parser.add_argument('--github', action='store_true', help='create the %s<version> GitHub release (gh)' % TAG_PREFIX)
    parser.add_argument('--dry-run', action='store_true', help='npm publish --dry-run, and no GitHub release')
    args = parser.parse_args()
    flavours = args.only or FLAVOURS
    base_url = SITE + '/v' + args.version if args.base_url is None else args.base_url
    dist = os.path.abspath(args.out)
    if os.path.exists(dist):
        shutil.rmtree(dist)
    os.makedirs(dist)

    build.main([])
    if 'maplibre' in flavours or args.screenshots:
        maplibre(os.path.join(dist, 'maplibre'), args.version, base_url)
    if any(f.startswith('cartocss') and 'iconfont' not in f for f in flavours):
        print(build.convert(os.path.join(dist, 'cartocss')))
    if any('iconfont' in f for f in flavours):
        print(build.convert(os.path.join(dist, 'cartocss-iconfont'), iconfont(os.path.join(dist, '.iconfont'))))
    for flavour in ('cartocss', 'cartocss-iconfont'):
        if flavour + '-compiled' in flavours:
            compiled(os.path.join(dist, flavour), os.path.join(dist, flavour + '-compiled'))
    if args.screenshots:
        shots = os.path.join(dist, 'screenshots')
        subprocess.run(['node', os.path.join(RELEASE, 'screenshots.mjs'), os.path.join(dist, 'maplibre'), shots], check=True)
        zip_dir(shots, os.path.join(dist, 'massif-screenshots-%s.zip' % args.version))
    for flavour in os.listdir(dist):
        if flavour not in flavours and flavour != 'screenshots' and os.path.isdir(os.path.join(dist, flavour)):
            shutil.rmtree(os.path.join(dist, flavour))
    for flavour in flavours:
        zip_dir(os.path.join(dist, flavour), os.path.join(dist, 'massif-%s-%s.zip' % (flavour, args.version)))
    npm_package(dist, args.version)
    print('\n'.join(sorted(f for f in os.listdir(dist))))
    publish(dist, args.version, args.npm, args.github, args.dry_run)


if __name__ == '__main__':
    main()
