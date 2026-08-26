//
// Created by Debreky on 18/06/2026.
//

#include "Camera.h"

#include <cmath>

#include "glm/gtc/matrix_transform.hpp"
#include "platform/Input.h"

namespace Osiris
{
    Camera::Camera(const glm::vec3 &position, const glm::vec3 &front, float speed, float sensitivity,
                   float aspect_ratio, float fov, float z_near, float z_far)
            :   m_Position(position),
                m_Front(front),
                m_Up(glm::vec3(0.0f, 1.0f, 0.0f)), m_WorldUp(glm::vec3(0.0f, 1.0f, 0.0f)), m_Yaw(-90.0f), m_Pitch(0),
                m_Roll(0), m_Speed(speed),
                m_Sensitivity(sensitivity),
                m_AspectRatio(aspect_ratio),
                m_Fov(glm::radians(fov)), m_zNear(z_near), m_zFar(z_far) {
        SetOrientation(front, m_WorldUp);
    }

    Camera::~Camera() {
    }

    glm::mat4 Camera::GetViewMatrix() const {
        const glm::vec3 renderPosition = GetRenderPosition();
        const glm::vec3 renderFront = GetRenderFront();
        const glm::vec3 right = glm::normalize(glm::cross(renderFront, m_Up));
        const glm::vec3 renderUp = glm::normalize(glm::cross(right, renderFront));
        return glm::lookAt(renderPosition, renderPosition + renderFront, renderUp);
    }

    glm::mat4 Camera::GetProjectionMatrix() const {
        glm::mat4 projection = glm::perspective(m_Fov, m_AspectRatio, m_zNear, m_zFar);
        // Vulkan's NDC has +Y pointing down (opposite of the OpenGL convention glm::perspective
        // assumes), so without this the whole scene renders vertically flipped on screen.
        projection[1][1] *= -1.0f;
        return projection;
    }

    glm::vec3 Camera::GetFront() const {
        return m_Front;
    }

    glm::vec3 Camera::GetRenderPosition() const {
        const glm::vec3 right = glm::normalize(glm::cross(m_Front, m_Up));
        return m_Position + right * m_ShakePosition.x + m_Up * m_ShakePosition.y;
    }

    glm::vec3 Camera::GetRenderFront() const {
        const glm::vec3 right = glm::normalize(glm::cross(m_Front, m_Up));
        const float pitchOffset = std::tan(glm::radians(m_ShakeRotation.x));
        const float yawOffset = std::tan(glm::radians(m_ShakeRotation.y));
        return glm::normalize(m_Front + m_Up * pitchOffset + right * yawOffset);
    }

    void Camera::Update(const Input& input, float deltaTime, bool applyMovement, bool applyLook) {
        // Mouse look
        if (applyLook && input.IsMouseButtonHeld(SDL_BUTTON_RIGHT)){
            glm::vec2 mouseDelta = input.GetMouseDelta();
            m_Yaw   += mouseDelta.x * m_Sensitivity;
            m_Pitch -= mouseDelta.y * m_Sensitivity;
            m_Pitch  = glm::clamp(m_Pitch, -89.0f, 89.0f);
        }

        // Recalculate front vector
        m_Front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
        m_Front.y = sin(glm::radians(m_Pitch));
        m_Front.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
        m_Front   = glm::normalize(m_Front);
        glm::vec3 right = glm::normalize(glm::cross(m_Front, m_WorldUp));
        m_Up = glm::normalize(glm::cross(right, m_Front));

        if (!applyMovement) return;

        // WASD movement
        if (input.IsKeyHeld(SDL_SCANCODE_W))
            m_Position += m_Front * m_Speed * deltaTime;
        if (input.IsKeyHeld(SDL_SCANCODE_S))
            m_Position -= m_Front * m_Speed * deltaTime;
        if (input.IsKeyHeld(SDL_SCANCODE_A))
            m_Position -= right * m_Speed * deltaTime;
        if (input.IsKeyHeld(SDL_SCANCODE_D))
            m_Position += right * m_Speed * deltaTime;
        if (input.IsKeyHeld(SDL_SCANCODE_Q))
            m_Position -= m_Up * m_Speed * deltaTime;
        if (input.IsKeyHeld(SDL_SCANCODE_E))
            m_Position += m_Up * m_Speed * deltaTime;
    }

    void Camera::Shake(float strength, float duration, float frequency) {
        if (strength <= 0.0f || duration <= 0.0f) {
            ClearShake();
            return;
        }

        m_ShakeStrength = glm::clamp(strength, 0.0f, 20.0f);
        m_ShakeDuration = duration;
        m_ShakeRemaining = duration;
        m_ShakeElapsed = 0.0f;
        m_ShakeFrequency = glm::clamp(frequency, 0.1f, 100.0f);
    }

    void Camera::UpdateShake(float deltaTime) {
        if (m_ShakeRemaining <= 0.0f) return;

        m_ShakeElapsed += glm::max(deltaTime, 0.0f);
        m_ShakeRemaining = glm::max(m_ShakeRemaining - glm::max(deltaTime, 0.0f), 0.0f);
        if (m_ShakeRemaining <= 0.0f) {
            ClearShake();
            return;
        }

        constexpr float twoPi = 6.28318530718f;
        const float phase = m_ShakeElapsed * m_ShakeFrequency * twoPi;
        const float amplitude = m_ShakeStrength * (m_ShakeRemaining / m_ShakeDuration);
        m_ShakeRotation.x = std::sin(phase * 1.13f) * amplitude;
        m_ShakeRotation.y = std::sin(phase * 1.47f + 1.7f) * amplitude;
        m_ShakePosition.x = std::sin(phase * 0.91f + 0.4f) * amplitude * 0.015f;
        m_ShakePosition.y = std::sin(phase * 1.31f + 2.2f) * amplitude * 0.01f;
    }

    void Camera::ClearShake() {
        m_ShakePosition = glm::vec2(0.0f);
        m_ShakeRotation = glm::vec2(0.0f);
        m_ShakeStrength = 0.0f;
        m_ShakeDuration = 0.0f;
        m_ShakeRemaining = 0.0f;
        m_ShakeElapsed = 0.0f;
    }

    glm::vec3 Camera::GetPosition() const {
        return m_Position;
    }

    void Camera::SetPosition(const glm::vec3& position) {
        m_Position = position;
    }

    void Camera::SetOrientation(const glm::vec3& front, const glm::vec3& up) {
        if (glm::dot(front, front) <= 0.0f || glm::dot(up, up) <= 0.0f) return;

        m_Front = glm::normalize(front);
        m_WorldUp = glm::normalize(up);
        // Avoid the near-parallel cross product that would make glm::lookAt produce NaN.
        if (glm::abs(glm::dot(m_Front, m_WorldUp)) > 0.9f) {
            m_WorldUp = glm::abs(m_Front.y) < 0.9f
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(0.0f, 0.0f, 1.0f);
        }

        const glm::vec3 right = glm::normalize(glm::cross(m_Front, m_WorldUp));
        m_Up = glm::normalize(glm::cross(right, m_Front));
        m_Yaw = glm::degrees(std::atan2(m_Front.z, m_Front.x));
        m_Pitch = glm::degrees(std::asin(glm::clamp(m_Front.y, -1.0f, 1.0f)));
    }

    void Camera::SetAspectRatio(float aspectRatio) {
        m_AspectRatio = aspectRatio;
    }

    float& Camera::GetSpeed() {
        return m_Speed;
    }
}
