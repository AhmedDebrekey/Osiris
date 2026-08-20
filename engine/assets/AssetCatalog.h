#ifndef OSIRIS_ASSETCATALOG_H
#define OSIRIS_ASSETCATALOG_H

#include <string>
#include <vector>

namespace Osiris {
    struct ModelAsset {
        std::string name;
        std::string relativePath;
    };

    class AssetCatalog {
    public:
        static std::vector<ModelAsset> ListModels();
    };
}

#endif //OSIRIS_ASSETCATALOG_H
