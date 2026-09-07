#include "css2xml.h"

#include <iostream>
#include <string>
#include <vector>

// One binary for every style conversion that has to agree with the decoder, so the npm wrapper
// (@massif-maps/style-tools) ships a single wasm module rather than one per tool.
// mvt2xml is deliberately NOT here: it needs compiled Boost.Serialization, and its path handling
// only compiles where std::filesystem::path::value_type is wchar_t.

namespace {
    int usage() {
        std::cerr << "Usage: massif-style <command> [args]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "  css2xml [--roundtrip] <input-project-file> <output-xml-file>" << std::endl;
        std::cerr << "      compile a CartoCSS style project to mapnik XML" << std::endl;
        return -1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return usage();
    }

    std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    if (command == "css2xml") {
        return massif::cssutils::css2xmlMain(args);
    }
    if (command == "--help" || command == "-h" || command == "help") {
        usage();
        return 0;
    }

    std::cerr << "Unknown command: " << command << std::endl;
    return usage();
}
