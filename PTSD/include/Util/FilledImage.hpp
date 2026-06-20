#ifndef UTIL_FILLED_IMAGE_HPP
#define UTIL_FILLED_IMAGE_HPP

#include "pch.hpp" // IWYU pragma: export

#include <array>
#include <memory>
#include <string>

#include "Core/Drawable.hpp"
#include "Core/Program.hpp"
#include "Core/Texture.hpp"
#include "Core/UniformBuffer.hpp"
#include "Core/VertexArray.hpp"

namespace Util {

/// Bottom-anchored vertical-fill quad geometry for a fill fraction @p fraction in
/// [0,1]. PURE (no GL) -> unit-testable, and the ultracode geometry anchor.
///
/// Vertex order matches Util::Image (TL, BL, BR, TR; index buffer 0,1,2, 0,2,3).
/// The BOTTOM edge is fixed at normalized y = -0.5 (scaled about the model centre ->
/// a fixed slot-bottom); only the TOP edge rises with @p fraction. This is a TRUE
/// vertical CROP (the quad shrinks + the UV.v range shrinks together), NOT a scale-
/// squash, so a non-rectangular sprite keeps its shape.
///
/// UV convention follows Util::Image: screen-bottom (vert y = -0.5) maps to UV.v = 1
/// (the texture's bottom row), so the visible texture rows are the BOTTOM @p fraction
/// of the image -> UV.v in [1 - fraction, 1].
struct FilledQuad {
    std::array<float, 8> verts; ///< (x,y) x4, TL,BL,BR,TR order (normalized -0.5..0.5).
    std::array<float, 8> uvs;   ///< (u,v) x4, TL,BL,BR,TR order.
};
FilledQuad VerticalBottomFilledQuad(float fraction);

/**
 * @class FilledImage
 * @brief A textured quad with Unity-style **Vertical / Bottom** `Image.fillAmount`:
 *        a TRUE UV-crop (the bottom @p fraction of the sprite is revealed, the rest
 *        cropped away), NOT a scale-squash -- so a non-rectangular sprite (e.g. the
 *        tapered skill-cooldown mask `ui_joy_3`) keeps its outline.
 *
 * Soul-Knight engine extension (A4). Mirrors Util::Image's setup (the shared Base
 * vert/frag program + a per-instance texture + uniform), but its vertex array is
 * REBUILT whenever @ref SetFraction changes -- `Base.frag` has no fill uniform, so the
 * crop lives in the quad geometry + UVs. `Base.frag` also has no tint uniform, so the
 * sprite must be PRE-TINTED (pass a baked PNG).
 */
class FilledImage : public Core::Drawable {
public:
    explicit FilledImage(const std::string &filepath, bool useAA = true);

    /// Fill fraction (clamped 0..1). 1 = full sprite, 0 = invisible. Rebuilds the
    /// cropped quad only when the value actually changes (cheap when static).
    void SetFraction(float fraction);
    float GetFraction() const { return m_Fraction; }

    void Draw(const Core::Matrices &data) override;
    glm::vec2 GetSize() const override { return m_Size; } ///< FULL native sprite px.

private:
    void RebuildQuad();
    static void InitProgram();

    static std::unique_ptr<Core::Program> s_Program;
    static constexpr int UNIFORM_SURFACE_LOCATION = 0;

    std::unique_ptr<Core::UniformBuffer<Core::Matrices>> m_UniformBuffer;
    std::unique_ptr<Core::Texture> m_Texture;
    std::unique_ptr<Core::VertexArray> m_VertexArray;
    glm::vec2 m_Size{0.0F, 0.0F};
    float m_Fraction = 1.0F;
};

} // namespace Util

#endif /* UTIL_FILLED_IMAGE_HPP */
