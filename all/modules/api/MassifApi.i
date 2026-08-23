#ifndef _MASSIFAPI_I
#define _MASSIFAPI_I

%module(directors="1") MassifApi

!proxy_imports(massif::api::MassifApi, core.BinaryData, components.Options, datasources.TileDataSource, layers.Layer, components.Layers, ui.BaseMapView, ui.MapEventListener, api.EventListener, api.UiDispatcher, layers.VectorTileEventListener, layers.VectorElementEventListener, utils.AssetPackage)

%{
#include "api/MassifApi.h"
#include "components/Exceptions.h"
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/BinaryData.i"
%import "components/Options.i"
%import "datasources/TileDataSource.i"
%import "layers/Layer.i"
%import "components/Layers.i"
%import "ui/BaseMapView.i"
%import "ui/MapEventListener.i"
%import "layers/VectorTileEventListener.i"
%import "layers/VectorElementEventListener.i"
%import "utils/AssetPackage.i"
%import "api/EventListener.i"
%import "api/UiDispatcher.i"

// A bulk result is thousands of numbers, so it crosses as one array rather than as the
// DoubleVector proxy, which is a JNI call per element. AFTER the imports on purpose: DoubleVector.i
// installs its own value_type typemaps for std::vector<double> and the last one declared wins.
// getDoubles is the only one in this module, so the typemap needs no name to key on.
#if SWIGJAVA
%typemap(jni) std::vector<double> "jdoubleArray"
%typemap(jtype) std::vector<double> "double[]"
%typemap(jstype) std::vector<double> "double[]"
%typemap(javaout) std::vector<double> {
  return $jnicall;
}
%typemap(out) std::vector<double> {
  $result = jenv->NewDoubleArray(static_cast<jsize>($1.size()));
  jenv->SetDoubleArrayRegion($result, 0, static_cast<jsize>($1.size()), $1.data());
}
#endif
// The same on iOS: NSData over the raw doubles, which is what a flat array is there. Read it with
// -bytes cast to const double*, or -getBytes:length:.
#ifdef SWIGOBJECTIVEC
%typemap(objctype) std::vector<double> "NSData*"
%typemap(objcout) std::vector<double> %{
    return (__bridge_transfer NSData*)$imcall;
%}
%typemap(out) std::vector<double> %{
    $result = (__bridge_retained void*)[NSData dataWithBytes:$1.data()
                                                      length:$1.size() * sizeof(double)];
%}
#endif

%std_exceptions(massif::api::MassifApi::create)
%std_exceptions(massif::api::MassifApi::call)
%std_exceptions(massif::api::MassifApi::callAsync)

%include "api/MassifApi.h"

#endif
