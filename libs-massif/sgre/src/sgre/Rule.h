/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_RULE_H_
#define _CARTO_SGRE_RULE_H_

#include "Base.h"
#include "Graph.h"

#include <string>
#include <array>
#include <vector>
#include <map>

#include <boost/optional.hpp>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class Rule final {
    public:
        using Filter = std::map<std::string, picojson::value>;

        Rule() = default;

        const boost::optional<std::vector<std::string>>& getProfiles() const { return _profiles; }
        const boost::optional<std::vector<Filter>>& getFilters() const { return _filters; }
        const boost::optional<Graph::LinkMode>& getLinkMode() const { return _linkMode; }
        const boost::optional<Graph::SearchCriteria>& getSearchCriteria() const { return _searchCriteria; }

        void apply(RoutingAttributes& attribs, bool forward) const;

        static Rule parse(const picojson::value& ruleDef);

    private:
        static std::array<boost::optional<float>, 2> readDirectionalFloatParameter(const picojson::value& ruleDef, const std::string& param);

        boost::optional<std::vector<std::string>> _profiles;
        boost::optional<std::vector<Filter>> _filters;
        boost::optional<Graph::LinkMode> _linkMode;
        boost::optional<Graph::SearchCriteria> _searchCriteria;

        std::array<boost::optional<float>, 2> _speed;
        std::array<boost::optional<float>, 2> _zSpeed;
        std::array<boost::optional<float>, 2> _turnSpeed;
        std::array<boost::optional<float>, 2> _delay;
    };

    class RuleList final {
    public:
        RuleList() = default;
        explicit RuleList(std::vector<Rule> rules) : _rules(std::move(rules)) { }

        const std::vector<Rule>& getRules() const { return _rules; }
        
        void filter(const std::string& profile);

        void apply(RoutingAttributes& attribs, bool forward) const;

        static RuleList parse(const picojson::value& ruleListDef);

    private:
        std::vector<Rule> _rules;
    };
} }

#endif
