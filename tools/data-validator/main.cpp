#include "data/RtsDataFile.h"

#include <array>
#include <iostream>
#include <string>

#ifndef RTS_ASSETS_DIR
#define RTS_ASSETS_DIR "assets"
#endif

int main()
{
    const std::array<std::string, 6> files = {
        std::string(RTS_ASSETS_DIR) + "/data/ai.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/buildings.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/focuses.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/technologies.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/textures.rtsdata",
        std::string(RTS_ASSETS_DIR) + "/data/units.rtsdata"};

    bool valid = true;
    for (const std::string& path : files)
    {
        const RtsDataDocument document = ReadRtsDataDocument(path);
        for (const RtsDataDiagnostic& diagnostic : document.diagnostics)
        {
            valid = false;
            std::cerr << path;
            if (diagnostic.line != 0)
                std::cerr << ':' << diagnostic.line;
            std::cerr << ": " << diagnostic.message << '\n';
        }
    }

    return valid ? 0 : 1;
}
