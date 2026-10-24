/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VALHALLAROUTINGPROXY_H_
#define _MASSIF_VALHALLAROUTINGPROXY_H_

#ifdef _MASSIF_ROUTING_SUPPORT

#include "core/Variant.h"
#include "routing/RoutingInstruction.h"

#include <memory>
#include <vector>

namespace massif {
    class HTTPClient;
    class Projection;
    class RoutingRequest;
    class RoutingResult;
    class RouteMatchingRequest;
    class RouteMatchingResult;
    class TileDataSource;
    class ElevationDecoder;
    
    class ValhallaRoutingProxy {
    public:
        static std::shared_ptr<RouteMatchingResult> MatchRoute(HTTPClient& httpClient, const std::string& baseURL, const std::string& profile, const std::shared_ptr<RouteMatchingRequest>& request, std::map<std::string, std::string>& headers);
        static std::shared_ptr<RoutingResult> CalculateRoute(HTTPClient& httpClient, const std::string& baseURL, const std::string& profile, const std::shared_ptr<RoutingRequest>& request, std::map<std::string, std::string>& headers);

#ifdef _MASSIF_VALHALLA_ROUTING_SUPPORT
        static std::shared_ptr<RouteMatchingResult> MatchRoute(const std::vector<std::string>& databasePaths, const std::string& profile, const Variant& config, const std::shared_ptr<RouteMatchingRequest>& request);
        static std::shared_ptr<RoutingResult> CalculateRoute(const std::vector<std::string>& databasePaths, const std::string& profile, const Variant& config, const std::shared_ptr<RoutingRequest>& request);
        static void AddLocale(const std::string& key, const std::string& json);
        static Variant GetDefaultConfiguration();
#endif

    private:
        ValhallaRoutingProxy();

        static bool TranslateManeuverType(int maneuverType, RoutingAction::RoutingAction& action);

        static std::string SerializeRouteMatchingRequest(const std::string& profile, const std::shared_ptr<RouteMatchingRequest>& request);

        static std::string SerializeRoutingRequest(const std::string& profile, const std::shared_ptr<RoutingRequest>& request);

        static std::shared_ptr<RouteMatchingResult> ParseRouteMatchingResult(const std::shared_ptr<Projection>& proj, const std::string& resultString);

        static std::shared_ptr<RoutingResult> ParseRoutingResult(const std::shared_ptr<Projection>& proj, const std::string& resultString);

        static std::string MakeHTTPRequest(HTTPClient& httpClient, const std::string& url, std::map<std::string, std::string>& headers);
  };

}

#endif

#endif
