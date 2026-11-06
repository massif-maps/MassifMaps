/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VALHALLAROUTINGPACKAGEHANDLER_H_
#define _MASSIF_VALHALLAROUTINGPACKAGEHANDLER_H_

#if defined(_MASSIF_VALHALLA_ROUTING_SUPPORT) && defined(_MASSIF_PACKAGEMANAGER_SUPPORT)

#include "core/MapTile.h"
#include "packagemanager/handlers/PackageHandler.h"

namespace massif {
    
    class ValhallaRoutingPackageHandler : public PackageHandler {
    public:
        explicit ValhallaRoutingPackageHandler(const std::string& fileName);
        virtual ~ValhallaRoutingPackageHandler();

        const std::string& getFilePath() const { return _fileName; }

        virtual void onImportPackage();
        virtual void onDeletePackage();

        virtual std::shared_ptr<PackageTileMask> calculateTileMask() const;
    };
    
}

#endif

#endif
