#ifndef _MASSIFAPI_I
#define _MASSIFAPI_I

%module(directors="1") MassifApi

!proxy_imports(massif::api::MassifApi, components.Options, datasources.TileDataSource, layers.Layer, ui.MapEventListener, api.EventListener, layers.VectorTileEventListener, layers.VectorElementEventListener)

%{
#include "api/MassifApi.h"
#include "components/Exceptions.h"
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>


%import "components/Options.i"
%import "datasources/TileDataSource.i"
%import "layers/Layer.i"
%import "ui/MapEventListener.i"
%import "layers/VectorTileEventListener.i"
%import "layers/VectorElementEventListener.i"
%import "api/EventListener.i"

%std_exceptions(massif::api::MassifApi::create)

%include "api/MassifApi.h"

#endif
