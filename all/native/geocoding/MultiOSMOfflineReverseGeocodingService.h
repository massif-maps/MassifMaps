/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MULTIOSMOFFILINEREVERSEGEOCODINGSERVICE_H_
#define _MASSIF_MULTIOSMOFFILINEREVERSEGEOCODINGSERVICE_H_

#if defined(_MASSIF_GEOCODING_SUPPORT) && defined(_MASSIF_OFFLINE_SUPPORT) && defined(_MASSIF_PACKAGEMANAGER_SUPPORT)

#include "geocoding/ReverseGeocodingService.h"
#include "packagemanager/handlers/GeocodingPackageHandler.h"

namespace sqlite3pp {
    class database;
}

namespace massif {
    namespace geocoding {
        class RevGeocoder;
    }

    /**
     * A reverse geocoding service that uses custom geocoding database files.
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class MultiOSMOfflineReverseGeocodingService : public ReverseGeocodingService {
    public:
        /**
         * Constructs a new instance of the MultiOSMOfflineReverseGeocodingService given package manager instance.
         */
        explicit MultiOSMOfflineReverseGeocodingService();
        virtual ~MultiOSMOfflineReverseGeocodingService();

        virtual std::string getLanguage() const;
        virtual void setLanguage(const std::string& lang);

        virtual std::vector<std::shared_ptr<GeocodingResult> > calculateAddresses(const std::shared_ptr<ReverseGeocodingRequest>& request) const;

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
        
    protected:
        std::string _language;

        mutable std::map<std::string, std::shared_ptr<sqlite3pp::database> > _cachedPackageDatabaseMap;
        mutable std::shared_ptr<geocoding::RevGeocoder> _cachedRevGeocoder;

        mutable std::mutex _mutex;

    private:
        void accessLocalPackages(const std::function<void(const std::map<std::string, std::shared_ptr<GeocodingPackageHandler> >&)>& callback) const;
        mutable std::map<std::string, std::shared_ptr<GeocodingPackageHandler> > _packageHandlerCache;
        std::vector<std::string > _localDbs;
        mutable std::mutex _packageFileMutex; // guards all package file accesses
    };
    
}

#endif

#endif
