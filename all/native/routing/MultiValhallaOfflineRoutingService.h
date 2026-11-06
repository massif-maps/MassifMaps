/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MULTIVALHALLAOFFLINEROUTINGSERVICE_H_
#define _MASSIF_MULTIVALHALLAOFFLINEROUTINGSERVICE_H_

#if defined(_MASSIF_ROUTING_SUPPORT) && defined(_MASSIF_VALHALLA_ROUTING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT)

#include "core/Variant.h"
#include "routing/RoutingService.h"
#include "packagemanager/handlers/ValhallaRoutingPackageHandler.h"

#include <memory>
#include <mutex>
#include <string>

namespace massif {

    /**
     * An offline routing service that uses Valhalla routing tiles.
     */
    class MultiValhallaOfflineRoutingService : public RoutingService {
    public:
        /**
         * Constructs a new MultiValhallaOfflineRoutingService instance.
         */
        explicit MultiValhallaOfflineRoutingService();
        virtual ~MultiValhallaOfflineRoutingService();

        /**
         * Returns the value of specified Valhalla configuration parameter.
         * @param param The name of the parameter. For example, "meili.auto.search_radius".
         * @return The value of the parameter. If the parameter does not exist, empty variant is returned.
         */
        Variant getConfigurationParameter(const std::string& param) const;
        /**
         * Sets the value of specified Valhalla configuration parameter.
         * @param param The name of the parameter. For example, "meili.auto.search_radius".
         * @param value The new value of the parameter.
         */
        void setConfigurationParameter(const std::string& param, const Variant& value);

        virtual std::string getProfile() const;
        virtual void setProfile(const std::string& profile);

        virtual std::shared_ptr<RouteMatchingResult> matchRoute(const std::shared_ptr<RouteMatchingRequest>& request) const;

        virtual std::shared_ptr<RoutingResult> calculateRoute(const std::shared_ptr<RoutingRequest>& request) const;

        /**
         * Adds a new database.
         * @param database The database file patht to be added.
         */
        void add(const std::string&  database);

        /**
         * Removes a database.
         * @param database The database file patht to be removed.
         * @return True if the  database was removed. False otherwise ( database was not found).
         */
        bool remove(const std::string&  database);

        void addLocale(const std::string& key, const std::string& json) const;
    private:
        void accessLocalPackages(const std::function<void(const std::map<std::string, std::shared_ptr<ValhallaRoutingPackageHandler> >&)>& callback) const;

        std::string _profile;
        Variant _configuration;
        mutable std::map<std::string, std::shared_ptr<ValhallaRoutingPackageHandler> > _packageHandlerCache;
        std::vector<std::string > _localDbs;
        mutable std::mutex _packageFileMutex; // guards all package file accesses
        mutable std::recursive_mutex _mutex; // guards all state
    };
    
}

#endif

#endif
