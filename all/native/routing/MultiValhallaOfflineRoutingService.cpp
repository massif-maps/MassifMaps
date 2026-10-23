#if defined(_MASSIF_ROUTING_SUPPORT) && defined(_MASSIF_VALHALLA_ROUTING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT)

#include "MultiValhallaOfflineRoutingService.h"
#include "components/Exceptions.h"
#include "routing/utils/ValhallaRoutingProxy.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <boost/algorithm/string.hpp>

namespace massif {

    MultiValhallaOfflineRoutingService::MultiValhallaOfflineRoutingService() :
        _profile("pedestrian"),
        _localDbs(),
        _configuration(ValhallaRoutingProxy::GetDefaultConfiguration()),
        _mutex()
    {
    }

    MultiValhallaOfflineRoutingService::~MultiValhallaOfflineRoutingService() {
    }

    void MultiValhallaOfflineRoutingService::accessLocalPackages(const std::function<void(const std::map<std::string, std::shared_ptr<ValhallaRoutingPackageHandler> >&)>& callback) const {
        // NOTE: should use shared_lock here, but iOS 9 does not support it
        std::unique_lock<std::mutex> packageLock(_packageFileMutex);

        // Find all package handlers
        std::map<std::string, std::shared_ptr<ValhallaRoutingPackageHandler> > packageHandlerMap;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            for (const std::string& localDb : _localDbs) {
                auto it = _packageHandlerCache.find(localDb);
                if (it == _packageHandlerCache.end()) {
                    auto handler = std::make_shared<ValhallaRoutingPackageHandler>(localDb);
                    if (!handler) {
                        continue;
                    }
                    it = _packageHandlerCache.insert(std::make_pair(localDb, handler)).first;
                }
                packageHandlerMap[localDb] = it->second;
            }
        }

        // Use the callback
        callback(packageHandlerMap);
    }


    Variant MultiValhallaOfflineRoutingService::getConfigurationParameter(const std::string& param) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::vector<std::string> keys;
        boost::split(keys, param, boost::is_any_of("."));
        picojson::value subValue = _configuration.toPicoJSON();
        for (const std::string& key : keys) {
            if (!subValue.is<picojson::object>()) {
                return Variant();
            }
            subValue = subValue.get(key);
        }
        return Variant::FromPicoJSON(subValue);
    }

    void MultiValhallaOfflineRoutingService::setConfigurationParameter(const std::string& param, const Variant& value) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::vector<std::string> keys;
        boost::split(keys, param, boost::is_any_of("."));
        picojson::value config = _configuration.toPicoJSON();
        picojson::value* subValue = &config;
        for (const std::string& key : keys) {
            if (!subValue->is<picojson::object>()) {
                subValue->set(picojson::object());
            }
            subValue = &subValue->get<picojson::object>()[key];
        }
        *subValue = value.toPicoJSON();
        _configuration = Variant::FromPicoJSON(config);
    }

    void MultiValhallaOfflineRoutingService::add(const std::string &database)
    {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto it = std::find_if(_localDbs.begin(), _localDbs.end(), [&database](const std::string db)
            { return db.compare(database) == 0; });
            if (it != _localDbs.end()) {
                return;
            }
            _localDbs.emplace_back(database);
        }
    }


    bool MultiValhallaOfflineRoutingService::remove(const std::string &database)
    {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto it = std::find_if(_localDbs.begin(), _localDbs.end(), [&database](const std::string db)
            { return db.compare(database) == 0; });
            if (it == _localDbs.end()) {
                return false;
            }
            _localDbs.erase(it);
        }
        return true;
    }

    std::string MultiValhallaOfflineRoutingService::getProfile() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _profile;
    }

    void MultiValhallaOfflineRoutingService::setProfile(const std::string& profile) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _profile = profile;
    }


    std::shared_ptr<RouteMatchingResult> MultiValhallaOfflineRoutingService::matchRoute(const std::shared_ptr<RouteMatchingRequest>& request) const {
        if (!request) {
            throw NullArgumentException("Null request");
        }

        // Do routing via package manager, so that all packages are locked during routing
        std::shared_ptr<RouteMatchingResult> result;
        accessLocalPackages([this, &result, &request](const std::map<std::string, std::shared_ptr<ValhallaRoutingPackageHandler> >& packageHandlerMap) {
            // Build list of routing package files
            std::vector<std::string> packageDatabases;
            for (auto it = packageHandlerMap.begin(); it != packageHandlerMap.end(); it++) {
                if (auto valhallaRoutingHandler = std::dynamic_pointer_cast<ValhallaRoutingPackageHandler>(it->second)) {
                    packageDatabases.push_back(valhallaRoutingHandler->getFilePath());
                }
            }

            // Copy routing parameters
            std::string profile;
            Variant configuration;
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                profile = _profile;
                configuration = _configuration;
            }

            result = ValhallaRoutingProxy::MatchRoute(packageDatabases, profile, configuration, request);
        });

        return result;
    }

    std::shared_ptr<RoutingResult> MultiValhallaOfflineRoutingService::calculateRoute(const std::shared_ptr<RoutingRequest>& request) const {
        if (!request) {
            throw NullArgumentException("Null request");
        }

        // Do routing via package manager, so that all packages are locked during routing
        std::shared_ptr<RoutingResult> result;
        accessLocalPackages([this, &result, &request](const std::map<std::string, std::shared_ptr<ValhallaRoutingPackageHandler> >& packageHandlerMap) {
            // Build list of routing package files
            std::vector<std::string> packageDatabases;
            for (auto it = packageHandlerMap.begin(); it != packageHandlerMap.end(); it++) {
                if (auto valhallaRoutingHandler = std::dynamic_pointer_cast<ValhallaRoutingPackageHandler>(it->second)) {
                    packageDatabases.push_back(valhallaRoutingHandler->getFilePath());
                }
            }

            // Copy routing parameters
            std::string profile;
            Variant configuration;
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                profile = _profile;
                configuration = _configuration;
            }

            result = ValhallaRoutingProxy::CalculateRoute(packageDatabases, profile, configuration, request);
        });

        return result;
    }

    void MultiValhallaOfflineRoutingService::addLocale(const std::string& key, const std::string& json) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        ValhallaRoutingProxy::AddLocale(key, json);
    }
}

#endif
