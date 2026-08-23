#ifndef _BUNDLEASSETPACKAGE_I
#define _BUNDLEASSETPACKAGE_I

%module(directors="1") BundleAssetPackage

!proxy_imports(massif::BundleAssetPackage, core.BinaryData, core.StringVector, utils.AssetPackage)

%{
#include "utils/BundleAssetPackage.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/BinaryData.i"
%import "core/StringVector.i"
%import "utils/AssetPackage.i"

!polymorphic_shared_ptr(massif::BundleAssetPackage, utils.BundleAssetPackage)


!spec(massif::BundleAssetPackage, assets, bundle, alias(path, basePath), alias(base, baseAssetPackage))
%attributestring(massif::BundleAssetPackage, std::string, BasePath, getBasePath)
%attributeval(massif::BundleAssetPackage, %arg(std::vector<std::string>), LocalAssetNames, getLocalAssetNames)

%include "utils/BundleAssetPackage.h"

#endif
