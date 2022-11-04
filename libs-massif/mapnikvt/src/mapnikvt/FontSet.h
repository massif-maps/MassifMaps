/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_FONTSET_H_
#define _CARTO_MAPNIKVT_FONTSET_H_

#include "Properties.h"

#include <string>
#include <vector>

namespace carto::mvt {
    class FontSet final {
    public:
        explicit FontSet(std::string name, std::vector<StringProperty> faceNames) : _name(std::move(name)), _faceNames(std::move(faceNames)) { }

        const std::string& getName() const { return _name; }
        const std::vector<StringProperty>& getFaceNames() const { return _faceNames; }

    private:
        const std::string _name;
        const std::vector<StringProperty> _faceNames;
    };
}

#endif
