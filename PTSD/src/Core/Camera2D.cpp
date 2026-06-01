#include "Core/Camera2D.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace Core {

void Camera2D::Follow(const glm::vec2 &target, float lerp) {
    const float t = std::clamp(lerp, 0.0F, 1.0F);
    m_Center += (target - m_Center) * t;
}

void Camera2D::AddTrauma(float amount) {
    m_Trauma = std::clamp(m_Trauma + amount, 0.0F, 1.0F);
}

void Camera2D::Update(float dtMs) {
    const float dt = dtMs > 0.0F ? dtMs : 0.0F;
    m_ShakeTimeMs += dt;
    m_Trauma = std::max(0.0F, m_Trauma - m_TraumaDecayPerMs * dt);

    // Offset scales with trauma^2 for a punchier falloff; bounded by m_MaxShake.
    const float mag = m_MaxShake * m_Trauma * m_Trauma;
    m_ShakeOffset.x = mag * std::sin(m_ShakeTimeMs * 0.0517F);
    m_ShakeOffset.y = mag * std::sin(m_ShakeTimeMs * 0.0719F + 1.3F);
}

glm::mat4 Camera2D::GetViewMatrix() const {
    glm::mat4 view(1.0F);
    view = glm::scale(view, glm::vec3(m_Zoom, m_Zoom, 1.0F));
    view = glm::rotate(view, -m_Rotation, glm::vec3(0.0F, 0.0F, 1.0F));
    const glm::vec2 eye = m_Center + m_ShakeOffset;
    view = glm::translate(view, glm::vec3(-eye, 0.0F));
    return view;
}

} // namespace Core
