#!/usr/bin/env python3
"""Release notes from the conventional commits between two tags, as Markdown on stdout.

    python3 scripts/release-notes.py v6.0.2 v6.1.0-rc.3 > release_body.md
    python3 scripts/release-notes.py --product styles massif-styles-v1.2.1 HEAD
    python3 scripts/release-notes.py --changelog CHANGELOG.md v6.0.2 v6.1.0

One line per squash commit - the PR title is the changelog entry. Breaking changes keep the first
paragraph of their BREAKING CHANGE footer; everything else is the subject alone, which is what keeps
the body under GitHub's 125,000 characters (the full commit bodies ran past 230 KB).

A commit belongs to a product by the paths it touches: one touching only the styles (and docs) is
left out of the SDK's notes, and a PR touching both lands in both.
"""
import argparse
import datetime
import re
import subprocess

REPO = 'https://github.com/massif-maps/MassifMaps'
SECTIONS = [('feat', 'New features'), ('fix', 'Bug fixes')]
HEADER = re.compile(r'^(\w+)(?:\(([^)]*)\))?(!)?: (.+)$')
STYLES = ('styles/massif/', 'tools/style-sprite/', 'tools/icon-font/')
NEUTRAL = ('docs/', 'website/', 'tests/', '.claude/', '.github/', 'tools/style-preview/', 'CLAUDE.md', 'CHANGELOG.md')


def commits(previous, tag):
    out = subprocess.run(['git', 'log', '--name-only', '--format=%x01%H%x00%B%x00', f'{previous}..{tag}'],
                         capture_output=True, text=True, check=True).stdout
    for entry in out.split('\x01'):
        sha, _, rest = entry.partition('\x00')
        message, _, paths = rest.partition('\x00')
        if sha:
            yield sha, message.strip(), paths.split()


def products(paths):
    styles = any(p.startswith(STYLES) for p in paths)
    sdk = any(not p.startswith(STYLES + NEUTRAL) for p in paths)
    return {'styles'} if styles and not sdk else {'sdk', 'styles'} if styles else {'sdk'}


def line(sha, scope, subject):
    subject = re.sub(r'\(#(\d+)\)$', lambda m: f'([#{m[1]}]({REPO}/pull/{m[1]}))', subject.strip())
    return f"- {f'**{scope}:** ' if scope else ''}{subject} ([`{sha[:7]}`]({REPO}/commit/{sha}))"


def notes(previous, tag, product):
    breaking, grouped = [], {kind: [] for kind, _ in SECTIONS}
    for sha, message, paths in commits(previous, tag):
        match = HEADER.match(message.splitlines()[0])
        if not match or product not in products(paths):
            continue
        kind, scope, bang, subject = match.groups()
        footer = re.search(r'^BREAKING[ -]CHANGE: (.+?)(?:\n\s*\n|\Z)', message, re.M | re.S)
        if bang or footer:
            note = ' '.join(footer[1].split()) if footer else ''
            breaking.append(line(sha, scope, subject) + (f'\n  {note}' if note else ''))
        if kind in grouped:
            grouped[kind].append(line(sha, scope, subject))
    parts = [('Breaking changes', breaking)] + [(title, grouped[kind]) for kind, title in SECTIONS]
    return '\n\n'.join(f'### {title}\n\n' + '\n'.join(lines) for title, lines in parts if lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--product', choices=['sdk', 'styles'], default='sdk')
    parser.add_argument('--changelog', help='prepend the notes to this file as a "## [<tag>]" entry')
    parser.add_argument('previous')
    parser.add_argument('tag')
    args = parser.parse_args()
    body = notes(args.previous, args.tag, args.product)
    if not args.changelog:
        print(body)
        return
    with open(args.changelog) as f:
        text = f.read()
    entry = f'## [{args.tag}] - {datetime.date.today().isoformat()}\n\n{body}\n\n'
    at = text.find('\n## [') + 1 or len(text)
    with open(args.changelog, 'w') as f:
        f.write(text[:at] + entry + text[at:])


if __name__ == '__main__':
    main()
