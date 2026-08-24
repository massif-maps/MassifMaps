#ifdef _MASSIF_GEOCODING_SUPPORT

#include "GeocodingResult.h"
#include "components/Exceptions.h"
#include "geometry/Feature.h"
#include "geometry/FeatureCollection.h"
#include "geometry/GeoJSONGeometryWriter.h"
#include "projections/Projection.h"

#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace massif {

    GeocodingResult::GeocodingResult(const std::shared_ptr<Projection>& projection, const GeocodingAddress& address, float rank, const std::shared_ptr<FeatureCollection>& featureCollection) :
        _address(address),
        _rank(rank),
        _featureCollection(featureCollection),
        _projection(projection)
    {
        if (!featureCollection) {
            throw NullArgumentException("Null geometry");
        }
        if (!projection) {
            throw NullArgumentException("Null projection");
        }
    }

    GeocodingResult::~GeocodingResult() {
    }

    const GeocodingAddress& GeocodingResult::getAddress() const {
        return _address;
    }

    float GeocodingResult::getRank() const {
        return _rank;
    }

    const std::shared_ptr<FeatureCollection>& GeocodingResult::getFeatureCollection() const {
        return _featureCollection;
    }

    const std::shared_ptr<Projection>& GeocodingResult::getProjection() const {
        return _projection;
    }

    std::string GeocodingResult::getGeoJSON() const {
        std::map<std::string, Variant> address;
        address["country"] = Variant(_address.getCountry());
        address["region"] = Variant(_address.getRegion());
        address["county"] = Variant(_address.getCounty());
        address["locality"] = Variant(_address.getLocality());
        address["neighbourhood"] = Variant(_address.getNeighbourhood());
        address["street"] = Variant(_address.getStreet());
        address["postcode"] = Variant(_address.getPostcode());
        address["houseNumber"] = Variant(_address.getHouseNumber());
        address["name"] = Variant(_address.getName());
        std::vector<Variant> categories;
        for (const std::string& category : _address.getCategories()) {
            categories.push_back(Variant(category));
        }
        address["categories"] = Variant(categories);

        // Rebuilt rather than written through: the features have to carry the address and the rank,
        // and a Feature's properties are immutable.
        std::vector<std::shared_ptr<Feature> > features;
        for (int index = 0; index < _featureCollection->getFeatureCount(); index++) {
            std::shared_ptr<Feature> feature = _featureCollection->getFeature(index);
            std::map<std::string, Variant> properties;
            const Variant& own = feature->getProperties();
            if (own.getType() == VariantType::VARIANT_TYPE_OBJECT) {
                for (const std::string& key : own.getObjectKeys()) {
                    properties[key] = own.getObjectElement(key);
                }
            }
            properties["address"] = Variant(address);
            properties["rank"] = Variant(static_cast<double>(_rank));
            features.push_back(std::make_shared<Feature>(feature->getGeometry(), Variant(properties)));
        }
        GeoJSONGeometryWriter writer;
        // The geometry is in the service's own projection; a caller asking for GeoJSON wants WGS84.
        writer.setSourceProjection(_projection);
        return writer.writeFeatureCollection(std::make_shared<FeatureCollection>(features));
    }

    std::string GeocodingResult::toString() const {
        std::stringstream ss;
        ss << std::setiosflags(std::ios::fixed);
        ss << "GeocodingResult [";
        ss << "rank=" << _rank << ", ";
        ss << "address=" << _address.toString();
        ss << "]";
        return ss.str();
    }
    
}

#endif
