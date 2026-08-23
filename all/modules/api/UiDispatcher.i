#ifndef _UIDISPATCHER_I
#define _UIDISPATCHER_I

%module(directors="1") UiDispatcher

%{
#include "api/UiDispatcher.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
// Needed even though nothing here takes a string: polymorphic_shared_ptr's generated
// swigGetClassName returns std::string, and without this it maps to a pointer.
%include <std_string.i>
%include <massifswig.i>

!polymorphic_shared_ptr(massif::api::UiDispatcher, api.UiDispatcher)

%feature("director") massif::api::UiDispatcher;

%include "api/UiDispatcher.h"

#endif
