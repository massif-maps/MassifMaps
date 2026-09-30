#!/usr/bin/env python3
"""Release notes from the conventional commits between two tags, as Markdown on stdout.

    python3 scripts/release-notes.py v6.0.2 v6.1.0-rc.3 > release_body.md

One line per squash commit - the PR title is the changelog entry. Breaking changes keep the first
paragraph of their BREAKING CHANGE footer; everything else is the subject alone, which is what keeps
the body under GitHub's 125,000 characters (the full commit bodies ran past 230 KB).
"""
import re
import subprocess
import sys

REPO = 'https://github.com/massif-maps/MassifMaps'
SECTIONS = [('feat', 'New features'), ('fix', 'Bug fixes')]
HEADER = re.compile(r'^(\w+)(?:\(([^)]*)\))?(!)?: (.+)$')


def commits(previous, tag):
    out = subprocess.run(['git', 'log', '--format=%H%x00%B%x01', f'{previous}..{tag}'],
                         capture_output=True, text=True, check=True).stdout
    for entry in out.split('\x01'):
        sha, _, message = entry.strip('\n').partition('\x00')
        if sha:
            yield sha, message.strip()


def line(sha, scope, subject):
    subject = re.sub(r'\(#(\d+)\)$', lambda m: f'([#{m[1]}]({REPO}/pull/{m[1]}))', subject.strip())
    return f"- {f'**{scope}:** ' if scope else ''}{subject} ([`{sha[:7]}`]({REPO}/commit/{sha}))"


def main():
    previous, tag = sys.argv[1], sys.argv[2]
    breaking, grouped = [], {kind: [] for kind, _ in SECTIONS}
    for sha, message in commits(previous, tag):
        match = HEADER.match(message.splitlines()[0])
        if not match:
            continue
        kind, scope, bang, subject = match.groups()
        footer = re.search(r'^BREAKING[ -]CHANGE: (.+?)(?:\n\s*\n|\Z)', message, re.M | re.S)
        if bang or footer:
            note = ' '.join(footer[1].split()) if footer else ''
            breaking.append(line(sha, scope, subject) + (f'\n  {note}' if note else ''))
        if kind in grouped:
            grouped[kind].append(line(sha, scope, subject))
    parts = [('Breaking changes', breaking)] + [(title, grouped[kind]) for kind, title in SECTIONS]
    print('\n\n'.join(f'### {title}\n\n' + '\n'.join(lines) for title, lines in parts if lines))


if __name__ == '__main__':
    main()
