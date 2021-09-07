#include "CartoCSSParser.h"
#include "Expression.h"
#include "Predicate.h"
#include "mapnikvt/CSSColorParser.h"
#include "mapnikvt/ParserUtils.h"
#include "mapnikvt/ValueConverter.h"

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/repository/include/qi_distinct.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_bind.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>

namespace carto { namespace css {
    namespace cssparserimpl {
        template <typename Iterator>
        struct Skipper : boost::spirit::qi::grammar<Iterator> {
            Skipper() : Skipper::base_type(skip, "PL/0") {
                using namespace boost::spirit;
                
                skip = qi::iso8859_1::space | ("/*" >> *(qi::char_ - "*/") >> "*/") | ("//" >> *(qi::char_ - qi::char_("\n\r")));
            }

            boost::spirit::qi::rule<Iterator> skip;
        };
        
        template <typename Iterator>
        struct Grammar : boost::spirit::qi::grammar<Iterator, StyleSheet(), Skipper<Iterator>> {
            Grammar() : Grammar::base_type(stylesheet) {
                using namespace boost;
                using namespace boost::spirit;
                using qi::_val;
                using qi::_1;
                using qi::_2;
                using qi::_3;
                using qi::_4;
                using qi::_pass;
                
                unesc_char.add("\\a", '\a')("\\b", '\b')("\\f", '\f')("\\n", '\n')
                              ("\\r", '\r')("\\t", '\t')("\\v", '\v')("\\\\", '\\')
                              ("\\\'", '\'')("\\\"", '\"');
                
                nonascii_  = qi::char_("\xA0-\xFF");
                unescaped_ = qi::char_("_a-zA-Z0-9-#().,%");
                hex_       = *qi::char_("0-9a-fA-F");

                string =
                      ('\'' >> *(unesc_char | "\\x" >> octet_ | (qi::char_ - '\''))) > '\''
                    | ('\"' >> *(unesc_char | "\\x" >> octet_ | (qi::char_ - '\"'))) > '\"'
                    ;
                
                literal =
                      (qi::char_("/_a-zA-Z-") | nonascii_) > *(qi::char_("/_a-zA-Z0-9-") | nonascii_)
                    ;

                uri =
                      (qi::lit("url") >> '(' >> string) > ')'
                    | (qi::lit("url") >> '(' >> *(unesc_char | "\\x" >> octet_ | (qi::print - ')'))) > ')'
                    ;

                color =
                      (qi::lit("rgb")  >> '(' >> number > ',' > number > ',' > number > ')') [_val = phoenix::bind(&makeRGBAColor, _1, _2, _3, Value(1.0))]
                    | (qi::lit("rgba") >> '(' >> number > ',' > number > ',' > number > ',' > number > ')') [_val = phoenix::bind(&makeRGBAColor, _1, _2, _3, _4)]
                    | qi::no_skip[qi::lit('#') > (*qi::char_("0-9A-Fa-f"))]    [_pass = phoenix::bind(&getHEXColor, _val, _1)]
                    | qi::no_skip[*qi::char_("a-z")]                           [_pass = phoenix::bind(&getCSSColor, _val, _1)]
                    ;

                number =
                      (qi::real_parser<double, qi::real_policies<double>>() >> *qi::space >> '%') [_val = phoenix::construct<Value>(_1 / 100.0)]
                    | (qi::real_parser<double, qi::real_policies<double>>() >> *qi::space >> "px") [_val = phoenix::construct<Value>(_1)]
                    | qi::real_parser<double, qi::strict_real_policies<double>>() [_val = phoenix::construct<Value>(_1)]
                    | qi::long_long                                 [_val = phoenix::construct<Value>(_1)]
                    ;

                constant =
                      qi::lit("null")                               [_val = phoenix::construct<Value>()]
                    | qi::lit("false")                              [_val = phoenix::construct<Value>(false)]
                    | qi::lit("true")                               [_val = phoenix::construct<Value>(true)]
                    | number                                        [_val = _1]
                    | uri                                           [_val = phoenix::construct<Value>(_1)]
                    | color                                         [_val = phoenix::construct<Value>(_1)]
                    | string                                        [_val = phoenix::construct<Value>(_1)]
                    | literal                                       [_val = phoenix::construct<Value>(_1)]
                    ;
                
                blockid = qi::lexeme[+(qi::char_("_a-zA-Z0-9-") | nonascii_)];
                propid  = qi::lexeme[(qi::char_("/_a-zA-Z-") | nonascii_) > *(qi::char_("/_a-zA-Z0-9-") | nonascii_)];
                funcid  = qi::lexeme[(qi::char_("_a-zA-Z")   | nonascii_) > *(qi::char_("_a-zA-Z0-9")   | nonascii_)];
                varid   = qi::lexeme[+(qi::char_("_a-zA-Z0-9-") | nonascii_)];
                fieldid = qi::lexeme[+(qi::char_("_a-zA-Z0-9-") | nonascii_)];
                unescapedfieldid = qi::lexeme[+(qi::print - qi::char_("[]{}")) > -(qi::char_("[") > unescapedfieldid > qi::char_("]")) > -(qi::char_("{") > unescapedfieldid > qi::char_("}"))];

                expressionlist =
                      (expression >> (',' > (expression % ',')))    [_val = phoenix::bind(&makeListExpression, _1, _2)]
                    | expression                                    [_val = _1]
                    ;

                expression =
                    term0                                           [_val = _1]
                    >> -((qi::lit("?") > expression > ':' > expression) [_val = phoenix::bind(&makeConditionalExpression, _val, _1, _2)]
                        )
                    ;

                term0 =
                    term1                                           [_val = _1]
                    >> *( (qi::lit("&&") > term1)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::AND, _val, _1)]
                        | (qi::lit("||") > term1)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::OR,  _val, _1)]
                        )
                    ;

