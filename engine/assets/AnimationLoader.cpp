#include "AnimationLoader.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Osiris {
    std::shared_ptr<const AnimationAsset> LoadAnimationAsset(const fastgltf::Asset& asset, std::string& error) {
        error.clear();
        if (asset.animations.empty() && asset.skins.empty()) return {};
        auto result = std::make_shared<AnimationAsset>();
        result->bindPose.resize(asset.nodes.size());
        result->animatedNodes.resize(asset.nodes.size(), false);
        for (size_t i = 0; i < asset.nodes.size(); i++) {
            auto& pose = result->bindPose[i];
            if (const auto* trs = std::get_if<fastgltf::TRS>(&asset.nodes[i].transform)) {
                pose.translation = glm::make_vec3(trs->translation.data());
                const glm::quat rotation(trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]);
                pose.rotation = glm::dot(rotation, rotation) > 1e-12f ? glm::normalize(rotation) : glm::quat(1, 0, 0, 0);
                pose.scale = glm::make_vec3(trs->scale.data());
            } else {
                // Matrix-authored nodes cannot be animation targets. Their exact local matrix
                // remains in the scene hierarchy; only TRS targets need a sampled bind pose.
                pose.translation = glm::vec3(glm::make_mat4(
                    std::get<fastgltf::Node::TransformMatrix>(asset.nodes[i].transform).data())[3]);
            }
        }
        for (const auto& skin : asset.skins) {
            AnimationSkin imported;
            for (size_t joint : skin.joints) {
                if (joint >= asset.nodes.size()) { error = "skin joint index is out of range"; return {}; }
                imported.joints.push_back(static_cast<uint32_t>(joint));
            }
            if (imported.joints.empty()) { error = "skin has no joints"; return {}; }
            imported.inverseBindMatrices.resize(imported.joints.size(), glm::mat4(1.0f));
            if (skin.inverseBindMatrices.has_value()) {
                if (skin.inverseBindMatrices.value() >= asset.accessors.size()) {
                    error = "inverse bind matrix accessor is out of range"; return {};
                }
                const auto& accessor = asset.accessors[skin.inverseBindMatrices.value()];
                if (accessor.type != fastgltf::AccessorType::Mat4 || accessor.count != skin.joints.size()) {
                    error = "inverse bind matrix count/type does not match the skin"; return {};
                }
                size_t i = 0;
                fastgltf::iterateAccessor<glm::mat4>(asset, accessor, [&](glm::mat4 matrix) {
                    for (int col = 0; col < 4; col++) for (int row = 0; row < 4; row++)
                        if (!std::isfinite(matrix[col][row])) error = "non-finite inverse bind matrix";
                    imported.inverseBindMatrices[i++] = matrix;
                });
                if (!error.empty()) return {};
            }
            result->skins.push_back(std::move(imported));
        }
        std::unordered_set<std::string> names;
        for (size_t clipIndex = 0; clipIndex < asset.animations.size(); clipIndex++) {
            const auto& animation = asset.animations[clipIndex];
            AnimationClip clip;
            clip.name = animation.name.empty() ? "Animation_" + std::to_string(clipIndex) : std::string(animation.name);
            if (!names.insert(clip.name).second) { error = "animation clip names must be unique: " + clip.name; return {}; }
            for (const auto& channel : animation.channels) {
                if (!channel.nodeIndex.has_value() || channel.nodeIndex.value() >= asset.nodes.size()
                    || channel.samplerIndex >= animation.samplers.size()) {
                    error = "animation channel references an invalid node or sampler"; return {};
                }
                AnimationChannel imported;
                imported.node = static_cast<uint32_t>(channel.nodeIndex.value());
                switch (channel.path) {
                    case fastgltf::AnimationPath::Translation: imported.path = AnimationPath::Translation; break;
                    case fastgltf::AnimationPath::Rotation: imported.path = AnimationPath::Rotation; break;
                    case fastgltf::AnimationPath::Scale: imported.path = AnimationPath::Scale; break;
                    default: error = "morph-weight animation is not supported yet"; return {};
                }
                if (!std::holds_alternative<fastgltf::TRS>(asset.nodes[imported.node].transform)) {
                    error = "animated nodes must use translation/rotation/scale, not a matrix"; return {};
                }
                const auto& sampler = animation.samplers[channel.samplerIndex];
                if (sampler.inputAccessor >= asset.accessors.size() || sampler.outputAccessor >= asset.accessors.size()) {
                    error = "animation sampler references an invalid accessor"; return {};
                }
                switch (sampler.interpolation) {
                    case fastgltf::AnimationInterpolation::Step: imported.interpolation = AnimationInterpolation::Step; break;
                    case fastgltf::AnimationInterpolation::Linear: imported.interpolation = AnimationInterpolation::Linear; break;
                    case fastgltf::AnimationInterpolation::CubicSpline: imported.interpolation = AnimationInterpolation::CubicSpline; break;
                    default: error = "unsupported animation interpolation"; return {};
                }
                const auto& input = asset.accessors[sampler.inputAccessor];
                const auto& output = asset.accessors[sampler.outputAccessor];
                const bool rotation = imported.path == AnimationPath::Rotation;
                const size_t stride = imported.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
                if (input.count == 0 || input.type != fastgltf::AccessorType::Scalar
                    || input.componentType != fastgltf::ComponentType::Float
                    || output.componentType != fastgltf::ComponentType::Float
                    || output.type != (rotation ? fastgltf::AccessorType::Vec4 : fastgltf::AccessorType::Vec3)
                    || output.count != input.count * stride) {
                    error = "animation keyframe accessor type/count mismatch"; return {};
                }
                fastgltf::iterateAccessor<float>(asset, input, [&](float time) { imported.times.push_back(time); });
                for (size_t i = 0; i < imported.times.size(); i++) {
                    if (!std::isfinite(imported.times[i]) || imported.times[i] < 0.0f
                        || (i && imported.times[i] <= imported.times[i - 1])) {
                        error = "animation times must be finite, non-negative and strictly increasing"; return {};
                    }
                }
                if (rotation) fastgltf::iterateAccessor<glm::vec4>(asset, output,
                    [&](glm::vec4 value) { imported.values.push_back(value); });
                else fastgltf::iterateAccessor<glm::vec3>(asset, output,
                    [&](glm::vec3 value) { imported.values.emplace_back(value, 0.0f); });
                for (const auto& value : imported.values) for (int axis = 0; axis < 4; axis++)
                    if (!std::isfinite(value[axis])) { error = "non-finite animation keyframe"; return {}; }
                clip.duration = std::max(clip.duration, imported.times.back());
                result->animatedNodes[imported.node] = true;
                clip.channels.push_back(std::move(imported));
            }
            result->clips.push_back(std::move(clip));
        }
        return result;
    }
}
