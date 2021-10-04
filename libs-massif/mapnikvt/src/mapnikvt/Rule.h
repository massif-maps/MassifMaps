/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_RULE_H_
#define _CARTO_MAPNIKVT_RULE_H_

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <set>

#include "Expression.h"

namespace carto { namespace mvt {
    class Filter;
    class Symbolizer;
    
    class Rule final {
    public:
        explicit Rule(std::string name, int minZoom, int maxZoom, std::shared_ptr<const Filter> filter, std::vector<std::shared_ptr<const Symbolizer>> symbolizers);

        const std::string& getName() const { return _name; }
        int getMinZoom() const { return _minZoom; }
        int getMaxZoom() const { return _maxZoom; }
        const std::shared_ptr<const Filter>& getFilter() const { return _filter; }
        const std::vector<std::shared_ptr<const Symbolizer>>& getSymbolizers() const { return _symbolizers; }

        const std::set<std::string>& getReferencedFilterFields() const { calculateReferencedFields(); return _referencedFilterFields; }
        const std::set<std::string>& getReferencedSymbolizerFields() const { calculateReferencedFields(); return _referencedSymbolizerFields; }

    private:
        void calculateReferencedFields() const;

        const std::string _name;
        const int _minZoom;
        const int _maxZoom;
        const std::shared_ptr<const Filter> _filter;
        const std::vector<std::shared_ptr<const Symbolizer>> _symbolizers;

        mutable std::mutex _referencedFieldsMutex;
        mutable bool _referencedFieldsCalculated = false;
        mutable std::set<std::string> _referencedFilterFields;
        mutable std::set<std::string> _referencedSymbolizerFields;
    };
} }

#endif
