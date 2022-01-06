#include "ParseTables.h"

namespace carto::mvt {
    const ParseTable<vt::LineCapMode>& getLineCapModeTable() {
        static const std::unordered_map<std::string, vt::LineCapMode> lineCapModeTable = {
            { "round",  vt::LineCapMode::ROUND  },
            { "square", vt::LineCapMode::SQUARE },
            { "butt",   vt::LineCapMode::NONE   }
        };
        return lineCapModeTable;
    }

    const ParseTable<vt::LineJoinMode>& getLineJoinModeTable() {
        static const std::unordered_map<std::string, vt::LineJoinMode> lineJoinModeTable = {
            { "none",  vt::LineJoinMode::NONE  },
            { "round", vt::LineJoinMode::ROUND },
            { "bevel", vt::LineJoinMode::BEVEL },
            { "miter", vt::LineJoinMode::MITER }
        };
        return lineJoinModeTable;
    }

    const ParseTable<vt::CompOp>& getCompOpTable() {
        static const std::unordered_map<std::string, vt::CompOp> compOpTable = {
            { "src",      vt::CompOp::SRC },
            { "src-over", vt::CompOp::SRC_OVER },
            { "src-in",   vt::CompOp::SRC_IN },
            { "src-atop", vt::CompOp::SRC_ATOP },
            { "dst",      vt::CompOp::DST },
            { "dst-over", vt::CompOp::DST_OVER },
            { "dst-in",   vt::CompOp::DST_IN },
            { "dst-atop", vt::CompOp::DST_ATOP },
            { "clear",    vt::CompOp::ZERO },
            { "zero",     vt::CompOp::ZERO },
            { "plus",     vt::CompOp::PLUS },
            { "minus",    vt::CompOp::MINUS },
            { "multiply", vt::CompOp::MULTIPLY },
            { "screen",   vt::CompOp::SCREEN },
            { "darken",   vt::CompOp::DARKEN },
            { "lighten",  vt::CompOp::LIGHTEN }
        };
        return compOpTable;
    }

    const ParseTable<vt::LabelOrientation>& getLabelOrientationTable() {
        static const std::unordered_map<std::string, vt::LabelOrientation> labelOrientationTable = {
            { "point",         vt::LabelOrientation::BILLBOARD_2D },
            { "nutibillboard", vt::LabelOrientation::BILLBOARD_3D },
            { "nutipoint",     vt::LabelOrientation::POINT },
            { "line",          vt::LabelOrientation::LINE }
        };
        return labelOrientationTable;
    }
}
