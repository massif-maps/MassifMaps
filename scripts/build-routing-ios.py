import os
import sys
import re
import shutil
import itertools
import argparse
import string
from build.sdk_build_utils import *

IOS_ARCHS = ['i386', 'x86_64', 'armv7', 'arm64', 'arm64-simulator', 'x86_64-maccatalyst', 'arm64-maccatalyst']
SDK_VERSION = "4.4.9"

FRAMEWORK_NAME="ValhallaRouting"
REPO_URL="https://github.com/massif-maps/MassifMaps"
# Objective-C class prefix — must match the %rename in swigpp-objc.py.
CLASS_PREFIX="MSF"

def getFinalBuildDir(target, arch=None):
  return getBuildDir( target, arch)

def getFinalDistDir(args):
  return getDistDir('routing-ios')

def getPlatformArch(baseArch):
  if baseArch.endswith('-maccatalyst'):
    return 'MACCATALYST', baseArch[:-12]
  if baseArch.endswith('-simulator'):
    return 'SIMULATOR', baseArch[:-10]
  return ('OS' if baseArch.startswith('arm') else 'SIMULATOR'), baseArch

def updateUmbrellaHeader(filename, defines):
  with open(filename, 'r') as f:
    lines = f.readlines()
    for i in range(0, len(lines)):
      match = re.search(r'^\s*#import\s+"(.*)".*', lines[i].rstrip('\n'))
      if match:
        headerFilename = match.group(1).split('/')[-1]
        if not headerFilename.startswith(CLASS_PREFIX):
          headerFilename = '%s%s' % (CLASS_PREFIX, headerFilename)
        lines[i] = '#import <%s/%s>\n' % (FRAMEWORK_NAME , headerFilename)
    for i in range(0, len(lines)):
      if re.search(r'^\s*#define\s+.*$', lines[i].rstrip('\n')):
        break
    lines = lines[:i+1] + ['\n'] + ['#define %s\n' % define for define in defines.split(';') if define] + lines[i+1:]
  with open(filename, 'w') as f:
    f.writelines(lines)

def replaceInFile(filePath, regexp, replacement):
  # Define the regular expression pattern
  pattern = re.compile(regexp)

  # Read from the input file
  with open(filePath, 'r') as file:
    content = file.read()

  # Perform the replacement
  modified_content = re.sub(pattern, replacement, content)

  # Write back to the output file
  with open(filePath, 'w') as file:
    file.write(modified_content)

def updatePublicHeader(filename):
  with open(filename, 'r') as f:
    lines = f.readlines()
    externCMode = False
    for i in range(0, len(lines)):
      if lines[i].find('extern "C"') != -1:
        externCMode = True
      match = re.search(r'^\s*#import\s+"(.*)".*', lines[i].rstrip('\n'))
      if match:
        headerFilename = match.group(1)
        if externCMode:
          lines[i] = '#ifdef __cplusplus\n}\n#endif\n#import "%s"\n#ifdef __cplusplus\nextern "C" {\n#endif\n' % headerFilename
        else:
          lines[i] = '#import "%s"\n' % headerFilename
  with open(filename, 'w') as f:
    f.writelines(lines)

def buildModuleMap(filename, publicHeaders, umbrella='%s/%s.h' % (FRAMEWORK_NAME, FRAMEWORK_NAME)):
  with open(filename, 'w') as f:
    f.write('module %s {\n' % FRAMEWORK_NAME)
    f.write('    umbrella header "%s"\n' % umbrella)
    # f.write('    link "c++"\n')
    # f.write('    link "z"\n')
    # for header in publicHeaders:
    #   f.write('    header "%s"\n' % header)
    f.write('    export *\n')
    f.write('    module * { export * }\n')
    f.write('}\n')
  return True

def copyXCFrameworkHeaders(args, baseDir, outputDir):
  destDir = '%s/%s' % (outputDir, FRAMEWORK_NAME)
  publicHeaders = []
  makedirs(destDir) 

  currentDir = os.getcwd()

  # The whole ObjC surface lives in one header; the umbrella below imports it.
  for extraHeader in ['%s/routing-lib/ios/%sValhallaRoutingService.h' % (baseDir, CLASS_PREFIX)]:
    filename = extraHeader.rsplit('/', 1)[1]
    publicHeaders.append(filename)
    if not copyfile(extraHeader, '%s/%s' % (destDir, filename)):
      return False

  if not copyfile('%s/scripts/routing-ios/%s.h' % (baseDir, FRAMEWORK_NAME), '%s/%s.h' % (destDir, FRAMEWORK_NAME)):
    return False
  updateUmbrellaHeader('%s/%s.h' % (destDir, FRAMEWORK_NAME), args.defines)

  # In Headers/<name>/, not Headers/: an app copies every xcframework's Headers into one include/,
  # where two root module maps collide (MassifMapsCore beside ValhallaRouting).
  return buildModuleMap('%s/%s/module.modulemap' % (outputDir, FRAMEWORK_NAME), publicHeaders, '%s.h' % FRAMEWORK_NAME)

