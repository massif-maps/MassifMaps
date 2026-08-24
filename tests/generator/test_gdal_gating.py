#!/usr/bin/env python3
"""
The GDAL/OGR classes only exist when the build defines _MASSIF_GDAL_SUPPORT, and the facade must
follow: a table generated without the profile must not carry specs for classes the build did not
compile, and one generated WITH it must carry all of them.

That gating is the whole reason these classes were dropped once already - the guard existed but no
profile ever defined it, so the code was unreachable. This test fails if either direction breaks.

The generator reads .i and .h files as TEXT, so this needs no GDAL headers and no link.
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

BASE = Path(__file__).resolve().parents[2]
GEN = BASE / "scripts" / "gen-api-tables.py"

MODULES = [
    "all/modules/datasources/GDALRasterTileDataSource.i",
    "all/modules/datasources/OGRVectorDataBase.i",
    "all/modules/datasources/OGRVectorDataSource.i",
    "all/modules/styles/StyleSelector.i",
    "all/modules/styles/StyleSelectorBuilder.i",
    # Not GDAL, and always in: the generator refuses a run whose modules are ALL gated out, so
    # without this the off-case would fail the same way a misconfigured --sourcedir does.
    "all/modules/datasources/HTTPTileDataSource.i",
]

# The control: present under both sets of defines, so a missing GDAL spec means gating and not a
# generator run that quietly did nothing.
CONTROL = "http"

# spec name -> the class it must build
EXPECTED = {
    "gdal": "massif::GDALRasterTileDataSource",
    "ogr": "massif::OGRVectorDataSource",
    "ogr-database": "massif::OGRVectorDataBase",
    "style-selector": "massif::StyleSelectorBuilder",
}


def specs_for(defines):
    """Every (name, class) the generator emits for the GDAL modules under these defines."""
    with tempfile.TemporaryDirectory() as out:
        schema = Path(out) / "massif-api.json"
        subprocess.run(
            [
                sys.executable, str(GEN),
                "--defines", defines,
                "--modules", ",".join(str(BASE / m) for m in MODULES),
                "--cppdir", str(BASE / "all" / "native"),
                "--outdir", out,
                "--schema", str(schema),
            ],
            check=True, capture_output=True,
        )
        return {entry["type"]: entry["cppClass"] for entry in json.loads(schema.read_text())["specs"]}


def main():
    failures = []

    on = specs_for("_MASSIF_GDAL_SUPPORT;_MASSIF_OFFLINE_SUPPORT")
    for name, cls in EXPECTED.items():
        if name not in on:
            failures.append("with _MASSIF_GDAL_SUPPORT: spec '%s' is missing" % name)
        elif on[name] != cls:
            failures.append("spec '%s' builds %s, expected %s" % (name, on[name], cls))

    off = specs_for("_MASSIF_OFFLINE_SUPPORT")
    if CONTROL not in off:
        failures.append("control spec '%s' is missing - the off-case run produced nothing" % CONTROL)
    for name in EXPECTED:
        if name in off:
            failures.append("without _MASSIF_GDAL_SUPPORT: spec '%s' leaked into the table" % name)

    for failure in failures:
        print("FAIL: %s" % failure)
    print("%d/%d checks passed" % (len(EXPECTED) * 2 + 1 - len(failures), len(EXPECTED) * 2 + 1))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
