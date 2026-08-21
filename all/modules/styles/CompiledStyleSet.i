#ifndef _COMPILEDSTYLESET_I
#define _COMPILEDSTYLESET_I

%module CompiledStyleSet

!proxy_imports(massif::CompiledStyleSet, utils.AssetPackage)

%{
#include "styles/CompiledStyleSet.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "utils/AssetPackage.i"

!shared_ptr(massif::CompiledStyleSet, styles.CompiledStyleSet)


!spec(massif::CompiledStyleSet, styleset, project, alias(assets, assetPackage), alias(name, styleName))
%attributestring(massif::CompiledStyleSet, std::string, StyleName, getStyleName)
%attributestring(massif::CompiledStyleSet, std::string, StyleAssetName, getStyleAssetName)
%attributestring(massif::CompiledStyleSet, std::shared_ptr<massif::AssetPackage>, AssetPackage, getAssetPackage)
%std_exceptions(massif::CompiledStyleSet::CompiledStyleSet)
!standard_equals(massif::CompiledStyleSet);

%include "styles/CompiledStyleSet.h"

#endif
