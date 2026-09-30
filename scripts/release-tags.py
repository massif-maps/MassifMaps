#!/usr/bin/env python3
"""Check a release tag against the repo's vX.Y.Z tags, and print the release it follows.

    python3 scripts/release-tags.py v7.0.0-rc.1 >> "$GITHUB_OUTPUT"    # previous=v6.0.2

Fails when a higher v-tag exists. `previous` is the last final release below the tag, so a release
candidate's notes, and the final release after it, both list everything since that release.
"""
import re
import subprocess
import sys

SEMVER = re.compile(r'v(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?')


def key(tag):
    m = SEMVER.fullmatch(tag)
    if not m:
        return None
    pre = m.group(4)
    # semver: a prerelease sorts below its release, numeric identifiers below alphanumeric ones
    pre_key = (0, tuple((0, int(p), '') if p.isdigit() else (1, 0, p) for p in pre.split('.'))) if pre else (1, ())
    return tuple(int(g) for g in m.groups()[:3]) + (pre_key,)


def main():
    tag = sys.argv[1]
    if key(tag) is None:
        sys.exit('::error::%s is not vMAJOR.MINOR.PATCH[-PRERELEASE]' % tag)
    tags = [t for t in subprocess.run(['git', 'tag', '--list', 'v*'], capture_output=True, text=True,
                                      check=True).stdout.split() if key(t)]
    higher = sorted((t for t in tags if key(t) > key(tag)), key=key)
    if higher:
        sys.exit('::error::%s is not the latest release, %s exists' % (tag, higher[-1]))
    finals = [t for t in tags if not SEMVER.fullmatch(t).group(4) and key(t) < key(tag)]
    if not finals:
        sys.exit('::error::no final release below %s' % tag)
    print('previous=%s' % max(finals, key=key))


if __name__ == '__main__':
    main()
