/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_PELIASONLINEREVERSEGEOCODINGSERVICE_H_
#define _MASSIF_PELIASONLINEREVERSEGEOCODINGSERVICE_H_

#if defined(_MASSIF_GEOCODING_SUPPORT)

#include "geocoding/ReverseGeocodingService.h"

namespace massif {

    /**
     * An online reverse geocoding service that uses Mapzen Pelias geocoder, provided "as-is" as an external service may change incompatibly.
     * Requests go over the network and must run on a non-UI thread. Be sure to read the Terms and Conditions of your Pelias service provider.
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class PeliasOnlineReverseGeocodingService : public ReverseGeocodingService {
    public:
        /**
         * Constructs a new instance of the PeliasOnlineReverseGeocodingService given API key.
         * @param apiKey The API key to use (registered with Mapzen).
         */
        explicit PeliasOnlineReverseGeocodingService(const std::string& apiKey);
        virtual ~PeliasOnlineReverseGeocodingService();

        /**
         * Returns the custom backend service URL.
         * @return The custom backend service URL. If this is not defined, an empty string is returned.
         */
        std::string getCustomServiceURL() const;
        /**
         * Sets the custom backend service URL. 
         * The custom URL may contain tag "{api_key}" which will be substituted with the set API key.
         * @param serviceURL The custom backend service URL to use. If this is empty, then the default service is used.
         */
        void setCustomServiceURL(const std::string& serviceURL);

        virtual std::string getLanguage() const;
        virtual void setLanguage(const std::string& lang);

        virtual std::vector<std::shared_ptr<GeocodingResult> > calculateAddresses(const std::shared_ptr<ReverseGeocodingRequest>& request) const;

    protected:
        static const std::string MAPZEN_SERVICE_URL;

        const std::string _apiKey;
        std::string _language;
        std::string _serviceURL;

        mutable std::mutex _mutex;
    };
    
}

#endif

#endif
