#!/bin/sh
# Mac Catalyst slice of the DEBUGMASSIF xcframework: see build-plugin-spm.sh.
exec "$(dirname "$0")/build-plugin-spm.sh" catalyst "$@"
