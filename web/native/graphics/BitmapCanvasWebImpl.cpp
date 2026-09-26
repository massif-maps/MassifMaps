#include "graphics/BitmapCanvasWebImpl.h"
#include "graphics/Bitmap.h"
#include "utils/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include <emscripten.h>

// EM_JS rather than EM_ASM, which splits its code on commas. Measure and draw share one layout;
// lines are fontSize * 1.2 high, as Android's StaticLayout and CoreText lay them out.
EM_JS(int, massif_canvas_create, (int width, int height), {
    if (!globalThis.__massifCanvas) {
        const canvases = new Map();
        let next = 1;
        globalThis.__massifCanvas = {
            canvases,
            add(entry) { const id = next++; canvases.set(id, entry); return id; },
            layout(context, text, maxWidth, breakLines) {
                const lines = [];
                for (const paragraph of text.split('\n')) {
                    if (!breakLines || maxWidth <= 0) { lines.push(paragraph); continue; }
                    let line = '';
                    for (const word of paragraph.split(' ')) {
                        const candidate = line ? line + ' ' + word : word;
                        if (line && context.measureText(candidate).width > maxWidth) { lines.push(line); line = word; } else { line = candidate; }
                    }
                    lines.push(line);
                }
                return lines;
            }
        };
    }
    const w = Math.max(1, width);
    const h = Math.max(1, height);
    const canvas = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(w, h) : Object.assign(document.createElement('canvas'), { width: w, height: h });
    const context = canvas.getContext('2d', { willReadFrequently: true });
    context.textBaseline = 'top';
    context.lineJoin = 'round';
    return globalThis.__massifCanvas.add({ canvas, context, fontSize: 12, lineCount: 0 });
});

EM_JS(void, massif_canvas_destroy, (int id), {
    globalThis.__massifCanvas.canvases.delete(id);
});

EM_JS(void, massif_canvas_paint, (int id, int r, int g, int b, int a, float lineWidth, const char* font, float fontSize), {
    const entry = globalThis.__massifCanvas.canvases.get(id);
    const style = 'rgba(' + r + ',' + g + ',' + b + ',' + (a / 255) + ')';
    entry.context.fillStyle = style;
    entry.context.strokeStyle = style;
    entry.context.lineWidth = lineWidth;
    entry.context.font = UTF8ToString(font);
    entry.fontSize = fontSize;
});

EM_JS(void, massif_canvas_clip, (int id, float x, float y, float width, float height), {
    const context = globalThis.__massifCanvas.canvases.get(id).context;
    context.save();
    context.beginPath();
    context.rect(x, y, width, height);
    context.clip();
});

EM_JS(void, massif_canvas_unclip, (int id), {
    globalThis.__massifCanvas.canvases.get(id).context.restore();
});

EM_JS(void, massif_canvas_text, (int id, const char* text, float x, float y, int maxWidth, int breakLines, int stroke), {
    const entry = globalThis.__massifCanvas.canvases.get(id);
    const context = entry.context;
    const lines = globalThis.__massifCanvas.layout(context, UTF8ToString(text), maxWidth, breakLines);
    const lineHeight = entry.fontSize * 1.2;
    // 'top' puts the em box at the top of a 1.2 em line box: half the leading above it.
    const lead = (lineHeight - entry.fontSize) / 2;
    lines.forEach((line, index) => {
        if (stroke) { context.strokeText(line, x, y + lead + index * lineHeight); } else { context.fillText(line, x, y + lead + index * lineHeight); }
    });
});

EM_JS(void, massif_canvas_polygon, (int id, const float* coords, int count, int stroke), {
    const context = globalThis.__massifCanvas.canvases.get(id).context;
    const values = HEAPF32.subarray(coords >> 2, (coords >> 2) + count * 2);
    context.beginPath();
    context.moveTo(values[0], values[1]);
    for (let i = 1; i < count; i++) { context.lineTo(values[i * 2], values[i * 2 + 1]); }
    context.closePath();
    if (stroke) { context.stroke(); } else { context.fill(); }
});