                term1 =
                    term2                                           [_val = _1]
                    >> *( (qi::lit("=~") > term2)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::MATCH, _val, _1)]
                        | (qi::lit("=")  > term2)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::EQ,  _val, _1)]
                        | (qi::lit("!=") > term2)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::NEQ, _val, _1)]
                        | (qi::lit("<=") > term2)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::LTE, _val, _1)]
                        | (qi::lit("<")  > term2)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::LT,  _val, _1)]
                        | (qi::lit(">=") > term2)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::GTE, _val, _1)]
                        | (qi::lit(">")  > term2)                   [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::GT,  _val, _1)]
                        )
                    ;

                term2 =
                    term3                                           [_val = _1]
                    >> *( (qi::lit("+") > term3)                    [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::ADD, _val, _1)]
                        | (qi::lit("-") > term3)                    [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::SUB, _val, _1)]
                        )
                    ;
                
                term3 =
                    unary                                           [_val = _1]
                    >> *( (qi::lit("*") > unary)                    [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::MUL, _val, _1)]
                        | (qi::lit("/") > unary)                    [_val = phoenix::bind(&makeBinaryExpression, BinaryExpression::Op::DIV, _val, _1)]
                        )
                    ;
                
                unary =
                      factor                                        [_val = _1]
                    | (qi::lit("-") > unary)                        [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::NEG, _1)]
                    | (qi::lit("!") > unary)                        [_val = phoenix::bind(&makeUnaryExpression, UnaryExpression::Op::NOT, _1)]
                    ;

                factor =
                      ('@' > varid)                                 [_val = phoenix::construct<FieldOrVar>(false, _1)]
                    | ('[' > unescapedfieldid > ']')                [_val = phoenix::construct<FieldOrVar>(true, _1)]
                    | (funcid [_pass = phoenix::bind(&checkFunction, _1)] >> ('(' > (expression % ',') > ')')) [_val = phoenix::bind(&makeFunctionExpression, _1, _2)]
                    | ('(' > expressionlist > ')')                  [_val = _1]
                    | constant                                      [_val = phoenix::bind(&makeStringExpressionOrConstant, _1, _pass)]
                    ;
                
                op =
                      qi::lit("=~")                                 [_val = OpPredicate::Op::MATCH]
                    | qi::lit("=")                                  [_val = OpPredicate::Op::EQ]
                    | qi::lit("!=")                                 [_val = OpPredicate::Op::NEQ]
                    | qi::lit("<=")                                 [_val = OpPredicate::Op::LTE]
                    | qi::lit("<")                                  [_val = OpPredicate::Op::LT]
                    | qi::lit(">=")                                 [_val = OpPredicate::Op::GTE]
                    | qi::lit(">")                                  [_val = OpPredicate::Op::GT]
                    ;

                predicate =
                      qi::lit("Map")                                [_val = phoenix::construct<MapPredicate>()]
                    | (qi::lit('#') > blockid)                      [_val = phoenix::construct<LayerPredicate>(_1)]
                    | (qi::lit('.') > blockid)                      [_val = phoenix::construct<ClassPredicate>(_1)]
                    | (qi::lit("::") > blockid)                     [_val = phoenix::construct<AttachmentPredicate>(_1)]
                    | ((qi::lit('[') >> '@') > varid > op > constant > ']') [_val = phoenix::bind(&makeOpPredicate, _2, false, _1, _3)]
                    | (qi::lit('[') > (fieldid | string) > op > constant > ']') [_val = phoenix::bind(&makeOpPredicate, _2, true, _1, _3)]
                    | ((qi::lit("when") >> '(') > expression > ')') [_val = phoenix::bind(&makeWhenPredicate, _1)]
                    ;
                
                selector = (*predicate)                             [_val = phoenix::construct<Selector>(_1)];
                
                propdeclaration = (propid >> (':' > expressionlist)) [_val = phoenix::bind(&makePropertyDeclaration, phoenix::ref(_declarationOrder), _1, _2)];
                
                blockelement =
                      propdeclaration                               [_val = phoenix::construct<Block::Element>(_1)]
                    | ruleset                                       [_val = phoenix::construct<Block::Element>(_1)]
                    ;
                block = (*(blockelement > -qi::lit(';')))           [_val = phoenix::construct<Block>(_1)];
                
                ruleset = ((selector % ',') >> ('{' > block > '}')) [_val = phoenix::construct<RuleSet>(_1, _2)];
                
                vardeclaration = ('@' > varid > ':' > expressionlist) [_val = phoenix::construct<VariableDeclaration>(_1, _2)];

                stylesheetelement =
                      vardeclaration                                [_val = phoenix::construct<StyleSheet::Element>(_1)] 
                    | ruleset                                       [_val = phoenix::construct<StyleSheet::Element>(_1)]
                    ;
                stylesheet = (*(stylesheetelement > -qi::lit(';'))) [_val = phoenix::construct<StyleSheet>(_1)];
            
                qi::on_error<qi::fail>(stylesheet, phoenix::ref(_errorPos) = _3 - _1);
            }

            std::string::size_type errorPos() const { return _errorPos; }
            
            boost::spirit::qi::int_parser<char, 16, 2, 2> octet_;
            boost::spirit::qi::symbols<char const, char const> unesc_char;
            boost::spirit::qi::rule<Iterator, char()> nonascii_, unescaped_;
            boost::spirit::qi::rule<Iterator, std::string()> hex_;
            boost::spirit::qi::rule<Iterator, std::string()> string, literal;
            boost::spirit::qi::rule<Iterator, std::string(), Skipper<Iterator> > uri;
            boost::spirit::qi::rule<Iterator, Color(), Skipper<Iterator> > color;
            boost::spirit::qi::rule<Iterator, Value(), Skipper<Iterator> > number, constant;
            boost::spirit::qi::rule<Iterator, std::string()> blockid, propid, funcid, varid, fieldid, unescapedfieldid;
            boost::spirit::qi::rule<Iterator, Expression(), Skipper<Iterator> > expressionlist, expression, term0, term1, term2, term3, unary, factor;
            boost::spirit::qi::rule<Iterator, OpPredicate::Op()> op;
            boost::spirit::qi::rule<Iterator, Predicate(), Skipper<Iterator> > predicate;
            boost::spirit::qi::rule<Iterator, Selector(), Skipper<Iterator> > selector;
            boost::spirit::qi::rule<Iterator, PropertyDeclaration(), Skipper<Iterator> > propdeclaration;
            boost::spirit::qi::rule<Iterator, Block::Element(), Skipper<Iterator> > blockelement;
            boost::spirit::qi::rule<Iterator, Block(), Skipper<Iterator> > block;
            boost::spirit::qi::rule<Iterator, RuleSet(), Skipper<Iterator> > ruleset;
            boost::spirit::qi::rule<Iterator, VariableDeclaration(), Skipper<Iterator> > vardeclaration;
            boost::spirit::qi::rule<Iterator, StyleSheet::Element(), Skipper<Iterator> > stylesheetelement;
            boost::spirit::qi::rule<Iterator, StyleSheet(), Skipper<Iterator> > stylesheet;

        private:
            static bool getHEXColor(Color& color, const std::vector<char>& code) {
                unsigned int value = 0;
                if (!mvt::parseCSSColor("#" + std::string(code.begin(), code.end()), value)) {
                    return false;
                }
                color = Color::fromValue(value);
                return true;
            }

            static bool getCSSColor(Color& color, const std::vector<char>& name) {
                unsigned int value = 0;
                if (!mvt::parseCSSColor(std::string(name.begin(), name.end()), value)) {
                    return false;
                }
                color = Color::fromValue(value);
                return true;
            }

            static Color makeRGBAColor(const Value& r, const Value& g, const Value& b, const Value& a) {
                auto getFloat = [](const Value& val) {
                    return std::holds_alternative<long long>(val) ? static_cast<float>(std::get<long long>(val)) : static_cast<float>(std::get<double>(val));
                };
                return Color::fromRGBA(getFloat(r) / 255.0f, getFloat(g) / 255.0f, getFloat(b) / 255.0f, getFloat(a));
            }

            static Expression makeStringExpressionOrConstant(const Value& val, bool& pass) {
                if (auto strVal = std::get_if<std::string>(&val)) {
                    try {
                        mvt::Expression mvtExpr = mvt::parseExpression(*strVal, true);
                        if (auto mvtVal = std::get_if<mvt::Value>(&mvtExpr)) {
                            return Value(mvt::ValueConverter<std::string>::convert(*mvtVal));
                        }
                        return std::make_shared<StringExpression>(*strVal);
                    }
                    catch (const std::exception&) {
                        pass = false;
                    }
                }
                return val;
            }

            static Expression makeListExpression(const Expression& initialExpr, const std::vector<Expression>& restExprs) {
                std::vector<Expression> exprs;
                exprs.push_back(initialExpr);
                exprs.insert(exprs.end(), restExprs.begin(), restExprs.end());
                return std::make_shared<ListExpression>(exprs);
            }

            static bool checkFunction(const std::string& func) {
                return func != "url" && func != "rgb" && func != "rgba"; // ignore special-built in constructors
            }

            static Expression makeFunctionExpression(const std::string& func, const std::vector<Expression>& exprs) {
                return std::make_shared<FunctionExpression>(func, exprs);
            }
                   
            static Expression makeUnaryExpression(UnaryExpression::Op op, const Expression& expr) {
                return std::make_shared<UnaryExpression>(op, expr);
            }

            static Expression makeBinaryExpression(BinaryExpression::Op op, const Expression& expr1, const Expression& expr2) {
                return std::make_shared<BinaryExpression>(op, expr1, expr2);
            }
            
            static Expression makeConditionalExpression(const Expression& cond, const Expression& expr1, const Expression& expr2) {
                return std::make_shared<ConditionalExpression>(cond, expr1, expr2);
            }

            static Predicate makeOpPredicate(OpPredicate::Op op, bool field, const std::string& name, const Value& refValue) {
                return OpPredicate(op, FieldOrVar(field, name), refValue);
            }

            static Predicate makeWhenPredicate(const Expression& expr) {
                return WhenPredicate(expr);
            }

            static PropertyDeclaration makePropertyDeclaration(int& order, const std::string& field, const Expression& expr) {
                return PropertyDeclaration(field, expr, order++);
            }

            int _declarationOrder = 0;
            std::string::size_type _errorPos = std::string::npos;
        };
    }
    
    StyleSheet CartoCSSParser::parse(const std::string& cartoCSS) {
        std::string::const_iterator it = cartoCSS.begin();
        std::string::const_iterator end = cartoCSS.end();
        cssparserimpl::Grammar<std::string::const_iterator> grammar;
        cssparserimpl::Skipper<std::string::const_iterator> skipper;
        StyleSheet styleSheet;
        bool result = false;
        try {
            result = boost::spirit::qi::phrase_parse(it, end, grammar, skipper, styleSheet);
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserError("Expectation error", resolvePosition(cartoCSS, ex.first - cartoCSS.begin()));
        }
        if (!result) {
            if (grammar.errorPos() != std::string::npos) {
                throw ParserError("Syntax error", resolvePosition(cartoCSS, grammar.errorPos()));
            }
            else {
                throw ParserError("Parsing error");
            }
        }
        else if (it != cartoCSS.end()) {
            throw ParserError("Failed to parse to the end", resolvePosition(cartoCSS, it - cartoCSS.begin()));
        }
        return styleSheet;
    }

    std::pair<int, int> CartoCSSParser::resolvePosition(const std::string& str, std::string::size_type pos) {
        std::pair<int, int> colLine(1, 1);
        for (std::string::size_type i = 0; i < pos; i++) {
            if (str[i] == '\n') {
                colLine.second++;
                colLine.first = 1;
            }
        }
        return colLine;
    }
} }
