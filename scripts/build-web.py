import os
import sys
import argparse

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build')))
from sdk_build_utils import *

def detectEmscripten(args):
  if args.emsdkpath != 'auto':
    return args.emsdkpath
  emsdkPath = os.environ.get('EMSDK', None)
  if emsdkPath:
    return emsdkPath
  # emcc on PATH means emsdk_env.sh was sourced, and it sits next to the toolchain file
  for path in os.environ.get('PATH', '').split(os.pathsep):
    if os.path.exists(os.path.join(path, 'emcc')):
      return os.path.abspath(os.path.join(path, '..', '..'))
  return None

def buildWebLib(args):
  version = getVersion(args.buildversion, args.buildnumber) if args.configuration == 'Release' else 'Devel'
  baseDir = getBaseDir()
  buildDir = getBuildDir('web')
  distDir = getDistDir('web')
  defines = ["-D%s" % define for define in args.defines.split(';') if define]
  options = ["-D%s" % option for option in args.cmakeoptions.split(';') if option]
  resetBuildDirOnGeneratorChange(args, buildDir)

  toolchainFile = '%s/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake' % args.emsdkpath
  if not os.path.exists(toolchainFile):
    print('Failed to find the emscripten CMake toolchain at %s. Use --emsdk to specify the emsdk location' % toolchainFile)
    return False

  if not cmake(args, buildDir, options + getGeneratorOptions(args) + getCCacheOptions(args) + [
    "-DCMAKE_TOOLCHAIN_FILE='%s'" % toolchainFile,
    "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON",
    "-DCMAKE_BUILD_TYPE=%s" % args.configuration,
    "-DSINGLE_LIBRARY:BOOL=ON",
    "-DBUILD_WEB_DEMO:BOOL=%s" % ('ON' if args.builddemo else 'OFF'),
    "-DSDK_CPP_DEFINES=%s" % " ".join(defines),
    "-DSDK_VERSION='%s'" % version,
    "-DSDK_PLATFORM='Web'",
    '%s/scripts/build' % baseDir
  ]):
    return False
  if not cmake(args, buildDir, [
    '--build', '.',
    '--parallel', str(os.cpu_count()),
    '--config', args.configuration,
  ]):
    return False
  if not (makedirs(distDir) and copyfile('%s/libmassif.a' % buildDir, '%s/libmassif.a' % distDir)):
    return False
  if args.builddemo:
    # website/static/preview is where the Docusaurus /preview page loads the module from, and is
    # gitignored: the binary is a build artefact, downloaded by the docs workflow.
    if args.website and not makedirs('%s/website/static/preview' % baseDir):
      return False
    for name in ['massif-demo.mjs', 'massif-demo.wasm', 'massif-demo.data']:
      # .data exists only when web/demo/fonts was there to preload.
      if not os.path.exists('%s/%s' % (buildDir, name)):
        continue
      if not copyfile('%s/%s' % (buildDir, name), '%s/%s' % (baseDir + '/web/demo', name)):
        return False
      if args.website and not copyfile('%s/%s' % (buildDir, name),
                                       '%s/website/static/preview/%s' % (baseDir, name)):
        return False
  return True

parser = argparse.ArgumentParser()
parser.add_argument('--profile', dest='profile', default='lite', type=validProfile, help='Build profile')
parser.add_argument('--emsdk', dest='emsdkpath', default='auto', help="Emscripten SDK path, 'auto' to read $EMSDK or find emcc on PATH")
parser.add_argument('--defines', dest='defines', default='', help='Defines for compilation')
parser.add_argument('--cmake', dest='cmake', default='cmake', help='CMake executable')
parser.add_argument('--cmake-options', dest='cmakeoptions', default='', help='CMake options')
parser.add_argument('--make', dest='make', default='make', help='Make executable, used only when no ninja is available')
parser.add_argument('--ninja', dest='ninja', default='auto', help="Ninja executable, 'auto' to detect one, 'none' to build with make")
parser.add_argument('--ccache', dest='ccache', default='auto', help="Ccache executable, 'auto' to detect one, 'none' to compile without a launcher")
parser.add_argument('--configuration', dest='configuration', default='Release', choices=['Release', 'RelWithDebInfo', 'Debug'], help='Configuration')
parser.add_argument('--build-number', dest='buildnumber', default='', help='Build sequence number, goes to version str')
parser.add_argument('--build-version', dest='buildversion', default='%s-devel' % SDK_VERSION, help='Build version, goes to distributions')
parser.add_argument('--build-demo', dest='builddemo', default=False, action='store_true', help='Also link web/demo into web/demo/massif-demo.mjs')
parser.add_argument('--website', dest='website', default=False, action='store_true', help='Also copy the demo module into website/static/preview for the /preview page')
args = parser.parse_args()
args.defines += ';' + getProfile(args.profile).get('defines', '')
args.cmakeoptions += ';' + getProfile(args.profile).get('cmake-options', '')

args.emsdkpath = detectEmscripten(args)
if not args.emsdkpath:
  print('Failed to find the emscripten SDK. Source emsdk_env.sh or use --emsdk to specify its location')
  sys.exit(-1)

if not checkExecutable(args.cmake, '--help'):
  print('Failed to find CMake executable. Use --cmake to specify its location')
  sys.exit(-1)

resolveBuildTools(args)

if not args.ninjapath and not checkExecutable(args.make, '--help'):
  print('Failed to find ninja or make executable. Use --ninja or --make to specify its location')
  sys.exit(-1)

if not buildWebLib(args):
  sys.exit(-1)