EM_JS(void, massif_canvas_round_rect, (int id, float x, float y, float width, float height, float radius, int stroke), {
    const context = globalThis.__massifCanvas.canvases.get(id).context;
    const r = Math.max(0, Math.min(radius, width / 2, height / 2));
    context.beginPath();
    context.moveTo(x + r, y);
    context.arcTo(x + width, y, x + width, y + height, r);
    context.arcTo(x + width, y + height, x, y + height, r);
    context.arcTo(x, y + height, x, y, r);
    context.arcTo(x, y, x + width, y, r);
    context.closePath();
    if (stroke) { context.stroke(); } else { context.fill(); }
});

EM_JS(void, massif_canvas_image, (int id, const unsigned char* pixels, int width, int height, float x, float y, float drawWidth, float drawHeight), {
    const context = globalThis.__massifCanvas.canvases.get(id).context;
    // A Bitmap is premultiplied, ImageData is not.
    const data = new Uint8ClampedArray(HEAPU8.slice(pixels, pixels + width * height * 4));
    for (let i = 0; i < data.length; i += 4) {
        const alpha = data[i + 3];
        if (alpha > 0 && alpha < 255) { for (let c = 0; c < 3; c++) { data[i + c] = data[i + c] * 255 / alpha; } }
    }
    const source = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(width, height) : Object.assign(document.createElement('canvas'), { width, height });
    source.getContext('2d').putImageData(new ImageData(data, width, height), 0, 0);
    context.drawImage(source, x, y, drawWidth, drawHeight);
});

EM_JS(double, massif_canvas_measure, (int id, const char* text, int maxWidth, int breakLines), {
    const entry = globalThis.__massifCanvas.canvases.get(id);
    const lines = globalThis.__massifCanvas.layout(entry.context, UTF8ToString(text), maxWidth, breakLines);
    entry.lineCount = lines.length;
    return lines.reduce((widest, line) => Math.max(widest, entry.context.measureText(line).width), 0);
});

EM_JS(double, massif_canvas_measured_height, (int id), {
    const entry = globalThis.__massifCanvas.canvases.get(id);
    return entry.lineCount * entry.fontSize * 1.2;
});

EM_JS(void, massif_canvas_read, (int id, unsigned char* pixels, int width, int height), {
    // getImageData is straight alpha, a Bitmap premultiplied.
    const data = globalThis.__massifCanvas.canvases.get(id).context.getImageData(0, 0, width, height).data;
    for (let i = 0; i < data.length; i += 4) {
        const alpha = data[i + 3];
        if (alpha < 255) { for (let c = 0; c < 3; c++) { data[i + c] = data[i + c] * alpha / 255; } }
    }
    HEAPU8.set(data, pixels);
});


namespace massif {

    namespace {
        // "Roboto Bold", "HelveticaNeue-Light": the family and the CSS weight a trailing style names.
        std::string cssFont(const std::string& familyName, float size) {
            static const struct { const char* suffix; int weight; } WEIGHTS[] = {
                { "black", 900 }, { "heavy", 800 }, { "extrabold", 800 }, { "semibold", 600 }, { "demibold", 600 },
                { "bold", 700 }, { "medium", 500 }, { "regular", 400 }, { "light", 300 }, { "thin", 100 }
            };
            std::string family = familyName;
            int weight = 400;
            std::string lower(family.size(), ' ');
            std::transform(family.begin(), family.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (const auto& entry : WEIGHTS) {
                std::string suffix(entry.suffix);
                if (lower.size() > suffix.size() + 1 && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    char separator = lower[lower.size() - suffix.size() - 1];
                    if (separator == ' ' || separator == '-') {
                        family = family.substr(0, family.size() - suffix.size() - 1);
                        weight = entry.weight;
                        break;
                    }
                }
            }
            std::string families = "system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif";
            if (!family.empty() && family != "sans-serif" && family != "system-ui") {
                families = "'" + family + "', " + families;
            }
            return std::to_string(weight) + " " + std::to_string(size) + "px " + families;
        }
    }

    BitmapCanvas::WebImpl::WebImpl(int width, int height) :
        _width(width),
        _height(height),
        _canvasId(0),
        _drawMode(FILL),
        _color(0xFF000000),
        _strokeWidth(1.0f),
        _font(cssFont("", 12.0f)),
        _fontSize(12.0f)
    {
        _canvasId = massif_canvas_create(width, height);
    }

