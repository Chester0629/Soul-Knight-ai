#include "Util/TransformUtils.hpp"

#include "config.hpp"
#include <glm/gtx/matrix_transform_2d.hpp>

namespace Util {

namespace {
// The active world-to-view matrix; identity until a camera sets it.
glm::mat4 s_ActiveViewMatrix(1.0F);
} // namespace

void SetActiveViewMatrix(const glm::mat4 &view) { s_ActiveViewMatrix = view; }
const glm::mat4 &GetActiveViewMatrix() { return s_ActiveViewMatrix; }

Core::Matrices ConvertToUniformBufferData(const Util::Transform &transform,
                                          const glm::vec2 &size,
                                          const float zIndex) {
    constexpr glm::mat4 eye(1.F);

    constexpr float nearClip = -100;
    constexpr float farClip = 100;

    auto projection =
        glm::ortho<float>(0.0F, 1.0F, 0.0F, 1.0F, nearClip, farClip);
    auto view = glm::scale(eye, {1.F / PTSD_Config::WINDOW_WIDTH,
                                 1.F / PTSD_Config::WINDOW_HEIGHT, 1.F}) *
                glm::translate(eye, {PTSD_Config::WINDOW_WIDTH / 2,
                                     PTSD_Config::WINDOW_HEIGHT / 2, 0}) *
                s_ActiveViewMatrix;

    // TODO: TRS comment
    auto model = glm::translate(eye, {transform.translation, zIndex}) *
                 glm::rotate(eye, transform.rotation, glm::vec3(0, 0, 1)) *
                 glm::scale(eye, {transform.scale * size, 1});

    Core::Matrices data = {
        model,
        projection * view,
    };

    return data;
}

} // namespace Util
