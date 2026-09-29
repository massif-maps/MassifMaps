#ifndef _MASSIF_CSSUTILS_LEGEND_H_
#define _MASSIF_CSSUTILS_LEGEND_H_

#include <string>
#include <vector>

namespace massif::cssutils {
    /**
     * Resolves a legend spec against a CartoCSS style project, as MBVectorTileDecoder::getLegend does.
     * args: [--spec legend.json] [--params name=value]... [--out file] input-project-file.
     * The spec defaults to legend.json beside the project; the result goes to stdout unless --out.
     */
    int legendMain(const std::vector<std::string>& args);
}

#endif
