#include "css2xml.h"
#include "legend.h"

#include <iostream>
#include <string>
#include <vector>

// One binary for every style conversion that must agree with the decoder, so @massif-maps/style-tools ships one wasm module.
// mvt2xml is NOT here: it needs compiled Boost.Serialization and a wchar_t std::filesystem::path::value_type.

namespace {
    int usage() {
        std::cerr << "Usage: massif-style <command> [args]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "  css2xml [--roundtrip] <input-project-file> <output-xml-file>" << std::endl;
        std::cerr << "      compile a CartoCSS style project to mapnik XML" << std::endl;
        std::cerr << "  legend [--spec legend.json] [--params name=value]... [--out file] <input-project-file>" << std::endl;
        std::cerr << "      resolve a legend spec against the style, as JSON" << std::endl;
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
    if (command == "legend") {
        return massif::cssutils::legendMain(args);
    }
    if (command == "--help" || command == "-h" || command == "help") {
        usage();
        return 0;
    }

    std::cerr << "Unknown command: " << command << std::endl;
    return usage();
}
