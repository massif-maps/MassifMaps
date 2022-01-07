/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_EXPRESSIONPARSER_H_
#define _CARTO_MAPNIKVT_EXPRESSIONPARSER_H_

#include "Value.h"
#include "Expression.h"
#include "Predicate.h"
#include "Transform.h"
#include "ValueParser.h"
#include "ParserUtils.h"

#include <memory>
#include <functional>
#include <typeinfo>

#include <boost/phoenix.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/repository/include/qi_distinct.hpp>

namespace carto::mvt {
    namespace exprparserimpl {
        using Skipper = boost::spirit::qi::iso8859_1::space_type;

        template <typename Iterator, bool StringExpression>
        struct Grammar : boost::spirit::qi::grammar<Iterator, Expression()> {
            Grammar() : Grammar::base_type(StringExpression ? stringExpression : genericExpression) {
                using namespace boost;
                using namespace boost::spirit;
                using qi::_val;
                using qi::_1;
                using qi::_2;
                using qi::_3;
                using qi::_4;
                using qi::_5;
                using qi::_6;

                unesc_char.add("\\a", '\a')("\\b", '\b')("\\f", '\f')("\\n", '\n')
                              ("\\r", '\r')("\\t", '\t')("\\v", '\v')("\\\\", '\\')
                              ("\\\'", '\'')("\\\"", '\"')("\\[", '[')("\\]", ']')("\\{", '{')("\\}", '}');

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

                matrix_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["matrix"]];
                translate_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["translate"]];
                rotate_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["rotate"]];
                scale_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["scale"]];
                skewx_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["skewx"]];
                skewy_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["skewy"]];

                constant = qi::lexeme[valueParser];

                string %= qi::lexeme[+(unesc_char | "\\x" >> octet | (qi::char_ - qi::char_("[]{}")))];

                stringExpression =
                    ( (string                                        [_val = phoenix::construct<Value>(_1)])
                    | ('[' > stringExpression > ']')                 [_val = phoenix::bind(&makeVariableExpression, _1)]
                    | ('{' > qi::skip(Skipper())[expression > '}'])  [_val = _1]
                    )
                    > -(stringExpression                             [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::CONCAT, _val, _1)])
                    ;

                genericExpression =
                    qi::skip(Skipper())[expression]                  [_val = _1]
                    ;

                expression =
                    term0                                            [_val = _1]
                    >> -( (qi::lit('?') > expression > ':' > expression) [_val = phoenix::bind(&makeTertiaryExpression, TertiaryExpression::Op::CONDITIONAL, _val, _1, _2)]
                        )
                    ;

                term0 =
                    term1                                            [_val = _1]
                    >> *( ((qi::lit("&&") | and_kw) > term1)         [_val = phoenix::bind(&makeAndPredicate, _val, _1)]
                        | ((qi::lit("||") | or_kw)  > term1)         [_val = phoenix::bind(&makeOrPredicate,  _val, _1)]
                        )
                    ;

                term1 =
                    term2                                            [_val = _1]
                    >> *( ((qi::lit("<>") | "!=" | neq_kw) > term2)  [_val = phoenix::bind(&makeComparisonPredicate, ComparisonPredicate::Op::NEQ, _val, _1)]
                        | ((qi::lit("<=") | le_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate, ComparisonPredicate::Op::LTE, _val, _1)]
                        | ((qi::lit(">=") | ge_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate, ComparisonPredicate::Op::GTE, _val, _1)]
                        | ((qi::lit('<' ) | lt_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate, ComparisonPredicate::Op::LT,  _val, _1)]
                        | ((qi::lit('>' ) | gt_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate, ComparisonPredicate::Op::GT,  _val, _1)]
                        | ((qi::lit('=' ) | eq_kw        ) > term2)  [_val = phoenix::bind(&makeComparisonPredicate, ComparisonPredicate::Op::EQ,  _val, _1)]
                        )
                    ;

                term2 =
                    term3                                            [_val = _1]
                    >> *( (qi::lit("+") > term3)                     [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::ADD, _val, _1)]
                        | (qi::lit("-") > term3)                     [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::SUB, _val, _1)]
                        )
                    ;

                term3 =
                    unary                                            [_val = _1]
                    >> *( (qi::lit("*") > unary)                     [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::MUL, _val, _1)]
                        | (qi::lit("/") > unary)                     [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::DIV, _val, _1)]
                        | (qi::lit("%") > unary)                     [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::MOD, _val, _1)]
                        )
                    ;

                unary =
                        postfix                                      [_val = _1]
                    |  (qi::lit('-')            > unary)             [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::NEG, _1)]
                    | ((qi::lit('!') || not_kw) > unary)             [_val = phoenix::bind(&makeNotPredicate, _1)]
                    ;

                postfix =
                    factor                                           [_val = _1]
                    >> *('.' >> ( length_kw                          [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::LENGTH, _val)]
                                | uppercase_kw                       [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::UPPER, _val)]
                                | lowercase_kw                       [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::LOWER, _val)]
                                | capitalize_kw                      [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::CAPITALIZE, _val)]
                                | (concat_kw  >> ('(' > expression > ')')) [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::CONCAT, _val, _1)]
                                | (match_kw   >> ('(' > expression > ')')) [_val = phoenix::bind(&makeComparisonPredicate, ComparisonPredicate::Op::MATCH, _val, _1)]
                                | (replace_kw >> ('(' > expression > ',' > expression > ')')) [_val = phoenix::bind(&makeTertiaryExpression, TertiaryExpression::Op::REPLACE, _val, _1, _2)]
                                )
                        )
                    ;

                factor =
                      constant                                          [_val = phoenix::bind(&makeConstExpression, _1)]
                    | (exp_kw       >> '(' > expression > ')')          [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::EXP, _1)]
                    | (log_kw       >> '(' > expression > ')')          [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::LOG, _1)]
                    | (pow_kw       >> '(' > expression > ',' > expression > ')') [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::POW, _1, _2)]
                    | (step_kw      >> '(' > expression > ',' > (constant % ',') > ')') [_val = phoenix::bind(&makeInterpolateExpression, InterpolateExpression::Method::STEP, _1, _2)]
                    | (linear_kw    >> '(' > expression > ',' > (constant % ',') > ')') [_val = phoenix::bind(&makeInterpolateExpression, InterpolateExpression::Method::LINEAR, _1, _2)]
                    | (cubic_kw     >> '(' > expression > ',' > (constant % ',') > ')') [_val = phoenix::bind(&makeInterpolateExpression, InterpolateExpression::Method::CUBIC, _1, _2)]
                    | (matrix_kw    >> '(' > expression >> ',' > expression > ',' > expression > ',' > expression > ',' > expression > ',' > expression > ')') [_val = phoenix::bind(&makeMatrixTransformExpression, _1, _2, _3, _4, _5, _6)]
                    | (translate_kw >> '(' > expression > ',' > expression > ')') [_val = phoenix::bind(&makeTranslateTransformExpression, _1, _2)]
                    | (rotate_kw    >> '(' >> expression >> ')')        [_val = phoenix::bind(&makeRotateTransformExpression, Expression(Value(0.0)), Expression(Value(0.0)), _1)]
                    | (rotate_kw    >> '(' > expression > ',' > expression > ',' > expression > ')') [_val = phoenix::bind(&makeRotateTransformExpression, _2, _3, _1)]
                    | (scale_kw     >> '(' >> expression >> ')')        [_val = phoenix::bind(&makeScaleTransformExpression, _1, _1)]
                    | (scale_kw     >> '(' > expression > ',' > expression > ')') [_val = phoenix::bind(&makeScaleTransformExpression, _1, _2)]
                    | (skewx_kw     >> '(' > expression > ')')          [_val = phoenix::bind(&makeSkewXTransformExpression, _1)]
                    | (skewy_kw     >> '(' > expression > ')')          [_val = phoenix::bind(&makeSkewYTransformExpression, _1)]
                    | ('[' > qi::no_skip[stringExpression] > ']')       [_val = phoenix::bind(&makeVariableExpression, _1)]
                    | ('(' > expression > ')')                          [_val = _1]
                    ;
            }

            ValueParserGrammar<Iterator> valueParser;

            boost::spirit::qi::uint_parser<unsigned char, 16, 2, 2> octet;
            boost::spirit::qi::symbols<char const, char const> unesc_char;
            boost::spirit::qi::rule<Iterator, Value()> constant;
            boost::spirit::qi::rule<Iterator, std::string()> string;
            boost::spirit::qi::rule<Iterator, boost::spirit::qi::unused_type()> le_kw, ge_kw, lt_kw, gt_kw, eq_kw, neq_kw, or_kw, and_kw, not_kw;
            boost::spirit::qi::rule<Iterator, boost::spirit::qi::unused_type()> exp_kw, log_kw, pow_kw, length_kw, uppercase_kw, lowercase_kw, capitalize_kw, concat_kw, match_kw, replace_kw;
            boost::spirit::qi::rule<Iterator, boost::spirit::qi::unused_type()> step_kw, linear_kw, cubic_kw;
            boost::spirit::qi::rule<Iterator, boost::spirit::qi::unused_type()> matrix_kw, translate_kw, rotate_kw, scale_kw, skewx_kw, skewy_kw;
            boost::spirit::qi::rule<Iterator, Expression()> stringExpression, genericExpression;
            boost::spirit::qi::rule<Iterator, Expression(), Skipper> expression, term0, term1, term2, term3, unary, postfix, factor;

        private:
            static Expression makeConstExpression(const Value& val) {
                if (StringExpression) {
                    if (auto strVal = std::get_if<std::string>(&val)) {
                        if (!strVal->empty()) {
                            return parseExpression(*strVal, true);
                        }
                    }
                }
                return val;
            }

            static Expression makeVariableExpression(const Expression& expr) {
                return std::make_shared<VariableExpression>(expr);
            }

            static Expression makeNotPredicate(const Expression& expr1) {
                Predicate pred1;
                if (auto pred = std::get_if<Predicate>(&expr1)) {
                    pred1 = *pred;
                }
                else {
                    pred1 = std::make_shared<ExpressionPredicate>(expr1);
                }
                return std::make_shared<NotPredicate>(std::move(pred1));
            }

            static Expression makeOrPredicate(const Expression& expr1, const Expression& expr2) {
                Predicate pred1, pred2;
                if (auto pred = std::get_if<Predicate>(&expr1)) {
                    pred1 = *pred;
                }
                else {
                    pred1 = std::make_shared<ExpressionPredicate>(expr1);
                }
                if (auto pred = std::get_if<Predicate>(&expr2)) {
                    pred2 = *pred;
                }
                else {
                    pred2 = std::make_shared<ExpressionPredicate>(expr2);
                }
                return std::make_shared<OrPredicate>(std::move(pred1), std::move(pred2));
            }

            static Expression makeAndPredicate(const Expression& expr1, const Expression& expr2) {
                Predicate pred1, pred2;
                if (auto pred = std::get_if<Predicate>(&expr1)) {
                    pred1 = *pred;
                }
                else {
                    pred1 = std::make_shared<ExpressionPredicate>(expr1);
                }
                if (auto pred = std::get_if<Predicate>(&expr2)) {
                    pred2 = *pred;
                }
                else {
                    pred2 = std::make_shared<ExpressionPredicate>(expr2);
                }
                return std::make_shared<AndPredicate>(std::move(pred1), std::move(pred2));
            }

            static Expression makeComparisonPredicate(ComparisonPredicate::Op op, const Expression& expr1, const Expression& expr2) {
                return std::make_shared<ComparisonPredicate>(op, expr1, expr2);
            }

            static Expression makeUnaryExpression(UnaryExpression::Op op, const Expression& expr1) {
                return std::make_shared<UnaryExpression>(op, expr1);
            }

            static Expression makeBinaryExpression(BinaryExpression::Op op, const Expression& expr1, const Expression& expr2) {
                return std::make_shared<BinaryExpression>(op, expr1, expr2);
            }

            static Expression makeTertiaryExpression(TertiaryExpression::Op op, const Expression& expr1, const Expression& expr2, const Expression& expr3) {
                return std::make_shared<TertiaryExpression>(op, expr1, expr2, expr3);
            }

            static Expression makeInterpolateExpression(InterpolateExpression::Method method, const Expression& timeExpr, const std::vector<Value>& keyFrames) {
                return std::make_shared<InterpolateExpression>(method, timeExpr, keyFrames);
            }

            static Expression makeMatrixTransformExpression(const Expression& a, const Expression& b, const Expression& c, const Expression& d, const Expression& e, const Expression& f) {
                std::array<Expression, 6> values = { a, b, c, d, e, f };
                return std::make_shared<TransformExpression>(MatrixTransform(values));
            }

            static Expression makeTranslateTransformExpression(const Expression& dx, const Expression& dy) {
                return std::make_shared<TransformExpression>(TranslateTransform(dx, dy));
            }

            static Expression makeRotateTransformExpression(const Expression& x, const Expression& y, const Expression& angle) {
                return std::make_shared<TransformExpression>(RotateTransform(x, y, angle));
            }

            static Expression makeScaleTransformExpression(const Expression& sx, const Expression& sy) {
                return std::make_shared<TransformExpression>(ScaleTransform(sx, sy));
            }

            static Expression makeSkewXTransformExpression(const Expression& angle) {
                return std::make_shared<TransformExpression>(SkewXTransform(angle));
            }

            static Expression makeSkewYTransformExpression(const Expression& angle) {
                return std::make_shared<TransformExpression>(SkewYTransform(angle));
            }
        };
    }

    template <typename Iterator> using ExpressionParserGrammar = exprparserimpl::Grammar<Iterator, false>;
    template <typename Iterator> using StringExpressionParserGrammar = exprparserimpl::Grammar<Iterator, true>;
}

#endif
