#ifndef _OSMOFFLINEGEOCODINGSERVICE_I
#define _OSMOFFLINEGEOCODINGSERVICE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") MultiOSMOfflineGeocodingService

#if defined(_MASSIF_GEOCODING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT)

!proxy_imports(massif::MultiOSMOfflineGeocodingService, geocoding.GeocodingService, geocoding.GeocodingRequest, geocoding.GeocodingResult, projections.Projection)

%{
#include "geocoding/MultiOSMOfflineGeocodingService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geocoding/GeocodingService.i"
%import "geocoding/GeocodingRequest.i"
%import "geocoding/GeocodingResult.i"

!polymorphic_shared_ptr(massif::MultiOSMOfflineGeocodingService, geocoding.MultiOSMOfflineGeocodingService)

// One .nutigeodb per downloaded area, found by scanning, so they are added not constructed.
!spec(massif::MultiOSMOfflineGeocodingService, geocoding, multi-osm-offline)
!method(massif::MultiOSMOfflineGeocodingService, add, arg(database, string), returns(void))
!method(massif::MultiOSMOfflineGeocodingService, remove, arg(database, string), returns(bool))

%std_io_exceptions(massif::MultiOSMOfflineGeocodingService::MultiOSMOfflineGeocodingService)
%std_io_exceptions(massif::MultiOSMOfflineGeocodingService::calculateAddresses)

%feature("director") massif::MultiOSMOfflineGeocodingService;

%include "geocoding/MultiOSMOfflineGeocodingService.h"

#endif

#endif
