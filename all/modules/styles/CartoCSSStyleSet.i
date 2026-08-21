#ifndef _CARTOCSSSTYLESET_I
#define _CARTOCSSSTYLESET_I

%module CartoCSSStyleSet

!proxy_imports(massif::CartoCSSStyleSet, utils.AssetPackage)

%{
#include "styles/CartoCSSStyleSet.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_map.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "utils/AssetPackage.i"

!shared_ptr(massif::CartoCSSStyleSet, styles.CartoCSSStyleSet)


!spec(massif::CartoCSSStyleSet, styleset, cartocss, alias(css, cartoCSS), alias(assets, assetPackage))
%attributestring(massif::CartoCSSStyleSet, std::string, CartoCSS, getCartoCSS)
%attributestring(massif::CartoCSSStyleSet, std::shared_ptr<massif::AssetPackage>, AssetPackage, getAssetPackage)
%std_exceptions(massif::CartoCSSStyleSet::CartoCSSStyleSet)
!standard_equals(massif::CartoCSSStyleSet);

%include "styles/CartoCSSStyleSet.h"

!value_template(std::map<std::string, std::shared_ptr<massif::CartoCSSStyleSet> >, styles.StringCartoCSSStyleSetMap);

#endif
