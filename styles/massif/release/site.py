"""The website's copy of the styles: every massif-styles-v* release's MapLibre flavour, CartoCSS project
(carto/, what the live examples load) and screenshots under v<version>/, the newest also at the root,
and versions.json. With no release yet, the root is built from this checkout.

    python3 styles/massif/site.py website/static/styles/massif [--repo massif-maps/MassifMaps]

Run by docs.yml; needs `gh` with a token. See docs/contributing/massif-style-release.md.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PREFIX = 'massif-styles-v'
SITE = 'https://massif-maps.github.io/MassifMaps/styles/massif'


def releases(repo):
    out = subprocess.run(['gh', 'release', 'list', '--repo', repo, '--limit', '200', '--json', 'tagName,isDraft,isPrerelease'],
                         capture_output=True, text=True, check=True).stdout
    tags = [r['tagName'] for r in json.loads(out) if r['tagName'].startswith(PREFIX) and not r['isDraft']]
    key = lambda v: [int(p) if p.isdigit() else p for p in v.replace('-', '.').split('.')]
    return sorted((t[len(PREFIX):] for t in tags), key=key, reverse=True)


def unpack(repo, version, dest):
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['gh', 'release', 'download', PREFIX + version, '--repo', repo, '--dir', tmp,
                        '--pattern', 'massif-maplibre-*.zip', '--pattern', 'massif-cartocss-[0-9]*.zip',
                        '--pattern', 'massif-screenshots-*.zip'], check=True)
        for f in os.listdir(tmp):
            target = (dest if 'maplibre' in f else os.path.join(dest, 'carto') if 'cartocss' in f
                      else os.path.join(dest, 'screenshots'))
            zipfile.ZipFile(os.path.join(tmp, f)).extractall(target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('out')
    parser.add_argument('--repo', default='massif-maps/MassifMaps')
    args = parser.parse_args()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    try:
        versions = releases(args.repo)
    except (subprocess.CalledProcessError, FileNotFoundError) as error:
        print('no release list (%s): building from source' % error, file=sys.stderr)
        versions = []
    for version in versions:
        unpack(args.repo, version, os.path.join(out, 'v' + version))
    if versions:
        latest = os.path.join(out, 'v' + versions[0])
        for f in os.listdir(latest):
            src = os.path.join(latest, f)
            (shutil.copytree if os.path.isdir(src) else shutil.copy)(src, os.path.join(out, f))
    else:
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([sys.executable, os.path.join(HERE, 'release.py'), '0.0.0-dev', '--only', 'maplibre',
                            '--out', tmp, '--base-url', SITE], check=True)
            shutil.copytree(os.path.join(tmp, 'maplibre'), out, dirs_exist_ok=True)
        shutil.copytree(os.path.join(os.path.dirname(HERE), 'carto'), os.path.join(out, 'carto'), dirs_exist_ok=True)
    json.dump({'latest': versions[0] if versions else None, 'versions': versions},
              open(os.path.join(out, 'versions.json'), 'w'), indent=2)
    print('styles/massif: %s' % (', '.join(versions) or 'built from source'))


if __name__ == '__main__':
    main()
