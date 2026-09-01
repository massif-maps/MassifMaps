#include "LineSymbolizer.h"

#include <cctype>
#include <cstdlib>
#include "ParserUtils.h"
#include "vt/BitmapCanvas.h"

#include <cmath>
#include <algorithm>

#include <boost/algorithm/string.hpp>

namespace massif::mvt {
    LineSymbolizer::FeatureProcessor LineSymbolizer::createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const {
        vt::FloatFunction strokeWidthFunc = _strokeWidth.getFunction(exprContext);
        vt::FloatFunction strokeOpacityFunc = _strokeOpacity.getFunction(exprContext);
        vt::ColorFunction strokeColorFunc = _stroke.getFunction(exprContext);
        if (strokeWidthFunc == vt::FloatFunction(0) || strokeOpacityFunc == vt::FloatFunction(0) || strokeColorFunc == vt::ColorFunction(vt::Color())) {
            return FeatureProcessor();
        }
        vt::FloatFunction offsetFunc = _offset.getFunction(exprContext);
        vt::FloatFunction gapWidthFunc = _gapWidth.getFunction(exprContext);
        vt::FloatFunction blurFunc = _blur.getFunction(exprContext);
        vt::FloatFunction borderWidthFunc = _borderWidth.getFunction(exprContext);

        vt::CompOp compOp = _compOp.getValue(exprContext);
        vt::LineJoinMode strokeLinejoin = _strokeLinejoin.getValue(exprContext);
        vt::LineCapMode strokeLinecap = _strokeLinecap.getValue(exprContext);
        vt::ColorFunction strokeFunc = _strokeFuncBuilder.createColorOpacityFunction(strokeColorFunc, strokeOpacityFunc);
        std::optional<vt::Transform> geometryTransform = _geometryTransform.getValue(exprContext);

        std::shared_ptr<const vt::BitmapPattern> strokePattern;
        std::string strokeDashArray = _strokeDashArray.getValue(exprContext);
        if (!strokeDashArray.empty()) {
            float height = 1.0f;
            if (strokeLinecap != vt::LineCapMode::NONE) {
                height = _strokeWidth.getStaticValue(exprContext);
            }
            std::string file = "__line_dasharray_" + std::to_string(height) + "_" + std::to_string(static_cast<int>(strokeLinecap)) + "_" + strokeDashArray;
            strokePattern = symbolizerContext.getBitmapManager()->getBitmapPattern(file);
            if (!strokePattern) {
                std::vector<std::string> dashList;
                boost::split(dashList, strokeDashArray, boost::is_any_of(","));
                std::vector<float> strokeDashArray;
                for (const std::string& dash : dashList) {
                    try {
                        strokeDashArray.push_back(std::stof(boost::trim_copy(dash)));
                    }
                    catch (const boost::bad_lexical_cast& ex) {
                        _logger->write(Logger::Severity::ERROR, "Illegal dash value: " + dash + ", ex: " +  ex.what() );
                    }
                }
                strokePattern = createDashBitmapPattern(strokeDashArray, height, strokeLinecap);
                symbolizerContext.getBitmapManager()->storeBitmapPattern(file, strokePattern);
            }
        }

        float splitDotLimit = SPLIT_DOT_LIMIT;
        float miterDotLimit = splitDotLimit;
        if (offsetFunc == vt::FloatFunction(0)) {
            if (strokeLinejoin == vt::LineJoinMode::BEVEL) {
                miterDotLimit = 1.0f;
            } else {
                // 'miterlimit' is the RATIO miter-length / line-width at which a join falls back to
                // a bevel (SVG, mapnik, tangram's PolyLineBuilder::miterLimit); the line width does
                // not enter it. It used to - min(width / limit, 1) taken as the sine of the half
                // angle - which made the cut depend on the static width in two wrong directions: a
                // thin line kept mitering into a 5x-long needle at a hairpin, and a line wider than
                // the limit never mitered at all.
                // It picks the BRANCH only; the inner corner every branch places is bounded by
                // vt's INNER_MITER_LIMIT instead - see TileLayerBuilder.
                // ratio = 1 / cos(turn / 2) = 1 / sqrt((1 + dot) / 2)  =>  dot = 2 / ratio^2 - 1.
                float strokeMiterLimit = std::max(_strokeMiterLimit.getStaticValue(exprContext), 1.0f);
                miterDotLimit = 2.0f / (strokeMiterLimit * strokeMiterLimit) - 1.0f;
            }
            if (strokePattern) {
                splitDotLimit = miterDotLimit = DASH_MITER_DOT_LIMIT;
            }
        }

        bool endArrow = _endArrow.getValue(exprContext);
        float arrowWidth = endArrow ? _arrowWidth.getValue(exprContext) : 0.0f;
        float arrowLength = endArrow ? _arrowLength.getValue(exprContext) : 0.0f;
        bool arrowOnly = endArrow && _arrowOnly.getValue(exprContext);
        std::shared_ptr<const std::vector<cglib::vec2<float>>> arrowShape;
        if (endArrow) {
            std::string arrowPath = _arrowPath.getValue(exprContext);
            if (!arrowPath.empty()) {
                arrowShape = parseArrowPath(arrowPath, arrowLength, arrowWidth,
                                            _arrowScale.getValue(exprContext), _arrowRotation.getValue(exprContext));
                if (!arrowShape) {
                    _logger->write(Logger::Severity::ERROR, "Ignoring unreadable arrow-path: " + arrowPath);
                }
                else if (!isConvexArrowPath(*arrowShape)) {
                    _logger->write(Logger::Severity::WARNING, "Concave arrow-path, its border will fold on itself: " + arrowPath);
                }
            }
        }

        vt::LineStyle style(compOp, strokeLinejoin, strokeLinecap, strokeFunc, strokeWidthFunc, offsetFunc, splitDotLimit, miterDotLimit, strokePattern, geometryTransform, arrowWidth, arrowLength, arrowOnly, arrowShape, gapWidthFunc, blurFunc, _strokeEmissive.getFunction(exprContext), _borderColorFuncBuilder.createColorOpacityFunction(_borderColor.getFunction(exprContext), strokeOpacityFunc), borderWidthFunc);
        
        std::shared_ptr<vt::StrokeMap> strokeMap = symbolizerContext.getStrokeMap();

        return [style, strokeMap, this](const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder) {
            bool suppressWarning = false;
            if (auto lineProcessor = layerBuilder.createLineProcessor(style, strokeMap)) {
                for (std::size_t featureIndex = 0; featureIndex < featureCollection.size(); featureIndex++) {
                    if (auto lineGeometry = std::get_if<LineGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& vertices : lineGeometry->getVerticesList()) {
                            lineProcessor(featureCollection.getLocalId(featureIndex), vertices);
                        }
                    }
                    else if (auto polygonGeometry = std::get_if<PolygonGeometry>(featureCollection.getGeometry(featureIndex).get())) {
                        for (const auto& verticesList : polygonGeometry->getPolygonList()) {
                            for (const auto& vertices : verticesList) {
                                lineProcessor(featureCollection.getLocalId(featureIndex), vertices);
                            }
                        }
                    }
                    else if (!suppressWarning) {
                        _logger->write(Logger::Severity::WARNING, "Unsupported geometry for LineSymbolizer");
                        suppressWarning = true;
                    }
                }
            }
        };
    }

    // The head is painted by offsetting the contour outward by half the line width. Where a
    // contour turns back on itself that offset folds over and the border blows out into blobs, so
    // only a CONVEX head is supported - removing those loops is a polygon-offsetting algorithm of
    // its own, and every navigation arrow head in the wild is convex.
    bool LineSymbolizer::isConvexArrowPath(const std::vector<cglib::vec2<float>>& points) {
        std::size_t n = points.size();
        int sign = 0;
        for (std::size_t i = 0; i < n; i++) {
            cglib::vec2<float> d0 = points[(i + 1) % n] - points[i];
            cglib::vec2<float> d1 = points[(i + 2) % n] - points[(i + 1) % n];
            float turn = d0(0) * d1(1) - d0(1) * d1(0);
            if (std::abs(turn) < 1.0e-6f) {
                continue;
            }
            int turnSign = turn > 0 ? 1 : -1;
            if (sign == 0) {
                sign = turnSign;
            }
            else if (sign != turnSign) {
                return false;
            }
        }
        return true;
    }

    // Enough of the SVG path grammar for an icon: M/L/H/V/C/S and Z, absolute or relative, with
    // curves flattened to a polyline. Not a general SVG reader - it takes the 'd' attribute, which
    // is what an icon set actually hands over, and keeps the head a POLYGON, since the tesselator
    // extrudes points and knows nothing of curves.
    std::shared_ptr<const std::vector<cglib::vec2<float>>> LineSymbolizer::parseArrowPath(const std::string& path, float boxLength, float boxWidth, float scale, float rotation) {
        constexpr int CURVE_SEGMENTS = 8;
        std::vector<cglib::vec2<float>> points;
        std::size_t pos = 0;
        char command = 0;
        cglib::vec2<float> cursor(0, 0), start(0, 0), lastControl(0, 0);
        bool hadCurve = false;

        auto skipSeparators = [&path, &pos]() {
            while (pos < path.size() && (std::isspace(static_cast<unsigned char>(path[pos])) || path[pos] == ',')) {
                pos++;
            }
        };
        auto readNumber = [&path, &pos, &skipSeparators](float& value) -> bool {
            skipSeparators();
            std::size_t begin = pos;
            if (pos < path.size() && (path[pos] == '-' || path[pos] == '+')) {
                pos++;
            }
            while (pos < path.size() && (std::isdigit(static_cast<unsigned char>(path[pos])) || path[pos] == '.')) {
                pos++;
            }
            if (pos < path.size() && (path[pos] == 'e' || path[pos] == 'E')) {
                pos++;
                if (pos < path.size() && (path[pos] == '-' || path[pos] == '+')) {
                    pos++;
                }
                while (pos < path.size() && std::isdigit(static_cast<unsigned char>(path[pos]))) {
                    pos++;
                }
            }
            if (pos == begin) {
                return false;
            }
            value = static_cast<float>(std::atof(path.substr(begin, pos - begin).c_str()));
            return true;
        };
        auto flattenCubic = [&points, CURVE_SEGMENTS](const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::vec2<float>& p2, const cglib::vec2<float>& p3) {
            for (int k = 1; k <= CURVE_SEGMENTS; k++) {
                float t = static_cast<float>(k) / CURVE_SEGMENTS, u = 1.0f - t;
                points.push_back(p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t));
            }
        };

        while (true) {
            skipSeparators();
            if (pos >= path.size()) {
                break;
            }
            char c = path[pos];
            if (std::isalpha(static_cast<unsigned char>(c))) {
                command = c;
                pos++;
                if (c == 'Z' || c == 'z') {
                    cursor = start;
                    continue;
                }
            }
            bool relative = std::islower(static_cast<unsigned char>(command));
            char op = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
            cglib::vec2<float> base = relative ? cursor : cglib::vec2<float>(0, 0);
            if (op == 'M' || op == 'L') {
                float x = 0, y = 0;
                if (!readNumber(x) || !readNumber(y)) {
                    return std::shared_ptr<const std::vector<cglib::vec2<float>>>();
                }
                cursor = base + cglib::vec2<float>(x, y);
                if (op == 'M' && points.empty()) {
                    start = cursor;
                }
                points.push_back(cursor);
                if (op == 'M') {
                    command = relative ? 'l' : 'L'; // further pairs after a moveto are linetos
                }
            }
            else if (op == 'H' || op == 'V') {
                float value = 0;
                if (!readNumber(value)) {
                    return std::shared_ptr<const std::vector<cglib::vec2<float>>>();
                }
                cursor = op == 'H' ? cglib::vec2<float>(base(0) + value, cursor(1))
                                   : cglib::vec2<float>(cursor(0), base(1) + value);
                points.push_back(cursor);
            }
            else if (op == 'C' || op == 'S') {
                cglib::vec2<float> c1, c2, end;
                float x = 0, y = 0;
                if (op == 'C') {
                    if (!readNumber(x) || !readNumber(y)) {
                        return std::shared_ptr<const std::vector<cglib::vec2<float>>>();
                    }
                    c1 = base + cglib::vec2<float>(x, y);
                }
                else {
                    c1 = hadCurve ? cursor * 2.0f - lastControl : cursor;
                }
                if (!readNumber(x) || !readNumber(y)) {
                    return std::shared_ptr<const std::vector<cglib::vec2<float>>>();
                }
                c2 = base + cglib::vec2<float>(x, y);
                if (!readNumber(x) || !readNumber(y)) {
                    return std::shared_ptr<const std::vector<cglib::vec2<float>>>();
                }
                end = base + cglib::vec2<float>(x, y);
                flattenCubic(cursor, c1, c2, end);
                lastControl = c2;
                hadCurve = true;
                cursor = end;
            }
            else {
                return std::shared_ptr<const std::vector<cglib::vec2<float>>>(); // arcs, quadratics: not read
            }
            if (op != 'C' && op != 'S') {
                hadCurve = false;
            }
        }
        if (points.size() < 3) {
            return std::shared_ptr<const std::vector<cglib::vec2<float>>>();
        }

        // Drop the points the curve flattening piles up on top of each other: they carry no shape
        // and each one is a degenerate ear that stops the triangulation dead, which shows up as
        // holes in the head.
        std::vector<cglib::vec2<float>> cleaned;
        cleaned.reserve(points.size());
        for (const cglib::vec2<float>& point : points) {
            if (cleaned.empty() || cglib::length(point - cleaned.back()) > 1.0e-4f) {
                cleaned.push_back(point);
            }
        }
        while (cleaned.size() > 1 && cglib::length(cleaned.front() - cleaned.back()) < 1.0e-4f) {
            cleaned.pop_back();
        }
        if (cleaned.size() < 3) {
            return std::shared_ptr<const std::vector<cglib::vec2<float>>>();
        }

        // Fit the contour into the arrow box (length along the line by width across it, both in
        // line widths) so an icon-set path works whatever its viewBox, keeping the ASPECT RATIO -
        // scaling the axes independently stops it being the shape the author drew. SVG's y grows
        // downwards and the tile's across the line, hence the flip.
        // CENTRED on the last vertex: offsets are in the rule's own line width, so placing it by
        // the back edge slides the casing half of (casing - fill) behind the fill instead of
        // wrapping it. Not slotted like the built-in triangle - a notch through the middle of an
        // icon that is not an arrow leaves the gashes it was meant to avoid, and the head is drawn
        // after the shaft anyway.
        cglib::vec2<float> minPos = cleaned[0], maxPos = cleaned[0];
        for (const cglib::vec2<float>& point : cleaned) {
            minPos = cglib::vec2<float>(std::min(minPos(0), point(0)), std::min(minPos(1), point(1)));
            maxPos = cglib::vec2<float>(std::max(maxPos(0), point(0)), std::max(maxPos(1), point(1)));
        }
        float spanX = std::max(1.0e-6f, maxPos(0) - minPos(0)), spanY = std::max(1.0e-6f, maxPos(1) - minPos(1));
        float fit = std::min(boxLength / spanX, boxWidth / spanY) * (scale > 0 ? scale : 1.0f);
        // The rotation turns the head about the same centre, in degrees clockwise on screen: the
        // tile's y runs across the line and downwards on screen, so a positive angle here reads
        // clockwise like a compass, not like a maths convention.
        float angle = rotation * boost::math::constants::pi<float>() / 180.0f;
        float cosA = std::cos(angle), sinA = std::sin(angle);
        auto shape = std::make_shared<std::vector<cglib::vec2<float>>>();
        shape->reserve(cleaned.size());
        for (const cglib::vec2<float>& point : cleaned) {
            float x = (point(0) - (minPos(0) + maxPos(0)) * 0.5f) * fit;
            float y = -(point(1) - (minPos(1) + maxPos(1)) * 0.5f) * fit;
            shape->emplace_back(x * cosA - y * sinA, x * sinA + y * cosA);
        }
        return shape;
    }

    std::shared_ptr<vt::BitmapPattern> LineSymbolizer::createDashBitmapPattern(const std::vector<float>& strokeDashArray, float height, vt::LineCapMode lineCap) {
        float size = std::accumulate(strokeDashArray.begin(), strokeDashArray.end(), 0.0f);
        if (size <= 0 || height <= 0) {
            return std::shared_ptr<vt::BitmapPattern>();
        }
        float superSamplingFactor = DASH_SUPERSAMPLING_FACTOR / std::accumulate(strokeDashArray.begin(), strokeDashArray.end(), 1.0f, [](float a, float b) { return b > 0 ? std::min(a, b) : a; });
        
        int pow2Size = 1;
        while (pow2Size < size * superSamplingFactor && pow2Size < 2048) {
            pow2Size *= 2;
        }
        float sizeScale = pow2Size / size;

        int pow2Height = 1;
        while (pow2Height < height * superSamplingFactor && pow2Height < 2048) {
            pow2Height *= 2;
        }
        float heightScale = pow2Height / height;

        vt::BitmapCanvas canvas(pow2Size, pow2Height);
        float radius = pow2Height * 0.5f * sizeScale / heightScale;
        float x0 = strokeDashArray.back() * 0.5f * sizeScale;
        float x1 = x0;
        float y0 = 0;
        float y1 = pow2Height;
        for (std::size_t n = 0; n < strokeDashArray.size(); n++) {
            x1 += strokeDashArray[n] * sizeScale;
            if (n % 2 == 0) {
                switch (lineCap) {
                case vt::LineCapMode::ROUND:
                    canvas.drawEllipse(cglib::vec2<float>(x0, (y0 + y1) * 0.5f), radius, pow2Height * 0.5f);
                    canvas.drawEllipse(cglib::vec2<float>(x1, (y0 + y1) * 0.5f), radius, pow2Height * 0.5f);
                    canvas.drawRectangle(cglib::vec2<float>(x0, y0), cglib::vec2<float>(x1, y1));
                    break;
                case vt::LineCapMode::SQUARE:
                    canvas.drawRectangle(cglib::vec2<float>(x0 - radius, y0), cglib::vec2<float>(x1 + radius, y1));
                    break;
                default:
                    canvas.drawRectangle(cglib::vec2<float>(x0, y0), cglib::vec2<float>(x1, y1));
                    break;
                }
            }
            x0 = x1;
        }
        
        return std::make_shared<vt::BitmapPattern>(DASH_PATTERN_SCALE / sizeScale, 1.0f / heightScale, canvas.buildBitmapImage()->bitmap);
    }
}
