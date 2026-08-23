#ifndef _ASSETUTILS_I
#define _ASSETUTILS_I

%module AssetUtils

!proxy_imports(massif::AssetUtils, core.BinaryData, core.StringVector)

%{
#include "utils/AssetUtils.h"
%}

%include <std_string.i>
%include <std_vector.i>

%import "core/BinaryData.i"
%import "core/StringVector.i"

%include "utils/AssetUtils.h"

#endif