def buildIOSLib(args, baseArch, outputDir=None):
  platform, arch = getPlatformArch(baseArch)
  version = getVersion(args.buildversion, args.buildnumber) if args.configuration == 'Release' else 'Devel'
  baseDir = getBaseDir()
  buildDir = outputDir or getFinalBuildDir('routing-ios', '%s-%s' % (platform, arch))
  defines = ["-D%s" % define for define in args.defines.split(';') if define]
  options = ["-D%s" % option for option in args.cmakeoptions.split(';') if option]

  if not cmake(args, buildDir, options + [
    '-G', 'Xcode',
    '-DCMAKE_SYSTEM_NAME=%s' % ('Darwin' if platform == 'MACCATALYST' else 'iOS'),
    # '-DWRAPPER_DIR=%s' % ('%s/generated/ios-objc/proxies' % baseDir),
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    '-DINCLUDE_OBJC:BOOL=ON',
    '-DSINGLE_LIBRARY:BOOL=ON',
    '-DSHARED_LIBRARY:BOOL=%s' % ('ON' if args.sharedlib else 'OFF'),
    '-DCMAKE_OSX_ARCHITECTURES=%s' % arch,
    '-DCMAKE_OSX_SYSROOT=%s' % ('macosx' if platform == 'MACCATALYST' else 'iphone%s' % platform.lower()),
    # Valhalla 3.8 uses std::filesystem, which Apple gates on iOS 13 / macOS 10.15.
    '-DCMAKE_OSX_DEPLOYMENT_TARGET=%s' % ('13.1' if platform == 'MACCATALYST' else '13.0'),
    '-DCMAKE_BUILD_TYPE=%s' % args.configuration,
    "-DSDK_CPP_DEFINES=%s" % " ".join(defines),
    "-DSDK_DEV_TEAM='%s'" % (args.devteam if args.devteam else ""),
    "-DSDK_VERSION='%s'" % version,
    "-DSDK_PLATFORM='iOS'",
    "-DSDK_IOS_ARCH='%s'" % arch,
    "-DSDK_IOS_BASEARCH='%s'" % baseArch,
    '%s/scripts/routing' % baseDir
  ]):
    return False

  # we need to fix targets for MACALYST which are wrong 
  pbxproj = '%s/valhalla_routing.xcodeproj/project.pbxproj' % buildDir
  print('pbxproj %s' % pbxproj)
  with open(pbxproj) as f:
      pbxprojContent = f.read().replace('-apple-ios-13.0-macabi', '-apple-ios13.1-macabi')
  with open(pbxproj, "w") as f:
      f.write(pbxprojContent)

  bitcodeOptions = ['ENABLE_BITCODE=NO']
  if not args.stripbitcode and baseArch in ('armv7', 'arm64'):
    bitcodeOptions = ['ENABLE_BITCODE=YES', 'BITCODE_GENERATION_MODE=bitcode']
  buildMode = ('archive' if args.configuration == 'Release' else 'build')
  return execute('xcodebuild', buildDir,
    '-project', 'valhalla_routing.xcodeproj', '-arch', arch, '-configuration', args.configuration, buildMode,
    *list(bitcodeOptions+['GCC_PREPROCESSOR_DEFINITIONS=_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION _LIBCPP_ENABLE_CXX20_REMOVED_UNCAUGHT_EXCEPTION'])
  )

