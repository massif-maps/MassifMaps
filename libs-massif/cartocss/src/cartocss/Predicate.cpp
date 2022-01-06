#include "Predicate.h"

#include <stdexcept>
#include <utility>

namespace {
    using carto::css::Color;

    template <template <typename> class Op>
    struct CompareOp {
        boost::tribool operator() (std::monostate val1, std::monostate val2) { return Op<std::monostate>()(val1, val2); }
        boost::tribool operator() (long long val1, long long val2) const { return Op<long long>()(val1, val2); }
        boost::tribool operator() (long long val1, double val2) const { return Op<double>()(static_cast<double>(val1), val2); }
        boost::tribool operator() (double val1, long long val2) const { return Op<double>()(val1, static_cast<double>(val2)); }
        boost::tribool operator() (double val1, double val2) const { return Op<double>()(val1, val2); }
        boost::tribool operator() (Color val1, Color val2) const { return Op<Color>()(val1, val2); }
        boost::tribool operator() (const std::string& val1, const std::string& val2) const { return Op<std::string>()(val1, val2); }
        template <typename S, typename T> boost::tribool operator() (S val1, T val2) const { return boost::indeterminate; }
    };
}

namespace carto::css {
    boost::tribool OpPredicate::applyOp(Op op, const Value& val1, const Value& val2) {
        switch (op) {
        case Op::EQ:
            if (std::visit(CompareOp<std::equal_to>(), val1, val2)) {
                return true;
            }
            return false;
        case Op::NEQ:
            if (!std::visit(CompareOp<std::not_equal_to>(), val1, val2)) {
                return false;
            }
            return true;
        case Op::LT:
            return std::visit(CompareOp<std::less>(), val1, val2);
        case Op::LTE:
            return std::visit(CompareOp<std::less_equal>(), val1, val2);
        case Op::GT:
            return std::visit(CompareOp<std::greater>(), val1, val2);
        case Op::GTE:
            return std::visit(CompareOp<std::greater_equal>(), val1, val2);
        case Op::MATCH:
            return boost::indeterminate;
        }
        throw std::invalid_argument("Unsupported predicate");
    }
}
