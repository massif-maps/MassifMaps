#include "Predicate.h"
#include "ValueConverter.h"
#include "StringUtils.h"

namespace {
    template <template <typename T> class Op, bool NullResult, bool MismatchResult>
    struct ComparisonOperator {
        bool operator() (std::monostate, std::monostate) const { return NullResult; }
        bool operator() (bool val1, long long val2) const { return Op<long long>()(static_cast<long long>(val1), val2); }
        bool operator() (bool val1, double val2) const { return Op<double>()(static_cast<double>(val1), val2); }
        bool operator() (long long val1, bool val2) const { return Op<long long>()(val1, static_cast<long long>(val2)); }
        bool operator() (long long val1, double val2) const { return Op<double>()(static_cast<double>(val1), val2); }
        bool operator() (double val1, bool val2) const { return Op<double>()(val1, static_cast<double>(val2)); }
        bool operator() (double val1, long long val2) const { return Op<double>()(val1, static_cast<double>(val2)); }
        template <typename T> bool operator() (T val1, T val2) const { return Op<T>()(val1, val2); }
        template <typename S, typename T> bool operator() (S val1, T val2) const { return MismatchResult; }
    };
}

namespace carto { namespace mvt {
    bool ComparisonPredicate::applyOp(Op op, const Value& val1, const Value& val2) {
        switch (op) {
        case Op::EQ:
            return std::visit(ComparisonOperator<std::equal_to, true, false>(), val1, val2);
        case Op::NEQ:
            return std::visit(ComparisonOperator<std::not_equal_to, false, true>(), val1, val2);
        case Op::LT:
            return std::visit(ComparisonOperator<std::less, false, false>(), val1, val2);
        case Op::LTE:
            return std::visit(ComparisonOperator<std::less_equal, true, false>(), val1, val2);
        case Op::GT:
            return std::visit(ComparisonOperator<std::greater, false, false>(), val1, val2);
        case Op::GTE:
            return std::visit(ComparisonOperator<std::greater_equal, true, false>(), val1, val2);
        case Op::MATCH:
            {
                std::string str1 = ValueConverter<std::string>::convert(val1);
                std::string str2 = ValueConverter<std::string>::convert(val2);
                return regexMatch(str1, str2);
            }
        }
        throw std::invalid_argument("Illegal operator");
    }
} }
