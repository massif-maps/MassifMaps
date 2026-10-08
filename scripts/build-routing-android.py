import os
import sys
import shutil
import argparse
from build.sdk_build_utils import *

ANDROID_ABIS = ['armeabi-v7a', 'arm64-v8a', 'x86_64']

def gradle(args, dir, *cmdArgs):
    return execute(args.gradle, dir, *cmdArgs)

def detectAndroidAPIs(args):
    api32, api64 = None, None
    with open('%s/meta/platforms.json' % args.androidndkpath, 'rb') as f:
        platforms = json.load(f)
        minapi = platforms.get('min', 1)
        maxapi = platforms.get('max', 0)
        for api in range(minapi, maxapi + 1):
            if api >= 11:
                api32 = min(api32 or api, api)
            if api >= 21:
                api64 = min(api64 or api, api)
    return api32, api64

def buildRoutingSO(args, abi):
    version = getVersion(args.buildversion, args.buildnumber) if args.configuration == 'Release' else 'Devel'
    baseDir = getBaseDir()
    buildDir = getBuildDir('routing-android', abi)
    distDir  = getDistDir('routing-android')
    defines  = ["-D%s" % d for d in args.defines.split(';') if d]
    options  = ["-D%s" % o for o in args.cmakeoptions.split(';') if o]

    api32, api64 = detectAndroidAPIs(args)
    if api32 is None or api64 is None:
        print('Failed to detect available platform APIs')
        return False
    print('Using API-%d for 32-bit builds, API-%d for 64-bit builds' % (api32, api64))
    resetBuildDirOnGeneratorChange(args, buildDir)

    if not cmake(args, buildDir, options + defines + getGeneratorOptions(args) + getCCacheOptions(args) + [
        "-DCMAKE_TOOLCHAIN_FILE='%s/build/cmake/android.toolchain.cmake'" % args.androidndkpath,
        "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON",
        "-DCMAKE_SYSTEM_NAME=Android",
        "-DCMAKE_BUILD_TYPE=%s" % args.configuration,
        "-DCMAKE_ANDROID_NDK='%s'" % args.androidndkpath,
        "-DCMAKE_ANDROID_ARCH_ABI='%s'" % abi,
        '-DSINGLE_LIBRARY:BOOL=ON',
        "-DANDROID_STL='c++_static'",
        "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
        "-DANDROID_NDK='%s'" % args.androidndkpath,
        "-DANDROID_ABI='%s'" % abi,
        "-DANDROID_PLATFORM='android-%d'" % (api64 if '64' in abi else api32),
        "-DANDROID_ARM_NEON:BOOL=%s" % ('ON' if abi == 'arm64-v8a' or api32 >= 19 else 'OFF'),
        "-DSDK_CPP_DEFINES=%s" % " ".join(defines),
        "-DSDK_VERSION='%s'" % version,
        "-DSDK_PLATFORM='Android'",
        "-DSDK_ANDROID_ABI='%s'" % abi,
        '%s/scripts/routing' % baseDir
    ]):
        return False

    if not cmake(args, buildDir, [
        '--build', '.',
        '--parallel', str(os.cpu_count()),
        '--config', args.configuration,
    ]):
        return False

    soFile = '%s/libvalhalla_routing.so' % buildDir
    destDir = '%s/%s' % (distDir, abi)
    return makedirs(destDir) and copyfile(soFile, '%s/libvalhalla_routing.so' % destDir)


