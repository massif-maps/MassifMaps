#include "Rule.h"

namespace carto { namespace sgre {
    void Rule::apply(Graph::Attributes& attribs, bool forward) const {
        int ruleIndex = forward ? 0 : 1;
        if (_speed[ruleIndex].which() != 0) {
            attribs.speed = _speed[ruleIndex];
        }
        if (_zSpeed[ruleIndex].which() != 0) {
            attribs.zSpeed = _zSpeed[ruleIndex];
        }
        if (_turnSpeed[ruleIndex].which() != 0) {
            attribs.turnSpeed = _turnSpeed[ruleIndex];
        }
        if (_delay[ruleIndex].which() != 0) {
            attribs.delay = _delay[ruleIndex];
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
            std::vector<FeatureFilter> filters;
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
        
        rule._speed = readDirectionalFloatParameters(ruleDef, "speed");
        rule._zSpeed = readDirectionalFloatParameters(ruleDef, "zspeed");
        rule._turnSpeed = readDirectionalFloatParameters(ruleDef, "turnspeed");
        rule._delay = readDirectionalFloatParameters(ruleDef, "delay");
        return rule;
    }

    FloatParameter Rule::readFloatParameter(const picojson::value& ruleDef, const std::string& paramName) {
        if (ruleDef.contains(paramName)) {
            const picojson::value& paramDef = ruleDef.get(paramName);
            if (paramDef.is<double>() || paramDef.is<std::int64_t>()) {
                float value = paramDef.is<double>() ? static_cast<float>(paramDef.get<double>()) : static_cast<float>(paramDef.get<std::int64_t>());
                if (value < 0) {
                    throw std::invalid_argument("Negative value for rule parameter");
                }
                return FloatParameter(value);
            } else if (paramDef.is<std::string>()) {
                std::string paramName = paramDef.get<std::string>();
                if (paramName.substr(0, 1) != "$") {
                    throw std::invalid_argument("Parameter names should start with '$' sign");
                }
                return FloatParameter(paramName);
            } else {
                throw std::invalid_argument("Invalid type for rule parameter");
            }
        }
        return FloatParameter();
    }

    std::array<FloatParameter, 2> Rule::readDirectionalFloatParameters(const picojson::value& ruleDef, const std::string& paramName) {
        return std::array<FloatParameter, 2> {{ readFloatParameter(ruleDef, paramName), readFloatParameter(ruleDef, "backward_" + paramName) }};
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

    void RuleList::apply(Graph::Attributes& attribs, bool forward) const {
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
