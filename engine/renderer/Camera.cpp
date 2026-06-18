//
// Created by Debreky on 18/06/2026.
//

#include "Camera.h"

#include "glm/gtc/matrix_transform.hpp"


Camera::Camera(const glm::vec3 &position, const glm::vec3 &front, float speed, float sensitivity,
               float aspect_ratio, float fov, float z_near, float z_far)
        :   m_Position(position),
            m_Front(front),
            m_Up(glm::vec3(0.0f, 1.0f, 0.0f)), m_WorldUp(glm::vec3(0.0f, 1.0f, 0.0f)), m_Yaw(0), m_Pitch(0),
            m_Roll(0), m_Speed(speed),
            m_Sensitivity(sensitivity),
            m_AspectRatio(aspect_ratio),
            m_Fov(glm::radians(fov)), m_zNear(z_near), m_zFar(z_far) {
}

Camera::~Camera() {
}

glm::mat4 Camera::GetViewMatrix() {
    return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
}

glm::mat4 Camera::GetProjectionMatrix() {
    return glm::perspective(m_Fov, m_AspectRatio, m_zNear, m_zFar);
}
