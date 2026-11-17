#include "LabelScenario.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace massif::vt::scenario {
    namespace {
        std::string escape(const std::string& text) {
            std::string out;
            for (char c : text) {
                out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
            }
            return out;
        }

        std::string number(double value) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.1f", value);
            return buffer;
        }

        struct Page {
            float height;

            // The culler's y goes up, the page's down.
            std::string points(const Quad& quad) const {
                std::string out;
                for (const cglib::vec2<float>& p : quad) {
                    out += number(p(0)) + "," + number(height - p(1)) + " ";
                }
                return out;
            }

            cglib::vec2<float> center(const Quad& quad) const {
                return cglib::vec2<float>((quad[0](0) + quad[2](0)) * 0.5f, height - (quad[0](1) + quad[2](1)) * 0.5f);
            }
        };

        std::string polygon(const Page& frame, const Quad& quad, const std::string& style) {
            return "<polygon points=\"" + frame.points(quad) + "\" " + style + "/>";
        }

        // The text of a box, squeezed to fill it: the box is what the culler tests, so it is what is shown.
        std::string textIn(const Page& frame, const Quad& quad, const std::string& text, const std::string& style) {
            float width = std::abs(quad[1](0) - quad[0](0)), height = std::abs(quad[2](1) - quad[0](1));
            cglib::vec2<float> middle = frame.center(quad);
            return "<text x=\"" + number(middle(0)) + "\" y=\"" + number(middle(1)) + "\" font-size=\"" + number(height * 0.75) + "\" text-anchor=\"middle\" dominant-baseline=\"central\" textLength=\"" + number(width) + "\" lengthAdjust=\"spacingAndGlyphs\" " + style + ">" + escape(text) + "</text>";
        }

        enum class Mark { NONE, DIFFERS, FLIPPED };

        std::string draw(const Page& frame, const Decision& decision, Mark mark, bool watched) {
            const std::string stroke = mark == Mark::DIFFERS ? "#dc2626" : mark == Mark::FLIPPED ? "#ea580c" : decision.line ? "#15803d" : decision.text ? "#1d4ed8" : "#b45309";
            const std::string fill = watched ? "#fde68a" : "none";
            const std::string outline = "stroke=\"" + stroke + "\" stroke-width=\"" + (mark != Mark::NONE || watched ? "1.6" : "0.7") + "\" fill=\"" + fill + "\"";
            std::ostringstream out;
            if (decision.line) {
                for (const Quad& glyph : decision.boxes) {
                    out << polygon(frame, glyph, outline);
                }
                if (!decision.boxes.empty()) {
                    cglib::vec2<float> first = frame.center(decision.boxes.front()), last = frame.center(decision.boxes.back());
                    float length = cglib::length(last - first) + std::abs(decision.boxes.front()[1](0) - decision.boxes.front()[0](0));
                    float height = std::abs(decision.boxes.front()[2](1) - decision.boxes.front()[0](1));
                    cglib::vec2<float> middle = (first + last) * 0.5f;
                    double angle = std::atan2(last(1) - first(1), last(0) - first(0)) * 180.0 / 3.14159265358979323846;
                    out << "<text transform=\"translate(" << number(middle(0)) << "," << number(middle(1)) << ") rotate(" << number(angle) << ")\" font-size=\"" << number(height * 0.7) << "\" text-anchor=\"middle\" dominant-baseline=\"central\" textLength=\"" << number(length) << "\" lengthAdjust=\"spacingAndGlyphs\" fill=\"" << stroke << "\">" << escape(decision.name) << "</text>";
                }
            } else if (decision.boxes.size() == 2) {
                out << polygon(frame, decision.boxes[0], outline) << polygon(frame, decision.boxes[1], outline) << textIn(frame, decision.boxes[0], decision.name, "fill=\"" + stroke + "\"");
            } else {
                for (const Quad& box : decision.boxes) {
                    out << polygon(frame, box, outline);
                }
            }
            return out.str();
        }
    }

    std::string renderHtml(const Scenario& scenario, const Evaluation& evaluation) {
        Page page { evaluation.height };
        const float band = evaluation.band;
        const std::string size = number(evaluation.width + 2 * band);
        const Camera finalCamera = evaluation.paths[0].frames.back().camera;
        const Decisions& reference = evaluation.paths[0].decisions;
        std::ostringstream out;
        out << "<!doctype html><meta charset=\"utf-8\"><title>" << escape(scenario.name) << "</title>"
            << "<style>body{font:13px sans-serif;margin:16px}.row{display:flex;gap:16px;flex-wrap:wrap}.pane{width:" << size << "px}"
            << "svg{background:#f8fafc;border:1px solid #cbd5e1}h2{font-size:15px;margin:0 0 4px}.cap{color:#475569;font-weight:normal}"
            << ".k{display:inline-block;width:10px;height:10px;margin:0 3px 0 10px;border:2px solid}#bar{position:sticky;top:0;background:#fff;padding:6px 0;z-index:1}#bar input{width:60%}</style>"
            << "<h1>" << escape(scenario.name) << "</h1>"
            << "<div id=bar><button id=play>play</button> <input id=slider type=range min=0 value=0> <span id=step></span> &nbsp; arrows step, space plays</div><p>"
            << "<span class=k style=\"border-color:#1d4ed8\"></span>POI with its name<span class=k style=\"border-color:#b45309\"></span>icon only"
            << "<span class=k style=\"border-color:#15803d\"></span>street name<span class=k style=\"border-color:#ea580c\"></span>changed since the pass before"
            << "<span class=k style=\"border-color:#dc2626\"></span>at the final camera: differs from the first path (dashed: it hid it)"
            << "<span class=k style=\"border-color:#fde68a;background:#fde68a\"></span>watched</p><div class=row>";
        std::size_t longest = 0;
        for (std::size_t index = 0; index < evaluation.paths.size(); index++) {
            const PathOutcome& outcome = evaluation.paths[index];
            longest = std::max(longest, outcome.frames.size());
            out << "<div class=pane><h2>" << escape(outcome.name) << " &mdash; final score " << number(outcome.score) << ": " << outcome.poiNames << " names, " << outcome.poiIcons << " icons, " << outcome.streets << " streets <span class=cap></span></h2>"
                << "<svg width=\"" << size << "\" height=\"" << number(evaluation.height + 2 * band) << "\" viewBox=\"" << number(-band) << " " << number(-band) << " " << size << " " << number(evaluation.height + 2 * band) << "\">"
                << "<rect x=\"0\" y=\"0\" width=\"" << number(evaluation.width) << "\" height=\"" << number(evaluation.height) << "\" fill=\"#fff\" stroke=\"#64748b\"/>";
            for (std::size_t pass = 0; pass < outcome.frames.size(); pass++) {
                const scenario::Frame& frame = outcome.frames[pass];
                bool atFinal = std::abs(frame.camera.zoom - finalCamera.zoom) < 1e-9 && std::abs(frame.camera.tileX - finalCamera.tileX) < 1e-9 && std::abs(frame.camera.tileY - finalCamera.tileY) < 1e-9;
                const Decisions* previous = pass > 0 ? &outcome.frames[pass - 1].decisions : nullptr;
                char caption[96];
                std::snprintf(caption, sizeof caption, "pass %zu/%zu, z%.2f%s", pass + 1, outcome.frames.size(), frame.camera.zoom, atFinal ? " (final camera)" : "");
                out << "<g class=frame data-cap=\"" << caption << "\" style=\"display:none\">";
                for (const auto& [key, decision] : frame.decisions) {
                    bool watched = std::find(scenario.watch.begin(), scenario.watch.end(), decision.name) != scenario.watch.end();
                    auto first = reference.find(key);
                    bool differs = index > 0 && atFinal && first != reference.end() && !first->second.sameAs(decision);
                    auto before = previous ? previous->find(key) : Decisions::const_iterator();
                    bool flipped = previous && before != previous->end() && !before->second.sameAs(decision);
                    if (decision.visible) {
                        out << draw(page, decision, differs ? Mark::DIFFERS : flipped ? Mark::FLIPPED : Mark::NONE, watched);
                    } else if (differs && first->second.visible) {
                        for (const Quad& box : first->second.boxes) {
                            out << polygon(page, box, "fill=\"none\" stroke=\"#dc2626\" stroke-width=\"1.2\" stroke-dasharray=\"3 2\"");
                        }
                    }
                }
                out << "</g>";
            }
            out << "</svg></div>";
        }
        out << "</div><script>"
            << "const panes=[...document.querySelectorAll('.pane')],slider=document.getElementById('slider'),play=document.getElementById('play'),step=document.getElementById('step');"
            << "const last=" << longest - 1 << ";slider.max=last;let timer=null;"
            << "function show(i){i=Math.max(0,Math.min(last,i));slider.value=i;step.textContent='step '+(i+1)+'/'+(last+1);"
            << "for(const p of panes){const f=p.querySelectorAll('.frame'),k=Math.min(i,f.length-1);f.forEach((g,j)=>g.style.display=j==k?'':'none');p.querySelector('.cap').textContent=f[k].dataset.cap}}"
            << "function stop(){clearInterval(timer);timer=null;play.textContent='play'}"
            << "function go(){if(timer)return stop();if(+slider.value>=last)show(0);play.textContent='pause';timer=setInterval(()=>{if(+slider.value>=last)return stop();show(+slider.value+1)},300)}"
            << "slider.oninput=()=>{stop();show(+slider.value)};play.onclick=go;"
            << "onkeydown=e=>{if(e.key=='ArrowRight'){stop();show(+slider.value+1)}else if(e.key=='ArrowLeft'){stop();show(+slider.value-1)}else if(e.key==' '){e.preventDefault();go()}};"
            << "show(0);</script>";
        return out.str();
    }
}
