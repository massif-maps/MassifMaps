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

FRAMEWORK_NAME="MassifMaps"
REPO_URL="https://github.com/massif-maps/MassifMaps"
# Objective-C class prefix — must match the %rename in swigpp-objc.py.
CLASS_PREFIX="MSF"

def getFinalBuildDir(target, arch=None):
  return getBuildDir(('%s_metal' % target) if args.metalangle else target, arch)

def getFinalDistDir(args):
  return getDistDir('ios_metal' if args.metalangle else 'ios')

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

def buildModuleMap(filename, publicHeaders):
  with open(filename, 'w') as f:
    f.write('module %s {\n' % FRAMEWORK_NAME)
    f.write('    umbrella header "%s/%s.h"\n' % (FRAMEWORK_NAME, FRAMEWORK_NAME))
    # f.write('    link "c++"\n')
    # f.write('    link "z"\n')
    # for header in publicHeaders:
    #   f.write('    header "%s"\n' % header)
    f.write('    export *\n')
    f.write('    module * { export * }\n')
    f.write('}\n')
  return True

def copyHeaders(args, baseDir, outputDir):
  proxyHeaderDir = '%s/generated/ios-objc/proxies' % baseDir
  destDir = '%s/Headers' % outputDir
  publicHeaders = []
  makedirs(destDir) 

  currentDir = os.getcwd()
  os.chdir(proxyHeaderDir)
  for dirpath, dirnames, filenames in os.walk('.'):
    for filename in filenames:
      if filename.endswith('.h'):
        publicHeaders.append(filename)
        if not copyfile(os.path.join(dirpath, filename), '%s/%s' % (destDir, filename)):
          os.chdir(currentDir)
          return False
        updatePublicHeader('%s/%s' % (destDir, filename))
  os.chdir(currentDir)

  extraHeaders = ['%s/ios/objc/utils/ExceptionWrapper.h', '%s/ios/objc/ui/MapView.h',
                  '%s/ios/objc/api/MSFMassif.h', '%s/ios/objc/api/MSFMapEvents.h',
                  '%s/ios/objc/api/MSFMassifObject.h', '%s/ios/objc/api/MSFMassifMap.h']
  if args.metalangle:
    for extraHeader in ['MGLKit.h', 'MGLKitPlatform.h', 'MGLContext.h', 'MGLKView.h', 'MGLLayer.h', 'MGLKViewController.h']:
      extraHeaders += ['%s/libs-external/angle-metal/include/' + extraHeader]
  for extraHeader in extraHeaders:
    dirpath, filename = (extraHeader % baseDir).rsplit('/', 1)
    # Already prefixed stays as it is: the facade sugar headers are written as MSF*, unlike
    # MapView.h which is renamed on the way in.
    destFilename = filename if filename.startswith('MGL') or filename.startswith(CLASS_PREFIX) \
                   else '%s%s' % (CLASS_PREFIX, filename)
    publicHeaders.append(destFilename)
    if not copyfile(os.path.join(dirpath, filename), '%s/%s' % (destDir, destFilename)):
      return False  

  if not copyfile('%s/ios/objc/%s.h' % (baseDir, FRAMEWORK_NAME), '%s/%s.h' % (destDir, FRAMEWORK_NAME)):
    return False
  updateUmbrellaHeader('%s/%s.h' % (destDir, FRAMEWORK_NAME), args.defines)

  makedirs('%s/Modules' % outputDir)
  return buildModuleMap('%s/Modules/module.modulemap' % outputDir, publicHeaders)

