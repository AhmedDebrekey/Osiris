//
// Created by Debreky on 18/06/2026.
//

#include "Camera.h"

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
    }

    Camera::~Camera() {
    }

    glm::mat4 Camera::GetViewMatrix() const {
        return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
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

        if (!applyMovement) return;

        // Derive right vector
        glm::vec3 right = glm::normalize(glm::cross(m_Front, m_WorldUp));

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

    glm::vec3 Camera::GetPosition() const {
        return m_Position;
    }

    void Camera::SetPosition(const glm::vec3& position) {
        m_Position = position;
    }

    void Camera::SetAspectRatio(float aspectRatio) {
        m_AspectRatio = aspectRatio;
    }

    float& Camera::GetSpeed() {
        return m_Speed;
    }
}
