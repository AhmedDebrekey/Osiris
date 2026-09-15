#include "Animation.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Osiris {
    namespace {
        glm::quat Quaternion(const glm::vec4& value) {
            const glm::quat q(value.w, value.x, value.y, value.z);
            return glm::dot(q, q) > 1e-12f ? glm::normalize(q) : glm::quat(1, 0, 0, 0);
        }

        float Advance(float time, float delta, float duration, bool loop) {
            if (duration <= 0.0f) return 0.0f;
            const double next = static_cast<double>(time) + delta;
            return static_cast<float>(loop ? std::fmod(next, duration) : std::min(next, static_cast<double>(duration)));
        }

        bool Compare(float value, AnimationComparison comparison, float threshold) {
            switch (comparison) {
                case AnimationComparison::Less: return value < threshold;
                case AnimationComparison::LessEqual: return value <= threshold;
                case AnimationComparison::Greater: return value > threshold;
                case AnimationComparison::GreaterEqual: return value >= threshold;
                // Graph parameters are typically driven by continuous gameplay values (speed, a
                // normalized ratio) or SetBool's 0/1, so exact equality would almost never fire.
                case AnimationComparison::Equal: return std::abs(value - threshold) < 1e-4f;
                case AnimationComparison::NotEqual: return std::abs(value - threshold) >= 1e-4f;
            }
            return false;
        }

        glm::vec4 SampleChannel(const AnimationChannel& channel, float time) {
            const bool cubic = channel.interpolation == AnimationInterpolation::CubicSpline;
            const size_t stride = cubic ? 3 : 1;
            const size_t offset = cubic ? 1 : 0;
            if (time <= channel.times.front()) return channel.values[offset];
            if (time >= channel.times.back()) return channel.values[(channel.times.size() - 1) * stride + offset];
            const size_t right = std::upper_bound(channel.times.begin(), channel.times.end(), time) - channel.times.begin();
            const size_t left = right - 1;
            const float interval = channel.times[right] - channel.times[left];
            const float t = (time - channel.times[left]) / interval;
            const glm::vec4 a = channel.values[left * stride + offset];
            const glm::vec4 b = channel.values[right * stride + offset];
            if (channel.interpolation == AnimationInterpolation::Step) return a;
            if (cubic) {
                const float t2 = t * t;
                const float t3 = t2 * t;
                return (2 * t3 - 3 * t2 + 1) * a
                     + (t3 - 2 * t2 + t) * interval * channel.values[left * 3 + 2]
                     + (-2 * t3 + 3 * t2) * b
                     + (t3 - t2) * interval * channel.values[right * 3];
            }
            if (channel.path == AnimationPath::Rotation) {
                const glm::quat q = glm::slerp(Quaternion(a), Quaternion(b), t);
                return {q.x, q.y, q.z, q.w};
            }
            return glm::mix(a, b, t);
        }
    }

    glm::mat4 AnimationPose::Matrix() const {
        return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation)
             * glm::scale(glm::mat4(1.0f), scale);
    }

    bool AnimationGraph::AddState(const std::string& name, const std::string& clip, bool loop, float speed) {
        if (name.empty() || clip.empty() || FindState(name) || !std::isfinite(speed) || speed < 0.0f) return false;
        m_States.push_back({name, clip, loop, speed});
        return true;
    }

    bool AnimationGraph::AddTransition(const std::string& from, const std::string& to, const std::string& parameter,
                                       AnimationComparison comparison, float threshold, float fadeSeconds) {
        if (!FindState(from) || !FindState(to) || from == to || parameter.empty()
            || !std::isfinite(threshold) || !std::isfinite(fadeSeconds) || fadeSeconds < 0.0f
            || comparison < AnimationComparison::Less || comparison > AnimationComparison::NotEqual) return false;
        m_Transitions.push_back({from, to, parameter, comparison, threshold, fadeSeconds});
        return true;
    }

    void AnimationGraph::Clear() { m_States.clear(); m_Transitions.clear(); }

    const AnimationState* AnimationGraph::FindState(const std::string& name) const {
        for (const auto& state : m_States) if (state.name == name) return &state;
        return nullptr;
    }

    void Animator::SetAsset(std::shared_ptr<const AnimationAsset> asset) {
        m_Asset = std::move(asset);
        m_Graph.Clear();
        m_Parameters.clear();
        Stop();
    }

    std::vector<std::string> Animator::GetClips() const {
        std::vector<std::string> names;
        if (m_Asset) for (const auto& clip : m_Asset->clips) names.push_back(clip.name);
        return names;
    }

    int Animator::FindClip(const std::string& name) const {
        if (m_Asset) for (size_t i = 0; i < m_Asset->clips.size(); i++)
            if (m_Asset->clips[i].name == name) return static_cast<int>(i);
        return -1;
    }

    void Animator::SwitchClip(int clip, float fadeSeconds, bool loop) {
        // Interrupting a fade starts from the displayed pose, not either source clip's raw pose.
        m_BlendFrom = m_Pose;
        m_PreviousClip = m_FadeDuration > m_FadeTime ? -1 : m_Clip;
        m_PreviousTime = m_Time;
        m_PreviousSpeed = m_StateSpeed;
        m_PreviousLoop = m_Loop;
        m_Clip = clip;
        m_Time = 0.0f;
        m_Loop = loop;
        m_Playing = true;
        m_FadeTime = 0.0f;
        m_FadeDuration = fadeSeconds;
        Evaluate();
    }

    bool Animator::Play(const std::string& clip, float fadeSeconds, bool loop) {
        const int index = FindClip(clip);
        if (index < 0 || !std::isfinite(fadeSeconds) || fadeSeconds < 0.0f) return false;
        m_State.clear();
        if (index != m_Clip) SwitchClip(index, fadeSeconds, loop);
        m_StateSpeed = 1.0f;
        m_Loop = loop;
        m_Playing = true;
        return true;
    }

    bool Animator::StartGraph(const std::string& name) {
        const auto* state = m_Graph.FindState(name);
        if (!state) return false;
        for (const auto& candidate : m_Graph.GetStates()) if (FindClip(candidate.clip) < 0) return false;
        SwitchClip(FindClip(state->clip), 0.0f, state->loop);
        m_State = state->name;
        m_StateSpeed = state->speed;
        return true;
    }

    bool Animator::SetFloat(const std::string& parameter, float value) {
        if (parameter.empty() || !std::isfinite(value)) return false;
        m_Parameters[parameter] = value;
        return true;
    }

    float Animator::GetFloat(const std::string& parameter) const {
        const auto found = m_Parameters.find(parameter);
        return found == m_Parameters.end() ? 0.0f : found->second;
    }

    bool Animator::SetSpeed(float speed) {
        if (!std::isfinite(speed) || speed < 0.0f) return false;
        m_Speed = speed;
        return true;
    }

    void Animator::Stop() {
        m_Clip = m_PreviousClip = -1;
        m_Time = m_FadeTime = m_FadeDuration = 0.0f;
        m_StateSpeed = 1.0f;
        m_State.clear();
        m_Playing = false;
        m_BlendFrom.clear();
        m_Pose = m_Asset ? m_Asset->bindPose : std::vector<AnimationPose>{};
    }

    void Animator::Seek(float time) {
        if (m_Clip < 0 || !std::isfinite(time)) return;
        m_Time = std::clamp(time, 0.0f, GetDuration());
        m_FadeDuration = 0.0f;
        Evaluate();
    }

    float Animator::GetDuration() const { return m_Clip < 0 ? 0.0f : m_Asset->clips[m_Clip].duration; }
    bool Animator::IsFinished() const { return m_Clip >= 0 && !m_Loop && m_Time >= GetDuration(); }
    std::string Animator::GetCurrentClip() const { return m_Clip < 0 ? std::string{} : m_Asset->clips[m_Clip].name; }

    void Animator::Sample(int clip, float time, std::vector<AnimationPose>& pose) const {
        pose = m_Asset->bindPose;
        if (clip < 0) return;
        for (const auto& channel : m_Asset->clips[clip].channels) {
            const glm::vec4 value = SampleChannel(channel, time);
            auto& node = pose[channel.node];
            switch (channel.path) {
                case AnimationPath::Translation: node.translation = glm::vec3(value); break;
                case AnimationPath::Rotation: node.rotation = Quaternion(value); break;
                case AnimationPath::Scale: node.scale = glm::vec3(value); break;
            }
        }
    }

    void Animator::Evaluate() {
        if (!m_Asset) return;
        Sample(m_Clip, m_Time, m_Pose);
        if (m_FadeDuration <= m_FadeTime) return;
        if (m_PreviousClip >= 0) Sample(m_PreviousClip, m_PreviousTime, m_BlendFrom);
        const float weight = m_FadeTime / m_FadeDuration;
        for (size_t i = 0; i < m_Pose.size(); i++) {
            m_Pose[i].translation = glm::mix(m_BlendFrom[i].translation, m_Pose[i].translation, weight);
            m_Pose[i].rotation = glm::normalize(glm::slerp(m_BlendFrom[i].rotation, m_Pose[i].rotation, weight));
            m_Pose[i].scale = glm::mix(m_BlendFrom[i].scale, m_Pose[i].scale, weight);
        }
    }

    void Animator::Update(float deltaTime) {
        if (!m_Playing || m_Clip < 0 || !std::isfinite(deltaTime) || deltaTime < 0.0f) return;
        if (!m_State.empty()) {
            for (const auto& transition : m_Graph.GetTransitions()) {
                if (transition.from != m_State
                    || !Compare(GetFloat(transition.parameter), transition.comparison, transition.threshold)) continue;
                const auto* state = m_Graph.FindState(transition.to);
                if (!state || FindClip(state->clip) < 0) continue;
                SwitchClip(FindClip(state->clip), transition.fadeSeconds, state->loop);
                m_State = state->name;
                m_StateSpeed = state->speed;
                break;
            }
        }
        const float delta = deltaTime * m_Speed;
        if (!std::isfinite(delta * m_StateSpeed) || !std::isfinite(delta * m_PreviousSpeed)) return;
        m_Time = Advance(m_Time, delta * m_StateSpeed, GetDuration(), m_Loop);
        if (m_PreviousClip >= 0) m_PreviousTime = Advance(m_PreviousTime, delta * m_PreviousSpeed,
            m_Asset->clips[m_PreviousClip].duration, m_PreviousLoop);
        m_FadeTime = std::min(m_FadeTime + deltaTime, m_FadeDuration);
        Evaluate();
    }
}
