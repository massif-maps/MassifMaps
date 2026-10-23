/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VALHALLAENGINE_H_
#define _MASSIF_VALHALLAENGINE_H_

#if defined(_MASSIF_ROUTING_SUPPORT) && defined(_MASSIF_VALHALLA_ROUTING_SUPPORT)

#include <string>
#include <vector>

namespace massif {

    /**
     * The offline Valhalla engine, libvalhalla_routing.so, reached through its C ABI
     * (routing-lib/native/engine/valhalla_engine.h) and loaded on first use.
     */
    class ValhallaEngine {
    public:
        /** @throws GenericException If the engine is not shipped with the app or has another ABI version. */
        static const ValhallaEngine& Get();

        std::string getDefaultConfiguration() const;
        void addLocale(const std::string& key, const std::string& json) const;
        std::string call(const std::vector<std::string>& paths, const std::string& configuration, const std::string& endpoint, const std::string& body) const;

    private:
        ValhallaEngine();
    };

}

#endif

#endif