def buildRoutingAAR(args):
    baseDir  = getBaseDir()
    buildDir = getBuildDir('routing-android-aar')
    distDir  = getDistDir('routing-android')
    version  = args.buildversion
    distName = getRoutingAndroidAarDistName(version)

    if not gradle(args, '%s/scripts' % baseDir,
        '-p', 'routing-android',
        '--project-cache-dir', buildDir,
        '--gradle-user-home', '%s/gradle' % buildDir,
        '-Dbuild-version=%s' % version,
        'assembleRelease',
    ):
        return False

    candidates = [
        '%s/outputs/aar/routing-android-release.aar'  % buildDir,
        '%s/outputs/aar/valhalla-routing-release.aar' % buildDir,
        '%s/outputs/aar/android-release.aar'          % buildDir,
    ]
    aarFile = next((p for p in candidates if os.path.exists(p)), None)
    if aarFile is None:
        for root, _, files in os.walk(buildDir):
            for f in files:
                if f.endswith('.aar'):
                    aarFile = os.path.join(root, f)
                    break
            if aarFile:
                break
    if aarFile is None:
        print("ERROR: Could not find built AAR under %s" % buildDir)
        return False

    return makedirs(distDir) and copyfile(aarFile, '%s/%s' % (distDir, distName))


parser = argparse.ArgumentParser(description="Build Valhalla routing library for Android (.aar)")
parser.add_argument('--android-abi',      dest='androidabi',     default=[],
                    choices=ANDROID_ABIS + ['all'], action='append',
                    help='Android target ABI(s). Can be specified multiple times.')
parser.add_argument('--android-ndk-path', dest='androidndkpath', default='auto',   help='Android NDK path')
parser.add_argument('--android-sdk-path', dest='androidsdkpath', default='auto',   help='Android SDK path')
parser.add_argument('--defines',          dest='defines',        default='',       help='C++ preprocessor defines (semicolon-separated)')
parser.add_argument('--javac',            dest='javac',          default='javac',  help='Java compiler executable (accepted, not used)')
parser.add_argument('--make',             dest='make',           default='make',   help='Make executable, used only when no ninja is available')
parser.add_argument('--ninja',            dest='ninja',          default='auto',   help="Ninja executable, 'auto' to detect one, 'none' to build with make")
parser.add_argument('--ccache',           dest='ccache',         default='auto',   help="Ccache executable, 'auto' to detect one, 'none' to compile without a launcher")
parser.add_argument('--cmake',            dest='cmake',          default='cmake',  help='CMake executable')
parser.add_argument('--cmake-options',    dest='cmakeoptions',   default='',       help='CMake options (semicolon-separated)')
parser.add_argument('--gradle',           dest='gradle',         default='gradle', help='Gradle executable')
parser.add_argument('--configuration',    dest='configuration',  default='Release',
                    choices=['Release', 'RelWithDebInfo', 'Debug'])
parser.add_argument('--build-number',     dest='buildnumber',    default='',       help='Build sequence number')
parser.add_argument('--build-version',    dest='buildversion',   default='%s-devel' % SDK_VERSION,
                    help='Build version, embedded in the dist file name')
args = parser.parse_args()

if 'all' in args.androidabi or args.androidabi == []:
    args.androidabi = ANDROID_ABIS

if args.androidsdkpath == 'auto':
    args.androidsdkpath = os.environ.get('ANDROID_HOME', None)
    if args.androidsdkpath is None:
        print("ANDROID_HOME variable not set")
        sys.exit(-1)

if args.androidndkpath == 'auto':
    args.androidndkpath = os.environ.get('ANDROID_NDK_HOME', None)
    if args.androidndkpath is None:
        args.androidndkpath = os.path.join(args.androidsdkpath, 'ndk-bundle')

if not checkExecutable(args.cmake, '--help'):
    print('Failed to find CMake executable. Use --cmake to specify its location')
    sys.exit(-1)

resolveBuildTools(args)

if not args.ninjapath and not checkExecutable(args.make, '--help'):
    print('Failed to find ninja or make executable. Use --ninja or --make to specify its location')
    sys.exit(-1)

if not checkExecutable(args.gradle, '--help'):
    print('Failed to find Gradle executable. Use --gradle to specify its location')
    sys.exit(-1)

# 1) Build .so per ABI via cmake
for abi in args.androidabi:
    if not buildRoutingSO(args, abi):
        sys.exit(-1)

# 2) Package AAR via gradle (picks up pre-built .so from dist/routing-android/)
if not buildRoutingAAR(args):
    sys.exit(-1)

print("Done. Output: dist/routing-android/%s" % getRoutingAndroidAarDistName(args.buildversion))
