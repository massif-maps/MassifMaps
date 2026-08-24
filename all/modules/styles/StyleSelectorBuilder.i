#ifndef _STYLESELECTORBUILDER_I
#define _STYLESELECTORBUILDER_I

%module StyleSelectorBuilder

#ifdef _MASSIF_GDAL_SUPPORT

!proxy_imports(massif::Style, styles.Style, styles.StyleSelector)

%{
#include "styles/StyleSelectorBuilder.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/Style.i"
%import "styles/StyleSelector.i"

!shared_ptr(massif::StyleSelectorBuilder, styles.StyleSelectorBuilder)

!spec(massif::StyleSelectorBuilder, elementstyle, style-selector)

!objc_rename(addFilterRule) massif::StyleSelectorBuilder::addRule(const std::string&, const std::shared_ptr<Style>&);
%ignore massif::StyleSelectorBuilder::addRule(const std::shared_ptr<StyleSelectorRule>&);

%include "styles/StyleSelectorBuilder.h"

#endif

#endif