def buildIOSXCFramework(args, baseArchs, outputDir=None):
  groupedPlatformArchs = {}
  for baseArch in baseArchs:
    platform, arch = getPlatformArch(baseArch)
    groupedPlatformArchs[platform] = groupedPlatformArchs.get(platform, []) + [baseArch]
  baseDir = getBaseDir()
  distDir = outputDir or getFinalDistDir(args)
  shutil.rmtree(distDir, True)
  makedirs(distDir)

  frameworkBuildDirs = []
  frameworkOptions = []

  headersDir = getFinalBuildDir('routing-ios', 'Headers')
  shutil.rmtree(headersDir, True)
  makedirs(headersDir)
  if not copyXCFrameworkHeaders(args, baseDir, headersDir):
    return False
  for platform, baseArchs in groupedPlatformArchs.items():
    libFilePaths = []
    for baseArch in baseArchs:
      platform, arch = getPlatformArch(baseArch)
      if platform == 'MACCATALYST':
        libFilePath = "%s/%s/libvalhalla_routing.%s" % (getFinalBuildDir('routing-ios', '%s-%s' % (platform, arch)), args.configuration, 'dylib' if args.sharedlib else 'a')
      else:
        libFilePath = "%s/%s-%s/libvalhalla_routing.%s" % (getFinalBuildDir('routing-ios', '%s-%s' % (platform, arch)), args.configuration, ('iphone%s' % platform.lower()), 'dylib' if args.sharedlib else 'a')
      libFilePaths.append(libFilePath)
    libFinalPath = "%s/%s.a" % (getFinalBuildDir('routing-ios', platform), FRAMEWORK_NAME)
    if not execute('lipo', baseDir,
      '-output', libFinalPath,
      '-create', *libFilePaths
    ):
      return False
    frameworkOptions.extend(["-library", str(libFinalPath), "-headers", str(headersDir) ])  
  # frameworkOptions = itertools.chain(*[['-framework', '%s/MassifMaps.framework' % frameworkBuildDir] for frameworkBuildDir in frameworkBuildDirs])
  if not execute('xcodebuild', baseDir,
    '-create-xcframework', '-output', '%s/%s.xcframework' % (distDir, FRAMEWORK_NAME),
    *list(frameworkOptions)
  ):
    return False

  if outputDir is None:
    print("iOS xcframework output available in:\n%s" % distDir)
  return True

def buildIOSPackage(args):
  baseDir = getBaseDir()
  distDir = getFinalDistDir(args)
  version = args.buildversion
  distName = getRoutingIOSZipDistName(version)
  frameworkName = FRAMEWORK_NAME
  frameworkDir = '%s.%s' % (FRAMEWORK_NAME, "xcframework" if args.buildxcframework else "framework")
  frameworks = ["CFNetwork", "Foundation"]
  weakFrameworks = []

  try:
    os.remove('%s/%s' % (distDir, distName))
  except:
    pass
  if not execute('ditto', distDir, '-c', '-k', '--sequesterRsrc', '--keepParent', frameworkDir, '%s/%s' % (distDir, distName)):
    return False

  print("iOS package output available in:\n%s" % distDir)
  return True

parser = argparse.ArgumentParser()
parser.add_argument('--ios-arch', dest='iosarch', default=[], choices=IOS_ARCHS + ['all'], action='append', help='iOS target architectures')
parser.add_argument('--defines', dest='defines', default='', help='Defines for compilation')
parser.add_argument('--cmake', dest='cmake', default='cmake', help='CMake executable')
parser.add_argument('--cmake-options', dest='cmakeoptions', default='', help='CMake options')
parser.add_argument('--configuration', dest='configuration', default='Release', choices=['Release', 'RelWithDebInfo', 'Debug'], help='Configuration')
parser.add_argument('--build-number', dest='buildnumber', default='', help='Build sequence number, goes to version str')
parser.add_argument('--build-version', dest='buildversion', default='%s-devel' % SDK_VERSION, help='Build version, goes to distributions')
parser.add_argument('--build-xcframework', dest='buildxcframework', default=False, action='store_true', help='Build XCFramework')
parser.add_argument('--strip-bitcode', dest='stripbitcode', default=False, action='store_true', help='Strip bitcode from the built framework')
parser.add_argument('--shared-framework', dest='sharedlib', default=False, action='store_true', help='Build shared framework instead of static')

args = parser.parse_args()
if 'all' in args.iosarch or args.iosarch == []:
  args.iosarch = list(filter(lambda arch: not (arch in ('i386', 'armv7')), IOS_ARCHS))
  if not args.buildxcframework:
    args.iosarch = list(filter(lambda arch: not (arch.endswith('-simulator') or arch.endswith('-maccatalyst')), args.iosarch))

# on iOS it needs to be defined or zstd wont build
args.defines += ';' + 'ZSTD_STATIC_LINKING_ONLY'

args.devteam = os.environ.get('IOS_DEV_TEAM', None)
if args.sharedlib:
  if args.devteam is None:
    print("Shared library requires development team, IOS_DEV_TEAM variable not set")
    sys.exit(-1)

if not checkExecutable(args.cmake, '--help'):
  print('Failed to find CMake executable. Use --cmake to specify its location')
  sys.exit(-1)

for arch in args.iosarch:
  if not buildIOSLib(args, arch):
    sys.exit(-1)

if not buildIOSXCFramework(args, args.iosarch):
    sys.exit(-1)

if not buildIOSPackage(args):
  sys.exit(-1)