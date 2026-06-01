#ifndef CORE_CAMERA2D_HPP
#define CORE_CAMERA2D_HPP

#include <glm/glm.hpp>

namespace Core {
/**
 * @class Camera2D
 * @brief A 2D camera producing a world-to-view matrix, with follow and shake.
 *
 * The camera lives in the engine's center-origin Cartesian world space. Its
 * @ref GetViewMatrix maps world coordinates into view space such that the
 * camera center maps to the view origin, scaled by @ref GetZoom and rotated by
 * the camera rotation. Multiply a projection by this view matrix to render the
 * world through the camera.
 *
 * Screen shake uses a trauma model (see @ref AddTrauma): trauma in [0, 1]
 * decays over time and drives a bounded, deterministic positional offset
 * (offset magnitude scales with trauma squared). The shake is produced by fixed
 * oscillators - no randomness - so behaviour is reproducible and testable.
 */
class Camera2D {
public:
    Camera2D() = default;

    /// Set the camera center in world space.
    void SetPosition(const glm::vec2 &center) { m_Center = center; }
    /// Get the camera center in world space.
    glm::vec2 GetPosition() const { return m_Center; }

    /// Set the zoom factor (>1 zooms in). Non-positive values are ignored.
    void SetZoom(float zoom) {
        if (zoom > 0.0F) {
            m_Zoom = zoom;
        }
    }
    /// Get the zoom factor.
    float GetZoom() const { return m_Zoom; }

    /// Set the camera rotation in radians.
    void SetRotation(float radians) { m_Rotation = radians; }
    /// Get the camera rotation in radians.
    float GetRotation() const { return m_Rotation; }

    /**
     * @brief Move the camera a fraction of the way toward @p target.
     * @param target The world point to move toward.
     * @param lerp   Interpolation factor, clamped to [0, 1].
     */
    void Follow(const glm::vec2 &target, float lerp);

    /**
     * @brief Add screen-shake trauma. Trauma accumulates and clamps to [0, 1].
     * @param amount The trauma to add (e.g. 0.5 for a medium hit).
     */
    void AddTrauma(float amount);
    /// Get the current trauma in [0, 1].
    float GetTrauma() const { return m_Trauma; }
    /// Get the current shake offset (added to the camera center when rendering).
    glm::vec2 GetShakeOffset() const { return m_ShakeOffset; }

    /// Set the maximum shake offset in pixels (at full trauma).
    void SetMaxShake(float pixels) { m_MaxShake = pixels; }
    /// Set how much trauma decays per millisecond.
    void SetTraumaDecayPerMs(float decay) { m_TraumaDecayPerMs = decay; }

    /**
     * @brief Advance shake and decay trauma.
     * @param dtMs Elapsed time in milliseconds (non-positive treated as 0).
     */
    void Update(float dtMs);

    /// Compute the world-to-view matrix (column-major, glm convention).
    glm::mat4 GetViewMatrix() const;

private:
    glm::vec2 m_Center{0.0F, 0.0F};
    glm::vec2 m_ShakeOffset{0.0F, 0.0F};
    float m_Zoom{1.0F};
    float m_Rotation{0.0F};
    float m_Trauma{0.0F};
    float m_MaxShake{16.0F};
    float m_TraumaDecayPerMs{1.0F / 1000.0F}; // full trauma decays in ~1 second
    float m_ShakeTimeMs{0.0F};
};
} // namespace Core

#endif /* CORE_CAMERA2D_HPP */
