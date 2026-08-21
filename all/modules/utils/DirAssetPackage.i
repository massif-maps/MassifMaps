#ifndef _DIRASSETPACKAGE_I
#define _DIRASSETPACKAGE_I

%module(directors="1") DirAssetPackage

!proxy_imports(massif::DirAssetPackage, core.BinaryData, core.StringVector, utils.AssetPackage)

%{
#include "utils/DirAssetPackage.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/BinaryData.i"
%import "core/StringVector.i"
%import "utils/AssetPackage.i"

!polymorphic_shared_ptr(massif::DirAssetPackage, utils.DirAssetPackage)


!spec(massif::DirAssetPackage, assets, dir, alias(path, dirPath), alias(base, baseAssetPackage))
%attributestring(massif::DirAssetPackage, std::string, DirPath, getDirPath)
%attributeval(massif::DirAssetPackage, %arg(std::vector<std::string>), LocalAssetNames, getLocalAssetNames)
%std_io_exceptions(massif::DirAssetPackage::DirAssetPackage)

%include "utils/DirAssetPackage.h"

#endif
