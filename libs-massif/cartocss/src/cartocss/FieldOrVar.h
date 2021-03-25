/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_FIELDORVAR_H_
#define _CARTO_CARTOCSS_FIELDORVAR_H_

#include <string>

namespace carto { namespace css {
    class FieldOrVar final {
    public:
        explicit FieldOrVar(bool field, std::string name) : _field(field), _name(std::move(name)) { }

        bool isField() const { return _field; }
        bool isVar() const { return !_field; }
        const std::string& getName() const { return _name; }

        bool operator == (const FieldOrVar& other) const { return _field == other._field && _name == other._name; }
        bool operator != (const FieldOrVar& other) const { return !(*this == other); }

    private:
        bool _field;
        std::string _name;
    };
} }

#endif
