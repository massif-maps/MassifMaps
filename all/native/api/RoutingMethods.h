/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_ROUTINGMETHODS_H_
#define _MASSIF_API_ROUTINGMETHODS_H_

#ifdef _MASSIF_ROUTING_SUPPORT

namespace massif { namespace api {

    /**
     * The routing methods, in their own translation unit and with their own entry point.
     *
     * Same reason as GeometryMethods: a request, a result and the RoutingService base need nothing
     * but a projection, so the host tests link these while the concrete Valhalla services - which
     * need sqlite and the routing library - stay out.
     */
    void registerRoutingMethods();

} }

#endif

#endif