def copyXCFrameworkHeaders(args, baseDir, outputDir):
  proxyHeaderDir = '%s/generated/ios-objc/proxies' % baseDir
  destDir = '%s/%s' % (outputDir, FRAMEWORK_NAME)
  publicHeaders = []
  makedirs(destDir) 

  currentDir = os.getcwd()
  os.chdir(proxyHeaderDir)
  for dirpath, dirnames, filenames in os.walk('.'):
    for filename in filenames:
      if filename.endswith('.h'):
        publicHeaders.append(filename)
        if not copyfile(os.path.join(dirpath, filename), '%s/%s' % (destDir, filename)):
          os.chdir(currentDir)
          return False
        updatePublicHeader('%s/%s' % (destDir, filename))
  os.chdir(currentDir)

  extraHeaders = ['%s/ios/objc/utils/ExceptionWrapper.h', '%s/ios/objc/ui/MapView.h',
                  '%s/ios/objc/api/MSFMassif.h', '%s/ios/objc/api/MSFMapEvents.h',
                  '%s/ios/objc/api/MSFMassifObject.h', '%s/ios/objc/api/MSFMassifMap.h']
  if args.metalangle:
    for extraHeader in ['MGLKit.h', 'MGLKitPlatform.h', 'MGLContext.h', 'MGLKView.h', 'MGLLayer.h', 'MGLKViewController.h']:
      extraHeaders += ['%s/libs-external/angle-metal/include/' + extraHeader]
  for extraHeader in extraHeaders:
    dirpath, filename = (extraHeader % baseDir).rsplit('/', 1)
    # Already prefixed stays as it is: the facade sugar headers are written as MSF*, unlike
    # MapView.h which is renamed on the way in.
    destFilename = filename if filename.startswith('MGL') or filename.startswith(CLASS_PREFIX) \
                   else '%s%s' % (CLASS_PREFIX, filename)
    publicHeaders.append(destFilename)
    if not copyfile(os.path.join(dirpath, filename), '%s/%s' % (destDir, destFilename)):
      return False  

  if not copyfile('%s/ios/objc/%s.h' % (baseDir, FRAMEWORK_NAME), '%s/%s.h' % (destDir, FRAMEWORK_NAME)):
    return False
  updateUmbrellaHeader('%s/%s.h' % (destDir, FRAMEWORK_NAME), args.defines)

  return buildModuleMap('%s/module.modulemap' % outputDir, publicHeaders)

def buildIOSLib(args, baseArch, outputDir=None):
  platform, arch = getPlatformArch(baseArch)
  version = getVersion(args.buildversion, args.buildnumber) if args.configuration == 'Release' else 'Devel'
  baseDir = getBaseDir()
  buildDir = outputDir or getFinalBuildDir('ios', '%s-%s' % (platform, arch))
  defines = ["-D%s" % define for define in args.defines.split(';') if define]
  options = ["-D%s" % option for option in args.cmakeoptions.split(';') if option]

  if not cmake(args, buildDir, options + [
    '-G', 'Xcode',
    '-DCMAKE_SYSTEM_NAME=%s' % ('Darwin' if platform == 'MACCATALYST' else 'iOS'),
    '-DWRAPPER_DIR=%s' % ('%s/generated/ios-objc/proxies' % baseDir),
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
    '%s/scripts/build' % baseDir
  ]):
    return False

  # we need to fix targets for MACALYST which are wrong 
  pbxproj = '%s/massif.xcodeproj/project.pbxproj' % buildDir
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
    '-project', 'massif.xcodeproj', '-arch', arch, '-configuration', args.configuration, buildMode,
    *list(bitcodeOptions+['GCC_PREPROCESSOR_DEFINITIONS=_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION'])
  )

