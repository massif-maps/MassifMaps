/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VALHALLAONLINEROUTINGSERVICE_H_
#define _MASSIF_VALHALLAONLINEROUTINGSERVICE_H_

#ifdef _MASSIF_ROUTING_SUPPORT

#include "routing/RoutingService.h"

#include <memory>
#include <mutex>
#include <string>

namespace sqlite3pp {
    class database;
}

namespace massif {

    /**
     * An online routing service using a (MapBox) Valhalla service, provided "as-is": check the provider's terms.
     * Routing and route matching make network requests and must run on a non-UI background thread.
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class ValhallaOnlineRoutingService : public RoutingService {
    public:
        /**
         * Constructs a new ValhallaOnlineRoutingService instance given an API key.
         * @param apiKey The API key (access token) to use registered with MapBox.
         */
        explicit ValhallaOnlineRoutingService(const std::string& apiKey);
        explicit ValhallaOnlineRoutingService();
        virtual ~ValhallaOnlineRoutingService();

        /**
         * Returns the custom backend service URL.
         * @return The custom backend service URL. If this is not defined, an empty string is returned.
         */
        std::string getCustomServiceURL() const;
        /**
         * Sets the custom backend service URL. The tag "{service}" is substituted with the service type
         * ("route" or "trace_route"), the optional tag "{api_key}" with the API key.
         * @param serviceURL The custom backend service URL to use. If this is empty, then the default service is used.
         */
        void setCustomServiceURL(const std::string& serviceURL);
         
        /**
         * Returns the current timeout value.
         * @return The current timeout value in seconds. If negative, then default platform-specific timeout is used.
         */
        int getTimeout() const;
        /**
         * Sets the current timeout value.
         * @param timeout The new timeout value in seconds. If negative, then default platform-specific timeout is used.
         */
        void setTimeout(int timeout);

        /**
         * Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders.
         * @returns The current set of custom HTTP headers.
         */
        std::map<std::string, std::string> getHTTPHeaders() const;
        /**
         * Sets HTTP headers for all subsequent requests.
         * @param headers A map of HTTP headers that will be used in subsequent requests.
         */
        void setHTTPHeaders(const std::map<std::string, std::string>& headers);

        virtual std::string getProfile() const;
        virtual void setProfile(const std::string& profile);

        virtual std::shared_ptr<RouteMatchingResult> matchRoute(const std::shared_ptr<RouteMatchingRequest>& request) const;

        virtual std::shared_ptr<RoutingResult> calculateRoute(const std::shared_ptr<RoutingRequest>& request) const;

    private:
        static const std::string MAPBOX_SERVICE_URL;

        const std::string _apiKey;

        std::string _profile;

        std::string _serviceURL;

        mutable std::mutex _mutex;

        int _timeout;
        std::map<std::string, std::string> _headers;
    };
    
}

#endif

#endif
