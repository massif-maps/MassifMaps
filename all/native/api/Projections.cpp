#include "api/Projections.h"
#include "projections/EPSG3857.h"
#include "projections/EPSG4326.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace massif { namespace api { namespace Projections {

    namespace {
        std::string lowered(const std::string& name) {
            std::string result = name;
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        std::mutex& mutex() {
            static std::mutex instance;
            return instance;
        }

        std::map<std::string, std::shared_ptr<Projection> >& registry() {
            static std::map<std::string, std::shared_ptr<Projection> > instance = {
                { "epsg:3857", std::make_shared<EPSG3857>() },
                { "epsg:4326", std::make_shared<EPSG4326>() }
            };
            return instance;
        }
    }

    std::shared_ptr<Projection> find(const std::string& name) {
        if (name.empty()) {
            return std::shared_ptr<Projection>();
        }
        std::lock_guard<std::mutex> lock(mutex());
        auto it = registry().find(lowered(name));
        return it == registry().end() ? std::shared_ptr<Projection>() : it->second;
    }

    void registerProjection(const std::shared_ptr<Projection>& projection) {
        if (!projection) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex());
        registry()[lowered(projection->getName())] = projection;
    }

} } }
