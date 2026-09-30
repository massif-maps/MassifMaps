#if defined(_MASSIF_GEOCODING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT) && defined(_MASSIF_PACKAGEMANAGER_SUPPORT)

#include "MultiOSMOfflineGeocodingService.h"
#include "components/Exceptions.h"
#include "geocoding/utils/MassifGeocodingProxy.h"
#include "packagemanager/handlers/GeocodingPackageHandler.h"

#include <geocoding/Geocoder.h>

#include <sqlite3pp.h>

namespace massif {

    MultiOSMOfflineGeocodingService::MultiOSMOfflineGeocodingService() :
        _autocomplete(false),
        _language(),
        _maxResults(10),
        _cachedPackageDatabaseMap(),
        _cachedGeocoder(),
        _mutex()
    {
    }

    MultiOSMOfflineGeocodingService::~MultiOSMOfflineGeocodingService() {
    }

    bool MultiOSMOfflineGeocodingService::isAutocomplete() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _autocomplete;
    }

    void MultiOSMOfflineGeocodingService::setAutocomplete(bool autocomplete) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (autocomplete != _autocomplete) {
            _autocomplete = autocomplete;
            _cachedGeocoder.reset();
        }
    }

    std::string MultiOSMOfflineGeocodingService::getLanguage() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _language;
    }

    void MultiOSMOfflineGeocodingService::setLanguage(const std::string& lang) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (lang != _language) {
            _language = lang;
            _cachedGeocoder.reset();
        }
    }

    int MultiOSMOfflineGeocodingService::getMaxResults() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _maxResults;
    }

    void MultiOSMOfflineGeocodingService::setMaxResults(int maxResults) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (maxResults != _maxResults) {
            _maxResults = maxResults;
            _cachedGeocoder.reset();
        }
    }

    void MultiOSMOfflineGeocodingService::accessLocalPackages(const std::function<void(const std::map<std::string, std::shared_ptr<GeocodingPackageHandler> >&)>& callback) const {
        // NOTE: should use shared_lock here, but iOS 9 does not support it
        std::unique_lock<std::mutex> packageLock(_packageFileMutex);

        // Find all package handlers
        std::map<std::string, std::shared_ptr<GeocodingPackageHandler> > packageHandlerMap;
        {
            std::lock_guard<std::mutex> lock(_mutex);

            for (const std::string& localDb : _localDbs) {
                auto it = _packageHandlerCache.find(localDb);
                if (it == _packageHandlerCache.end()) {
                    auto handler = std::make_shared<GeocodingPackageHandler>(localDb);
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

    std::vector<std::shared_ptr<GeocodingResult> > MultiOSMOfflineGeocodingService::calculateAddresses(const std::shared_ptr<GeocodingRequest>& request) const {
        if (!request) {
            throw NullArgumentException("Null request");
        }

        // Do routing via package manager, so that all packages are locked during routing
        std::vector<std::shared_ptr<GeocodingResult> > results;
        accessLocalPackages([this, &results, &request](const std::map<std::string, std::shared_ptr<GeocodingPackageHandler> >& packageHandlerMap) {
            // Build map of geocoding databases
            std::map<std::string, std::shared_ptr<sqlite3pp::database> > packageDatabaseMap;
            for (auto it = packageHandlerMap.begin(); it != packageHandlerMap.end(); it++) {
                if (auto geocodingHandler = std::dynamic_pointer_cast<GeocodingPackageHandler>(it->second)) {
                    if (auto packageDatabase = geocodingHandler->getDatabase()) {
                        packageDatabaseMap[it->first] = packageDatabase;
                    }
                }
            }

            // Now check if we have to reinitialize the geocoder
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_cachedGeocoder || packageDatabaseMap != _cachedPackageDatabaseMap) {
                auto geocoder = std::make_shared<geocoding::Geocoder>();
                geocoder->setAutocomplete(_autocomplete);
                geocoder->setLanguage(_language);
                geocoder->setMaxResults(_maxResults);
                for (auto it = packageDatabaseMap.begin(); it != packageDatabaseMap.end(); it++) {
                    try {
                        if (!geocoder->import(it->second)) {
                            throw FileException("Failed to import geocoding database " + it->first, "");
                        }
                    }
                    catch (const std::exception& ex) {
                        throw GenericException("Exception while importing geocoding database " + it->first, ex.what());
                    }
                }
                _cachedPackageDatabaseMap = packageDatabaseMap;
                _cachedGeocoder = geocoder;
            }

            results = MassifGeocodingProxy::CalculateAddresses(_cachedGeocoder, request);
        });
        return results;
    }
    void MultiOSMOfflineGeocodingService::add(const std::string &database)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = std::find_if(_localDbs.begin(), _localDbs.end(), [&database](const std::string db)
            { return db.compare(database) == 0; });
            if (it != _localDbs.end()) {
                return;
            }
            _localDbs.emplace_back(database);
        }
    }


    bool MultiOSMOfflineGeocodingService::remove(const std::string &database)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = std::find_if(_localDbs.begin(), _localDbs.end(), [&database](const std::string db)
            { return db.compare(database) == 0; });
            if (it == _localDbs.end()) {
                return false;
            }
            _localDbs.erase(it);
        }
        return true;
    }
}

#endif
