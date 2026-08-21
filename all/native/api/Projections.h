/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_PROJECTIONS_H_
#define _MASSIF_API_PROJECTIONS_H_

#include <memory>
#include <string>

namespace massif {
    class Projection;

    namespace api {

    /**
     * Projections by their well-known name, so a caller can ask for "EPSG:4326" without holding
     * a Projection object - which a C or JavaScript binding cannot.
     */
    namespace Projections {

        /**
         * Looks up a projection by name, case-insensitively. EPSG:3857 and EPSG:4326 are built in.
         * @return The projection, or null when the name is not registered.
         */
        std::shared_ptr<Projection> find(const std::string& name);

        /**
         * Adds a projection under its own getName(). Lets a plugin make its projection reachable
         * by name without the facade knowing the class.
         */
        void registerProjection(const std::shared_ptr<Projection>& projection);

    }

} }

#endif
