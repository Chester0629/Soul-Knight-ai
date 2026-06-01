#ifndef UTIL_TRANSFORM_UTILS_HPP
#define UTIL_TRANSFORM_UTILS_HPP

#include "Core/Drawable.hpp"

#include "Util/Transform.hpp"
#include "pch.hpp"

namespace Util {

/**
 * @brief Converts a Transform object into uniform buffer data.
 *
 * Converts transform data in Core::UniformBuffer format.
 * Call it and pass the returned value to Core::UniformBuffer->setData().
 *
 * @param transform The Transform object to be converted.
 * @param size The size of the object.
 * @param zIndex The z-index of the transformation.
 * @return A Matrices object representing the uniform buffer data.
 *
 */
Core::Matrices ConvertToUniformBufferData(const Util::Transform &transform,
                                          const glm::vec2 &size, float zIndex);

/**
 * @brief Set the active world-to-view matrix applied to all subsequent drawing.
 *
 * Multiplied into the screen mapping inside ConvertToUniformBufferData, so a
 * Core::Camera2D can pan/zoom the world. Defaults to identity, which leaves
 * rendering unchanged. Set it before drawing world objects, and reset it to
 * identity before drawing screen-space UI that should not move with the camera.
 *
 * @param view The world-to-view matrix (e.g. Core::Camera2D::GetViewMatrix()).
 */
void SetActiveViewMatrix(const glm::mat4 &view);

/// Get the active world-to-view matrix (identity by default).
const glm::mat4 &GetActiveViewMatrix();

} // namespace Util

#endif // UTIL_TRANSFORM_UTILS_HPP
