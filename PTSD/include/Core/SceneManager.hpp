#ifndef CORE_SCENE_MANAGER_HPP
#define CORE_SCENE_MANAGER_HPP

#include <cstddef>
#include <memory>
#include <vector>

#include "Core/Scene.hpp"

namespace Core {
/**
 * @class SceneManager
 * @brief A stack of Core::Scene objects driving the active game screen.
 *
 * The top of the stack is the active scene. Pushing a scene pauses the one
 * below (e.g. open a pause menu over gameplay); popping resumes it. @ref Update
 * and @ref Render are forwarded to the top scene only.
 *
 * Transitions requested from inside a scene's @ref Update (a scene that pushes
 * or pops during its own update) are deferred and applied after the update
 * returns, so the stack is never mutated while it is being walked.
 */
class SceneManager {
public:
    SceneManager() = default;

    /// Push @p scene on top; pauses the current top (if any), then enters @p scene.
    void Push(const std::shared_ptr<Scene> &scene);

    /// Pop the top scene (exits it) and resume the one below (if any).
    void Pop();

    /// Replace the top scene with @p scene (exit old, enter new; no pause/resume).
    void Replace(const std::shared_ptr<Scene> &scene);

    /// Pop every scene, top-first, exiting each.
    void Clear();

    /// Update the top scene, then apply any transitions it requested.
    void Update(float dtMs);

    /// Render the top scene.
    void Render();

    /// The active (top) scene, or nullptr if the stack is empty.
    std::shared_ptr<Scene> Current() const;

    /// Number of scenes on the stack.
    std::size_t Count() const;

    /// Whether the stack is empty.
    bool Empty() const;

private:
    enum class OpType { Push, Pop, Replace, Clear };
    struct Op {
        OpType type;
        std::shared_ptr<Scene> scene;
    };

    void DoPush(const std::shared_ptr<Scene> &scene, bool pauseBelow);
    void DoPop(bool resumeBelow);

    std::vector<std::shared_ptr<Scene>> m_Stack;
    std::vector<Op> m_Pending;
    bool m_Updating = false;
};
} // namespace Core

#endif /* CORE_SCENE_MANAGER_HPP */
