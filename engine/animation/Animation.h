#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Osiris {
    struct AnimationPose {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 Matrix() const;
    };

    enum class AnimationPath { Translation, Rotation, Scale };
    enum class AnimationInterpolation { Step, Linear, CubicSpline };

    struct AnimationChannel {
        uint32_t node = 0;
        AnimationPath path = AnimationPath::Translation;
        AnimationInterpolation interpolation = AnimationInterpolation::Linear;
        std::vector<float> times;
        std::vector<glm::vec4> values;
    };

    struct AnimationClip {
        std::string name;
        float duration = 0.0f;
        std::vector<AnimationChannel> channels;
    };

    struct AnimationSkin {
        std::vector<uint32_t> joints;
        std::vector<glm::mat4> inverseBindMatrices;
    };

    struct AnimationAsset {
        std::vector<AnimationPose> bindPose;
        std::vector<bool> animatedNodes;
        std::vector<AnimationClip> clips;
        std::vector<AnimationSkin> skins;
    };

    enum class AnimationComparison { Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };

    struct AnimationState {
        std::string name;
        std::string clip;
        bool loop = true;
        float speed = 1.0f;
    };

    struct AnimationTransition {
        std::string from;
        std::string to;
        std::string parameter;
        AnimationComparison comparison = AnimationComparison::Greater;
        float threshold = 0.0f;
        float fadeSeconds = 0.2f;
    };

    class AnimationGraph {
    public:
        bool AddState(const std::string& name, const std::string& clip, bool loop = true, float speed = 1.0f);
        bool AddTransition(const std::string& from, const std::string& to, const std::string& parameter,
                           AnimationComparison comparison, float threshold, float fadeSeconds = 0.2f);
        void Clear();
        const AnimationState* FindState(const std::string& name) const;
        const std::vector<AnimationState>& GetStates() const { return m_States; }
        const std::vector<AnimationTransition>& GetTransitions() const { return m_Transitions; }

    private:
        std::vector<AnimationState> m_States;
        std::vector<AnimationTransition> m_Transitions;
    };

    class Animator {
    public:
        void SetAsset(std::shared_ptr<const AnimationAsset> asset);
        const std::shared_ptr<const AnimationAsset>& GetAsset() const { return m_Asset; }
        std::vector<std::string> GetClips() const;
        bool Play(const std::string& clip, float fadeSeconds = 0.2f, bool loop = true);
        bool StartGraph(const std::string& state);
        AnimationGraph& GetGraph() { return m_Graph; }
        const AnimationGraph& GetGraph() const { return m_Graph; }
        bool SetFloat(const std::string& parameter, float value);
        float GetFloat(const std::string& parameter) const;
        void SetBool(const std::string& parameter, bool value) { SetFloat(parameter, value ? 1.0f : 0.0f); }
        const std::unordered_map<std::string, float>& GetParameters() const { return m_Parameters; }
        void Pause() { m_Playing = false; }
        void Resume() { m_Playing = m_Clip >= 0; }
        void Stop();
        void Seek(float time);
        bool SetSpeed(float speed);
        float GetSpeed() const { return m_Speed; }
        bool IsPlaying() const { return m_Playing; }
        bool IsLooping() const { return m_Loop; }
        // Unlike Play(clip, ..., loop), this only flips the loop flag: it doesn't touch m_State, so
        // it's safe to call while graph-driven playback is active without silently kicking the
        // Animator out of the graph.
        void SetLooping(bool loop) { m_Loop = loop; }
        bool IsFinished() const;
        float GetTime() const { return m_Time; }
        float GetDuration() const;
        std::string GetCurrentClip() const;
        const std::string& GetCurrentState() const { return m_State; }
        void Update(float deltaTime);
        const std::vector<AnimationPose>& GetPose() const { return m_Pose; }

    private:
        int FindClip(const std::string& name) const;
        void SwitchClip(int clip, float fadeSeconds, bool loop);
        void Evaluate();
        void Sample(int clip, float time, std::vector<AnimationPose>& pose) const;
        std::shared_ptr<const AnimationAsset> m_Asset;
        AnimationGraph m_Graph;
        std::unordered_map<std::string, float> m_Parameters;
        std::string m_State;
        std::vector<AnimationPose> m_Pose;
        std::vector<AnimationPose> m_BlendFrom;
        int m_Clip = -1;
        int m_PreviousClip = -1;
        float m_Time = 0.0f;
        float m_PreviousTime = 0.0f;
        float m_PreviousSpeed = 1.0f;
        bool m_PreviousLoop = true;
        float m_FadeTime = 0.0f;
        float m_FadeDuration = 0.0f;
        float m_Speed = 1.0f;
        float m_StateSpeed = 1.0f;
        bool m_Loop = true;
        bool m_Playing = false;
    };
}
