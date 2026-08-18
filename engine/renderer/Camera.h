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
        // applyMovement=false skips the WASD position update (mouse-look/yaw/pitch still
        // apply) — used when something else (e.g. a physics character controller) owns position.
        void Update(const Input& input, float deltaTime, bool applyMovement = true);
        glm::vec3 GetPosition() const;
        void SetPosition(const glm::vec3& position);
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

    };
}


#endif //OSIRIS_CAMERA_H