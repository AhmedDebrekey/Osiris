#pragma once
#include "Animation.h"
#include <nlohmann/json.hpp>

namespace Osiris {
    nlohmann::json SaveAnimator(const Animator& animator);
    bool LoadAnimator(Animator& animator, const nlohmann::json& json, std::string& error);
}
