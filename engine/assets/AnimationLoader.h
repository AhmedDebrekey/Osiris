#pragma once
#include "animation/Animation.h"
namespace fastgltf { class Asset; }
namespace Osiris {
    std::shared_ptr<const AnimationAsset> LoadAnimationAsset(const fastgltf::Asset& asset, std::string& error);
}
