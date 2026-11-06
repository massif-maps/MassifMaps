## How to contribute:

* [Add issues](https://github.com/massif-maps/MassifMaps/issues) for found bugs and new feature ideas. Check first if issue for specific thing does not already exist
* See [README.md](https://github.com/massif-maps/MassifMaps/blob/master/README.md) for compilation guides
* Use Pull Requests to contribute with own code additions

Additional developer channels and more detailed guidelines will be added soon.

## Submitting contributions

There are several rules you should follow when a new pull request is created:

- Title has to be descriptive. If you are fixing a bug don't use the ticket title or number.
- Explain what you have achieved in the description and choose a reviewer for your code. If you have doubts, just ask for one.

## Development environments

* [iOS instructions](https://github.com/massif-maps/MassifMaps/tree/master/scripts/ios-dev)
* [Android instructions](https://github.com/massif-maps/MassifMaps/tree/master/scripts/android-dev)

## Directories

* all/native - C++ sources for all platforms, main part of the SDK
* all/modules - Swig configuration files for all platforms
* android - Android-specific (C++, Java) code, Swig wrapper config and helpers
* ios - iOS-specific (C++, Objective-C) code, Swig wrapper config and helpers
* winphone - Windows Phone-specific (C++, C#) code, Swig wrapper config and helpers
* dotnet - generic C# code used by the Windows Phone build
* assets - images and style files sources used for map rendering. Converted to C++ headers
* docs - documentation; the published site is built from `website/`
* scripts - scripts for building SDK (using Python, CMake, Swig and other tools)


## valhalla

If you need to update the Valhalla submodule you might need to update the proto files.
```
cd libs-external/protobuf/protobuf
make -j$(nproc)
./src/protoc --proto_path ../../valhalla/valhalla/proto/ --cpp_out ../../valhalla/proto/valhalla/proto/ api.proto common.proto directions.proto incidents.proto info.proto options.proto sign.proto status.proto transit.proto transit_fetch.proto trip.proto matrix.proto isochrone.proto expansion.proto
```
You might also need to update locales
```
cd libs-external/valhalla/valhalla
cmake -DMSVC= -P cmake/ValhallaBin2Header.cmake path_to_wanted_valhalla_json_locales/ ../config/valhalla/odin/locales.h --locales