    BitmapCanvas::WebImpl::~WebImpl() {
        massif_canvas_destroy(_canvasId);
    }

    void BitmapCanvas::WebImpl::setDrawMode(DrawMode mode) {
        _drawMode = mode;
    }

    void BitmapCanvas::WebImpl::setColor(const Color& color) {
        _color = color;
    }

    void BitmapCanvas::WebImpl::setStrokeWidth(float width) {
        _strokeWidth = width;
    }

    void BitmapCanvas::WebImpl::setFont(const std::string& familyName, const std::string& fileName, float size) {
        _font = cssFont(familyName, size);
        _fontSize = size;
    }

    void BitmapCanvas::WebImpl::applyPaint() const {
        massif_canvas_paint(_canvasId, _color.getR(), _color.getG(), _color.getB(), _color.getA(), _strokeWidth, _font.c_str(), _fontSize);
    }

    void BitmapCanvas::WebImpl::pushClipRect(const ScreenBounds& clipRect) {
        massif_canvas_clip(_canvasId, clipRect.getMin().getX(), clipRect.getMin().getY(), clipRect.getWidth(), clipRect.getHeight());
    }

    void BitmapCanvas::WebImpl::popClipRect() {
        massif_canvas_unclip(_canvasId);
    }

    void BitmapCanvas::WebImpl::drawText(std::string text, const ScreenPos& pos, int maxWidth, bool breakLines) {
        if (text.empty()) {
            return;
        }
        applyPaint();
        massif_canvas_text(_canvasId, text.c_str(), pos.getX(), pos.getY(), maxWidth, breakLines ? 1 : 0, _drawMode == STROKE ? 1 : 0);
    }

    void BitmapCanvas::WebImpl::drawPolygon(const std::vector<ScreenPos>& poses) {
        if (poses.empty()) {
            return;
        }
        applyPaint();
        std::vector<float> coords;
        coords.reserve(poses.size() * 2);
        for (const ScreenPos& pos : poses) {
            coords.push_back(pos.getX());
            coords.push_back(pos.getY());
        }
        massif_canvas_polygon(_canvasId, coords.data(), static_cast<int>(poses.size()), _drawMode == STROKE ? 1 : 0);
    }

    void BitmapCanvas::WebImpl::drawRoundRect(const ScreenBounds& rect, float radius) {
        applyPaint();
        massif_canvas_round_rect(_canvasId, rect.getMin().getX(), rect.getMin().getY(), rect.getWidth(), rect.getHeight(), radius, _drawMode == STROKE ? 1 : 0);
    }

    void BitmapCanvas::WebImpl::drawBitmap(const ScreenBounds& rect, const std::shared_ptr<Bitmap>& bitmap) {
        std::shared_ptr<Bitmap> rgbaBitmap = bitmap ? bitmap->getRGBABitmap() : std::shared_ptr<Bitmap>();
        if (!rgbaBitmap) {
            return;
        }
        massif_canvas_image(_canvasId, rgbaBitmap->getPixelData().data(), static_cast<int>(rgbaBitmap->getWidth()), static_cast<int>(rgbaBitmap->getHeight()),
                            rect.getMin().getX(), rect.getMin().getY(), rect.getWidth(), rect.getHeight());
    }

    ScreenBounds BitmapCanvas::WebImpl::measureTextSize(std::string text, int maxWidth, bool breakLines) const {
        if (text.empty()) {
            return ScreenBounds(ScreenPos(0, 0), ScreenPos(0, 0));
        }
        applyPaint();
        double width = massif_canvas_measure(_canvasId, text.c_str(), maxWidth, breakLines ? 1 : 0);
        double height = massif_canvas_measured_height(_canvasId);
        return ScreenBounds(ScreenPos(0, 0), ScreenPos(static_cast<float>(std::ceil(width)), static_cast<float>(std::ceil(height))));
    }

    std::shared_ptr<Bitmap> BitmapCanvas::WebImpl::buildBitmap() const {
        int width = std::max(1, _width);
        int height = std::max(1, _height);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
        massif_canvas_read(_canvasId, pixels.data(), width, height);
        return std::make_shared<Bitmap>(pixels.data(), width, height, ColorFormat::COLOR_FORMAT_RGBA, width * 4);
    }

}
