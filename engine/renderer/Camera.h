//
// Created by Debreky on 18/06/2026.
//

#ifndef OSIRIS_CAMERA_H
#define OSIRIS_CAMERA_H
#include "glm/glm.hpp"


namespace Osiris
{
    class Input;

    class Camera {
    public:
        Camera(const glm::vec3 &position, const glm::vec3 &front, float speed = 10.0f, float sensitivity = 0.1f,
            float aspect_ratio = 16.0f/9.0f, float fov = 60.0f, float z_near = 0.1f, float z_far = 100.0f);
        ~Camera();

        glm::mat4 GetViewMatrix() const;
        glm::mat4 GetProjectionMatrix() const;
        glm::vec3 GetFront() const;
        glm::vec3 GetRenderPosition() const;
        glm::vec3 GetRenderFront() const;
        // Movement and look can be gated independently when another system owns either one.
        void Update(const Input& input, float deltaTime, bool applyMovement = true, bool applyLook = true);
        void Shake(float strength, float duration, float frequency = 24.0f);
        void UpdateShake(float deltaTime);
        void ClearShake();
        glm::vec3 GetPosition() const;
        void SetPosition(const glm::vec3& position);
        void SetOrientation(const glm::vec3& front, const glm::vec3& up);
        void SetAspectRatio(float aspectRatio);
        float& GetSpeed();

    private:
        glm::vec3 m_Position;
        glm::vec3 m_Front;
        glm::vec3 m_Up;
        glm::vec3 m_WorldUp;
        float m_Yaw;
        float m_Pitch;
        float m_Roll;
        float m_Speed;
        float m_Sensitivity;
        float m_AspectRatio;
        float m_Fov;
        float m_zNear;
        float m_zFar;

        glm::vec2 m_ShakePosition = glm::vec2(0.0f);
        glm::vec2 m_ShakeRotation = glm::vec2(0.0f);
        float m_ShakeStrength = 0.0f;
        float m_ShakeDuration = 0.0f;
        float m_ShakeRemaining = 0.0f;
        float m_ShakeElapsed = 0.0f;
        float m_ShakeFrequency = 24.0f;

    };
}


#endif //OSIRIS_CAMERA_H
