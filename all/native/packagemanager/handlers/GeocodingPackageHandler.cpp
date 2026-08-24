#if defined(_MASSIF_GEOCODING_SUPPORT) && defined(_MASSIF_PACKAGEMANAGER_SUPPORT)

#include "GeocodingPackageHandler.h"
#include "packagemanager/PackageTileMask.h"
#include "utils/Log.h"

#include <stdext/utf8_filesystem.h>
#include <stdext/ungzip.h>

#include <sqlite3pp.h>

namespace massif {

    GeocodingPackageHandler::GeocodingPackageHandler(const std::string& fileName) :
        PackageHandler(fileName),
        _uncompressedFileName(fileName + ".uncompressed"),
        _packageDb()
    {
    }

    GeocodingPackageHandler::~GeocodingPackageHandler() {
    }

    std::shared_ptr<sqlite3pp::database> GeocodingPackageHandler::getDatabase() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        if (!_packageDb) {
            try {
                // Open package database
                _packageDb = std::make_shared<sqlite3pp::database>();
                if (_packageDb->connect_v2(_uncompressedFileName.c_str(), SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX) != SQLITE_OK) { // try locally uncompressed package first
                    if (_packageDb->connect_v2(_fileName.c_str(), SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX) != SQLITE_OK) { // assume that the package was not gzipped, so use original file
                        Log::Errorf("GeocodingPackageHandler::getDatabase: Can not connect to database %s", _fileName.c_str());
                        _packageDb.reset();
                    }
                }
                if (_packageDb) {
                    _packageDb->execute("PRAGMA temp_store=MEMORY");
                    _packageDb->execute("PRAGMA cache_size=256");
                }
            }
            catch (const std::exception& ex) {
                Log::Errorf("GeocodingPackageHandler::getDatabase: Exception %s", ex.what());
                _packageDb.reset();
            }
        }
        return _packageDb;
    }

    void GeocodingPackageHandler::onImportPackage() {
        // Check before wrapping: shared_ptr runs the deleter even on a null pointer, and
        // fclose(nullptr) aborts the process under FORTIFY
        FILE* fpInRaw = utf8_filesystem::fopen(_fileName.c_str(), "rb");
        if (!fpInRaw) {
            Log::Errorf("GeocodingPackageHandler::onImportPackage: Failed to open %s", _fileName.c_str());
            return;
        }
        std::shared_ptr<FILE> fpIn(fpInRaw, fclose);
        FILE* fpOutRaw = utf8_filesystem::fopen(_uncompressedFileName.c_str(), "wb");
        if (!fpOutRaw) {
            Log::Errorf("GeocodingPackageHandler::onImportPackage: Failed to create %s", _uncompressedFileName.c_str());
            return;
        }
        std::shared_ptr<FILE> fpOut(fpOutRaw, fclose);
        if (!zlib::ungzip_file(fpIn.get(), fpOut.get())) {
            fpOut.reset();
            utf8_filesystem::unlink(_uncompressedFileName.c_str());
        }
    }

    void GeocodingPackageHandler::onDeletePackage() {
        utf8_filesystem::unlink(_uncompressedFileName.c_str());
    }

    std::shared_ptr<PackageTileMask> GeocodingPackageHandler::calculateTileMask() const {
        return std::shared_ptr<PackageTileMask>();
    }

}

#endif
