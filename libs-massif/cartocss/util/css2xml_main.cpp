#include "css2xml.h"

#include <string>
#include <vector>

// The standalone css2xml, kept beside massif-style so existing scripts and CI keep working.
int main(int argc, char* argv[]) {
    return massif::cssutils::css2xmlMain(std::vector<std::string>(argv + 1, argv + argc));
}