def buildIOSFramework(args, baseArchs, outputDir=None):
  baseDir = getBaseDir()
  distDir = outputDir or getFinalDistDir(args)
  frameworkName = "%s.framework" % FRAMEWORK_NAME
  if args.sharedlib:
    frameworkDir = '%s/%s' % (distDir, frameworkName)
  else:
    frameworkDir = '%s/%s/Versions/A' % (distDir, frameworkName)
  shutil.rmtree(distDir, True)
  makedirs(frameworkDir)

  libFilePaths = []
  for baseArch in baseArchs:
    platform, arch = getPlatformArch(baseArch)
    if platform == 'MACCATALYST':
      libFilePath = "%s/%s/libmassif.%s" % (getFinalBuildDir('ios', '%s-%s' % (platform, arch)), args.configuration, 'dylib' if args.sharedlib else 'a')
    else:
      libFilePath = "%s/%s-%s/libmassif.%s" % (getFinalBuildDir('ios', '%s-%s' % (platform, arch)), args.configuration, ('iphone%s' % platform.lower()), 'dylib' if args.sharedlib else 'a')
    libFilePaths.append(libFilePath)

  if not execute('lipo', baseDir,
    '-output', '%s/%s' % (frameworkDir, FRAMEWORK_NAME),
    '-create', *libFilePaths
  ):
    return False

  outputInfoPlist = '%s/%s/Info.plist' % (distDir, frameworkName)

  if not copyfile('%s/scripts/ios/Info.plist' % baseDir, outputInfoPlist):
      return False
  # change version name in info.plist
  # \g<n> (not \n) — the version starts with a digit, which \1 would swallow into an invalid group ref.
  replaceInFile(outputInfoPlist, r'(?P<key>CFBundleShortVersionString</key>[\n\t\s]*<string>)([\d\.]+)(</string>)', r'\g<1>%s\g<3>' % args.buildversion)

  if args.sharedlib:
    if not execute('install_name_tool', frameworkDir,
      '-id', '@rpath/%s/%s' %(frameworkName, FRAMEWORK_NAME),
      FRAMEWORK_NAME
    ):
      return False

  makedirs('%s/Headers' % frameworkDir)
  if not args.sharedlib:
    if not makesymlink('%s/%s/Versions' % (distDir, frameworkName), 'A', 'Current'):
      return False
    if not makesymlink('%s/%s' % (distDir, frameworkName), 'Versions/A/Modules', 'Modules'):
      return False
    if not makesymlink('%s/%s' % (distDir, frameworkName), 'Versions/A/Headers', 'Headers'):
      return False
    if not makesymlink('%s/%s' % (distDir, frameworkName), 'Versions/A/%s' % FRAMEWORK_NAME, FRAMEWORK_NAME):
      return False
  if not copyHeaders(args, baseDir, frameworkDir):
    return False

  if outputDir is None:
    print("iOS framework output available in:\n%s" % distDir)
  return True

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

  headersDir = getFinalBuildDir('ios', 'Headers')
  makedirs(headersDir)
  if not copyXCFrameworkHeaders(args, baseDir, headersDir):
    return False
  for platform, baseArchs in groupedPlatformArchs.items():
    libFilePaths = []
    for baseArch in baseArchs:
      platform, arch = getPlatformArch(baseArch)
      if platform == 'MACCATALYST':
        libFilePath = "%s/%s/libmassif.%s" % (getFinalBuildDir('ios', '%s-%s' % (platform, arch)), args.configuration, 'dylib' if args.sharedlib else 'a')
      else:
        libFilePath = "%s/%s-%s/libmassif.%s" % (getFinalBuildDir('ios', '%s-%s' % (platform, arch)), args.configuration, ('iphone%s' % platform.lower()), 'dylib' if args.sharedlib else 'a')
      libFilePaths.append(libFilePath)
    libFinalPath = "%s/%s.a" % (getFinalBuildDir('ios', platform), FRAMEWORK_NAME)
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
  distName = getIOSZipDistName(version, args.profile)
  frameworkName = FRAMEWORK_NAME
  frameworkDir = '%s.%s' % (FRAMEWORK_NAME, "xcframework" if args.buildxcframework else "framework")
  frameworks = (["QuartzCore"] if args.metalangle else ["OpenGLES", "GLKit"]) + ["UIKit", "CoreGraphics", "CoreText", "CFNetwork", "Foundation"]
  weakFrameworks = (["Metal"] if args.metalangle else [])

  try:
    os.remove('%s/%s' % (distDir, distName))
  except:
    pass
  if not execute('ditto', distDir, '-c', '-k', '--sequesterRsrc', '--keepParent', frameworkDir, '%s/%s' % (distDir, distName)):
    return False

  # if buildCocoapod:
  #   with open('%s/scripts/ios-cocoapod/podspec.template' % baseDir, 'r') as f:
  #     cocoapodFile = string.Template(f.read()).safe_substitute({
  #       'baseDir': baseDir,
  #       'distDir': distDir,
  #       'distName': distName,
  #       'repoUrl': REPO_URL,
  #       'frameworkName': frameworkName,
  #       'frameworkDir': frameworkDir,
  #       'version': version,
  #       'license': readLicense(),
  #       'frameworks': ', '.join('"%s"' % framework for framework in frameworks) if frameworks else 'nil',
  #       'weakFrameworks': ', '.join('"%s"' % framework for framework in weakFrameworks) if weakFrameworks else 'nil'
  #     })
  #   with open('%s/%s.podspec' % (distDir, frameworkName), 'w') as f:
  #     f.write(cocoapodFile)


  print("iOS package output available in:\n%s" % distDir)
  return True

