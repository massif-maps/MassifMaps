#ifndef _EVENTLISTENER_I
#define _EVENTLISTENER_I

%module(directors="1") EventListener

%{
#include "api/EventListener.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

!polymorphic_shared_ptr(massif::api::EventListener, api.EventListener)

%feature("director") massif::api::EventListener;

%include "api/EventListener.h"

#endif
