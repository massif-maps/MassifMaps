#!/usr/bin/env python3
"""Check a release tag against the repo's tags of its prefix (vX.Y.Z, massif-styles-vX.Y.Z), and print
the release it follows.

    python3 scripts/release-tags.py v6.1.0-rc.1 >> "$GITHUB_OUTPUT"    # previous=v6.0.2
    python3 scripts/release-tags.py massif-styles-v1.3.0                 # previous=massif-styles-v1.2.1

Fails when a higher tag of the same prefix exists. `previous` is the last final release below the tag, so a release
candidate's notes, and the final release after it, both list everything since that release.
"""
import re
import subprocess
import sys

SEMVER = re.compile(r'(massif-styles-v|v)(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?')


def key(tag):
    m = SEMVER.fullmatch(tag)
    if not m:
        return None
    pre = m.group(5)
    # semver: a prerelease sorts below its release, numeric identifiers below alphanumeric ones
    pre_key = (0, tuple((0, int(p), '') if p.isdigit() else (1, 0, p) for p in pre.split('.'))) if pre else (1, ())
    return (m.group(1),) + tuple(int(g) for g in m.groups()[1:4]) + (pre_key,)


def main():
    tag = sys.argv[1]
    if key(tag) is None:
        sys.exit('::error::%s is not [massif-styles-]vMAJOR.MINOR.PATCH[-PRERELEASE]' % tag)
    prefix = key(tag)[0]
    tags = [t for t in subprocess.run(['git', 'tag', '--list', prefix + '*'], capture_output=True, text=True,
                                      check=True).stdout.split() if key(t) and key(t)[0] == prefix]
    higher = sorted((t for t in tags if key(t) > key(tag)), key=key)
    if higher:
        sys.exit('::error::%s is not the latest release, %s exists' % (tag, higher[-1]))
    finals = [t for t in tags if not SEMVER.fullmatch(t).group(5) and key(t) < key(tag)]
    if not finals:
        sys.exit('::error::no final release below %s' % tag)
    print('previous=%s' % max(finals, key=key))


if __name__ == '__main__':
    main()
