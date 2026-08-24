import os
import sys
import shutil
import argparse
import string
from build.sdk_build_utils import *

ANDROID_ABIS = ['armeabi-v7a', 'x86', 'arm64-v8a', 'x86_64']

def javac(args, dir, *cmdArgs):
  return execute(args.javac, dir, *cmdArgs)

def jar(args, dir, *cmdArgs):
  return execute(args.jar, dir, *cmdArgs)

def gradle(args, dir, *cmdArgs):
  return execute(args.gradle, dir, *cmdArgs)

def zip(args, dir, *cmdArgs):
  return execute(args.zip, dir, *cmdArgs)

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

def detectAndroidJavaAPI(args):
  apiJava = None
  for name in os.listdir('%s/platforms' % args.androidsdkpath):
    if name.startswith('android-'):
      api = int(name[8:])
      if api >= 14:
        apiJava = min(apiJava or api, api)
  return apiJava

def buildAndroidSO(args, abi):
  version = getVersion(args.buildversion, args.buildnumber) if args.configuration == 'Release' else 'Devel'
  baseDir = getBaseDir()
  buildDir = getBuildDir('android', abi)
  distDir = getDistDir('android')
  defines = ["-D%s" % define for define in args.defines.split(';') if define]
  # '{abi}' in a --cmake-options value expands to the ABI being built, so one option can point at a
  # per-ABI prefix: a cross-built dependency has one install tree per ABI and no single path works.
  options = ["-D%s" % option.replace('{abi}', abi) for option in args.cmakeoptions.split(';') if option]
  api32, api64 = detectAndroidAPIs(args)
  if api32 is None or api64 is None:
    print('Failed to detect available platform APIs')
    return False
  print('Using API-%d for 32-bit builds, API-%d for 64-bit builds' % (api32, api64))
  resetBuildDirOnGeneratorChange(args, buildDir)

  if not cmake(args, buildDir, options + getGeneratorOptions(args) + getCCacheOptions(args) + [
    "-DCMAKE_TOOLCHAIN_FILE='%s/build/cmake/android.toolchain.cmake'" % args.androidndkpath,
    "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON",
    "-DCMAKE_SYSTEM_NAME=Android",
    "-DCMAKE_BUILD_TYPE=%s" % args.configuration,
    "-DCMAKE_ANDROID_NDK='%s'" % args.androidndkpath,
    "-DCMAKE_ANDROID_ARCH_ABI='%s'" % abi,
    "-DWRAPPER_DIR=%s" % ('%s/generated/android-java/wrappers' % baseDir),
    "-DSINGLE_LIBRARY:BOOL=ON",
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
    '%s/scripts/build' % baseDir
  ]):
    return False
  if not cmake(args, buildDir, [
    '--build', '.',
    '--parallel', str(os.cpu_count()),
    '--config', args.configuration,
  ]):
    return False
  return makedirs('%s/%s' % (distDir, abi)) and copyfile('%s/libmassif.so' % buildDir, '%s/%s/libmassif.so' % (distDir, abi))

def buildAndroidJAR(args):
  shutil.rmtree(getBuildDir('android_java'), True)

  baseDir = getBaseDir()
  buildDir = getBuildDir('android_java')
  distDir = getDistDir('android')
  apiJava = detectAndroidJavaAPI(args)
  if apiJava is None:
    print('Failed to detect available platform APIs')
    return False
  print('Using target API %d for Java files' % apiJava)

  javaFiles = []
  for sourceDir in ["%s/generated/android-java/proxies" % baseDir, "%s/android/java" % baseDir]:
    for dirpath, dirnames, filenames in os.walk(sourceDir):
      for filename in [f for f in filenames if f.endswith(".java")]:
        if os.name == 'nt':
          javaFiles.append(os.path.join(dirpath, "*.java"))
          break
        javaFiles.append(os.path.join(dirpath, filename))

  if not javac(args, buildDir,
    '-g:vars',
    '-source', '1.7',
    '-target', '1.7',
    '-bootclasspath', '%s/platforms/android-%d/android.jar' % (args.androidsdkpath, apiJava),
    '-d', buildDir,
    *javaFiles
  ):
    return False

  currentDir = os.getcwd()
  os.chdir(buildDir)
  classFiles = []
  for dirpath, dirnames, filenames in os.walk("."):
    for filename in [f for f in filenames if f.endswith(".class")]:
      if os.name == 'nt':
        classFiles.append(os.path.join(dirpath[2:], "*.class"))
        break
      classFiles.append(os.path.join(dirpath[2:], filename))
  os.chdir(currentDir)

  if not jar(args, buildDir,
    'cf', 'massif.jar',
    *classFiles
  ):
    return False

  if not makedirs(distDir) or \
     not copyfile('%s/massif.jar' % buildDir, '%s/massif.jar' % distDir):
    return False

  print("JAR output available in:\n%s" % distDir)
  return True

