#ifndef _MASSIFAPI_I
#define _MASSIFAPI_I

%module(directors="1") MassifApi

// No SDK module here on purpose: this class names none, so its wrapper depends on no
// object-API proxy (#159). Anything that needs one lives in MassifInterop.
!proxy_imports(massif::api::MassifApi, api.EventListener, api.UiDispatcher)

%{
#include "api/MassifApi.h"
#include "components/Exceptions.h"
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%include <massifswig.i>

%import "api/EventListener.i"
%import "api/UiDispatcher.i"

// Bulk results cross as one array, not the DoubleVector proxy (a JNI call per element). Must follow
// the imports: DoubleVector.i installs its own std::vector<double> typemaps and the last one wins.
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
// iOS: NSData over the raw doubles; read it with -bytes cast to const double*.
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

// A blob crosses as raw bytes for the same reason, and to keep BinaryData - an SDK type - off this
// class entirely (#159).
#if SWIGJAVA
%typemap(jni) std::vector<unsigned char> "jbyteArray"
%typemap(jtype) std::vector<unsigned char> "byte[]"
%typemap(jstype) std::vector<unsigned char> "byte[]"
%typemap(javaout) std::vector<unsigned char> {
  return $jnicall;
}
%typemap(out) std::vector<unsigned char> {
  $result = jenv->NewByteArray(static_cast<jsize>($1.size()));
  jenv->SetByteArrayRegion($result, 0, static_cast<jsize>($1.size()),
                           reinterpret_cast<const jbyte*>($1.data()));
}
%typemap(jni) const std::vector<unsigned char>& "jbyteArray"
%typemap(jtype) const std::vector<unsigned char>& "byte[]"
%typemap(jstype) const std::vector<unsigned char>& "byte[]"
%typemap(javain) const std::vector<unsigned char>& "$javainput"
%typemap(in) const std::vector<unsigned char>& (std::vector<unsigned char> bytes) {
  jsize size = jenv->GetArrayLength($input);
  bytes.resize(static_cast<size_t>(size));
  jenv->GetByteArrayRegion($input, 0, size, reinterpret_cast<jbyte*>(bytes.data()));
  $1 = &bytes;
}
#endif
#ifdef SWIGOBJECTIVEC
%typemap(objctype) std::vector<unsigned char> "NSData*"
%typemap(objcout) std::vector<unsigned char> %{
    return (__bridge_transfer NSData*)$imcall;
%}
%typemap(out) std::vector<unsigned char> %{
    $result = (__bridge_retained void*)[NSData dataWithBytes:$1.data() length:$1.size()];
%}
%typemap(objctype) const std::vector<unsigned char>& "NSData*"
%typemap(objcin) const std::vector<unsigned char>& "$objcinput"
%typemap(in) const std::vector<unsigned char>& (std::vector<unsigned char> bytes) %{
    NSData* data = (__bridge NSData*)$input;
    bytes.assign(static_cast<const unsigned char*>(data.bytes), static_cast<const unsigned char*>(data.bytes) + data.length);
    $1 = &bytes;
%}
#endif

%std_exceptions(massif::api::MassifApi::create)
%std_exceptions(massif::api::MassifApi::createData)
%std_exceptions(massif::api::MassifApi::call)
%std_exceptions(massif::api::MassifApi::callAsync)

%include "api/MassifApi.h"

#endif
