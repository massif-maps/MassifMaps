/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_GEOCODINGMETHODS_H_
#define _MASSIF_API_GEOCODINGMETHODS_H_

#ifdef _MASSIF_GEOCODING_SUPPORT

namespace massif { namespace api {

    /** The geocoding methods. Its own TU so a reduced table links without the geocoder. */
    void registerGeocodingMethods();

} }

#endif

#endif
