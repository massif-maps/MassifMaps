/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_ROUTINGMETHODS_H_
#define _MASSIF_API_ROUTINGMETHODS_H_

#ifdef _MASSIF_ROUTING_SUPPORT

namespace massif { namespace api {

    /** Registers the routing methods; own TU so host tests link them without the Valhalla services. */
    void registerRoutingMethods();

} }

#endif

#endif