parser = argparse.ArgumentParser()
parser.add_argument('--profile', dest='profile', default=getDefaultProfileId(), type=validProfile, help='Build profile')
parser.add_argument('--ios-arch', dest='iosarch', default=[], choices=IOS_ARCHS + ['all'], action='append', help='iOS target architectures')
parser.add_argument('--defines', dest='defines', default='', help='Defines for compilation')
parser.add_argument('--cmake', dest='cmake', default='cmake', help='CMake executable')
parser.add_argument('--cmake-options', dest='cmakeoptions', default='', help='CMake options')
parser.add_argument('--configuration', dest='configuration', default='Release', choices=['Release', 'RelWithDebInfo', 'Debug'], help='Configuration')
parser.add_argument('--build-number', dest='buildnumber', default='', help='Build sequence number, goes to version str')
parser.add_argument('--build-version', dest='buildversion', default='%s-devel' % SDK_VERSION, help='Build version, goes to distributions')
parser.add_argument('--build-xcframework', dest='buildxcframework', default=False, action='store_true', help='Build XCFramework')
parser.add_argument('--build-cocoapod', dest='buildcocoapod', default=False, action='store_true', help='Build CocoaPod')
parser.add_argument('--use-metalangle', dest='metalangle', default=False, action='store_true', help='Use MetalANGLE instead of Apple GL')
parser.add_argument('--strip-bitcode', dest='stripbitcode', default=False, action='store_true', help='Strip bitcode from the built framework')
parser.add_argument('--shared-framework', dest='sharedlib', default=False, action='store_true', help='Build shared framework instead of static')

args = parser.parse_args()
if 'all' in args.iosarch or args.iosarch == []:
  args.iosarch = list(filter(lambda arch: not (arch in ('i386', 'armv7')), IOS_ARCHS))
  if not args.buildxcframework:
    args.iosarch = list(filter(lambda arch: not (arch.endswith('-simulator') or arch.endswith('-maccatalyst')), args.iosarch))
  if not args.metalangle:
    args.iosarch = list(filter(lambda arch: not arch.endswith('-maccatalyst'), args.iosarch))
# on iOS it needs to be defined or zstd wont build
args.defines += ';' + 'ZSTD_STATIC_LINKING_ONLY'
args.defines += ';' + getProfile(args.profile).get('defines', '')
if args.metalangle:
  args.defines += ';' + '_MASSIF_USE_METALANGLE'
else:
  if list(filter(lambda arch: arch.endswith('-maccatalyst'), args.iosarch)):
    print('Mac Catalyst builds are only supported with MetalANGLE')
    sys.exit(-1)
args.cmakeoptions += ';' + getProfile(args.profile).get('cmake-options', '')

args.devteam = os.environ.get('IOS_DEV_TEAM', None)
if args.sharedlib:
  if args.devteam is None:
    print("Shared library requires development team, IOS_DEV_TEAM variable not set")
    sys.exit(-1)

if not os.path.exists("%s/generated/ios-objc/proxies" % getBaseDir()):
  print("Proxies/wrappers not generated yet, run swigpp script first.")
  sys.exit(-1)

if not checkExecutable(args.cmake, '--help'):
  print('Failed to find CMake executable. Use --cmake to specify its location')
  sys.exit(-1)

for arch in args.iosarch:
  if not buildIOSLib(args, arch):
    sys.exit(-1)

if args.buildxcframework:
  if not buildIOSXCFramework(args, args.iosarch):
    sys.exit(-1)
else:
  if not buildIOSFramework(args, args.iosarch):
    sys.exit(-1)

if not buildIOSPackage(args):
  sys.exit(-1)
