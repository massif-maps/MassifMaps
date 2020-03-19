/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_EXPRESSIONPARSER_H_
#define _CARTO_MAPNIKVT_EXPRESSIONPARSER_H_

#include "Value.h"
#include "Expression.h"
#include "ExpressionOperator.h"
#include "Predicate.h"
#include "PredicateOperator.h"
#include "ValueParser.h"
#include "ParserUtils.h"

#include <memory>
#include <functional>
#include <typeinfo>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_bind.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/repository/include/qi_distinct.hpp>

namespace carto { namespace mvt {
    namespace exprparserimpl {
        using Skipper = boost::spirit::qi::iso8859_1::space_type;

        template <typename Iterator, bool StringExpression>
        struct Grammar : boost::spirit::qi::grammar<Iterator, std::shared_ptr<Expression>()> {
            Grammar() : Grammar::base_type(StringExpression ? stringExpression : genericExpression) {
                using namespace boost;
                using namespace boost::spirit;
                using qi::_val;
                using qi::_1;
                using qi::_2;

                le_kw  = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["le"]];
                ge_kw  = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["ge"]];
                lt_kw  = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["lt"]];
                gt_kw  = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["gt"]];
                eq_kw  = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["eq"]];
                neq_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["neq"]];
                or_kw  = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["or"]];
                and_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["and"]];
                not_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["not"]];
                exp_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["exp"]];
                log_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["log"]];
                pow_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["pow"]];
                length_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["length"]];
                uppercase_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["uppercase"]];
                lowercase_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["lowercase"]];
                capitalize_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["capitalize"]];
                concat_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["concat"]];
                match_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["match"]];
                replace_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["replace"]];
                step_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["step"]];
                linear_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["linear"]];
                cubic_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["cubic"]];

                constant = qi::lexeme[valueParser];

                string %= qi::lexeme[+(qi::char_ - qi::char_("[]{}"))];

                stringExpression =
                    ( (string                                        [_val = phoenix::bind(&makeStringExpression, _1)])
                    | ('[' > stringExpression > ']')                 [_val = phoenix::bind(&makeVariableExpression, _1)]
                    | ('{' > qi::skip(Skipper())[expression > '}'])  [_val = _1]
                    )
                    > -(stringExpression                             [_val = phoenix::bind(&makeBinaryExpression<ConcatenateOperator>, _val, _1)])
                    ;

                genericExpression =
                    qi::skip(Skipper())[expression]                  [_val = _1]
                    ;

                expression =
                    term0                                            [_val = _1]
                    >> -( (qi::lit('?') > expression > ':' > expression) [_val = phoenix::bind(&makeTertiaryExpression<ConditionalOperator>, _val, _1, _2)]
                        )
                    ;

                term0 =
                    term1                                            [_val = _1]
                    >> *( ((qi::lit("&&") | and_kw) > term1)         [_val = phoenix::bind(&makeAndPredicate, _val, _1)]
                        | ((qi::lit("||") | or_kw) > term1)          [_val = phoenix::bind(&makeOrPredicate,  _val, _1)]
                        )
                    ;

                term1 =
                    term2                                            [_val = _1]
                    >> *( ((qi::lit("<>") | "!=" | neq_kw) > term2)  [_val = phoenix::bind(&makeComparisonPredicate<NEQOperator>, _val, _1)]
                        | ((qi::lit("<=") | le_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate<LTEOperator>, _val, _1)]
                        | ((qi::lit(">=") | ge_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate<GTEOperator>, _val, _1)]
                        | ((qi::lit('<' ) | lt_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate<LTOperator>,  _val, _1)]
                        | ((qi::lit('>' ) | gt_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate<GTOperator>,  _val, _1)]
                        | ((qi::lit('=' ) | eq_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate<EQOperator>,  _val, _1)]
                        )
                    ;

                term2 =
                    term3                                            [_val = _1]
                    >> *( (qi::lit("+") > term3)                     [_val = phoenix::bind(&makeBinaryExpression<AddOperator>, _val, _1)]
                        | (qi::lit("-") > term3)                     [_val = phoenix::bind(&makeBinaryExpression<SubOperator>, _val, _1)]
                        )
                    ;

                term3 =
                    unary                                            [_val = _1]
                    >> *( (qi::lit("*") > unary)                     [_val = phoenix::bind(&makeBinaryExpression<MulOperator>, _val, _1)]
                        | (qi::lit("/") > unary)                     [_val = phoenix::bind(&makeBinaryExpression<DivOperator>, _val, _1)]
                        | (qi::lit("%") > unary)                     [_val = phoenix::bind(&makeBinaryExpression<ModOperator>, _val, _1)]
                        )
                    ;

                unary =
                        postfix                                      [_val = _1]
                    |  (qi::lit('-')            > unary)             [_val = phoenix::bind(&makeUnaryExpression<NegOperator>, _1)]
                    | ((qi::lit('!') || not_kw) > unary)             [_val = phoenix::bind(&makeNotPredicate, _1)]
                    ;

                postfix =
                    factor                                           [_val = _1]
                    >> *('.' >> ( length_kw                          [_val = phoenix::bind(&makeUnaryExpression<LengthOperator>, _val)]
                                | uppercase_kw                       [_val = phoenix::bind(&makeUnaryExpression<UpperCaseOperator>, _val)]
                                | lowercase_kw                       [_val = phoenix::bind(&makeUnaryExpression<LowerCaseOperator>, _val)]
                                | capitalize_kw                      [_val = phoenix::bind(&makeUnaryExpression<CapitalizeOperator>, _val)]
                                | (concat_kw  >> ('(' > expression > ')')) [_val = phoenix::bind(&makeBinaryExpression<ConcatenateOperator>, _val, _1)]
                                | (match_kw >> ('(' > expression > ')')) [_val = phoenix::bind(&makeComparisonPredicate<MatchOperator>, _val, _1)]
                                | (replace_kw >> ('(' > expression > ',' > expression > ')')) [_val = phoenix::bind(&makeTertiaryExpression<ReplaceOperator>, _val, _1, _2)]
                                )
                        )
                    ;

                factor =
                      constant                                       [_val = phoenix::bind(&makeConstExpression, _1)]
                    | (exp_kw    >> '(' > expression > ')')          [_val = phoenix::bind(&makeUnaryExpression<ExpOperator>, _1)]
                    | (log_kw    >> '(' > expression > ')')          [_val = phoenix::bind(&makeUnaryExpression<LogOperator>, _1)]
                    | (pow_kw    >> '(' > expression > ',' > expression > ')') [_val = phoenix::bind(&makeBinaryExpression<PowOperator>, _1, _2)]
                    | (step_kw   >> '(' > expression > ',' > (constant % ',') > ')') [_val = phoenix::bind(&makeInterpolateExpression, InterpolateExpression::Method::STEP, _1, _2)]
                    | (linear_kw >> '(' > expression > ',' > (constant % ',') > ')') [_val = phoenix::bind(&makeInterpolateExpression, InterpolateExpression::Method::LINEAR, _1, _2)]
                    | (cubic_kw  >> '(' > expression > ',' > (constant % ',') > ')') [_val = phoenix::bind(&makeInterpolateExpression, InterpolateExpression::Method::CUBIC, _1, _2)]
                    | ('[' > qi::no_skip[stringExpression] > ']') [_val = phoenix::bind(&makeVariableExpression, _1)]
                    | ('(' > expression > ')')                       [_val = _1]
                    ;
            }

            ValueParserGrammar<Iterator> valueParser;

            boost::spirit::qi::rule<Iterator, Value()> constant;
            boost::spirit::qi::rule<Iterator, std::string()> string;
            boost::spirit::qi::rule<Iterator, boost::spirit::qi::unused_type()> not_kw, and_kw, or_kw, neq_kw, eq_kw, le_kw, ge_kw, lt_kw, gt_kw, exp_kw, log_kw, pow_kw, length_kw, uppercase_kw, lowercase_kw, capitalize_kw, concat_kw, match_kw, replace_kw, step_kw, linear_kw, cubic_kw;
            boost::spirit::qi::rule<Iterator, std::shared_ptr<Expression>()> stringExpression, genericExpression;
            boost::spirit::qi::rule<Iterator, std::shared_ptr<Expression>(), Skipper> expression, term0, term1, term2, term3, unary, postfix, factor;

        private:
            static std::shared_ptr<Expression> makeStringExpression(std::string str) {
                return std::make_shared<ConstExpression>(Value(std::move(str)));
            }

            static std::shared_ptr<Expression> makeConstExpression(Value val) {
                if (StringExpression) {
                    if (auto stringVal = boost::get<std::string>(&val)) {
                        if (!stringVal->empty()) {
                            return parseExpression(*stringVal, true);
                        }
                    }
                }
                return std::make_shared<ConstExpression>(std::move(val));
            }

            static std::shared_ptr<Expression> makeVariableExpression(std::shared_ptr<const Expression> expr) {
                return std::make_shared<VariableExpression>(std::move(expr));
            }

            static std::shared_ptr<Expression> makeNotPredicate(std::shared_ptr<const Expression> expr) {
                std::shared_ptr<const Predicate> exprPred;
                if (auto predExpr = std::dynamic_pointer_cast<const PredicateExpression>(expr)) {
                    exprPred = predExpr->getPredicate();
                } else {
                    exprPred = std::make_shared<ExpressionPredicate>(expr);
                }
                return std::make_shared<PredicateExpression>(std::make_shared<NotPredicate>(exprPred));
            }

            static std::shared_ptr<Expression> makeOrPredicate(std::shared_ptr<const Expression> expr1, std::shared_ptr<const Expression> expr2) {
                std::shared_ptr<const Predicate> exprPred1, exprPred2;
                if (auto predExpr1 = std::dynamic_pointer_cast<const PredicateExpression>(expr1)) {
                    exprPred1 = predExpr1->getPredicate();
                }
                else {
                    exprPred1 = std::make_shared<ExpressionPredicate>(expr1);
                }
                if (auto predExpr2 = std::dynamic_pointer_cast<const PredicateExpression>(expr2)) {
                    exprPred2 = predExpr2->getPredicate();
                }
                else {
                    exprPred2 = std::make_shared<ExpressionPredicate>(expr2);
                }
                return std::make_shared<PredicateExpression>(std::make_shared<OrPredicate>(exprPred1, exprPred2));
            }

            static std::shared_ptr<Expression> makeAndPredicate(std::shared_ptr<const Expression> expr1, std::shared_ptr<const Expression> expr2) {
                std::shared_ptr<const Predicate> exprPred1, exprPred2;
                if (auto predExpr1 = std::dynamic_pointer_cast<const PredicateExpression>(expr1)) {
                    exprPred1 = predExpr1->getPredicate();
                }
                else {
                    exprPred1 = std::make_shared<ExpressionPredicate>(expr1);
                }
                if (auto predExpr2 = std::dynamic_pointer_cast<const PredicateExpression>(expr2)) {
                    exprPred2 = predExpr2->getPredicate();
                }
                else {
                    exprPred2 = std::make_shared<ExpressionPredicate>(expr2);
                }
                return std::make_shared<PredicateExpression>(std::make_shared<AndPredicate>(exprPred1, exprPred2));
            }

            template <typename Op>
            static std::shared_ptr<Expression> makeComparisonPredicate(std::shared_ptr<const Expression> expr1, std::shared_ptr<const Expression> expr2) {
                static const std::shared_ptr<Op> op = std::make_shared<Op>();
                return std::make_shared<PredicateExpression>(std::make_shared<ComparisonPredicate>(op, std::move(expr1), std::move(expr2)));
            }

            template <typename Op>
            static std::shared_ptr<Expression> makeUnaryExpression(std::shared_ptr<const Expression> expr) {
                static const std::shared_ptr<Op> op = std::make_shared<Op>();
                return std::make_shared<UnaryExpression>(op, std::move(expr));
            }

            template <typename Op>
            static std::shared_ptr<Expression> makeBinaryExpression(std::shared_ptr<const Expression> expr1, std::shared_ptr<const Expression> expr2) {
                static const std::shared_ptr<Op> op = std::make_shared<Op>();
                return std::make_shared<BinaryExpression>(op, std::move(expr1), std::move(expr2));
            }

            template <typename Op>
            static std::shared_ptr<Expression> makeTertiaryExpression(std::shared_ptr<const Expression> expr1, std::shared_ptr<const Expression> expr2, std::shared_ptr<const Expression> expr3) {
                static const std::shared_ptr<Op> op = std::make_shared<Op>();
                return std::make_shared<TertiaryExpression>(op, std::move(expr1), std::move(expr2), std::move(expr3));
            }

            static std::shared_ptr<Expression> makeInterpolateExpression(InterpolateExpression::Method method, std::shared_ptr<const Expression> timeExpr, std::vector<Value> keyFrames) {
                return std::make_shared<InterpolateExpression>(method, timeExpr, keyFrames);
            }
        };
    }

    template <typename Iterator> using ExpressionParserGrammar = exprparserimpl::Grammar<Iterator, false>;
    template <typename Iterator> using StringExpressionParserGrammar = exprparserimpl::Grammar<Iterator, true>;
} }

#endif
