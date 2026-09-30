"""
gen-api-tables.py must skip a module unless EVERY define on its guard line is set: the Valhalla
routing module is guarded by routing AND valhalla, and a routing-only build failed to compile the
Valhalla constructors it was handed. Covers the facade generator only, not swigpp-*.py (needs SWIG).
"""
import os
import subprocess
import sys
import tempfile

base = sys.argv[1]
scripts = os.path.join(base, 'scripts')
modules = ','.join(os.path.join(base, 'all', 'modules', m)
                   for m in ['routing/ValhallaOfflineRoutingService.i', 'datasources/TileDataSource.i'])
failures = 0


def constructors(defines):
    with tempfile.TemporaryDirectory() as out:
        subprocess.run([sys.executable, 'gen-api-tables.py', '--modules', modules, '--defines', defines,
                        '--cppdir', os.path.join(base, 'all', 'native'), '--outdir', out],
                       cwd=scripts, check=True, stdout=subprocess.DEVNULL)
        with open(os.path.join(out, 'SpecConstructors.inc')) as f:
            return f.read()


def check(condition, claim):
    global failures
    print('%s: %s' % ('ok' if condition else 'FAIL', claim))
    failures += 0 if condition else 1


check('ValhallaOfflineRoutingService' not in constructors('_MASSIF_ROUTING_SUPPORT;_MASSIF_OFFLINE_SUPPORT'),
      'a routing build without valhalla gets no Valhalla constructor')
check('ValhallaOfflineRoutingService' in constructors(
          '_MASSIF_ROUTING_SUPPORT;_MASSIF_OFFLINE_SUPPORT;_MASSIF_VALHALLA_ROUTING_SUPPORT'),
      'a build with every define on the guard line does get it')
sys.exit(1 if failures else 0)