def buildAndroidAAR(args):
  shutil.rmtree(getBuildDir('android-src'), True)

  baseDir = getBaseDir()
  srcDir = getBuildDir('android-src')
  buildDir = getBuildDir('android-aar')
  distDir = getDistDir('android')
  version = args.buildversion
  distName = getAndroidAarDistName(version, args.profile)

  # with open('%s/scripts/android/massif.pom.template' % baseDir, 'r') as f:
  #   pomFile = string.Template(f.read()).safe_substitute({
  #     'baseDir': baseDir,
  #     'buildDir': buildDir,
  #     'distDir': distDir,
  #     'version': version
  #   })
  # pomFileName = '%s/massif.pom' % buildDir
  # with open(pomFileName, 'w') as f:
  #   f.write(pomFile)

  javaFiles = []
  for sourceDir in ["%s/generated/android-java/proxies" % baseDir, "%s/android/java" % baseDir]:
    for dirpath, dirnames, filenames in os.walk(sourceDir):
      relpath = os.path.relpath(dirpath, sourceDir)
      makedirs(os.path.join(srcDir, relpath))
      for filename in [f for f in filenames if f.endswith(".java")]:
        copyfile(os.path.join(dirpath, filename), os.path.join(srcDir, relpath, filename))

  # srcFileName = '%s/sources.jar' % buildDir
  # if not jar(args, srcDir,
  #   'cf', srcFileName, '.'):
  #   return False

  if not gradle(args, '%s/scripts' % baseDir,
    '-p', 'android',
    '--project-cache-dir', buildDir,
    '--gradle-user-home', '%s/gradle' % buildDir,
    'assembleRelease'
  ):
    return False
  aarFileName = '%s/outputs/aar/massif-%s.aar' % (buildDir, args.configuration.lower())
  if not os.path.exists(aarFileName):
    aarFileName = '%s/outputs/aar/android-release.aar' % buildDir
  if not makedirs(distDir) or \
     not copyfile(aarFileName, '%s/%s' % (distDir, distName)):
    #  not copyfile(srcFileName, '%s/massif-android-%s-sources.jar' % (distDir, version)):
      #  not copyfile(pomFileName, '%s/massif-android-%s.pom' % (distDir, version)) or \
    #  not zip(args, '%s/scripts/android/src/main' % baseDir, '%s/massif-%s.aar' % (distDir, version), 'R.txt'):
    return False

  # if buildForJitpack:
  #   with open('%s/scripts/android-jitpack/jitpack.yml.template' % baseDir, 'r') as f:
  #     packageFile = string.Template(f.read()).safe_substitute({
  #       'baseDir': baseDir,
  #       'distDir': distDir,
  #       'repoUrl': REPO_URL,
  #       'distName': distName,
  #       'version': version,
  #       'checksum': checksumSHA256('%s/%s' % (distDir, distName))
  #     })
  #   with open('%s/jitpack.yml' % distDir, 'w') as f:
  #     f.write(packageFile)

  print("AAR output available in:\n%s" % distDir)
  return True

parser = argparse.ArgumentParser()
parser.add_argument('--profile', dest='profile', default=getDefaultProfileId(), type=validProfile, help='Build profile')
parser.add_argument('--android-abi', dest='androidabi', default=[], choices=ANDROID_ABIS + ['all'], action='append', help='Android target ABIs')
parser.add_argument('--android-ndk-path', dest='androidndkpath', default='auto', help='Android NDK path')
parser.add_argument('--android-sdk-path', dest='androidsdkpath', default='auto', help='Android SDK path')
parser.add_argument('--defines', dest='defines', default='', help='Defines for compilation')
parser.add_argument('--javac', dest='javac', default='javac', help='Java compiler executable')
parser.add_argument('--jar', dest='jar', default='jar', help='Jar executable')
parser.add_argument('--zip', dest='zip', default='zip', help='Zip executable')
parser.add_argument('--make', dest='make', default='make', help='Make executable, used only when no ninja is available')
parser.add_argument('--ninja', dest='ninja', default='auto', help="Ninja executable, 'auto' to detect one, 'none' to build with make")
parser.add_argument('--ccache', dest='ccache', default='auto', help="Ccache executable, 'auto' to detect one, 'none' to compile without a launcher")
parser.add_argument('--cmake', dest='cmake', default='cmake', help='CMake executable')
parser.add_argument('--cmake-options', dest='cmakeoptions', default='', help='CMake options')
parser.add_argument('--gradle', dest='gradle', default='gradle', help='Gradle executable')
parser.add_argument('--configuration', dest='configuration', default='Release', choices=['Release', 'RelWithDebInfo', 'Debug'], help='Configuration')
parser.add_argument('--build-number', dest='buildnumber', default='', help='Build sequence number, goes to version str')
parser.add_argument('--build-version', dest='buildversion', default='%s-devel' % SDK_VERSION, help='Build version, goes to distributions')
parser.add_argument('--build-aar', dest='buildaar', default=False, action='store_true', help='Build Android .aar package')
parser.add_argument('--build-jar', dest='buildjar', default=False, action='store_true', help='Build Android .jar package')
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
args.defines += ';' + getProfile(args.profile).get('defines', '')
args.cmakeoptions += ';' + getProfile(args.profile).get('cmake-options', '')

if not os.path.exists("%s/generated/android-java/proxies" % getBaseDir()):
  print("Proxies/wrappers not generated yet, run swigpp script first.")
  sys.exit(-1)

if not checkExecutable(args.cmake, '--help'):
  print('Failed to find CMake executable. Use --cmake to specify its location')
  sys.exit(-1)

resolveBuildTools(args)

if not args.ninjapath and not checkExecutable(args.make, '--help'):
  print('Failed to find ninja or make executable. Use --ninja or --make to specify its location')
  sys.exit(-1)

if not checkExecutable(args.javac, '-help'):
  print('Failed to find javac executable. Use --javac to specify its location')
  sys.exit(-1)

if args.buildaar:
  if not checkExecutable(args.zip, '-h'):
    print('Failed to find zip executable. Use --zip to specify its location')
    sys.exit(-1)
  if not checkExecutable(args.gradle, '--help'):
    print('Failed to find gradle executable. Use --gradle to specify its location')
    sys.exit(-1)

for abi in args.androidabi:
  if not buildAndroidSO(args, abi):
    sys.exit(-1)

if args.buildjar:
  if not buildAndroidJAR(args):
    sys.exit(-1)

if args.buildaar:
  if not buildAndroidAAR(args):
    sys.exit(-1)
