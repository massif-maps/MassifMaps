#ifndef _MASSIFAPI_I
#define _MASSIFAPI_I

%module MassifApi

!proxy_imports(massif::api::MassifApi, components.Options, datasources.TileDataSource)

%{
#include "api/MassifApi.h"
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "components/Options.i"
%import "datasources/TileDataSource.i"

%include "api/MassifApi.h"

#endif
