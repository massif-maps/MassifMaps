#ifndef _MASSIFINTEROP_I
#define _MASSIFINTEROP_I

%module MassifInterop

!proxy_imports(massif::api::MassifInterop, components.Options, datasources.TileDataSource, layers.Layer, components.Layers, ui.BaseMapView, ui.MapEventListener, layers.VectorTileEventListener, layers.VectorElementEventListener, utils.AssetPackage)

%{
#include "api/MassifInterop.h"
#include "components/Exceptions.h"
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "components/Options.i"
%import "datasources/TileDataSource.i"
%import "layers/Layer.i"
%import "components/Layers.i"
%import "ui/BaseMapView.i"
%import "ui/MapEventListener.i"
%import "layers/VectorTileEventListener.i"
%import "layers/VectorElementEventListener.i"
%import "utils/AssetPackage.i"

%include "api/MassifInterop.h"

#endif
