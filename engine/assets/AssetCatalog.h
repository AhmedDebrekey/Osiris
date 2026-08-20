#ifndef OSIRIS_ASSETCATALOG_H
#define OSIRIS_ASSETCATALOG_H

#include <string>
#include <vector>

namespace Osiris {
    struct AssetEntry {
        std::string name;
        std::string relativePath;
    };

    class AssetCatalog {
    public:
        static std::vector<AssetEntry> ListModels();
        static std::vector<AssetEntry> ListScenes();
    };
}

#endif //OSIRIS_ASSETCATALOG_H
