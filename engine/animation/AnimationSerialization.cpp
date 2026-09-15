#include "AnimationSerialization.h"
#include <algorithm>

namespace Osiris {
    nlohmann::json SaveAnimator(const Animator& animator) {
        nlohmann::json json = {
            {"clip", animator.GetCurrentClip()}, {"state", animator.GetCurrentState()},
            {"loop", animator.IsLooping()}, {"speed", animator.GetSpeed()}, {"playing", animator.IsPlaying()},
            {"parameters", animator.GetParameters()},
            {"states", nlohmann::json::array()}, {"transitions", nlohmann::json::array()},
        };
        for (const auto& state : animator.GetGraph().GetStates()) json["states"].push_back({
            {"name", state.name}, {"clip", state.clip}, {"loop", state.loop}, {"speed", state.speed}});
        for (const auto& transition : animator.GetGraph().GetTransitions()) json["transitions"].push_back({
            {"from", transition.from}, {"to", transition.to}, {"parameter", transition.parameter},
            {"comparison", static_cast<int>(transition.comparison)}, {"threshold", transition.threshold},
            {"fadeSeconds", transition.fadeSeconds}});
        return json;
    }

    bool LoadAnimator(Animator& animator, const nlohmann::json& json, std::string& error) {
        error.clear();
        try {
            if (!json.is_object()) { error = "animator must be an object"; return false; }
            Animator loaded;
            loaded.SetAsset(animator.GetAsset());
            const auto states = json.value("states", nlohmann::json::array());
            const auto transitions = json.value("transitions", nlohmann::json::array());
            if (!states.is_array() || !transitions.is_array()) { error = "states and transitions must be arrays"; return false; }
            const auto clips = loaded.GetClips();
            for (const auto& state : states) {
                if (std::ranges::find(clips, state.at("clip").get<std::string>()) == clips.end()) {
                    error = "graph state references an unknown clip"; return false;
                }
                if (!loaded.GetGraph().AddState(state.at("name"), state.at("clip"), state.value("loop", true), state.value("speed", 1.0f))) {
                    error = "invalid or duplicate graph state"; return false;
                }
            }
            for (const auto& transition : transitions) {
                if (!loaded.GetGraph().AddTransition(transition.at("from"), transition.at("to"), transition.at("parameter"),
                    static_cast<AnimationComparison>(transition.at("comparison").get<int>()),
                    transition.at("threshold"), transition.value("fadeSeconds", 0.2f))) {
                    error = "invalid graph transition"; return false;
                }
            }
            const auto parameters = json.value("parameters", nlohmann::json::object());
            if (!parameters.is_object()) { error = "parameters must be an object"; return false; }
            for (const auto& [name, value] : parameters.items()) if (!loaded.SetFloat(name, value.get<float>())) {
                error = "invalid graph parameter"; return false;
            }
            if (!loaded.SetSpeed(json.value("speed", 1.0f))) { error = "invalid playback speed"; return false; }
            const std::string state = json.value("state", std::string{});
            const std::string clip = json.value("clip", std::string{});
            if (!state.empty()) {
                if (!loaded.StartGraph(state)) { error = "unknown state or animation clip"; return false; }
            } else if (!clip.empty() && !loaded.Play(clip, 0.0f, json.value("loop", true))) {
                error = "unknown animation clip: " + clip; return false;
            }
            if (!json.value("playing", true)) loaded.Pause();
            animator = std::move(loaded);
            return true;
        } catch (const nlohmann::json::exception& exception) {
            error = exception.what();
            return false;
        }
    }
}
