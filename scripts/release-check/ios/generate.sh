#!/bin/sh
# ./generate.sh [version] [product]   product for MapCheck: MassifMaps (full), MassifMapsCore, MassifMapsLite
cd "$(dirname "$0")"
MASSIF_VERSION="${1:-6.1.0-rc.2}" MASSIF_PRODUCT="${2:-MassifMaps}" xcodegen generate
