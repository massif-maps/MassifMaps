#ifndef _STYLESELECTOR_I
#define _STYLESELECTOR_I

%module StyleSelector

#ifdef _MASSIF_GDAL_SUPPORT

%{
#include "styles/StyleSelector.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

!shared_ptr(massif::StyleSelector, styles.StyleSelector)

%ignore massif::StyleSelector::StyleSelector;
%ignore massif::StyleSelector::getStyle;
!standard_equals(massif::StyleSelector);

// '-' like every other style: the constructor is ignored above, so the builder is the only way in.
!spec(massif::StyleSelector, elementstyle, -)

%include "styles/StyleSelector.h"

#endif

#endif
