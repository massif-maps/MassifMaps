#include "ExpressionUtils.h"
#include "PredicateUtils.h"

namespace massif::mvt {
    Value ExpressionEvaluator::operator() (const Predicate& pred) const {
        return std::visit(PredicateEvaluator(_context, _viewState), pred);
    }

    void ExpressionVariableVisitor::operator() (const Predicate& pred) const {
        std::visit(PredicateVariableVisitor(_visitor), pred);
    }

    bool ExpressionDeepEqualsChecker::operator() (const Predicate& pred1, const Predicate& pred2) const {
        return std::visit(PredicateDeepEqualsChecker(), pred1, pred2);
    }

    namespace {
        bool readsLiveVariables(const Expression& expr) {
            bool live = false;
            std::visit(ExpressionVariableVisitor([&live](const std::shared_ptr<VariableExpression>& varExpr) {
                auto val = std::get_if<Value>(&varExpr->getVariableExpression());
                if (!val) {
                    live = true;
                    return;
                }
                std::string name = ValueConverter<std::string>::convert(*val);
                live = live || ExpressionContext::isViewStateVariable(name) || ExpressionContext::isStyleParameterVariable(name);
            }), expr);
            return live;
        }

        struct ContextFolder {
            explicit ContextFolder(const ExpressionContext& context) : _context(context) { }

            Expression fold(const Expression& expr) const {
                if (!readsLiveVariables(expr)) {
                    try {
                        return std::visit(ExpressionEvaluator(_context, nullptr), expr);
                    }
                    catch (const std::exception&) {
                        return expr; // left to fail the same way at draw time
                    }
                }
                return std::visit(*this, expr);
            }

            Expression operator() (const Value& val) const { return val; }
            Expression operator() (const Predicate& pred) const { return pred; }
            Expression operator() (const std::shared_ptr<VariableExpression>& varExpr) const { return varExpr; }
            Expression operator() (const std::shared_ptr<UnaryExpression>& expr) const {
                return std::make_shared<UnaryExpression>(expr->getOp(), fold(expr->getExpression()));
            }
            Expression operator() (const std::shared_ptr<BinaryExpression>& expr) const {
                return std::make_shared<BinaryExpression>(expr->getOp(), fold(expr->getExpression1()), fold(expr->getExpression2()));
            }
            Expression operator() (const std::shared_ptr<TertiaryExpression>& expr) const {
                return std::make_shared<TertiaryExpression>(expr->getOp(), fold(expr->getExpression1()), fold(expr->getExpression2()), fold(expr->getExpression3()));
            }
            Expression operator() (const std::shared_ptr<InterpolateExpression>& expr) const {
                std::vector<Expression> keyFrames;
                keyFrames.reserve(expr->getKeyFrames().size());
                for (const Expression& keyFrame : expr->getKeyFrames()) {
                    keyFrames.push_back(fold(keyFrame));
                }
                return std::make_shared<InterpolateExpression>(expr->getMethod(), fold(expr->getTimeExpression()), std::move(keyFrames), expr->getBase());
            }
            Expression operator() (const std::shared_ptr<TransformExpression>& expr) const { return expr; }
            Expression operator() (const std::shared_ptr<FunctionExpression>& expr) const {
                std::vector<Expression> args;
                args.reserve(expr->getExpressions().size());
                for (const Expression& arg : expr->getExpressions()) {
                    args.push_back(fold(arg));
                }
                return std::make_shared<FunctionExpression>(expr->getFunc(), std::move(args));
            }

        private:
            const ExpressionContext& _context;
        };
    }

    Expression foldContextExpressions(const Expression& expr, const ExpressionContext& context) {
        return ContextFolder(context).fold(expr);
    }
}
