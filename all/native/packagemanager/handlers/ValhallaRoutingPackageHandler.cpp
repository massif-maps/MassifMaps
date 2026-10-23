#if defined(_MASSIF_VALHALLA_ROUTING_SUPPORT) && defined(_MASSIF_PACKAGEMANAGER_SUPPORT)

#include "ValhallaRoutingPackageHandler.h"
#include "packagemanager/PackageTileMask.h"
#include "utils/Log.h"

#include <stdext/zlib.h>

namespace massif {

    ValhallaRoutingPackageHandler::ValhallaRoutingPackageHandler(const std::string& fileName) :
        PackageHandler(fileName)
    {
    }

    ValhallaRoutingPackageHandler::~ValhallaRoutingPackageHandler() {
    }

    void ValhallaRoutingPackageHandler::onImportPackage() {
    }

    void ValhallaRoutingPackageHandler::onDeletePackage() {
    }

    std::shared_ptr<PackageTileMask> ValhallaRoutingPackageHandler::calculateTileMask() const {
        return std::shared_ptr<PackageTileMask>();
    }

}

#endif
