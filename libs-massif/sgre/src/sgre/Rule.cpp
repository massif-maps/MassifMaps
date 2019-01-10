#include "Rule.h"

namespace carto { namespace sgre {
    void Rule::apply(RoutingAttributes& attribs, bool forward) const {
        int ruleIndex = forward ? 0 : 1;
        if (auto speed = _speed[ruleIndex]) {
            attribs.speed = *speed;
        }
        if (auto zSpeed = _zSpeed[ruleIndex]) {
            attribs.zSpeed = *zSpeed;
        }
        if (auto turnSpeed = _turnSpeed[ruleIndex]) {
            attribs.turnSpeed = *turnSpeed;
        }
        if (auto delay = _delay[ruleIndex]) {
            attribs.delay = *delay;
        }
    }

    Rule Rule::parse(const picojson::value& ruleDef) {
        Rule rule;
        if (ruleDef.contains("profiles")) {
            std::vector<std::string> profiles;
            const picojson::array& profilesDef = ruleDef.get("profiles").get<picojson::array>();
            for (const picojson::value& profileDef : profilesDef) {
                profiles.push_back(profileDef.get<std::string>());
            }
            rule._profiles = std::move(profiles);
        }

        if (ruleDef.contains("filters")) {
            std::vector<Filter> filters;
            const picojson::array& filtersDef = ruleDef.get("filters").get<picojson::array>();
            for (const picojson::value& filterDef : filtersDef) {
                filters.push_back(filterDef.get<picojson::object>());
            }
            rule._filters = std::move(filters);
        }

        if (ruleDef.contains("link")) {
            std::string linkMode = ruleDef.get("link").get<std::string>();
            if (linkMode == "none") {
                rule._linkMode = Graph::LinkMode::NONE;
            } else if (linkMode == "endpoints") {
                rule._linkMode = Graph::LinkMode::ENDPOINTS;
            } else if (linkMode == "all") {
                rule._linkMode = Graph::LinkMode::ALL;
            } else {
                throw std::runtime_error("Illegal link mode value");
            }
        }
        
        if (ruleDef.contains("search")) {
            std::string searchCriteria = ruleDef.get("search").get<std::string>();
            if (searchCriteria == "none") {
                rule._searchCriteria = Graph::SearchCriteria::NONE;
            } else if (searchCriteria == "vertex") {
                rule._searchCriteria = Graph::SearchCriteria::VERTEX;
            } else if (searchCriteria == "firstlastvertex") {
                rule._searchCriteria = Graph::SearchCriteria::FIRST_LAST_VERTEX;
            } else if (searchCriteria == "edge") {
                rule._searchCriteria = Graph::SearchCriteria::EDGE;
            } else if (searchCriteria == "surface") {
                rule._searchCriteria = Graph::SearchCriteria::SURFACE;
            } else {
                throw std::runtime_error("Illegal search criteria value");
            }
        }
        
        rule._speed = readDirectionalFloatParameter(ruleDef, "speed");
        rule._zSpeed = readDirectionalFloatParameter(ruleDef, "zspeed");
        rule._turnSpeed = readDirectionalFloatParameter(ruleDef, "turnspeed");
        rule._delay = readDirectionalFloatParameter(ruleDef, "delay");
        return rule;
    }

    std::array<boost::optional<float>, 2> Rule::readDirectionalFloatParameter(const picojson::value& ruleDef, const std::string& param) {
        std::array<boost::optional<float>, 2> values;
        if (ruleDef.contains(param)) {
            values[0] = static_cast<float>(ruleDef.get(param).get<double>());
            if (*values[0] < 0) {
                throw std::invalid_argument("Negative value for rule parameter");
            }
        }
        if (ruleDef.contains("backward_" + param)) {
            values[1] = static_cast<float>(ruleDef.get("backward_" + param).get<double>());
            if (*values[1] < 0) {
                throw std::invalid_argument("Negative value for backward rule parameter");
            }
        }
        return values;
    }

    void RuleList::filter(const std::string& profile) {
        std::vector<Rule> rules;
        rules.reserve(_rules.size());
        for (const Rule& rule : _rules) {
            if (auto ruleProfiles = rule.getProfiles()) {
                if (std::find((*ruleProfiles).begin(), (*ruleProfiles).end(), profile) == (*ruleProfiles).end()) {
                    continue;
                }
            }
            rules.push_back(rule);
        }
        std::swap(rules, _rules);
    }

    void RuleList::apply(RoutingAttributes& attribs, bool forward) const {
        for (const Rule& rule : _rules) {
            rule.apply(attribs, forward);
        }
    }

    RuleList RuleList::parse(const picojson::value& ruleListDef) {
        const picojson::array& ruleListArray = ruleListDef.get<picojson::array>();
        
        std::vector<Rule> rules;
        rules.reserve(ruleListArray.size());
        for (const picojson::value& ruleDef : ruleListArray) {
            rules.push_back(Rule::parse(ruleDef));
        }
        return RuleList(std::move(rules));
    }
} }
